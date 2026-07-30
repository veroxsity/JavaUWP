#include "runtime_manager.h"

#include "loader.h"
#include "launcher_common.h"
#include "profiles.h"
#include "runtime_config.h"

#include <winhttp.h>
#include <bcrypt.h>

#include <algorithm>
#include <fstream>
#include <functional>
#include <mutex>
#include <sstream>
#include <thread>
#include <vector>

#include <winrt/Windows.Security.ExchangeActiveSyncProvisioning.h>

#include "third_party/miniz/miniz.h"

static std::wstring RuntimeSeedStamp(const std::wstring& packageDir) {
    return std::wstring(L"seedVersion=5\n") +
        L"packageDir=" + packageDir + L"\n" +
        L"exe=" + FileStamp(packageDir + L"\\MC.Xbox.exe") + L"\n" +
        L"manifest=" + FileStamp(packageDir + L"\\AppxManifest.xml") + L"\n" +
        L"downloadManifest=" + FileStamp(packageDir + L"\\download_manifest.tsv") + L"\n" +
        L"versionCatalog=" + FileStamp(packageDir + L"\\runtime\\version_catalog.tsv") + L"\n" +
        L"minecraft=" + std::wstring(kMinecraftVersionW) + L"\n" +
        L"jreRelease=" + FileStamp(packageDir + L"\\jre\\release") + L"\n" +
        L"jvm=" + FileStamp(packageDir + L"\\jre\\bin\\server\\jvm.dll") + L"\n" +
        L"javaBasePatch=" + FileStamp(packageDir + L"\\java-base-uwp-filesystem.jar") + L"\n" +
        L"zipfsPatch=" + FileStamp(packageDir + L"\\java-zipfs-realpath.jar") + L"\n" +
        L"javaDesktopPatch=" + FileStamp(packageDir + L"\\java-desktop-uwp-awt.jar") + L"\n" +
        L"jre21Release=" + FileStamp(packageDir + L"\\jre21\\release") + L"\n" +
        L"jvm21=" + FileStamp(packageDir + L"\\jre21\\bin\\server\\jvm.dll") + L"\n" +
        L"javaBasePatch21=" + FileStamp(packageDir + L"\\java-base-uwp-filesystem-21.jar") + L"\n" +
        L"zipfsPatch21=" + FileStamp(packageDir + L"\\java-zipfs-realpath-21.jar") + L"\n" +
        L"javaDesktopPatch21=" + FileStamp(packageDir + L"\\java-desktop-uwp-awt-21.jar") + L"\n" +
        L"secureJarHandlerPatch=" + FileStamp(packageDir + L"\\securejarhandler-uwp-patch.jar") + L"\n" +
        L"patchedFabricLoader=" + FileStamp(packageDir + L"\\runtime\\libraries\\net\\fabricmc\\fabric-loader\\" + a2w(kFabricLoaderVersion) + L"\\fabric-loader-" + a2w(kFabricLoaderVersion) + L".jar") + L"\n" +
        L"bundledMods=" + FileStamp(packageDir + L"\\runtime\\bundled-mods") + L"\n" +
        L"logConfig=" + FileStamp(packageDir + L"\\runtime\\log_configs\\client-uwp.xml") + L"\n" +
        L"nativeGlfw=" + FileStamp(packageDir + L"\\natives\\glfw.dll") + L"\n" +
        L"nativeLwjgl=" + FileStamp(packageDir + L"\\natives\\lwjgl.dll") + L"\n" +
        L"mesaOpenGl=" + FileStamp(packageDir + L"\\graphics\\mesa\\opengl32.dll") + L"\n";
}

bool IsLocalRuntimeSeedCurrent(const std::wstring& packageDir, const std::wstring& localDir) {
    const std::wstring markerPath = localDir + L"\\.runtime_seed";
    std::wstring marker;
    if (!ReadTextFile(markerPath, marker)) {
        return false;
    }
    if (marker != RuntimeSeedStamp(packageDir)) {
        return false;
    }

    const bool hasGameSupport =
        GetFileAttributesW((localDir + L"\\game\\mods").c_str()) != INVALID_FILE_ATTRIBUTES &&
        GetFileAttributesW((localDir + L"\\game\\log_configs\\client-uwp.xml").c_str()) != INVALID_FILE_ATTRIBUTES;
    const bool hasNatives = GetFileAttributesW((localDir + L"\\natives").c_str()) != INVALID_FILE_ATTRIBUTES;
    const bool hasGraphics = GetFileAttributesW((localDir + L"\\graphics").c_str()) != INVALID_FILE_ATTRIBUTES ||
        GetFileAttributesW((localDir + L"\\natives\\opengl32.dll").c_str()) != INVALID_FILE_ATTRIBUTES;
    const bool hasJre =
        GetFileAttributesW((localDir + L"\\jre\\bin\\server\\jvm.dll").c_str()) != INVALID_FILE_ATTRIBUTES &&
        GetFileAttributesW((localDir + L"\\jre\\conf\\security\\java.security").c_str()) != INVALID_FILE_ATTRIBUTES;
    const bool hasJavaBasePatch =
        GetFileAttributesW((localDir + L"\\java-base-uwp-filesystem.jar").c_str()) != INVALID_FILE_ATTRIBUTES;
    const bool hasJavaZipfsPatch =
        GetFileAttributesW((localDir + L"\\java-zipfs-realpath.jar").c_str()) != INVALID_FILE_ATTRIBUTES;
    const bool hasJavaDesktopPatch =
        GetFileAttributesW((localDir + L"\\java-desktop-uwp-awt.jar").c_str()) != INVALID_FILE_ATTRIBUTES;
    const bool packageHasJre21 =
        GetFileAttributesW((packageDir + L"\\jre21\\bin\\server\\jvm.dll").c_str()) != INVALID_FILE_ATTRIBUTES;
    const bool hasJre21 = !packageHasJre21 ||
        (GetFileAttributesW((localDir + L"\\jre21\\bin\\server\\jvm.dll").c_str()) != INVALID_FILE_ATTRIBUTES &&
            GetFileAttributesW((localDir + L"\\jre21\\conf\\security\\java.security").c_str()) != INVALID_FILE_ATTRIBUTES);
    const bool hasJavaBasePatch21 = !packageHasJre21 ||
        GetFileAttributesW((localDir + L"\\java-base-uwp-filesystem-21.jar").c_str()) != INVALID_FILE_ATTRIBUTES;
    const bool hasJavaZipfsPatch21 = !packageHasJre21 ||
        GetFileAttributesW((localDir + L"\\java-zipfs-realpath-21.jar").c_str()) != INVALID_FILE_ATTRIBUTES;
    const bool hasJavaDesktopPatch21 = !packageHasJre21 ||
        GetFileAttributesW((localDir + L"\\java-desktop-uwp-awt-21.jar").c_str()) != INVALID_FILE_ATTRIBUTES;
    const bool packageHasSecureJarHandlerPatch =
        GetFileAttributesW((packageDir + L"\\securejarhandler-uwp-patch.jar").c_str()) != INVALID_FILE_ATTRIBUTES;
    const bool hasSecureJarHandlerPatch = !packageHasSecureJarHandlerPatch ||
        GetFileAttributesW((localDir + L"\\securejarhandler-uwp-patch.jar").c_str()) != INVALID_FILE_ATTRIBUTES;
    return hasGameSupport && hasNatives && hasGraphics && hasJre && hasJavaBasePatch && hasJavaZipfsPatch && hasJavaDesktopPatch && hasJre21 && hasJavaBasePatch21 && hasJavaZipfsPatch21 && hasJavaDesktopPatch21 && hasSecureJarHandlerPatch;
}

