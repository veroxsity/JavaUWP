#include "crash_report.h"

#include "launcher_common.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <fcntl.h>
#include <io.h>
#include <share.h>
#include <vector>

#include "third_party/miniz/miniz.h"

struct CrashZipEntry {
    std::string name;
    std::vector<unsigned char> data;
    mz_uint32 crc = 0;
    mz_uint32 localHeaderOffset = 0;
};

static void AddCrashZipMemoryEntry(
    std::vector<CrashZipEntry>& entries,
    const std::string& name,
    const std::string& text) {
    if (name.empty() || name.size() > 65535) return;
    CrashZipEntry entry;
    entry.name = name;
    entry.data.assign(text.begin(), text.end());
    entry.crc = static_cast<mz_uint32>(mz_crc32(MZ_CRC32_INIT, entry.data.data(), entry.data.size()));
    entries.push_back(std::move(entry));
}

static void AddCrashZipFileEntry(
    std::vector<CrashZipEntry>& entries,
    const std::wstring& path,
    const std::string& archiveName) {
    if (archiveName.empty() || archiveName.size() > 65535 || entries.size() >= 64) return;
    size_t collected = 0;
    for (const auto& existing : entries) {
        if (existing.name == archiveName) return;
        collected += existing.data.size();
    }
    constexpr size_t budget = 32ull * 1024ull * 1024ull;
    if (collected >= budget) return;
    const DWORD attrs = GetFileAttributesW(path.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES || (attrs & FILE_ATTRIBUTE_DIRECTORY)) return;

    CrashZipEntry entry;
    entry.name = archiveName;
    if (!ReadBinaryFileLimited(path, entry.data, (std::min)(budget - collected, size_t(8ull * 1024ull * 1024ull)))) return;
    entry.crc = static_cast<mz_uint32>(mz_crc32(MZ_CRC32_INIT, entry.data.data(), entry.data.size()));
    entries.push_back(std::move(entry));
}

