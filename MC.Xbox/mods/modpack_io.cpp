#include "modpack_io.h"

#include "launcher_common.h"
#include "curseforge.h"
#include "manual_downloads.h"
#include "mod_defaults.h"
#include "mods_browser.h"
#include "profiles.h"
#include "runtime_manager.h"
#include "http_client.h"

#include "third_party/miniz/miniz.h"

#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>

#include <algorithm>
#include <fstream>
#include <vector>

using namespace winrt::Windows::Data::Json;

// pack paths come from the archive, so anything that could climb out of the game dir is refused.
// win32 drops trailing dots and spaces from a segment, so ".. " and "..." count as climbing too
static bool IsSafePackRelativePath(const std::wstring& rel) {
    if (rel.empty() || rel[0] == L'\\' || rel.find(L':') != std::wstring::npos) return false;
    size_t start = 0;
    while (true) {
        const size_t sep = rel.find(L'\\', start);
        const std::wstring segment = rel.substr(start, sep == std::wstring::npos ? std::wstring::npos : sep - start);
        const size_t kept = segment.find_last_not_of(L". ");
        if (!segment.empty() && segment != L"." && kept == std::wstring::npos) return false;
        if (sep == std::wstring::npos) return true;
        start = sep + 1;
    }
}

// empty when the path is unsafe, callers skip the entry
static std::wstring ModpackDestForRelative(const std::wstring& relRaw, const std::wstring& gameDir, const std::wstring& userModsDir) {
    std::wstring rel = relRaw;
    std::replace(rel.begin(), rel.end(), L'/', L'\\');
    if (!IsSafePackRelativePath(rel)) return std::wstring();
    const std::wstring lower = ToLowerW(rel);
    const size_t slash = rel.find_last_of(L'\\');
    const std::wstring base = slash == std::wstring::npos ? rel : rel.substr(slash + 1);
    if (lower.rfind(L"mods\\", 0) == 0) {
        return userModsDir + L"\\" + base;
    }
    return gameDir + L"\\" + rel;
}

static std::wstring ModrinthDependencyLoaderKey(const std::wstring& loader) {
    std::wstring l = loader;
    for (auto& c : l) c = static_cast<wchar_t>(towlower(c));
    if (l == L"fabric") return L"fabric-loader";
    if (l == L"quilt") return L"quilt-loader";
    if (l == L"neoforge") return L"neoforge";
    if (l == L"forge") return L"forge";
    return L"";
}

static bool ReadBinaryFileAll(const std::wstring& path, std::vector<unsigned char>& out) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return false;
    const std::streamoff sz = f.tellg();
    if (sz <= 0) return false;
    out.resize(static_cast<size_t>(sz));
    f.seekg(0);
    f.read(reinterpret_cast<char*>(out.data()), sz);
    return f.good() || f.eof();
}

static bool WriteAllBytes(const std::wstring& path, const void* data, size_t size) {
    EnsureDirectoryTree(GetParentDir(path));
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"wb") != 0 || !f) return false;
    const bool ok = fwrite(data, 1, size, f) == size;
    fclose(f);
    return ok;
}

std::wstring ProfileExportsDir(const std::wstring& runtimeRoot) {
    return runtimeRoot + L"\\exports";
}

std::wstring DefaultProfileExportPath(const std::wstring& runtimeRoot, const std::wstring& profileId) {
    const Profile profile = GetProfileById(runtimeRoot, profileId);
    std::wstring stem = profile.name.empty() ? profileId : profile.name;
    for (wchar_t& ch : stem) {
        if (ch == L'\\' || ch == L'/' || ch == L':' || ch == L'*' || ch == L'?' || ch == L'"' || ch == L'<' || ch == L'>' || ch == L'|') {
            ch = L'_';
        }
    }
    if (stem.empty()) stem = profileId;
    return ProfileExportsDir(runtimeRoot) + L"\\" + SafeFileName(stem) + L".mrpack";
}

