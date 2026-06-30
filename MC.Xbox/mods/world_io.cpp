#include "world_io.h"

#include "launcher_common.h"
#include "profiles.h"

#include "third_party/miniz/miniz.h"

#include <algorithm>
#include <vector>

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

std::wstring DefaultWorldExportPath(const std::wstring& runtimeRoot, const std::wstring& worldName) {
    return WorldExportsDir(runtimeRoot) + L"\\" + SafeFileName(worldName) + L".zip";
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

static void CollectDirectoryFiles(
    const std::wstring& rootDir,
    const std::wstring& relDir,
    std::vector<std::pair<std::wstring, std::wstring>>& out) {
    const std::wstring scanDir = relDir.empty() ? rootDir : rootDir + L"\\" + relDir;
    WIN32_FIND_DATAW fd = {};
    HANDLE h = FindFirstFileW((scanDir + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
        const std::wstring childRel = relDir.empty() ? fd.cFileName : relDir + L"\\" + fd.cFileName;
        const std::wstring fullPath = rootDir + L"\\" + childRel;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            CollectDirectoryFiles(rootDir, childRel, out);
        } else {
            out.push_back({ fullPath, childRel });
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
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

static bool ZipAddFileFromPath(mz_zip_archive* zip, const std::string& archiveName, const std::wstring& path) {
    std::vector<unsigned char> bytes;
    if (!ReadBinaryFileLimited(path, bytes, 256ull * 1024ull * 1024ull)) return false;
    if (bytes.empty()) return false;
    return mz_zip_writer_add_mem(zip, archiveName.c_str(), bytes.data(), bytes.size(), MZ_DEFAULT_COMPRESSION) != 0;
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
    CollectDirectoryFiles(worldDir, L"", files);
    if (files.empty()) {
        error = L"World folder is empty";
        return false;
    }

    DeleteFileW(outputPath.c_str());
    EnsureDirectoryTree(GetParentDir(outputPath));

    FILE* outFile = nullptr;
    if (_wfopen_s(&outFile, outputPath.c_str(), L"wb") != 0 || !outFile) {
        error = L"Could not create world archive";
        return false;
    }

    ZipFileWriterState writerState{ outFile };
    mz_zip_archive zip{};
    zip.m_pIO_opaque = &writerState;
    zip.m_pWrite = ZipFileWriteCallback;
    if (!mz_zip_writer_init(&zip, 0)) {
        fclose(outFile);
        DeleteFileW(outputPath.c_str());
        error = L"Could not start world archive";
        return false;
    }

    const std::string prefix = w2a(worldName) + "/";
    int added = 0;
    for (const auto& entry : files) {
        const std::string archiveName = ArchivePathJoin(prefix, entry.second);
        if (ZipAddFileFromPath(&zip, archiveName, entry.first)) {
            ++added;
        } else {
            WriteLogF(L"Export world skipped file: %s", entry.second.c_str());
        }
    }

    if (added == 0 || !mz_zip_writer_finalize_archive(&zip) || !mz_zip_writer_end(&zip)) {
        fclose(outFile);
        DeleteFileW(outputPath.c_str());
        error = L"Could not finalize world archive";
        return false;
    }
    fclose(outFile);

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

    std::vector<unsigned char> packBytes;
    if (!ReadBinaryFileLimited(zipPath, packBytes, 512ull * 1024ull * 1024ull)) {
        error = L"World archive is too large or could not be read";
        return false;
    }
    if (packBytes.empty()) {
        error = L"World archive was empty";
        return false;
    }

    mz_zip_archive zip{};
    if (!mz_zip_reader_init_mem(&zip, packBytes.data(), packBytes.size(), 0)) {
        error = L"World archive is not a valid zip file";
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
    if (DirectoryExists(destDir) && replaceExisting) {
        if (!DeleteDirectoryTree(destDir)) {
            mz_zip_reader_end(&zip);
            error = L"Could not remove the existing world";
            return false;
        }
    }
    EnsureDirectoryTree(destDir);

    int extracted = 0;
    const mz_uint count = mz_zip_reader_get_num_files(&zip);
    for (mz_uint i = 0; i < count; ++i) {
        if (mz_zip_reader_is_file_a_directory(&zip, i)) continue;
        mz_zip_archive_file_stat st{};
        if (!mz_zip_reader_file_stat(&zip, i, &st)) continue;
        std::string name = NormalizeZipEntryName(st.m_filename);
        if (name.empty()) continue;
        if (!stripPrefix.empty()) {
            if (name.rfind(stripPrefix, 0) != 0) continue;
            name = name.substr(stripPrefix.size());
        }
        if (name.empty()) continue;
        if (name.find("..") != std::string::npos) continue;

        const std::wstring rel = a2w(name.c_str());
        const std::wstring destPath = destDir + L"\\" + rel;
        if (!IsSafeRelativePathForWorld(rel)) {
            WriteLogF(L"Skipping unsafe world zip entry: %s", rel.c_str());
            continue;
        }
        EnsureDirectoryTree(GetParentDir(destPath));

        size_t outSize = 0;
        void* p = mz_zip_reader_extract_to_heap(&zip, i, &outSize, 0);
        if (!p) continue;
        if (WriteAllBytes(destPath, p, outSize)) {
            ++extracted;
        }
        mz_free(p);
    }

    mz_zip_reader_end(&zip);
    if (extracted == 0) {
        error = L"No world files were extracted";
        return false;
    }
    if (GetFileAttributesW((destDir + L"\\level.dat").c_str()) == INVALID_FILE_ATTRIBUTES &&
        GetFileAttributesW((destDir + L"\\level.dat_old").c_str()) == INVALID_FILE_ATTRIBUTES) {
        error = L"Imported archive did not produce a valid world";
        return false;
    }

    WriteLogF(L"Imported world %s into profile %s files=%d", worldName.c_str(), profileId.c_str(), extracted);
    return true;
}