static void MarkLocalRuntimeSeedCurrent(const std::wstring& packageDir, const std::wstring& localDir) {
    const std::wstring markerPath = localDir + L"\\.runtime_seed";
    if (WriteTextFile(markerPath, RuntimeSeedStamp(packageDir))) {
        WriteLog(L"LocalState runtime seed marker written");
    } else {
        WriteLogF(L"Failed to write LocalState runtime seed marker err=%u", GetLastError());
    }
}

static void CopyFileIfNeeded(const std::wstring& src, const std::wstring& dst) {
    if (GetFileAttributesW(src.c_str()) == INVALID_FILE_ATTRIBUTES) return;

    WIN32_FILE_ATTRIBUTE_DATA srcData = {};
    WIN32_FILE_ATTRIBUTE_DATA dstData = {};
    const bool hasDst = GetFileAttributesExW(dst.c_str(), GetFileExInfoStandard, &dstData);
    if (hasDst && GetFileAttributesExW(src.c_str(), GetFileExInfoStandard, &srcData)) {
        if (srcData.nFileSizeHigh == dstData.nFileSizeHigh &&
            srcData.nFileSizeLow == dstData.nFileSizeLow &&
            CompareFileTime(&srcData.ftLastWriteTime, &dstData.ftLastWriteTime) <= 0) {
            return;
        }
    }

    EnsureDirectoryTree(GetParentDir(dst));
    CopyFileW(src.c_str(), dst.c_str(), FALSE);
}

static void CopyFileAlways(const std::wstring& src, const std::wstring& dst) {
    if (GetFileAttributesW(src.c_str()) == INVALID_FILE_ATTRIBUTES) return;

    EnsureDirectoryTree(GetParentDir(dst));
    SetFileAttributesW(dst.c_str(), FILE_ATTRIBUTE_NORMAL);
    CopyFileW(src.c_str(), dst.c_str(), FALSE);
}

static void CopyDirectoryContentsIfNeeded(const std::wstring& src, const std::wstring& dst) {
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((src + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;

    EnsureDirectoryTree(dst);
    do {
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
        const std::wstring srcPath = src + L"\\" + fd.cFileName;
        const std::wstring dstPath = dst + L"\\" + fd.cFileName;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            CopyDirectoryContentsIfNeeded(srcPath, dstPath);
        } else {
            CopyFileIfNeeded(srcPath, dstPath);
        }
    } while (FindNextFileW(h, &fd));

    FindClose(h);
}

void CopyDirectoryContentsAlways(const std::wstring& src, const std::wstring& dst) {
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((src + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;

    EnsureDirectoryTree(dst);
    do {
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
        const std::wstring srcPath = src + L"\\" + fd.cFileName;
        const std::wstring dstPath = dst + L"\\" + fd.cFileName;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            CopyDirectoryContentsAlways(srcPath, dstPath);
        } else {
            CopyFileAlways(srcPath, dstPath);
        }
    } while (FindNextFileW(h, &fd));

    FindClose(h);
}

static bool IsBanditControllerJarName(const std::wstring& fileName) {
    const std::wstring lower = ToLowerW(fileName);
    return lower.size() > 4 &&
        lower.compare(lower.size() - 4, 4, L".jar") == 0 &&
        (lower.rfind(L"banditvault-fabric-controller", 0) == 0 ||
            lower.rfind(L"banditvault-forge-controller", 0) == 0 ||
            lower.rfind(L"banditvault-neoforge-controller", 0) == 0);
}

static bool IsControlifyJarName(const std::wstring& fileName) {
    const std::wstring lower = ToLowerW(fileName);
    return lower.size() > 4 &&
        lower.compare(lower.size() - 4, 4, L".jar") == 0 &&
        lower.rfind(L"controlify", 0) == 0;
		lower.rfind(L"lambdacontrols", 0) == 0;
		lower.rfind(L"lambda-controls", 0) == 0;
		lower.rfind(L"midnightcontrols", 0) == 0;
		lower.rfind(L"midnight-controls", 0) == 0;
}

int DeleteBanditControllerModsFromDir(const std::wstring& modsDir) {
    int removed = 0;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((modsDir + L"\\*.jar").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;

    do {
        if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || !IsBanditControllerJarName(fd.cFileName)) continue;
        const std::wstring path = modsDir + L"\\" + fd.cFileName;
        SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_NORMAL);
        if (DeleteFileW(path.c_str())) ++removed;
    } while (FindNextFileW(h, &fd));

    FindClose(h);
    return removed;
}

int DeleteControlifyModsFromDir(const std::wstring& modsDir) {
    int removed = 0;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((modsDir + L"\\*.jar").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;

    do {
        if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || !IsControlifyJarName(fd.cFileName)) continue;
        const std::wstring path = modsDir + L"\\" + fd.cFileName;
        SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_NORMAL);
        if (DeleteFileW(path.c_str())) ++removed;
    } while (FindNextFileW(h, &fd));

    FindClose(h);
    return removed;
}