static void AddCrashZipMatchingFiles(
    std::vector<CrashZipEntry>& entries,
    const std::wstring& dir,
    const std::wstring& pattern,
    const std::string& archivePrefix,
    size_t newestLimit = 16) {
    WIN32_FIND_DATAW fd = {};
    HANDLE h = FindFirstFileW((dir + L"\\" + pattern).c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;

    struct Match {
        std::wstring name;
        FILETIME written;
    };
    std::vector<Match> matches;
    do {
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        matches.push_back(Match{ fd.cFileName, fd.ftLastWriteTime });
    } while (FindNextFileW(h, &fd));

    FindClose(h);

    if (newestLimit > 0 && matches.size() > newestLimit) {
        std::sort(matches.begin(), matches.end(), [](const Match& a, const Match& b) {
            return CompareFileTime(&a.written, &b.written) > 0;
        });
        matches.resize(newestLimit);
    }

    for (const Match& match : matches) {
        AddCrashZipFileEntry(entries, dir + L"\\" + match.name, archivePrefix + "/" + w2a(match.name));
    }
}

static bool ZipWrite(FILE* f, const void* data, size_t size) {
    return size == 0 || fwrite(data, 1, size, f) == size;
}

static bool ZipWriteLe16(FILE* f, unsigned value) {
    unsigned char bytes[2] = {
        static_cast<unsigned char>(value & 0xFFu),
        static_cast<unsigned char>((value >> 8) & 0xFFu)
    };
    return ZipWrite(f, bytes, sizeof(bytes));
}

static bool ZipWriteLe32(FILE* f, unsigned long value) {
    unsigned char bytes[4] = {
        static_cast<unsigned char>(value & 0xFFul),
        static_cast<unsigned char>((value >> 8) & 0xFFul),
        static_cast<unsigned char>((value >> 16) & 0xFFul),
        static_cast<unsigned char>((value >> 24) & 0xFFul)
    };
    return ZipWrite(f, bytes, sizeof(bytes));
}

static bool WriteStoredZip(const std::wstring& zipPath, std::vector<CrashZipEntry>& entries) {
    if (entries.empty()) return false;
    EnsureDirectoryTree(GetParentDir(zipPath));

    FILE* f = nullptr;
    if (_wfopen_s(&f, zipPath.c_str(), L"wb") != 0 || !f) return false;

    bool ok = true;
    for (CrashZipEntry& entry : entries) {
        const __int64 offset = _ftelli64(f);
        if (offset < 0 || offset > 0xFFFFFFFFll || entry.data.size() > 0xFFFFFFFFull) {
            ok = false;
            break;
        }
        entry.localHeaderOffset = static_cast<mz_uint32>(offset);
        const unsigned nameLen = static_cast<unsigned>(entry.name.size());
        const unsigned dataLen = static_cast<unsigned>(entry.data.size());

        ok = ok &&
            ZipWriteLe32(f, 0x04034b50ul) &&
            ZipWriteLe16(f, 20) &&
            ZipWriteLe16(f, 0x0800) &&
            ZipWriteLe16(f, 0) &&
            ZipWriteLe16(f, 0) &&
            ZipWriteLe16(f, 0) &&
            ZipWriteLe32(f, entry.crc) &&
            ZipWriteLe32(f, dataLen) &&
            ZipWriteLe32(f, dataLen) &&
            ZipWriteLe16(f, nameLen) &&
            ZipWriteLe16(f, 0) &&
            ZipWrite(f, entry.name.data(), entry.name.size()) &&
            ZipWrite(f, entry.data.data(), entry.data.size());
        if (!ok) break;
    }

    const __int64 cdStart = _ftelli64(f);
    if (ok && (cdStart < 0 || cdStart > 0xFFFFFFFFll)) ok = false;

    for (const CrashZipEntry& entry : entries) {
        const unsigned nameLen = static_cast<unsigned>(entry.name.size());
        const unsigned dataLen = static_cast<unsigned>(entry.data.size());
        ok = ok &&
            ZipWriteLe32(f, 0x02014b50ul) &&
            ZipWriteLe16(f, 20) &&
            ZipWriteLe16(f, 20) &&
            ZipWriteLe16(f, 0x0800) &&
            ZipWriteLe16(f, 0) &&
            ZipWriteLe16(f, 0) &&
            ZipWriteLe16(f, 0) &&
            ZipWriteLe32(f, entry.crc) &&
            ZipWriteLe32(f, dataLen) &&
            ZipWriteLe32(f, dataLen) &&
            ZipWriteLe16(f, nameLen) &&
            ZipWriteLe16(f, 0) &&
            ZipWriteLe16(f, 0) &&
            ZipWriteLe16(f, 0) &&
            ZipWriteLe16(f, 0) &&
            ZipWriteLe32(f, 0) &&
            ZipWriteLe32(f, entry.localHeaderOffset) &&
            ZipWrite(f, entry.name.data(), entry.name.size());
        if (!ok) break;
    }

    const __int64 cdEnd = _ftelli64(f);
    if (ok && (cdEnd < cdStart || cdEnd > 0xFFFFFFFFll)) ok = false;
    const unsigned long cdSize = ok ? static_cast<unsigned long>(cdEnd - cdStart) : 0;
    const unsigned long cdOffset = ok ? static_cast<unsigned long>(cdStart) : 0;
    const unsigned count = static_cast<unsigned>(entries.size());

    ok = ok &&
        count <= 0xFFFFu &&
        ZipWriteLe32(f, 0x06054b50ul) &&
        ZipWriteLe16(f, 0) &&
        ZipWriteLe16(f, 0) &&
        ZipWriteLe16(f, count) &&
        ZipWriteLe16(f, count) &&
        ZipWriteLe32(f, cdSize) &&
        ZipWriteLe32(f, cdOffset) &&
        ZipWriteLe16(f, 0);

    if (fclose(f) != 0) ok = false;
    if (!ok) {
        DeleteFileW(zipPath.c_str());
    }
    return ok;
}

bool RewriteZipTextEntry(
    const std::wstring& zipPath,
    const char* entryName,
    const std::wstring& replacementText,
    const std::wstring& backupPath) {
    std::ifstream in(zipPath, std::ios::binary);
    if (!in) return false;
    std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    in.close();
    if (bytes.empty() || in.fail()) return false;

    mz_zip_archive zip{};
    if (!mz_zip_reader_init_mem(&zip, bytes.data(), bytes.size(), 0)) return false;

    std::vector<CrashZipEntry> entries;
    const mz_uint count = mz_zip_reader_get_num_files(&zip);
    bool replaced = false;
    for (mz_uint i = 0; i < count; ++i) {
        mz_zip_archive_file_stat st{};
        if (!mz_zip_reader_file_stat(&zip, i, &st)) continue;
        std::string name = st.m_filename ? st.m_filename : "";
        if (name.empty()) continue;
        if (mz_zip_reader_is_file_a_directory(&zip, i)) continue;

        std::string lower = name;
        std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        auto endsWith = [](const std::string& value, const char* suffix) {
            const size_t suffixLen = std::strlen(suffix);
            return value.size() >= suffixLen && value.compare(value.size() - suffixLen, suffixLen, suffix) == 0;
        };
        if (lower.rfind("meta-inf/", 0) == 0 &&
            (endsWith(lower, ".sf") || endsWith(lower, ".rsa") || endsWith(lower, ".dsa") || endsWith(lower, ".ec"))) {
            continue;
        }

        CrashZipEntry out;
        out.name = name;
        if (_stricmp(name.c_str(), entryName) == 0) {
            const std::string replacement = w2a(replacementText);
            out.data.assign(replacement.begin(), replacement.end());
            replaced = true;
        } else {
            size_t outSize = 0;
            void* p = mz_zip_reader_extract_to_heap(&zip, i, &outSize, 0);
            if (!p) {
                mz_zip_reader_end(&zip);
                return false;
            }
            const unsigned char* data = static_cast<const unsigned char*>(p);
            out.data.assign(data, data + outSize);
            mz_free(p);
        }
        out.crc = static_cast<mz_uint32>(mz_crc32(MZ_CRC32_INIT, out.data.data(), out.data.size()));
        entries.push_back(std::move(out));
    }
    mz_zip_reader_end(&zip);

    if (!replaced || entries.empty()) return false;
    if (!backupPath.empty() && GetFileAttributesW(backupPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
        EnsureDirectoryTree(GetParentDir(backupPath));
        if (!CopyFileW(zipPath.c_str(), backupPath.c_str(), TRUE)) return false;
    }

    const std::wstring tempPath = zipPath + L".rewrite";
    DeleteFileW(tempPath.c_str());
    if (!WriteStoredZip(tempPath, entries)) {
        DeleteFileW(tempPath.c_str());
        return false;
    }
    SetFileAttributesW(zipPath.c_str(), FILE_ATTRIBUTE_NORMAL);
    if (!MoveFileExW(tempPath.c_str(), zipPath.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        DeleteFileW(tempPath.c_str());
        return false;
    }
    return true;
}

bool CreateCrashReportZip(const std::wstring& runtimeRoot, const std::wstring& reason) {
    const std::wstring currentLogs = LogsCurrentDir(runtimeRoot);
    const std::wstring previousLogs = LogsPreviousDir(runtimeRoot);
    const std::wstring reportsDir = CrashReportsDir(runtimeRoot);
    EnsureDirectoryTree(reportsDir);

    std::vector<CrashZipEntry> entries;
    std::wstring markerText;
    ReadTextFile(CrashLaunchMarkerPath(runtimeRoot), markerText);
    if (markerText.empty()) {
        ReadTextFile(runtimeRoot + L"\\.minecraft_launch_active", markerText);
    }
    std::wstring reportGameDir = runtimeRoot + L"\\game";
    {
        const std::wstring key = L"gameDir=";
        const size_t start = markerText.find(key);
        if (start != std::wstring::npos) {
            const size_t valueStart = start + key.size();
            size_t valueEnd = markerText.find(L'\n', valueStart);
            if (valueEnd == std::wstring::npos) valueEnd = markerText.size();
            std::wstring parsed = markerText.substr(valueStart, valueEnd - valueStart);
            if (!parsed.empty() && parsed.back() == L'\r') parsed.pop_back();
            if (!parsed.empty()) reportGameDir = parsed;
        }
    }

    std::string summary =
        "Bandit Launcher crash report\n"
        "reason=" + w2a(reason) + "\n"
        "runtimeRoot=" + w2a(runtimeRoot) + "\n"
        "created=" + w2a(CrashTimestampForFileName()) + "\n";
    if (!markerText.empty()) {
        summary +=
            "\nprevious launch marker:\n"
            "# This describes the Minecraft run that failed to exit cleanly before this app start.\n"
            "# It may not match the profile currently selected in the launcher UI.\n" +
            w2a(markerText) + "\n";
    }
    AddCrashZipMemoryEntry(entries, "summary.txt", summary);

    AddCrashZipFileEntry(entries, currentLogs + L"\\mc_launch.log", "logs/current/mc_launch.log");
    AddCrashZipFileEntry(entries, currentLogs + L"\\java_output.log", "logs/current/java_output.log");
    AddCrashZipFileEntry(entries, currentLogs + L"\\stderr_stream.log", "logs/current/stderr_stream.log");
    AddCrashZipFileEntry(entries, currentLogs + L"\\glfw_uwp.log", "logs/current/glfw_uwp.log");
    AddCrashZipFileEntry(entries, currentLogs + L"\\java_args.txt", "logs/current/java_args.txt");
    AddCrashZipFileEntry(entries, currentLogs + L"\\java_classpath_final.txt", "logs/current/java_classpath_final.txt");
    AddCrashZipFileEntry(entries, reportGameDir + L"\\logs\\latest.log", "profile/game/logs/latest.log");
    AddCrashZipFileEntry(entries, reportGameDir + L"\\logs\\debug.log", "profile/game/logs/debug.log");
    AddCrashZipFileEntry(entries, reportGameDir + L"\\logs\\fabric-loader.log", "profile/game/logs/fabric-loader.log");
    AddCrashZipFileEntry(entries, reportGameDir + L"\\logs\\forge-loader.log", "profile/game/logs/forge-loader.log");
    AddCrashZipFileEntry(entries, reportGameDir + L"\\xbox_compat.log", "profile/game/xbox_compat.log");
    AddCrashZipMatchingFiles(entries, reportGameDir, L"hs_err_pid*.log", "profile/game");
    AddCrashZipMatchingFiles(entries, reportGameDir + L"\\crash-reports", L"*.txt", "profile/game/crash-reports", 4);
    AddCrashZipMatchingFiles(entries, currentLogs, L"*.log", "logs/current");
    AddCrashZipFileEntry(entries, previousLogs + L"\\java_args.txt", "logs_previous/java_args.txt");
    AddCrashZipFileEntry(entries, previousLogs + L"\\java_classpath_final.txt", "logs_previous/java_classpath_final.txt");
    AddCrashZipMatchingFiles(entries, previousLogs, L"*.log", "logs_previous");
    AddCrashZipMatchingFiles(entries, reportsDir, L"*.txt", "crash-reports");

    const std::wstring zipPath = reportsDir + L"\\BanditLauncher-crash-" + CrashTimestampForFileName() + L".zip";
    const bool ok = WriteStoredZip(zipPath, entries);
    if (ok) {
        WriteTextFile(runtimeRoot + L"\\last_crash_report.txt", zipPath + L"\n");
        WriteLogF(L"Crash report zip written: %s entries=%zu", zipPath.c_str(), entries.size());
    } else {
        WriteLogF(L"Crash report zip failed: %s entries=%zu", zipPath.c_str(), entries.size());
    }
    return ok;
}

void ArchivePreviousCrashIfNeeded(const std::wstring& runtimeRoot) {
    std::wstring markerPath = CrashLaunchMarkerPath(runtimeRoot);
    const std::wstring legacyMarkerPath = runtimeRoot + L"\\.minecraft_launch_active";
    if (GetFileAttributesW(markerPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
        if (GetFileAttributesW(legacyMarkerPath.c_str()) == INVALID_FILE_ATTRIBUTES) return;
        markerPath = legacyMarkerPath;
    }

    // the os can reclaim suspended apps during a normal quit
    const std::wstring suspendedPath = LaunchSuspendedMarkerPath(runtimeRoot);
    if (GetFileAttributesW(suspendedPath.c_str()) != INVALID_FILE_ATTRIBUTES) {
        WriteLog(L"Previous Minecraft launch was suspended, clearing markers");
        DeleteFileW(suspendedPath.c_str());
        DeleteFileW(markerPath.c_str());
        return;
    }

    WriteLog(L"Previous Minecraft launch marker found; archiving logs before truncation");
    CreateCrashReportZip(runtimeRoot, L"Previous Minecraft launch did not exit cleanly");
    DeleteFileW(markerPath.c_str());
}
