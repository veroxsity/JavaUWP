#pragma once

#include <functional>
#include <string>
#include <vector>

#include "profiles.h"

struct DownloadManifestEntry {
    std::wstring relativePath;
    std::wstring url;
    std::string sha1;
    unsigned long long size = 0;
};

struct DownloadOptions {
    bool forceRepair = false;
    int workerCount = 4;
};

struct JavaRuntimeInfo {
    std::wstring runtimeId;
    std::wstring packageRelativeDir;
    std::wstring packageDir;
    std::wstring localDir;
    std::wstring selectedDir;
    std::wstring javaBasePatchName;
    std::wstring zipfsPatchName;
};

struct MinecraftVersionInfo {
    std::wstring targetId;
    std::wstring minecraftVersion;
    std::wstring loader;
    std::wstring loaderVersion;
    std::wstring javaRuntime;
    std::wstring assetIndex;
    std::wstring launchVersion;
    std::wstring mainClass;
    std::vector<std::wstring> extraJvmArgs;
    std::vector<std::wstring> extraGameArgs;
    std::wstring neoFormVersion;
    std::wstring neoForgeInstallToolsVersion;
    std::wstring neoForgeJarSplitterVersion;
    std::wstring neoForgeBinaryPatcherVersion;
    std::wstring neoForgeAutoRenamingToolVersion;
    std::wstring manifestPath;
    std::wstring loaderJar;
    std::wstring clientJar;
    std::wstring bundledModsDir;
    bool supported = false;
};

using RuntimeSeedProgressCallback = std::function<void(const wchar_t*, const wchar_t*, float)>;
using DownloadProgressCallback = std::function<void(const wchar_t*, const wchar_t*, float)>;

bool ReadDownloadManifest(const std::wstring& path, std::vector<DownloadManifestEntry>& entries);
std::wstring JoinRuntimeRelativePath(const std::wstring& root, std::wstring relativePath);
bool IsLocalRuntimeSeedCurrent(const std::wstring& packageDir, const std::wstring& localDir);
bool SeedLocalRuntime(
    const std::wstring& packageDir,
    const std::wstring& localDir,
    const RuntimeSeedProgressCallback& progress = RuntimeSeedProgressCallback());
void CopyDirectoryContentsAlways(const std::wstring& src, const std::wstring& dst);
int DeleteBanditControllerModsFromDir(const std::wstring& modsDir);
int DeleteControlifyModsFromDir(const std::wstring& modsDir);
std::wstring PrepareEffectiveBundledModsDir(
    const std::wstring& runtimeRoot,
    const std::wstring& targetId,
    const std::wstring& bundledModsDir,
    bool controllerModEnabled);
void ArchiveCurrentLogsToPrevious(const std::wstring& runtimeRoot);
std::wstring DetectGraphicsRuntimeName();
bool EnsureRuntimeDownloads(
    const std::wstring& manifestPath,
    const std::wstring& runtimeRoot,
    const std::wstring& targetId,
    const DownloadProgressCallback& progress,
    const DownloadOptions& options);
MinecraftVersionInfo ResolveVersionInfo(
    const std::wstring& packageDir,
    const std::wstring& runtimeRoot,
    const LaunchTarget& target);
JavaRuntimeInfo ResolveJavaRuntimeInfo(
    const std::wstring& packageDir,
    const std::wstring& localRoot,
    const std::wstring& requestedRuntime);
bool PrepareTargetNativeDir(
    const std::wstring& manifestPath,
    const std::wstring& runtimeRoot,
    const std::wstring& packageDir,
    const std::wstring& targetId,
    std::wstring& nativeDir);
bool Sha1File(const std::wstring& path, std::string* outHex);
bool FileMatchesSha1(const std::wstring& path, const std::string& expectedSha1);
bool DownloadUrlToFile(
    const std::wstring& url,
    const std::wstring& destination,
    const std::function<void(unsigned long long)>& progressCallback);