static void CopyDirectoryContentsExceptController(const std::wstring& src, const std::wstring& dst) {
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((src + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;

    EnsureDirectoryTree(dst);
    do {
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
        const std::wstring srcPath = src + L"\\" + fd.cFileName;
        const std::wstring dstPath = dst + L"\\" + fd.cFileName;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            CopyDirectoryContentsExceptController(srcPath, dstPath);
        } else if (!IsBanditControllerJarName(fd.cFileName)) {
            CopyFileAlways(srcPath, dstPath);
        }
    } while (FindNextFileW(h, &fd));

    FindClose(h);
}

std::wstring PrepareEffectiveBundledModsDir(
    const std::wstring& runtimeRoot,
    const std::wstring& targetId,
    const std::wstring& bundledModsDir,
    bool controllerModEnabled) {
    if (controllerModEnabled || bundledModsDir.empty() || !DirectoryExists(bundledModsDir)) {
        return bundledModsDir;
    }

    const std::wstring filteredDir = runtimeRoot + L"\\runtime\\version-mods-no-controller\\" + SafeFileName(targetId);
    DeleteDirectoryTree(filteredDir);
    CopyDirectoryContentsExceptController(bundledModsDir, filteredDir);
    return filteredDir;
}

bool SeedLocalRuntime(
    const std::wstring& packageDir,
    const std::wstring& localDir,
    const RuntimeSeedProgressCallback& progress) {
    if (packageDir.empty() || localDir.empty()) return false;

    EnsureDirectoryTree(localDir);
    WriteLogF(L"Seeding LocalState runtime from %s", packageDir.c_str());
    if (progress) {
        progress(L"Copying launcher files", L"Preparing mods and log configuration", 0.12f);
    }
    CopyDirectoryContentsIfNeeded(packageDir + L"\\runtime\\bundled-mods", localDir + L"\\game\\mods");
    CopyDirectoryContentsIfNeeded(packageDir + L"\\runtime\\log_configs", localDir + L"\\game\\log_configs");
    CopyFileIfNeeded(packageDir + L"\\runtime\\version_catalog.tsv", localDir + L"\\runtime\\version_catalog.tsv");
    if (progress) {
        progress(L"Copying Java runtime", L"Preparing JVM files", 0.52f);
    }
    CopyDirectoryContentsIfNeeded(packageDir + L"\\jre", localDir + L"\\jre");
    CopyDirectoryContentsIfNeeded(packageDir + L"\\jre21", localDir + L"\\jre21");
    CopyDirectoryContentsIfNeeded(packageDir + L"\\jre17", localDir + L"\\jre17");
    std::wstring xboxSecurityProperties;
    if (ReadTextFile(packageDir + L"\\xbox_security.properties", xboxSecurityProperties)) {
        const std::wstring runtimeDirs[] = { L"jre", L"jre21", L"jre17" };
        for (const std::wstring& runtimeDir : runtimeDirs) {
            const std::wstring localSecurityDir = localDir + L"\\" + runtimeDir + L"\\conf\\security";
            if (GetFileAttributesW(localSecurityDir.c_str()) == INVALID_FILE_ATTRIBUTES) continue;
            if (!WriteTextFile(localSecurityDir + L"\\java.security", xboxSecurityProperties)) {
                WriteLogF(L"Failed to rewrite LocalState %s java.security err=%u", runtimeDir.c_str(), GetLastError());
            }
            if (!WriteTextFile(localSecurityDir + L"\\xbox.properties", xboxSecurityProperties)) {
                WriteLogF(L"Failed to write LocalState %s xbox.properties err=%u", runtimeDir.c_str(), GetLastError());
            }
        }
    } else {
        WriteLogF(L"Failed to read packaged xbox_security.properties err=%u", GetLastError());
    }
    if (progress) {
        progress(L"Copying native libraries", L"Preparing graphics and input runtime", 0.80f);
    }
    CopyDirectoryContentsIfNeeded(packageDir + L"\\natives", localDir + L"\\natives");
    CopyDirectoryContentsIfNeeded(packageDir + L"\\graphics", localDir + L"\\graphics");
    if (progress) {
        progress(L"Finalizing runtime", L"Writing launch configuration", 0.96f);
    }
    CopyFileIfNeeded(packageDir + L"\\xbox_security.properties", localDir + L"\\xbox_security.properties");
    CopyFileIfNeeded(packageDir + L"\\java-base-uwp-filesystem.jar", localDir + L"\\java-base-uwp-filesystem.jar");
    CopyFileIfNeeded(packageDir + L"\\java-base-uwp-filesystem-21.jar", localDir + L"\\java-base-uwp-filesystem-21.jar");
    CopyFileIfNeeded(packageDir + L"\\java-zipfs-realpath.jar", localDir + L"\\java-zipfs-realpath.jar");
    CopyFileIfNeeded(packageDir + L"\\java-zipfs-realpath-21.jar", localDir + L"\\java-zipfs-realpath-21.jar");
    CopyFileIfNeeded(packageDir + L"\\java-desktop-uwp-awt.jar", localDir + L"\\java-desktop-uwp-awt.jar");
    CopyFileIfNeeded(packageDir + L"\\java-desktop-uwp-awt-21.jar", localDir + L"\\java-desktop-uwp-awt-21.jar");
    CopyFileIfNeeded(packageDir + L"\\securejarhandler-uwp-patch.jar", localDir + L"\\securejarhandler-uwp-patch.jar");
    if (progress) {
        progress(L"Runtime ready", L"Starting Minecraft", 1.0f);
    }
    MarkLocalRuntimeSeedCurrent(packageDir, localDir);
    WriteLog(L"LocalState runtime seed complete");
    return true;
}

static std::wstring TrimTrailingSlash(std::wstring path) {
    while (!path.empty() && (path.back() == L'\\' || path.back() == L'/')) {
        path.pop_back();
    }
    return path;
}

std::wstring JoinRuntimeRelativePath(const std::wstring& root, std::wstring relativePath) {
    for (wchar_t& ch : relativePath) {
        if (ch == L'/') ch = L'\\';
    }

    if (relativePath.empty() ||
        relativePath[0] == L'\\' ||
        relativePath.find(L":") != std::wstring::npos) {
        return std::wstring();
    }

    size_t start = 0;
    while (start <= relativePath.size()) {
        const size_t slash = relativePath.find(L'\\', start);
        const std::wstring segment = relativePath.substr(
            start,
            slash == std::wstring::npos ? std::wstring::npos : slash - start);
        if (segment.empty() || segment == L"." || segment == L"..") {
            return std::wstring();
        }
        if (slash == std::wstring::npos) break;
        start = slash + 1;
    }

    return TrimTrailingSlash(root) + L"\\" + relativePath;
}

static std::string BytesToHex(const unsigned char* data, size_t length) {
    static const char* digits = "0123456789abcdef";
    std::string out;
    out.reserve(length * 2);
    for (size_t i = 0; i < length; ++i) {
        out.push_back(digits[(data[i] >> 4) & 0xF]);
        out.push_back(digits[data[i] & 0xF]);
    }
    return out;
}

bool Sha1File(const std::wstring& path, std::string* outHex) {
    if (!outHex) return false;

    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"rb") != 0 || !f) {
        return false;
    }

    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    DWORD objectLength = 0;
    DWORD dataLength = 0;
    std::vector<unsigned char> hashObject;
    unsigned char hashBytes[20] = {};
    bool ok = false;

    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA1_ALGORITHM, nullptr, 0) != 0) goto done;
    if (BCryptGetProperty(alg, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&objectLength),
        sizeof(objectLength), &dataLength, 0) != 0 || objectLength == 0) goto done;

    hashObject.resize(objectLength);
    if (BCryptCreateHash(alg, &hash, hashObject.data(), objectLength, nullptr, 0, 0) != 0) goto done;

    {
        unsigned char buffer[64 * 1024];
        for (;;) {
            const size_t read = fread(buffer, 1, sizeof(buffer), f);
            if (read > 0 && BCryptHashData(hash, buffer, static_cast<ULONG>(read), 0) != 0) goto done;
            if (read < sizeof(buffer)) {
                if (ferror(f)) goto done;
                break;
            }
        }
    }

    if (BCryptFinishHash(hash, hashBytes, sizeof(hashBytes), 0) != 0) goto done;
    *outHex = BytesToHex(hashBytes, sizeof(hashBytes));
    ok = true;

done:
    if (hash) BCryptDestroyHash(hash);
    if (alg) BCryptCloseAlgorithmProvider(alg, 0);
    fclose(f);
    return ok;
}

bool FileMatchesSha1(const std::wstring& path, const std::string& expectedSha1) {
    if (expectedSha1.empty()) {
        return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
    }

    std::string actual;
    if (!Sha1File(path, &actual)) return false;
    return ToLowerAscii(actual) == ToLowerAscii(expectedSha1);
}

bool ReadDownloadManifest(const std::wstring& path, std::vector<DownloadManifestEntry>& entries) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return false;

    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;

        std::vector<std::string> parts;
        size_t start = 0;
        for (;;) {
            const size_t tab = line.find('\t', start);
            parts.push_back(line.substr(start, tab == std::string::npos ? std::string::npos : tab - start));
            if (tab == std::string::npos) break;
            start = tab + 1;
        }

        if (parts.size() < 4) {
            WriteLogF(L"Skipping malformed download manifest line: %s", a2w(line.c_str()).c_str());
            continue;
        }

        unsigned long long size = 0;
        try {
            size = parts[2].empty() ? 0 : std::stoull(parts[2]);
        } catch (...) {
            size = 0;
        }

        entries.push_back(DownloadManifestEntry{
            a2w(parts[0].c_str()),
            a2w(parts[3].c_str()),
            ToLowerAscii(parts[1]),
            size
        });
    }

    return true;
}