bool InstallModpackFromFile(
    const std::wstring& mrpackPath,
    const std::wstring& runtimeRoot,
    const std::wstring& profileId,
    std::wstring& error,
    std::wstring* skippedNote,
    const ModpackProgressFactory& progressFor) {
    error.clear();
    if (skippedNote) skippedNote->clear();
    if (profileId == kVanillaProfileId) {
        error = L"Vanilla is read-only. Pick or create a profile first.";
        return false;
    }
    if (GetFileAttributesW(mrpackPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
        error = L"Pack file not found";
        return false;
    }

    EnsureProfileGameDataInitialized(runtimeRoot, profileId);
    const std::wstring gameDir = ProfileGameDir(runtimeRoot, profileId);
    const std::wstring userModsDir = ProfileModsDir(runtimeRoot, profileId);
    ConfigureKnownModDefaults(gameDir, userModsDir, GetProfileById(runtimeRoot, profileId).minecraftVersion);

    std::vector<unsigned char> packBytes;
    if (!ReadBinaryFileAll(mrpackPath, packBytes)) {
        error = L"Could not read pack file";
        return false;
    }
    if (packBytes.empty()) {
        error = L"Pack file was empty";
        return false;
    }

    mz_zip_archive zip{};
    if (!mz_zip_reader_init_mem(&zip, packBytes.data(), packBytes.size(), 0)) {
        error = L"Pack is not a valid .mrpack archive";
        return false;
    }

    struct PackFile { std::wstring path; std::wstring url; std::string sha1; unsigned long long size = 0; bool curseForge = false; };
    std::vector<PackFile> jobs;
    int skipped = 0;
    int blockedCount = 0;
    int missingCount = 0;
    int unsupportedCount = 0;
    std::vector<ManualDownload> manual;

    auto readZipText = [&](const char* entry, std::string& out) {
        const int idx = mz_zip_reader_locate_file(&zip, entry, nullptr, 0);
        if (idx < 0) return false;
        size_t outSize = 0;
        void* p = mz_zip_reader_extract_to_heap(&zip, static_cast<mz_uint>(idx), &outSize, 0);
        if (!p) return false;
        out.assign(static_cast<const char*>(p), outSize);
        mz_free(p);
        return true;
    };

    std::string indexJson;
    std::string manifestJson;
    if (!readZipText("modrinth.index.json", indexJson) && !readZipText("manifest.json", manifestJson)) {
        mz_zip_reader_end(&zip);
        error = L"Pack has no modrinth.index.json or manifest.json";
        return false;
    }

    if (!indexJson.empty()) {
    try {
        JsonObject root = JsonObject::Parse(winrt::to_hstring(indexJson));
        JsonArray files{ nullptr };
        if (root.HasKey(L"files") && root.GetNamedValue(L"files").ValueType() == JsonValueType::Array) {
            files = root.GetNamedArray(L"files");
        }
        const uint32_t fcount = files ? files.Size() : 0;
        for (uint32_t i = 0; i < fcount; ++i) {
            auto v = files.GetAt(i);
            if (v.ValueType() != JsonValueType::Object) continue;
            JsonObject fo = v.GetObject();
            if (fo.HasKey(L"env") && fo.GetNamedValue(L"env").ValueType() == JsonValueType::Object) {
                JsonObject env = fo.GetNamedObject(L"env");
                if (env.HasKey(L"client") && env.Lookup(L"client").ValueType() == JsonValueType::String &&
                    env.Lookup(L"client").GetString() == L"unsupported") {
                    continue;
                }
            }
            const std::wstring path = fo.Lookup(L"path").GetString().c_str();
            if (path.empty()) continue;
            std::wstring durl;
            if (fo.HasKey(L"downloads") && fo.GetNamedValue(L"downloads").ValueType() == JsonValueType::Array) {
                JsonArray dls = fo.GetNamedArray(L"downloads");
                if (dls.Size() > 0 && dls.GetAt(0).ValueType() == JsonValueType::String) {
                    durl = dls.GetAt(0).GetString().c_str();
                }
            }
            std::string sha1;
            if (fo.HasKey(L"hashes") && fo.GetNamedValue(L"hashes").ValueType() == JsonValueType::Object) {
                sha1 = w2a(fo.GetNamedObject(L"hashes").Lookup(L"sha1").GetString().c_str());
            }
            unsigned long long fsize = 0;
            if (fo.HasKey(L"fileSize") && fo.GetNamedValue(L"fileSize").ValueType() == JsonValueType::Number) {
                fsize = static_cast<unsigned long long>(fo.GetNamedNumber(L"fileSize"));
            }
            const size_t slash = path.find_last_of(L"/\\");
            const std::wstring base = slash == std::wstring::npos ? path : path.substr(slash + 1);
            if (IsBlockedModFileName(base)) {
                WriteLogF(L"Skipping blocked modpack file: %s", base.c_str());
                ++skipped;
                continue;
            }
            jobs.push_back({ path, durl, sha1, fsize });
        }
    } catch (const winrt::hresult_error&) {
        mz_zip_reader_end(&zip);
        error = L"Could not parse pack index";
        return false;
    }
    }

    if (!manifestJson.empty()) {
        std::vector<long long> fileIds;
        try {
            JsonObject root = JsonObject::Parse(winrt::to_hstring(manifestJson));
            if (root.HasKey(L"files") && root.GetNamedValue(L"files").ValueType() == JsonValueType::Array) {
                JsonArray arr = root.GetNamedArray(L"files");
                for (uint32_t i = 0; i < arr.Size(); ++i) {
                    if (arr.GetAt(i).ValueType() != JsonValueType::Object) continue;
                    JsonObject fo = arr.GetAt(i).GetObject();
                    if (!fo.HasKey(L"fileID") || fo.GetNamedValue(L"fileID").ValueType() != JsonValueType::Number) continue;
                    const long long fileId = static_cast<long long>(fo.GetNamedNumber(L"fileID"));
                    if (fileId > 0) fileIds.push_back(fileId);
                }
            }
        } catch (const winrt::hresult_error&) {
            mz_zip_reader_end(&zip);
            error = L"Could not parse pack manifest";
            return false;
        }

        // a curseforge manifest carries ids only, so every entry needs resolving before download
        std::vector<curseforge::FileRef> refs;
        std::wstring resolveError;
        if (progressFor) {
            const auto lookup = progressFor(L"Looking up " + std::to_wstring(fileIds.size()) + L" files on CurseForge", 0);
            if (lookup) lookup(0);
        }
        if (!curseforge::ResolveFilesById(fileIds, refs, resolveError)) {
            mz_zip_reader_end(&zip);
            error = resolveError.empty() ? L"Could not resolve this pack on CurseForge" : resolveError;
            return false;
        }

        // the batch endpoint silently drops deleted files
        for (const long long fileId : fileIds) {
            const bool found = std::any_of(refs.begin(), refs.end(),
                [fileId](const curseforge::FileRef& ref) { return ref.fileId == fileId; });
            if (found) continue;
            ++missingCount;
            WriteLogF(L"Pack file %lld is gone from CurseForge", fileId);
        }

        // class id picks the folder, and the same lookup names blocked files
        std::vector<long long> modIds;
        for (const curseforge::FileRef& ref : refs) modIds.push_back(ref.modId);
        std::vector<curseforge::ModInfo> infos;
        std::wstring infoError;
        if (!curseforge::ResolveModInfo(modIds, infos, infoError)) {
            WriteLogF(L"Pack project lookup failed, routing jars only: %s", infoError.c_str());
        }
        auto infoFor = [&infos](long long modId) -> const curseforge::ModInfo* {
            for (const curseforge::ModInfo& info : infos) {
                if (info.modId == modId) return &info;
            }
            return nullptr;
        };

        for (const curseforge::FileRef& ref : refs) {
            const curseforge::ModInfo* info = infoFor(ref.modId);
            // without project info only a jar is safe to call a mod
            const bool routable = info ? info->kindKnown : EndsWithInsensitive(ref.fileName, L".jar");
            if (!routable) {
                ++unsupportedCount;
                WriteLogF(L"Pack file has no folder the launcher installs to: %s", ref.fileName.c_str());
                continue;
            }
            const ContentKind kind = info ? info->kind : ContentKind::Mod;
            if (kind == ContentKind::Modpack) {
                ++unsupportedCount;
                WriteLogF(L"Pack lists another modpack, skipped: %s", ref.fileName.c_str());
                continue;
            }
            if (kind == ContentKind::Mod && IsBlockedModFileName(ref.fileName)) {
                WriteLogF(L"Skipping blocked modpack file: %s", ref.fileName.c_str());
                ++skipped;
                continue;
            }
            if (ref.distributionBlocked) {
                ManualDownload item;
                item.fileName = ref.fileName;
                item.modName = info && !info->name.empty() ? info->name : ref.fileName;
                item.folder = ContentFolder(kind);
                // the website's own download link, one click in the user's browser saves the file
                item.url = L"https://www.curseforge.com/api/v1/mods/" + std::to_wstring(ref.modId) +
                    L"/files/" + std::to_wstring(ref.fileId) + L"/download";
                manual.push_back(item);
                ++blockedCount;
                WriteLogF(L"Pack file blocks third party download: %s", ref.fileName.c_str());
                continue;
            }
            PackFile job;
            job.path = std::wstring(ContentFolder(kind)) + L"/" + SafeFileName(ref.fileName);
            job.url = ref.downloadUrl;
            job.sha1 = ref.sha1;
            job.size = ref.fileSize;
            job.curseForge = true;
            jobs.push_back(job);
        }
    }

    std::wstring firstError;
    int done = 0;
    for (const PackFile& job : jobs) {
        const std::wstring dest = ModpackDestForRelative(job.path, gameDir, userModsDir);
        if (dest.empty()) {
            WriteLogF(L"Refusing pack file outside the game dir: %s", job.path.c_str());
            ++skipped;
            continue;
        }
        const size_t bslash = job.path.find_last_of(L"/\\");
        const std::wstring base = bslash == std::wstring::npos ? job.path : job.path.substr(bslash + 1);
        WriteLogF(L"Modpack install file %d/%zu: %s", done + 1, jobs.size(), base.c_str());
        const std::wstring progressLabel =
            L"[" + std::to_wstring(done + 1) + L"/" + std::to_wstring(jobs.size()) + L"] " + base;
        const auto fileProgress = progressFor ? progressFor(progressLabel, job.size) : nullptr;
        if (fileProgress) fileProgress(0);
        if (!job.sha1.empty() && FileMatchesSha1(dest, job.sha1)) {
            ++done;
            continue;
        }
        EnsureDirectoryTree(GetParentDir(dest));
        const std::wstring tmp = dest + L".download";
        DeleteFileW(tmp.c_str());
        bool fileOk = false;
        if (!job.url.empty()) {
            bool fetched = false;
            if (job.curseForge) {
                curseforge::FileRef ref;
                ref.downloadUrl = job.url;
                fetched = curseforge::DownloadFile(ref, tmp, fileProgress);
            } else {
                fetched = DownloadUrlToFile(job.url, tmp, fileProgress);
            }
            fileOk = fetched && (job.sha1.empty() || FileMatchesSha1(tmp, job.sha1));
        } else {
            std::string relA = w2a(job.path);
            std::replace(relA.begin(), relA.end(), '\\', '/');
            const int embedded = mz_zip_reader_locate_file(&zip, relA.c_str(), nullptr, 0);
            if (embedded >= 0) {
                size_t outSize = 0;
                void* p = mz_zip_reader_extract_to_heap(&zip, static_cast<mz_uint>(embedded), &outSize, 0);
                if (p && outSize > 0) {
                    fileOk = WriteAllBytes(tmp, p, outSize);
                    if (fileOk && !job.sha1.empty()) {
                        fileOk = FileMatchesSha1(tmp, job.sha1);
                    }
                    mz_free(p);
                }
            }
        }
        if (fileOk) {
            DeleteFileW(dest.c_str());
            if (!MoveFileExW(tmp.c_str(), dest.c_str(), MOVEFILE_REPLACE_EXISTING)) {
                DeleteFileW(tmp.c_str());
                if (firstError.empty()) firstError = L"Some pack files could not be saved";
            }
        } else {
            DeleteFileW(tmp.c_str());
            if (firstError.empty()) firstError = L"Some pack files failed to install";
        }
        ++done;
    }

    WriteLog(L"Applying modpack overrides...");
    const mz_uint entryCount = mz_zip_reader_get_num_files(&zip);
    for (mz_uint i = 0; i < entryCount; ++i) {
        if (mz_zip_reader_is_file_a_directory(&zip, i)) continue;
        mz_zip_archive_file_stat st;
        if (!mz_zip_reader_file_stat(&zip, i, &st)) continue;
        std::string name = st.m_filename;
        std::string prefix;
        if (name.rfind("overrides/", 0) == 0) prefix = "overrides/";
        else if (name.rfind("client-overrides/", 0) == 0) prefix = "client-overrides/";
        else continue;
        const std::string relA = name.substr(prefix.size());
        if (relA.empty()) continue;
        const std::wstring rel = a2w(relA.c_str());
        const std::wstring dest = ModpackDestForRelative(rel, gameDir, userModsDir);
        if (dest.empty()) {
            WriteLogF(L"Refusing pack override outside the game dir: %s", rel.c_str());
            ++skipped;
            continue;
        }
        const size_t slash = dest.find_last_of(L'\\');
        const std::wstring base = slash == std::wstring::npos ? dest : dest.substr(slash + 1);
        if (ToLowerW(dest).find(ToLowerW(userModsDir)) == 0 && IsBlockedModFileName(base)) {
            WriteLogF(L"Skipping blocked modpack override: %s", base.c_str());
            ++skipped;
            continue;
        }
        size_t outSize = 0;
        void* p = mz_zip_reader_extract_to_heap(&zip, i, &outSize, 0);
        if (!p) continue;
        WriteAllBytes(dest, p, outSize);
        mz_free(p);
    }

    mz_zip_reader_end(&zip);
    PurgeBlockedModsFromDir(runtimeRoot, userModsDir);

    WriteLogF(L"Modpack import done: %d indexed files, %d blocked", done, skipped);
    RecordManualDownloads(profileId, manual);

    std::wstring note;
    if (blockedCount > 0) {
        note = std::to_wstring(blockedCount) +
            (blockedCount == 1 ? L" file could not be downloaded" : L" files could not be downloaded") +
            L" because their authors block third party launchers. Open Remote Files, the Manual downloads panel has a link for each one.";
        WriteLogF(L"Modpack skipped %d blocked files", blockedCount);
    }
    if (missingCount > 0) {
        if (!note.empty()) note += L" ";
        note += std::to_wstring(missingCount) +
            (missingCount == 1 ? L" file in the pack no longer exists" : L" files in the pack no longer exist") +
            L" on CurseForge, so the pack may not start.";
    }
    if (unsupportedCount > 0) {
        if (!note.empty()) note += L" ";
        note += std::to_wstring(unsupportedCount) +
            (unsupportedCount == 1 ? L" file is" : L" files are") +
            L" content the launcher does not install, like worlds or data packs.";
    }
    if (skippedNote) *skippedNote = note;
    if (jobs.empty() && firstError.empty()) {
        error = note.empty() ? L"Pack had no installable client files" : note;
        return false;
    }
    if (!firstError.empty()) {
        error = firstError;
        return false;
    }
    return true;
}

static bool ZipAddBytes(mz_zip_archive* zip, const std::string& archiveName, const std::vector<unsigned char>& bytes) {
    if (bytes.empty()) return false;
    return mz_zip_writer_add_mem(zip, archiveName.c_str(), bytes.data(), bytes.size(), MZ_DEFAULT_COMPRESSION) != 0;
}

static bool ZipAddFileFromPath(mz_zip_archive* zip, const std::string& archiveName, const std::wstring& path) {
    std::vector<unsigned char> bytes;
    if (!ReadBinaryFileAll(path, bytes)) return false;
    return ZipAddBytes(zip, archiveName, bytes);
}

struct MrpackIndexedFile {
    std::wstring path;
    std::wstring downloadUrl;
    std::string sha1;
    std::string sha512;
    unsigned long long fileSize = 0;
};

static std::wstring JsonStringOrEmpty(const JsonObject& obj, const wchar_t* key) {
    if (!obj.HasKey(key) || obj.Lookup(key).ValueType() != JsonValueType::String) return L"";
    return obj.Lookup(key).GetString().c_str();
}

static bool JsonBoolOrFalse(const JsonObject& obj, const wchar_t* key) {
    if (!obj.HasKey(key) || obj.Lookup(key).ValueType() != JsonValueType::Boolean) return false;
    return obj.Lookup(key).GetBoolean();
}

static bool TryResolveModrinthIndexedFile(
    const std::wstring& localPath,
    const wchar_t* pathPrefix,
    MrpackIndexedFile& out) {
    out = {};
    std::string sha1;
    if (!Sha1File(localPath, &sha1) || sha1.empty()) return false;

    const std::wstring url =
        L"https://api.modrinth.com/v2/version_file/" + a2w(sha1.c_str()) + L"?algorithm=sha1";
    const HttpResult response = HttpGetString(url.c_str());
    if (!response.success()) return false;

    try {
        JsonObject version = JsonObject::Parse(winrt::to_hstring(response.body));
        if (!version.HasKey(L"files") || version.GetNamedValue(L"files").ValueType() != JsonValueType::Array) {
            return false;
        }
        JsonArray files = version.GetNamedArray(L"files");
        JsonObject selected = nullptr;
        for (uint32_t i = 0; i < files.Size(); ++i) {
            auto value = files.GetAt(i);
            if (value.ValueType() != JsonValueType::Object) continue;
            JsonObject file = value.GetObject();
            if (!selected || JsonBoolOrFalse(file, L"primary")) {
                selected = file;
                if (JsonBoolOrFalse(file, L"primary")) break;
            }
        }
        if (!selected) return false;

        const std::wstring downloadUrl = JsonStringOrEmpty(selected, L"url");
        const std::wstring filename = JsonStringOrEmpty(selected, L"filename");
        if (downloadUrl.empty() || filename.empty()) return false;

        std::string resolvedSha1 = sha1;
        std::string sha512;
        if (selected.HasKey(L"hashes") && selected.GetNamedValue(L"hashes").ValueType() == JsonValueType::Object) {
            JsonObject hashes = selected.GetNamedObject(L"hashes");
            const std::wstring sha1Text = JsonStringOrEmpty(hashes, L"sha1");
            const std::wstring sha512Text = JsonStringOrEmpty(hashes, L"sha512");
            if (!sha1Text.empty()) resolvedSha1 = w2a(sha1Text.c_str());
            if (!sha512Text.empty()) sha512 = w2a(sha512Text.c_str());
        }
        if (sha512.empty()) return false;

        unsigned long long fileSize = 0;
        if (selected.HasKey(L"size") && selected.GetNamedValue(L"size").ValueType() == JsonValueType::Number) {
            fileSize = static_cast<unsigned long long>(selected.GetNamedNumber(L"size"));
        }
        if (fileSize == 0) {
            WIN32_FILE_ATTRIBUTE_DATA fad = {};
            if (GetFileAttributesExW(localPath.c_str(), GetFileExInfoStandard, &fad)) {
                fileSize = (static_cast<unsigned long long>(fad.nFileSizeHigh) << 32) | fad.nFileSizeLow;
            }
        }

        out.path = std::wstring(pathPrefix) + filename;
        out.downloadUrl = downloadUrl;
        out.sha1 = resolvedSha1;
        out.sha512 = sha512;
        out.fileSize = fileSize;
        return true;
    } catch (const winrt::hresult_error&) {
        return false;
    }
}

static JsonObject MakeIndexedFileObject(const MrpackIndexedFile& file) {
    JsonObject fileObj;
    fileObj.SetNamedValue(L"path", JsonValue::CreateStringValue(file.path));
    JsonObject hashes;
    hashes.SetNamedValue(L"sha1", JsonValue::CreateStringValue(a2w(file.sha1.c_str())));
    hashes.SetNamedValue(L"sha512", JsonValue::CreateStringValue(a2w(file.sha512.c_str())));
    fileObj.SetNamedValue(L"hashes", hashes);
    fileObj.SetNamedValue(L"fileSize", JsonValue::CreateNumberValue(static_cast<double>(file.fileSize)));
    JsonArray downloads;
    downloads.Append(JsonValue::CreateStringValue(file.downloadUrl));
    fileObj.SetNamedValue(L"downloads", downloads);
    return fileObj;
}

static bool ConfigDirHasFiles(const std::wstring& configDir) {
    WIN32_FIND_DATAW fd = {};
    HANDLE h = FindFirstFileW((configDir + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return false;
    bool any = false;
    do {
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            any = true;
            break;
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return any;
}

static void AddConfigOverridesToZip(mz_zip_archive* zip, const std::wstring& configDir, const char* prefix) {
    WIN32_FIND_DATAW fd = {};
    HANDLE h = FindFirstFileW((configDir + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
        const std::wstring full = configDir + L"\\" + fd.cFileName;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        const std::string archiveName = std::string(prefix) + w2a(fd.cFileName);
        ZipAddFileFromPath(zip, archiveName, full);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

bool ExportProfileMrpack(
    const std::wstring& runtimeRoot,
    const std::wstring& profileId,
    const std::wstring& outputPath,
    std::wstring& error) {
    error.clear();
    if (profileId == kVanillaProfileId) {
        error = L"Vanilla cannot be exported as a modpack";
        return false;
    }

    const Profile profile = GetProfileById(runtimeRoot, profileId);
    const LaunchTarget target = ResolveProfileTarget(runtimeRoot, profile);
    const std::wstring gameDir = ProfileGameDir(runtimeRoot, profileId);
    const std::wstring userModsDir = ProfileModsDir(runtimeRoot, profileId);
    const std::vector<std::wstring> mods = ListProfileMods(runtimeRoot, profileId);

    JsonObject root;
    root.SetNamedValue(L"formatVersion", JsonValue::CreateNumberValue(1));
    root.SetNamedValue(L"game", JsonValue::CreateStringValue(L"minecraft"));
    root.SetNamedValue(L"versionId", JsonValue::CreateStringValue(target.minecraftVersion));
    root.SetNamedValue(L"name", JsonValue::CreateStringValue(profile.name));
    root.SetNamedValue(L"summary", JsonValue::CreateStringValue(
        L"Exported from Bandit Launcher profile " + profile.name + L" (" + TargetProfileText(target) + L")"));

    JsonArray files = JsonArray();
    std::vector<std::pair<std::string, std::wstring>> overrideFiles;
    int indexedMods = 0;
    int overrideMods = 0;

    for (const std::wstring& jar : mods) {
        if (IsBlockedModFileName(jar)) continue;
        const std::wstring jarPath = userModsDir + L"\\" + jar;
        if (GetFileAttributesW(jarPath.c_str()) == INVALID_FILE_ATTRIBUTES) continue;

        MrpackIndexedFile indexed;
        if (TryResolveModrinthIndexedFile(jarPath, L"mods/", indexed)) {
            files.Append(MakeIndexedFileObject(indexed));
            ++indexedMods;
        } else {
            overrideFiles.push_back({ "overrides/mods/" + w2a(jar), jarPath });
            ++overrideMods;
            WriteLogF(L"Export override mod (not on Modrinth CDN): %s", jar.c_str());
        }
    }

    const std::wstring resourceDir = gameDir + L"\\resourcepacks";
    WIN32_FIND_DATAW fd = {};
    HANDLE h = FindFirstFileW((resourceDir + L"\\*.zip").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            const std::wstring packPath = resourceDir + L"\\" + fd.cFileName;
            MrpackIndexedFile indexed;
            if (TryResolveModrinthIndexedFile(packPath, L"resourcepacks/", indexed)) {
                files.Append(MakeIndexedFileObject(indexed));
            } else {
                overrideFiles.push_back({ "overrides/resourcepacks/" + w2a(fd.cFileName), packPath });
                WriteLogF(L"Export override resource pack: %s", fd.cFileName);
            }
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }

    const bool hasConfigOverrides = ConfigDirHasFiles(gameDir + L"\\config");
    if (files.Size() == 0 && overrideFiles.empty() && !hasConfigOverrides) {
        error = L"This profile has no mods or resource packs to export";
        return false;
    }
    root.SetNamedValue(L"files", files);

    JsonObject deps;
    deps.SetNamedValue(L"minecraft", JsonValue::CreateStringValue(target.minecraftVersion));
    const std::wstring loaderKey = ModrinthDependencyLoaderKey(target.loader);
    if (!loaderKey.empty() && !target.loaderVersion.empty()) {
        deps.SetNamedValue(loaderKey, JsonValue::CreateStringValue(target.loaderVersion));
    }
    root.SetNamedValue(L"dependencies", deps);

    const std::wstring indexText = std::wstring(root.Stringify().c_str());
    const std::string indexUtf8 = w2a(indexText.c_str());

    DeleteFileW(outputPath.c_str());
    EnsureDirectoryTree(GetParentDir(outputPath));

    mz_zip_archive zip{};
    if (!mz_zip_writer_init_heap(&zip, 0, 1024 * 1024)) {
        error = L"Could not create export archive";
        return false;
    }

    if (!mz_zip_writer_add_mem(&zip, "modrinth.index.json", indexUtf8.data(), indexUtf8.size(), MZ_DEFAULT_COMPRESSION)) {
        mz_zip_writer_end(&zip);
        error = L"Could not write pack index";
        return false;
    }

    for (const auto& overrideFile : overrideFiles) {
        if (!ZipAddFileFromPath(&zip, overrideFile.first, overrideFile.second)) {
            WriteLogF(L"Export skipped override file: %s", a2w(overrideFile.first.c_str()).c_str());
        }
    }

    AddConfigOverridesToZip(&zip, gameDir + L"\\config", "overrides/config/");

    void* heapBuf = nullptr;
    size_t heapSize = 0;
    if (!mz_zip_writer_finalize_heap_archive(&zip, &heapBuf, &heapSize) || !heapBuf || heapSize == 0) {
        mz_zip_writer_end(&zip);
        error = L"Could not finalize export archive";
        return false;
    }
    const bool wrote = WriteAllBytes(outputPath, heapBuf, heapSize);
    mz_free(heapBuf);
    mz_zip_writer_end(&zip);
    if (!wrote) {
        DeleteFileW(outputPath.c_str());
        error = L"Could not write export archive";
        return false;
    }

    WriteLogF(L"Exported profile %s as mrpack: %s indexed=%d override=%d",
        profileId.c_str(), outputPath.c_str(), indexedMods, overrideMods);
    return true;
}
