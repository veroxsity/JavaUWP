#include "world_io.h"

#include "launcher_common.h"
#include "profiles.h"

#include "third_party/miniz/miniz.h"

#include <algorithm>
#include <vector>
#include <memory>

bool IsSafeWorldName(const std::wstring& name) {
    if (name.empty()) return false;
    if (name == L"." || name == L"..") return false;
    return name.find(L'\\') == std::wstring::npos &&
        name.find(L'/') == std::wstring::npos &&
        name.find(L':') == std::wstring::npos;
}

std::wstring WorldExportsDir(const std::wstring& runtimeRoot) {
    return runtimeRoot + L"\\exports\\worlds";
}

std::wstring DefaultWorldExportPath(const std::wstring& runtimeRoot, const std::wstring& profileId, const std::wstring& worldName) {
    return WorldExportsDir(runtimeRoot) + L"\\" + SafeFileName(profileId) + L"\\" + SafeFileName(worldName) + L".zip";
}

std::vector<std::wstring> ListProfileWorlds(const std::wstring& runtimeRoot, const std::wstring& profileId) {
    std::vector<std::wstring> worlds;
    EnsureProfileGameDataInitialized(runtimeRoot, profileId);
    const std::wstring savesDir = ProfileGameDir(runtimeRoot, profileId) + L"\\saves";
    WIN32_FIND_DATAW fd = {};
    HANDLE h = FindFirstFileW((savesDir + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return worlds;
    do {
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            worlds.push_back(fd.cFileName);
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    std::sort(worlds.begin(), worlds.end(), [](const std::wstring& a, const std::wstring& b) {
        return ToLowerW(a) < ToLowerW(b);
    });
    return worlds;
}

static std::string ArchivePathJoin(const std::string& prefix, const std::wstring& rel) {
    std::string relA = w2a(rel);
    std::replace(relA.begin(), relA.end(), '\\', '/');
    if (prefix.empty()) return relA;
    if (relA.empty()) return prefix;
    return prefix + relA;
}

static bool CollectDirectoryFiles(
    const std::wstring& rootDir,
    const std::wstring& relDir,
    std::vector<std::pair<std::wstring, std::wstring>>& out) {
    const std::wstring scanDir = relDir.empty() ? rootDir : rootDir + L"\\" + relDir;
    WIN32_FIND_DATAW fd = {};
    HANDLE h = FindFirstFileW((scanDir + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return GetLastError() == ERROR_FILE_NOT_FOUND;
    bool ok = true;
    do {
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
        const std::wstring childRel = relDir.empty() ? fd.cFileName : relDir + L"\\" + fd.cFileName;
        const std::wstring fullPath = rootDir + L"\\" + childRel;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (!CollectDirectoryFiles(rootDir, childRel, out)) { ok = false; break; }
        } else {
            out.push_back({ fullPath, childRel });
        }
    } while (FindNextFileW(h, &fd));
    if (GetLastError() != ERROR_NO_MORE_FILES) ok = false;
    FindClose(h);
    return ok;
}

struct ZipFileWriterState {
    FILE* file = nullptr;
};

static size_t ZipFileWriteCallback(void* opaque, mz_uint64 file_ofs, const void* pBuf, size_t n) {
    auto* state = static_cast<ZipFileWriterState*>(opaque);
    if (!state || !state->file || !pBuf) return 0;
    if (_fseeki64(state->file, static_cast<__int64>(file_ofs), SEEK_SET) != 0) return 0;
    return fwrite(pBuf, 1, n, state->file) == n ? n : 0;
}

static size_t ZipReadFileCallback(void* opaque, mz_uint64 offset, void* data, size_t size) {
    FILE* file = static_cast<FILE*>(opaque);
    if (_fseeki64(file, static_cast<__int64>(offset), SEEK_SET) != 0) return 0;
    return fread(data, 1, size, file);
}

static bool ZipAddFileFromPath(mz_zip_archive* zip, const std::string& archiveName, const std::wstring& path) {
    FILE* file = nullptr;
    if (_wfopen_s(&file, path.c_str(), L"rb") != 0 || !file) return false;
    if (_fseeki64(file, 0, SEEK_END) != 0) { fclose(file); return false; }
    const __int64 size = _ftelli64(file);
    bool ok = size >= 0 && mz_zip_writer_add_read_buf_callback(zip, archiveName.c_str(), ZipReadFileCallback,
        file, static_cast<mz_uint64>(size), nullptr, nullptr, 0, static_cast<mz_uint>(MZ_DEFAULT_COMPRESSION), nullptr, 0, nullptr, 0) != 0;
    if (fclose(file) != 0) ok = false;
    return ok;
}

bool ExportWorldZip(
    const std::wstring& runtimeRoot,
    const std::wstring& profileId,
    const std::wstring& worldName,
    const std::wstring& outputPath,
    std::wstring& error) {
    error.clear();
    if (!IsSafeWorldName(worldName)) {
        error = L"Invalid world name";
        return false;
    }

    EnsureProfileGameDataInitialized(runtimeRoot, profileId);
    const std::wstring worldDir = ProfileGameDir(runtimeRoot, profileId) + L"\\saves\\" + worldName;
    if (!DirectoryExists(worldDir)) {
        error = L"World not found";
        return false;
    }
    if (GetFileAttributesW((worldDir + L"\\level.dat").c_str()) == INVALID_FILE_ATTRIBUTES &&
        GetFileAttributesW((worldDir + L"\\level.dat_old").c_str()) == INVALID_FILE_ATTRIBUTES) {
        error = L"This folder does not look like a Minecraft world";
        return false;
    }

    std::vector<std::pair<std::wstring, std::wstring>> files;
    if (!CollectDirectoryFiles(worldDir, L"", files)) { error = L"Could not read every world folder"; return false; }
    if (files.empty()) {
        error = L"World folder is empty";
        return false;
    }

    const std::wstring temporary = outputPath + L".exporting";
    DeleteFileW(temporary.c_str());
    EnsureDirectoryTree(GetParentDir(outputPath));

    FILE* outFile = nullptr;
    if (_wfopen_s(&outFile, temporary.c_str(), L"wb") != 0 || !outFile) {
        error = L"Could not create world archive";
        return false;
    }

    ZipFileWriterState writerState{ outFile };
    mz_zip_archive zip{};
    zip.m_pIO_opaque = &writerState;
    zip.m_pWrite = ZipFileWriteCallback;
    if (!mz_zip_writer_init(&zip, 0)) {
        fclose(outFile);
        DeleteFileW(temporary.c_str());
        error = L"Could not start world archive";
        return false;
    }

    const std::string prefix = w2a(worldName) + "/";
    int added = 0;
    bool complete = true;
    for (const auto& entry : files) {
        const std::string archiveName = ArchivePathJoin(prefix, entry.second);
        if (ZipAddFileFromPath(&zip, archiveName, entry.first)) {
            ++added;
        } else {
            WriteLogF(L"Export world failed file %s", entry.second.c_str());
            complete = false;
            break;
        }
    }

    const bool finalized = complete && added > 0 && mz_zip_writer_finalize_archive(&zip) != 0;
    const bool ended = mz_zip_writer_end(&zip) != 0;
    const bool closed = fclose(outFile) == 0;
    if (!finalized || !ended || !closed ||
        !MoveFileExW(temporary.c_str(), outputPath.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(temporary.c_str());
        error = L"Could not finish world archive. Previous export kept";
        return false;
    }

    WriteLogF(L"Exported world %s to %s files=%d", worldName.c_str(), outputPath.c_str(), added);
    return true;
}

static bool IsSafeRelativePathForWorld(const std::wstring& rel) {
    if (rel.empty()) return false;
    if (rel.find(L':') != std::wstring::npos) return false;
    if (!rel.empty() && (rel.front() == L'\\' || rel.front() == L'/')) return false;
    size_t start = 0;
    while (start <= rel.size()) {
        const size_t slash = rel.find_first_of(L"\\/", start);
        const std::wstring part = rel.substr(start, slash == std::wstring::npos ? std::wstring::npos : slash - start);
        if (part == L"." || part == L"..") return false;
        if (slash == std::wstring::npos) break;
        start = slash + 1;
    }
    return true;
}

static std::string NormalizeZipEntryName(const std::string& name) {
    std::string out = name;
    std::replace(out.begin(), out.end(), '\\', '/');
    while (!out.empty() && out.front() == '/') out.erase(out.begin());
    return out;
}

static int ZipSlashDepth(const std::string& prefix) {
    int depth = 0;
    for (char c : prefix) if (c == '/') ++depth;
    return depth;
}

// real worlds ship both level.dat and level.dat_old and may sit at any depth in the zip,
// so take the shallowest directory holding a level file as the world root; deeper level
// files (backups inside the world) are ignored, two worlds at the same depth are ambiguous
static bool DetectWorldZipPrefix(mz_zip_archive& zip, std::string& stripPrefix) {
    stripPrefix.clear();
    std::string best;
    bool have = false;
    bool tie = false;
    int bestDepth = 0;
    const mz_uint count = mz_zip_reader_get_num_files(&zip);
    for (mz_uint i = 0; i < count; ++i) {
        if (mz_zip_reader_is_file_a_directory(&zip, i)) continue;
        mz_zip_archive_file_stat st{};
        if (!mz_zip_reader_file_stat(&zip, i, &st)) continue;
        const std::string name = NormalizeZipEntryName(st.m_filename);
        if (name.empty()) continue;
        const size_t slash = name.find_last_of('/');
        const std::string base = slash == std::string::npos ? name : name.substr(slash + 1);
        if (base != "level.dat" && base != "level.dat_old") continue;
        const std::string prefix = slash == std::string::npos ? std::string() : name.substr(0, slash + 1);
        const int depth = ZipSlashDepth(prefix);
        if (!have || depth < bestDepth) {
            best = prefix;
            bestDepth = depth;
            have = true;
            tie = false;
        } else if (depth == bestDepth && prefix != best) {
            tie = true;
        }
    }
    if (!have || tie) return false;
    stripPrefix = best;
    return true;
}

bool ImportWorldFromZip(
    const std::wstring& zipPath,
    const std::wstring& runtimeRoot,
    const std::wstring& profileId,
    const std::wstring& worldName,
    bool replaceExisting,
    std::wstring& error) {
    error.clear();
    if (profileId == kVanillaProfileId) {
        error = L"Vanilla is read-only. Pick or create a profile first.";
        return false;
    }
    if (!IsSafeWorldName(worldName)) {
        error = L"Invalid world name";
        return false;
    }
    if (GetFileAttributesW(zipPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
        error = L"World archive not found";
        return false;
    }

    FILE* inputFile = nullptr;
    if (_wfopen_s(&inputFile, zipPath.c_str(), L"rb") != 0 || !inputFile) {
        error = L"Could not open world archive";
        return false;
    }
    std::unique_ptr<FILE, decltype(&fclose)> input(inputFile, fclose);
    if (_fseeki64(input.get(), 0, SEEK_END) != 0) { error = L"Could not read archive size"; return false; }
    const __int64 archiveSize = _ftelli64(input.get());
    if (archiveSize <= 0 || archiveSize > 128ll * 1024ll * 1024ll) {
        error = L"World archive is empty or exceeds the 128 MB input limit";
        return false;
    }
    mz_zip_archive zip{};
    zip.m_pRead = ZipReadFileCallback;
    zip.m_pIO_opaque = input.get();
    if (!mz_zip_reader_init(&zip, static_cast<mz_uint64>(archiveSize), 0)) {
        error = L"World archive is not a valid ZIP archive";
        return false;
    }

    std::string stripPrefix;
    if (!DetectWorldZipPrefix(zip, stripPrefix)) {
        mz_zip_reader_end(&zip);
        error = L"Zip does not contain a single Minecraft world";
        return false;
    }

    EnsureProfileGameDataInitialized(runtimeRoot, profileId);
    const std::wstring destDir = ProfileGameDir(runtimeRoot, profileId) + L"\\saves\\" + worldName;
    if (DirectoryExists(destDir) && !replaceExisting) {
        mz_zip_reader_end(&zip);
        error = L"A world with that name already exists. Enable replace to overwrite it.";
        return false;
    }
    const std::wstring temporary = destDir + L".importing-" + std::to_wstring(GetTickCount64());
    if (!EnsureDirectoryTree(temporary)) { mz_zip_reader_end(&zip); error = L"Could not stage world"; return false; }
    int extracted = 0;
    bool ok = true;
    unsigned long long total = 0;
    const mz_uint count = mz_zip_reader_get_num_files(&zip);
    if (count > 20000) ok = false;
    for (mz_uint i = 0; ok && i < count; ++i) {
        if (mz_zip_reader_is_file_a_directory(&zip, i)) continue;
        mz_zip_archive_file_stat st{};
        if (!mz_zip_reader_file_stat(&zip, i, &st)) { ok = false; break; }
        if (st.m_uncomp_size > 64ull * 1024ull * 1024ull || st.m_uncomp_size > 2ull * 1024ull * 1024ull * 1024ull - total) { ok = false; break; }
        total += st.m_uncomp_size;
        std::string name = NormalizeZipEntryName(st.m_filename);
        if (!stripPrefix.empty()) {
            if (name.rfind(stripPrefix, 0) != 0) continue;
            name = name.substr(stripPrefix.size());
        }
        if (name.empty()) continue;
        std::wstring rel = a2w(name.c_str());
        std::replace(rel.begin(), rel.end(), L'/', L'\\');
        if (!IsSafeRelativePathForWorld(rel)) { ok = false; break; }
        size_t size = 0;
        void* data = mz_zip_reader_extract_to_heap(&zip, i, &size, 0);
        ok = data && WriteAllBytes(temporary + L"\\" + rel, data, size);
        mz_free(data);
        if (ok) ++extracted;
    }
    mz_zip_reader_end(&zip);
    ok = ok && extracted > 0 &&
        (GetFileAttributesW((temporary + L"\\level.dat").c_str()) != INVALID_FILE_ATTRIBUTES ||
         GetFileAttributesW((temporary + L"\\level.dat_old").c_str()) != INVALID_FILE_ATTRIBUTES);
    if (!ok) {
        DeleteDirectoryTree(temporary);
        error = L"World import failed or exceeded archive limits. Existing world kept";
        return false;
    }
    const std::wstring previous = destDir + L".previous-" + std::to_wstring(GetTickCount64());
    const bool hadWorld = DirectoryExists(destDir);
    if (hadWorld && !MoveFileExW(destDir.c_str(), previous.c_str(), MOVEFILE_WRITE_THROUGH)) {
        DeleteDirectoryTree(temporary); error = L"Could not preserve existing world"; return false;
    }
    if (!MoveFileExW(temporary.c_str(), destDir.c_str(), MOVEFILE_WRITE_THROUGH)) {
        if (hadWorld && !MoveFileExW(previous.c_str(), destDir.c_str(), MOVEFILE_WRITE_THROUGH)) {
            error = L"Could not restore world. Original retained at " + previous;
        } else { error = L"Could not install replacement. Existing world kept"; }
        DeleteDirectoryTree(temporary);
        return false;
    }
    if (hadWorld) DeleteDirectoryTree(previous);

    WriteLogF(L"Imported world %s into profile %s files=%d", worldName.c_str(), profileId.c_str(), extracted);
    return true;
}