static void CollectManifestLibraryJars(
    const std::wstring& manifestPath,
    const std::wstring& runtimeRoot,
    const std::wstring& packageDir,
    std::vector<std::wstring>& jars) {
    std::vector<DownloadManifestEntry> entries;
    if (!ReadDownloadManifest(manifestPath, entries)) return;
    for (const auto& e : entries) {
        std::wstring rel = e.relativePath;
        std::replace(rel.begin(), rel.end(), L'\\', L'/');
        for (wchar_t& c : rel) c = static_cast<wchar_t>(towlower(c));
        if (rel.rfind(L"game/libraries/", 0) != 0) continue;
        if (rel.size() < 4 || rel.compare(rel.size() - 4, 4, L".jar") != 0) continue;
        if (rel.find(L"-natives-") != std::wstring::npos) continue;
        if (rel.find(L"-installer.jar") != std::wstring::npos) continue;
        const std::wstring libraryRelative = e.relativePath.substr(wcslen(L"game\\libraries\\"));
        const std::wstring packagedOverride = packageDir + L"\\runtime\\libraries\\" + libraryRelative;
        const std::wstring abs =
            GetFileAttributesW(packagedOverride.c_str()) != INVALID_FILE_ATTRIBUTES
                ? packagedOverride
                : JoinRuntimeRelativePath(runtimeRoot, e.relativePath);
        if (!abs.empty()) jars.push_back(abs);
    }
}


void ArchiveCurrentLogsToPrevious(const std::wstring& runtimeRoot) {
    const std::wstring current = LogsCurrentDir(runtimeRoot);
    const std::wstring previous = LogsPreviousDir(runtimeRoot);
    DeleteDirectoryTree(previous);
    EnsureDirectoryTree(previous);

    const wchar_t* logNames[] = {
        L"mc_launch.log",
        L"java_output.log",
        L"stderr_stream.log",
        L"glfw_uwp.log",
        L"java_args.txt",
        L"hwnd.txt"
    };

    for (const wchar_t* name : logNames) {
        MovePathIfExists(current + L"\\" + name, previous + L"\\" + name);
        MovePathIfExists(runtimeRoot + L"\\" + name, previous + L"\\" + name);
    }

    EnsureDirectoryTree(current);
}

static std::wstring DownloadMarkerPath(const std::wstring& runtimeRoot, const std::wstring& targetId) {
    return runtimeRoot + L"\\markers\\" + targetId + L".marker";
}

static std::wstring BuildDownloadMarker(const std::wstring& manifestPath) {
    std::string sha1;
    Sha1File(manifestPath, &sha1);
    return std::wstring(L"markerVersion=2\n") +
        L"manifestSha1=" + a2w(sha1.c_str()) + L"\n";
}

static void CleanupDownloadedRuntimeFiles(const std::wstring& runtimeRoot, const std::wstring& targetId, const wchar_t* reason) {
    WriteLogF(L"Cleaning downloaded runtime files target=%s reason=%s", targetId.c_str(), reason ? reason : L"unknown");
    DeleteDirectoryTree(runtimeRoot + L"\\assets");
    DeleteDirectoryTree(runtimeRoot + L"\\game\\libraries");
    DeleteDirectoryTree(runtimeRoot + L"\\game\\versions");
    DeleteFileW(DownloadMarkerPath(runtimeRoot, targetId).c_str());
}

static bool EnsureDownloadMarkerMatches(
    const std::wstring& manifestPath,
    const std::wstring& runtimeRoot,
    const std::wstring& targetId,
    bool forceRepair) {
    if (forceRepair) {
        CleanupDownloadedRuntimeFiles(runtimeRoot, targetId, L"repair requested");
        return true;
    }

    const std::wstring expected = BuildDownloadMarker(manifestPath);
    std::wstring actual;
    if (!ReadTextFile(DownloadMarkerPath(runtimeRoot, targetId), actual)) {
        return true;
    }
    if (actual == expected) {
        return true;
    }

    CleanupDownloadedRuntimeFiles(runtimeRoot, targetId, L"manifest marker changed");
    return true;
}

static void MarkDownloadManifestCurrent(const std::wstring& manifestPath, const std::wstring& runtimeRoot, const std::wstring& targetId) {
    if (WriteTextFile(DownloadMarkerPath(runtimeRoot, targetId), BuildDownloadMarker(manifestPath))) {
        WriteLog(L"Download manifest marker written");
    } else {
        WriteLogF(L"Failed to write download manifest marker err=%u", GetLastError());
    }
}

static constexpr int kDownloadFileAttempts = 5;
static constexpr DWORD kDownloadRetryBaseDelayMs = 750;

static std::wstring BuildRedirectUrl(const std::wstring& currentUrl, const std::wstring& location) {
    if (location.find(L"://") != std::wstring::npos) {
        return location;
    }

    URL_COMPONENTS uc = {};
    uc.dwStructSize = sizeof(uc);
    uc.dwSchemeLength = static_cast<DWORD>(-1);
    uc.dwHostNameLength = static_cast<DWORD>(-1);
    uc.dwUrlPathLength = static_cast<DWORD>(-1);
    uc.dwExtraInfoLength = static_cast<DWORD>(-1);

    std::wstring mutableUrl = currentUrl;
    if (!WinHttpCrackUrl(mutableUrl.c_str(), 0, 0, &uc)) {
        return location;
    }

    std::wstring scheme(uc.lpszScheme, uc.dwSchemeLength);
    std::wstring host(uc.lpszHostName, uc.dwHostNameLength);
    std::wstring path(uc.lpszUrlPath, uc.dwUrlPathLength);
    std::wstring extra;
    if (uc.dwExtraInfoLength > 0) {
        extra.assign(uc.lpszExtraInfo, uc.dwExtraInfoLength);
    }

    if (!location.empty() && location[0] == L'/') {
        return scheme + L"://" + host + location;
    }

    const size_t slash = path.find_last_of(L'/');
    path = slash == std::wstring::npos ? L"/" : path.substr(0, slash + 1);
    return scheme + L"://" + host + path + location;
}

bool DownloadUrlToFile(
    const std::wstring& url,
    const std::wstring& destination,
    const std::function<void(unsigned long long)>& progressCallback) {
    std::wstring currentUrl = url;

    for (int redirect = 0; redirect < 6; ++redirect) {
        URL_COMPONENTS uc = {};
        uc.dwStructSize = sizeof(uc);
        uc.dwSchemeLength = static_cast<DWORD>(-1);
        uc.dwHostNameLength = static_cast<DWORD>(-1);
        uc.dwUrlPathLength = static_cast<DWORD>(-1);
        uc.dwExtraInfoLength = static_cast<DWORD>(-1);

        std::wstring mutableUrl = currentUrl;
        if (!WinHttpCrackUrl(mutableUrl.c_str(), 0, 0, &uc)) {
            WriteLogF(L"WinHttpCrackUrl failed err=%u url=%s", GetLastError(), currentUrl.c_str());
            return false;
        }

        std::wstring host(uc.lpszHostName, uc.dwHostNameLength);
        std::wstring objectName;
        if (uc.dwUrlPathLength > 0) objectName.assign(uc.lpszUrlPath, uc.dwUrlPathLength);
        if (uc.dwExtraInfoLength > 0) objectName.append(uc.lpszExtraInfo, uc.dwExtraInfoLength);
        if (objectName.empty()) objectName = L"/";

        HINTERNET session = WinHttpOpen(
            L"MinecraftJavaUWP/1.0",
            WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
            WINHTTP_NO_PROXY_NAME,
            WINHTTP_NO_PROXY_BYPASS,
            0);
        if (!session) {
            WriteLogF(L"WinHttpOpen failed err=%u", GetLastError());
            return false;
        }
        WinHttpSetTimeouts(session, 15000, 15000, 30000, 30000);

        HINTERNET connect = WinHttpConnect(session, host.c_str(), uc.nPort, 0);
        if (!connect) {
            WriteLogF(L"WinHttpConnect failed err=%u host=%s", GetLastError(), host.c_str());
            WinHttpCloseHandle(session);
            return false;
        }

        const DWORD flags = (uc.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
        HINTERNET request = WinHttpOpenRequest(
            connect,
            L"GET",
            objectName.c_str(),
            nullptr,
            WINHTTP_NO_REFERER,
            WINHTTP_DEFAULT_ACCEPT_TYPES,
            flags);
        if (!request) {
            WriteLogF(L"WinHttpOpenRequest failed err=%u path=%s", GetLastError(), objectName.c_str());
            WinHttpCloseHandle(connect);
            WinHttpCloseHandle(session);
            return false;
        }

        BOOL sent = WinHttpSendRequest(
            request,
            WINHTTP_NO_ADDITIONAL_HEADERS,
            0,
            WINHTTP_NO_REQUEST_DATA,
            0,
            0,
            0);
        BOOL received = sent ? WinHttpReceiveResponse(request, nullptr) : FALSE;
        if (!sent || !received) {
            WriteLogF(L"WinHttp request failed err=%u url=%s", GetLastError(), currentUrl.c_str());
            WinHttpCloseHandle(request);
            WinHttpCloseHandle(connect);
            WinHttpCloseHandle(session);
            return false;
        }

        DWORD status = 0;
        DWORD statusSize = sizeof(status);
        if (!WinHttpQueryHeaders(
            request,
            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX,
            &status,
            &statusSize,
            WINHTTP_NO_HEADER_INDEX)) {
            WriteLogF(L"WinHttpQueryHeaders(status) failed err=%u", GetLastError());
            WinHttpCloseHandle(request);
            WinHttpCloseHandle(connect);
            WinHttpCloseHandle(session);
            return false;
        }

        if (status == 301 || status == 302 || status == 303 || status == 307 || status == 308) {
            DWORD locationBytes = 0;
            WinHttpQueryHeaders(
                request,
                WINHTTP_QUERY_LOCATION,
                WINHTTP_HEADER_NAME_BY_INDEX,
                WINHTTP_NO_OUTPUT_BUFFER,
                &locationBytes,
                WINHTTP_NO_HEADER_INDEX);
            std::vector<wchar_t> location((locationBytes / sizeof(wchar_t)) + 1);
            if (locationBytes > 0 &&
                WinHttpQueryHeaders(
                    request,
                    WINHTTP_QUERY_LOCATION,
                    WINHTTP_HEADER_NAME_BY_INDEX,
                    location.data(),
                    &locationBytes,
                    WINHTTP_NO_HEADER_INDEX)) {
                currentUrl = BuildRedirectUrl(currentUrl, location.data());
                WinHttpCloseHandle(request);
                WinHttpCloseHandle(connect);
                WinHttpCloseHandle(session);
                continue;
            }
        }

        if (status != 200) {
            WriteLogF(L"Download failed HTTP %u url=%s", status, currentUrl.c_str());
            WinHttpCloseHandle(request);
            WinHttpCloseHandle(connect);
            WinHttpCloseHandle(session);
            return false;
        }

        EnsureDirectoryTree(GetParentDir(destination));
        FILE* out = nullptr;
        if (_wfopen_s(&out, destination.c_str(), L"wb") != 0 || !out) {
            WriteLogF(L"Could not open download output %s err=%u", destination.c_str(), GetLastError());
            WinHttpCloseHandle(request);
            WinHttpCloseHandle(connect);
            WinHttpCloseHandle(session);
            return false;
        }

        bool ok = true;
        unsigned long long fileBytesRead = 0;
        unsigned char buffer[64 * 1024];
        for (;;) {
            DWORD bytesRead = 0;
            if (!WinHttpReadData(request, buffer, sizeof(buffer), &bytesRead)) {
                WriteLogF(L"WinHttpReadData failed err=%u url=%s", GetLastError(), currentUrl.c_str());
                ok = false;
                break;
            }
            if (bytesRead == 0) break;
            if (fwrite(buffer, 1, bytesRead, out) != bytesRead) {
                WriteLogF(L"fwrite failed while downloading %s", destination.c_str());
                ok = false;
                break;
            }
            fileBytesRead += bytesRead;
            if (progressCallback) {
                progressCallback(fileBytesRead);
            }
        }

        fclose(out);
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return ok;
    }

    WriteLogF(L"Too many redirects url=%s", url.c_str());
    return false;
}

bool EnsureRuntimeDownloads(
    const std::wstring& manifestPath,
    const std::wstring& runtimeRoot,
    const std::wstring& targetId,
    const DownloadProgressCallback& progress = DownloadProgressCallback(),
    const DownloadOptions& options = DownloadOptions()) {
    if (GetFileAttributesW(manifestPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
        WriteLogF(L"No download manifest found at %s", manifestPath.c_str());
        return false;
    }
    if (!EnsureDownloadMarkerMatches(manifestPath, runtimeRoot, targetId, options.forceRepair)) {
        return false;
    }

    std::vector<DownloadManifestEntry> entries;
    if (!ReadDownloadManifest(manifestPath, entries)) {
        WriteLogF(L"Failed to read download manifest: %s", manifestPath.c_str());
        return false;
    }
    if (entries.empty()) {
        WriteLog(L"Download manifest is empty");
        if (progress) progress(L"No downloads needed", L"Launching Minecraft", 1.0f);
        return true;
    }

    unsigned long long totalBytes = 0;
    for (const auto& entry : entries) {
        totalBytes += entry.size;
    }

    size_t verified = 0;
    size_t downloaded = 0;
    unsigned long long completedBytes = 0;
    std::vector<size_t> missing;
    const std::wstring totalText = std::to_wstring(entries.size()) + L" files, " +
        std::to_wstring(totalBytes / (1024ULL * 1024ULL)) + L" MB";
    auto formatDownloadDetail = [&](size_t fileIndex, unsigned long long bytesDone) {
        return std::to_wstring(fileIndex) + L"/" + std::to_wstring(entries.size()) +
            L" files  " + std::to_wstring(bytesDone / (1024ULL * 1024ULL)) +
            L"/" + std::to_wstring(totalBytes / (1024ULL * 1024ULL)) + L" MB";
    };
    WriteLogF(L"Download manifest entries=%zu totalBytes=%llu", entries.size(), totalBytes);
    if (progress) progress(L"Preparing download", totalText.c_str(), 0.0f);

    for (size_t i = 0; i < entries.size(); ++i) {
        const auto& entry = entries[i];
        const std::wstring finalPath = JoinRuntimeRelativePath(runtimeRoot, entry.relativePath);
        if (finalPath.empty()) {
            WriteLogF(L"Invalid manifest relative path: %s", entry.relativePath.c_str());
            return false;
        }

        if (FileMatchesSha1(finalPath, entry.sha1)) {
            ++verified;
            completedBytes += entry.size;
            if (progress && (verified < 25 || verified % 100 == 0 || verified == entries.size())) {
                const float ratio = totalBytes > 0
                    ? static_cast<float>(static_cast<double>(completedBytes) / static_cast<double>(totalBytes))
                    : static_cast<float>(static_cast<double>(verified) / static_cast<double>(entries.size()));
                const std::wstring detail = formatDownloadDetail(verified, completedBytes);
                progress(L"Checking installed files", detail.c_str(), ratio);
            }
            continue;
        }

        missing.push_back(i);
    }

    if (missing.empty()) {
        WriteLogF(L"Download pass complete verified=%zu downloaded=0", verified);
        MarkDownloadManifestCurrent(manifestPath, runtimeRoot, targetId);
        if (progress) progress(L"Download complete", L"Launching Minecraft", 1.0f);
        return true;
    }

    const unsigned workerCount = static_cast<unsigned>((std::max)(1, (std::min)(8, options.workerCount)));
    const unsigned workersToStart = (std::min<unsigned>)(workerCount, static_cast<unsigned>(missing.size()));
    WriteLogF(L"Downloading missing runtime files missing=%zu workers=%u", missing.size(), workersToStart);

    std::mutex stateMutex;
    std::vector<unsigned long long> inProgressBytes(entries.size(), 0);
    size_t nextMissing = 0;
    bool failed = false;
    std::wstring failureStatus;
    std::wstring failureDetail;
    int activeWorkers = static_cast<int>(workersToStart);

    auto workerProc = [&]() {
        for (;;) {
            size_t entryIndex = 0;
            {
                std::lock_guard<std::mutex> lock(stateMutex);
                if (failed || nextMissing >= missing.size()) {
                    break;
                }
                entryIndex = missing[nextMissing++];
                inProgressBytes[entryIndex] = 0;
            }

            const auto& entry = entries[entryIndex];
            const std::wstring finalPath = JoinRuntimeRelativePath(runtimeRoot, entry.relativePath);
            const std::wstring tempPath = finalPath + L".download";
            DeleteFileW(tempPath.c_str());

            if (entryIndex < 25 || entryIndex % 100 == 0) {
                WriteLogF(L"Downloading [%zu/%zu] %s", entryIndex + 1, entries.size(), entry.relativePath.c_str());
            }

            auto progressCallback = [&](unsigned long long fileBytesRead) {
                const unsigned long long cappedFileBytes = entry.size > 0
                    ? std::min<unsigned long long>(fileBytesRead, entry.size)
                    : fileBytesRead;
                std::lock_guard<std::mutex> lock(stateMutex);
                inProgressBytes[entryIndex] = cappedFileBytes;
            };

            bool downloadedOk = false;
            for (int attempt = 1; attempt <= kDownloadFileAttempts; ++attempt) {
                {
                    std::lock_guard<std::mutex> lock(stateMutex);
                    if (failed) {
                        break;
                    }
                    inProgressBytes[entryIndex] = 0;
                }

                DeleteFileW(tempPath.c_str());
                if (attempt > 1) {
                    const DWORD delayMs = kDownloadRetryBaseDelayMs * static_cast<DWORD>(attempt - 1);
                    WriteLogF(L"Retrying download attempt=%d/%d delayMs=%u file=%s",
                        attempt,
                        kDownloadFileAttempts,
                        delayMs,
                        entry.relativePath.c_str());
                    std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
                }

                if (DownloadUrlToFile(entry.url, tempPath, progressCallback)) {
                    downloadedOk = true;
                    break;
                }

                WriteLogF(L"Download attempt failed attempt=%d/%d file=%s",
                    attempt,
                    kDownloadFileAttempts,
                    entry.relativePath.c_str());
            }

            if (!downloadedOk) {
                DeleteFileW(tempPath.c_str());
                std::lock_guard<std::mutex> lock(stateMutex);
                failed = true;
                failureStatus = L"Download failed after retries";
                failureDetail = entry.relativePath;
                break;
            }

            if (!FileMatchesSha1(tempPath, entry.sha1)) {
                std::string actual;
                Sha1File(tempPath, &actual);
                WriteLogF(L"SHA1 mismatch for %s expected=%s actual=%s",
                    entry.relativePath.c_str(),
                    a2w(entry.sha1.c_str()).c_str(),
                    a2w(actual.c_str()).c_str());
                DeleteFileW(tempPath.c_str());
                std::lock_guard<std::mutex> lock(stateMutex);
                failed = true;
                failureStatus = L"File verification failed";
                failureDetail = entry.relativePath;
                break;
            }

            EnsureDirectoryTree(GetParentDir(finalPath));
            DeleteFileW(finalPath.c_str());
            if (!MoveFileExW(tempPath.c_str(), finalPath.c_str(), MOVEFILE_REPLACE_EXISTING)) {
                WriteLogF(L"MoveFileEx failed for %s err=%u", finalPath.c_str(), GetLastError());
                DeleteFileW(tempPath.c_str());
                std::lock_guard<std::mutex> lock(stateMutex);
                failed = true;
                failureStatus = L"Download install failed";
                failureDetail = entry.relativePath;
                break;
            }

            {
                std::lock_guard<std::mutex> lock(stateMutex);
                inProgressBytes[entryIndex] = 0;
                completedBytes += entry.size;
                ++downloaded;
                ++verified;
            }
        }

        {
            std::lock_guard<std::mutex> lock(stateMutex);
            --activeWorkers;
        }
    };

    std::vector<std::thread> workers;
    workers.reserve(workersToStart);
    for (unsigned i = 0; i < workersToStart; ++i) {
        workers.emplace_back(workerProc);
    }

    while (true) {
        bool done = false;
        bool failedSnapshot = false;
        std::wstring failureStatusSnapshot;
        std::wstring failureDetailSnapshot;
        size_t verifiedSnapshot = 0;
        unsigned long long bytesSnapshot = 0;
        {
            std::lock_guard<std::mutex> lock(stateMutex);
            done = activeWorkers == 0;
            failedSnapshot = failed;
            failureStatusSnapshot = failureStatus;
            failureDetailSnapshot = failureDetail;
            verifiedSnapshot = verified;
            bytesSnapshot = completedBytes;
            for (unsigned long long bytes : inProgressBytes) {
                bytesSnapshot += bytes;
            }
        }

        if (progress) {
            const float ratio = totalBytes > 0
                ? static_cast<float>(static_cast<double>(bytesSnapshot) / static_cast<double>(totalBytes))
                : static_cast<float>(static_cast<double>(verifiedSnapshot) / static_cast<double>(entries.size()));
            if (failedSnapshot) {
                progress(
                    failureStatusSnapshot.empty() ? L"Download failed" : failureStatusSnapshot.c_str(),
                    failureDetailSnapshot.c_str(),
                    ratio);
            } else {
                const std::wstring detail = formatDownloadDetail(verifiedSnapshot, bytesSnapshot);
                progress(L"Downloading Minecraft files", detail.c_str(), ratio);
            }
        }

        if (done) {
            break;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    for (std::thread& worker : workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }

    if (failed) {
        return false;
    }

    WriteLogF(L"Download pass complete verified=%zu downloaded=%zu", verified, downloaded);
    MarkDownloadManifestCurrent(manifestPath, runtimeRoot, targetId);
    if (progress) progress(L"Download complete", L"Launching Minecraft", 1.0f);
    return true;
}

static bool ContainsInsensitive(const std::wstring& value, const wchar_t* needle) {
    if (!needle || !*needle) return false;

    std::wstring haystack = value;
    std::wstring target = needle;
    std::transform(haystack.begin(), haystack.end(), haystack.begin(),
        [](wchar_t c) { return static_cast<wchar_t>(towlower(c)); });
    std::transform(target.begin(), target.end(), target.begin(),
        [](wchar_t c) { return static_cast<wchar_t>(towlower(c)); });
    return haystack.find(target) != std::wstring::npos;
}

std::wstring DetectGraphicsRuntimeName() {
    const std::wstring overrideValue = GetEnvVarString(L"MC_GRAPHICS_RUNTIME");
    if (!overrideValue.empty()) {
        WriteLogF(L"Graphics runtime override: %s", overrideValue.c_str());
        return overrideValue;
    }

    try {
        using namespace winrt::Windows::Security::ExchangeActiveSyncProvisioning;
        EasClientDeviceInformation info;
        const std::wstring manufacturer = info.SystemManufacturer().c_str();
        const std::wstring productName = info.SystemProductName().c_str();
        const std::wstring sku = info.SystemSku().c_str();
        const std::wstring friendlyName = info.FriendlyName().c_str();
        const std::wstring probe = manufacturer + L" " + productName + L" " + sku + L" " + friendlyName;

        WriteLogF(L"Device manufacturer: %s", manufacturer.c_str());
        WriteLogF(L"Device product: %s", productName.c_str());
        WriteLogF(L"Device SKU: %s", sku.c_str());
        WriteLogF(L"Device friendly name: %s", friendlyName.c_str());

        if (ContainsInsensitive(probe, L"xbox series") ||
            ContainsInsensitive(probe, L"scarlett") ||
            ContainsInsensitive(probe, L"anaconda") ||
            ContainsInsensitive(probe, L"lockhart")) {
            return L"mesa";
        }
    } catch (...) {
        WriteLog(L"Device graphics runtime detection failed; defaulting to Mesa");
    }

    return L"mesa";
}

JavaRuntimeInfo ResolveJavaRuntimeInfo(
    const std::wstring& packageDir,
    const std::wstring& localRoot,
    const std::wstring& requestedRuntime) {
    std::wstring id = ToLowerW(requestedRuntime);
    if (id.empty()) id = L"current";

    JavaRuntimeInfo info;
    info.runtimeId = id;
    if (id == L"java21" || id == L"legacy21" || id == L"jdk21" || id == L"21") {
        info.runtimeId = L"java21";
        info.packageRelativeDir = L"jre21";
        info.javaBasePatchName = L"java-base-uwp-filesystem-21.jar";
        info.zipfsPatchName = L"java-zipfs-realpath-21.jar";
    } else if (id == L"java17" || id == L"jdk17" || id == L"17") {
        info.runtimeId = L"java17";
        info.packageRelativeDir = L"jre17";
        info.javaBasePatchName = L"java-base-uwp-filesystem-17.jar";
        info.zipfsPatchName = L"java-zipfs-realpath-17.jar";
    } else if (id == L"legacy" || id == L"java8" || id == L"jdk8" || id == L"8") {
        info.runtimeId = L"legacy";
        info.packageRelativeDir = L"jre8";
        info.javaBasePatchName = L"java-base-uwp-filesystem-8.jar";
        info.zipfsPatchName = L"java-zipfs-realpath-8.jar";
    } else {
        info.runtimeId = L"current";
        info.packageRelativeDir = L"jre";
        info.javaBasePatchName = L"java-base-uwp-filesystem.jar";
        info.zipfsPatchName = L"java-zipfs-realpath.jar";
    }

    info.packageDir = packageDir + L"\\" + info.packageRelativeDir;
    info.localDir = localRoot + L"\\" + info.packageRelativeDir;
    info.selectedDir =
        GetFileAttributesW((info.packageDir + L"\\bin\\server\\jvm.dll").c_str()) != INVALID_FILE_ATTRIBUTES
            ? info.packageDir
            : info.localDir;
    return info;
}

static std::vector<std::wstring> SplitManifestValueList(const std::wstring& value) {
    std::vector<std::wstring> out;
    size_t start = 0;
    while (start <= value.size()) {
        const size_t sep = value.find(L'\x1f', start);
        std::wstring item = value.substr(start, sep == std::wstring::npos ? std::wstring::npos : sep - start);
        if (!item.empty()) out.push_back(item);
        if (sep == std::wstring::npos) break;
        start = sep + 1;
    }
    return out;
}

static void ReadManifestHeader(
    const std::wstring& manifestPath,
    std::wstring& assetIndex,
    std::wstring& launchVersion,
    std::wstring& mainClass,
    std::vector<std::wstring>& extraJvmArgs,
    std::vector<std::wstring>& extraGameArgs,
    std::wstring& neoFormVersion,
    std::wstring& neoForgeInstallToolsVersion,
    std::wstring& neoForgeJarSplitterVersion,
    std::wstring& neoForgeBinaryPatcherVersion,
    std::wstring& neoForgeAutoRenamingToolVersion) {
    std::ifstream f(manifestPath, std::ios::binary);
    if (!f) return;
    std::string line;
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        if (line[0] != '#') break;
        const size_t tab = line.find('\t');
        if (tab == std::string::npos || tab < 2) continue;
        const std::string key = line.substr(2, tab - 2);
        const std::string val = line.substr(tab + 1);
        if (key == "assetIndex") assetIndex = a2w(val.c_str());
        else if (key == "launchVersion") launchVersion = a2w(val.c_str());
        else if (key == "mainClass") mainClass = a2w(val.c_str());
        else if (key == "jvmArgs") extraJvmArgs = SplitManifestValueList(a2w(val.c_str()));
        else if (key == "gameArgs") extraGameArgs = SplitManifestValueList(a2w(val.c_str()));
        else if (key == "neoFormVersion" || key == "forgeMcpVersion") neoFormVersion = a2w(val.c_str());
        else if (key == "neoForgeInstallToolsVersion") neoForgeInstallToolsVersion = a2w(val.c_str());
        else if (key == "neoForgeJarSplitterVersion") neoForgeJarSplitterVersion = a2w(val.c_str());
        else if (key == "neoForgeBinaryPatcherVersion") neoForgeBinaryPatcherVersion = a2w(val.c_str());
        else if (key == "neoForgeAutoRenamingToolVersion") neoForgeAutoRenamingToolVersion = a2w(val.c_str());
    }
}

MinecraftVersionInfo ResolveVersionInfo(const std::wstring& packageDir, const std::wstring& runtimeRoot, const LaunchTarget& target) {
    MinecraftVersionInfo info;
    info.targetId = target.targetId;
    info.minecraftVersion = target.minecraftVersion;
    info.loader = target.loader;
    info.loaderVersion = target.loaderVersion;
    info.javaRuntime = target.javaRuntime.empty() ? L"current" : target.javaRuntime;
    info.clientJar = runtimeRoot + L"\\game\\versions\\" + target.minecraftVersion + L"\\" + target.minecraftVersion + L".jar";

    const LaunchTarget def = DefaultLaunchTarget();
    const bool isDefault = target.targetId == def.targetId;
    const std::wstring perVersion = packageDir + L"\\runtime\\manifests\\" + target.targetId + L".tsv";
    const std::wstring legacy = packageDir + L"\\download_manifest.tsv";

    if (GetFileAttributesW(perVersion.c_str()) != INVALID_FILE_ATTRIBUTES) {
        info.manifestPath = perVersion;
    } else if (isDefault && GetFileAttributesW(legacy.c_str()) != INVALID_FILE_ATTRIBUTES) {
        info.manifestPath = legacy;
    }

    if (!info.manifestPath.empty()) {
        ReadManifestHeader(info.manifestPath, info.assetIndex, info.launchVersion, info.mainClass, info.extraJvmArgs, info.extraGameArgs,
            info.neoFormVersion, info.neoForgeInstallToolsVersion, info.neoForgeJarSplitterVersion,
            info.neoForgeBinaryPatcherVersion, info.neoForgeAutoRenamingToolVersion);
    }

    if (info.assetIndex.empty() && isDefault) info.assetIndex = a2w(kMinecraftAssetIndex);
    if (info.launchVersion.empty() && isDefault) info.launchVersion = a2w(kFabricLaunchVersion);

    const std::wstring perVersionMods = packageDir + L"\\runtime\\version-mods\\" + target.targetId;
    if (GetFileAttributesW(perVersionMods.c_str()) != INVALID_FILE_ATTRIBUTES) {
        info.bundledModsDir = perVersionMods;
    } else if (isDefault) {
        info.bundledModsDir = packageDir + L"\\runtime\\bundled-mods";
    }

    LoaderFinalizeVersionInfo(info, target, packageDir, def);
    return info;
}

static std::wstring SafePathSegment(const std::wstring& value) {
    std::wstring out;
    out.reserve(value.size());
    for (wchar_t c : value) {
        const bool ok =
            (c >= L'a' && c <= L'z') ||
            (c >= L'A' && c <= L'Z') ||
            (c >= L'0' && c <= L'9') ||
            c == L'.' || c == L'_' || c == L'-';
        out.push_back(ok ? c : L'_');
    }
    return out.empty() ? L"default" : out;
}

static bool EndsWithAsciiNoCase(const std::string& value, const char* suffix) {
    const std::string lower = ToLowerAscii(value);
    const std::string suffixLower = ToLowerAscii(suffix);
    return lower.size() >= suffixLower.size() &&
        lower.compare(lower.size() - suffixLower.size(), suffixLower.size(), suffixLower) == 0;
}

static bool ExtractDllsFromJar(const std::wstring& jarPath, const std::wstring& destDir, bool jnaOnly) {
    std::ifstream in(jarPath, std::ios::binary);
    if (!in) {
        WriteLogF(L"Could not open native jar: %s", jarPath.c_str());
        return false;
    }
    std::vector<unsigned char> jarBytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (jarBytes.empty()) {
        WriteLogF(L"Native jar is empty: %s", jarPath.c_str());
        return false;
    }

    mz_zip_archive zip{};
    if (!mz_zip_reader_init_mem(&zip, jarBytes.data(), jarBytes.size(), 0)) {
        WriteLogF(L"Could not open native jar: %s", jarPath.c_str());
        return false;
    }

    bool extractedAny = false;
    const mz_uint entryCount = mz_zip_reader_get_num_files(&zip);
    for (mz_uint i = 0; i < entryCount; ++i) {
        if (mz_zip_reader_is_file_a_directory(&zip, i)) continue;
        mz_zip_archive_file_stat st;
        if (!mz_zip_reader_file_stat(&zip, i, &st)) continue;

        std::string name = st.m_filename ? st.m_filename : "";
        std::replace(name.begin(), name.end(), '\\', '/');
        const size_t slash = name.find_last_of('/');
        const std::string base = slash == std::string::npos ? name : name.substr(slash + 1);
        const std::string lowerName = ToLowerAscii(name);
        const std::string lowerBase = ToLowerAscii(base);
        if (!EndsWithAsciiNoCase(base, ".dll")) continue;
        if (jnaOnly && (lowerBase != "jnidispatch.dll" || lowerName.find("win32-x86-64/") == std::string::npos)) continue;

        size_t outSize = 0;
        void* p = mz_zip_reader_extract_to_heap(&zip, i, &outSize, 0);
        if (!p) {
            WriteLogF(L"Could not extract %s from %s", a2w(name.c_str()).c_str(), jarPath.c_str());
            continue;
        }

        const bool ok = WriteAllBytes(destDir + L"\\" + a2w(base.c_str()), p, outSize);
        mz_free(p);
        if (ok) extractedAny = true;
    }

    mz_zip_reader_end(&zip);
    return extractedAny;
}

static void CollectManifestNativeSources(
    const std::wstring& manifestPath,
    const std::wstring& runtimeRoot,
    std::vector<std::wstring>& lwjglNativeJars,
    std::vector<std::wstring>& jnaJars) {
    std::vector<DownloadManifestEntry> entries;
    if (!ReadDownloadManifest(manifestPath, entries)) return;
    for (const auto& e : entries) {
        std::wstring rel = e.relativePath;
        std::replace(rel.begin(), rel.end(), L'\\', L'/');
        for (wchar_t& c : rel) c = static_cast<wchar_t>(towlower(c));
        if (rel.rfind(L"game/libraries/", 0) != 0) continue;
        if (rel.size() < 4 || rel.compare(rel.size() - 4, 4, L".jar") != 0) continue;

        const std::wstring abs = JoinRuntimeRelativePath(runtimeRoot, e.relativePath);
        if (abs.empty()) continue;
        if (rel.find(L"/org/lwjgl/") != std::wstring::npos &&
            rel.find(L"-natives-windows.jar") != std::wstring::npos) {
            lwjglNativeJars.push_back(abs);
        } else if (rel.find(L"/net/java/dev/jna/jna/") != std::wstring::npos &&
            rel.find(L"/jna-") != std::wstring::npos) {
            jnaJars.push_back(abs);
        }
    }
}

bool PrepareTargetNativeDir(
    const std::wstring& manifestPath,
    const std::wstring& runtimeRoot,
    const std::wstring& packageDir,
    const std::wstring& targetId,
    std::wstring& nativeDir) {
    if (manifestPath.empty() || targetId.empty()) return false;

    nativeDir = runtimeRoot + L"\\runtime\\natives\\" + SafePathSegment(targetId);
    const std::wstring packageNativesDir = packageDir + L"\\natives";
    const std::wstring markerPath = nativeDir + L"\\.native_manifest";
    const std::wstring marker = L"nativeVersion=3\nmanifest=" + FileStamp(manifestPath) +
        L"\nglfw=" + FileStamp(packageNativesDir + L"\\glfw.dll") + L"\n";

    std::wstring existingMarker;
    if (ReadTextFile(markerPath, existingMarker) && existingMarker == marker &&
        GetFileAttributesW((nativeDir + L"\\lwjgl.dll").c_str()) != INVALID_FILE_ATTRIBUTES &&
        GetFileAttributesW((nativeDir + L"\\glfw.dll").c_str()) != INVALID_FILE_ATTRIBUTES &&
        GetFileAttributesW((nativeDir + L"\\jemalloc.dll").c_str()) != INVALID_FILE_ATTRIBUTES &&
        GetFileAttributesW((nativeDir + L"\\jnidispatch.dll").c_str()) != INVALID_FILE_ATTRIBUTES) {
        return true;
    }

    DeleteDirectoryTree(nativeDir);
    EnsureDirectoryTree(nativeDir);

    std::vector<std::wstring> lwjglNativeJars;
    std::vector<std::wstring> jnaJars;
    CollectManifestNativeSources(manifestPath, runtimeRoot, lwjglNativeJars, jnaJars);
    WriteLogF(L"Preparing target natives target=%s lwjglNativeJars=%zu jnaJars=%zu",
        targetId.c_str(), lwjglNativeJars.size(), jnaJars.size());

    bool extractedLwjgl = false;
    for (const std::wstring& jar : lwjglNativeJars) {
        extractedLwjgl = ExtractDllsFromJar(jar, nativeDir, false) || extractedLwjgl;
    }

    bool extractedJna = false;
    for (const std::wstring& jar : jnaJars) {
        extractedJna = ExtractDllsFromJar(jar, nativeDir, true) || extractedJna;
    }

    CopyFileIfNeeded(packageNativesDir + L"\\glfw.dll", nativeDir + L"\\glfw.dll");

    const bool ready =
        extractedLwjgl &&
        extractedJna &&
        GetFileAttributesW((nativeDir + L"\\lwjgl.dll").c_str()) != INVALID_FILE_ATTRIBUTES &&
        GetFileAttributesW((nativeDir + L"\\glfw.dll").c_str()) != INVALID_FILE_ATTRIBUTES &&
        GetFileAttributesW((nativeDir + L"\\jemalloc.dll").c_str()) != INVALID_FILE_ATTRIBUTES &&
        GetFileAttributesW((nativeDir + L"\\jnidispatch.dll").c_str()) != INVALID_FILE_ATTRIBUTES;
    if (ready) {
        WriteTextFile(markerPath, marker);
    } else {
        WriteLogF(L"Target native preparation incomplete target=%s dir=%s", targetId.c_str(), nativeDir.c_str());
    }
    return ready;
}
