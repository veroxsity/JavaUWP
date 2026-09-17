#pragma once

#include <functional>
#include <string>
#include <vector>

#include <windows.ui.core.h>

#include "loader_common.h"
#include "minecraft_auth.h"
#include "runtime_manager.h"

struct LaunchUiSnapshot {
    std::wstring status;
    std::wstring detail;
    float progress = 0.1f;
    std::wstring logTail;
    bool graphicsReady = false;
};

std::wstring ReadLaunchLogTailForUi(const std::vector<std::wstring>& paths, size_t maxLines = 9);
LaunchUiSnapshot BuildLaunchUiSnapshot(
    const std::vector<std::wstring>& launchLogPaths,
    const std::wstring& glfwLogPath,
    const std::wstring& latestLogPath,
    const std::wstring& loaderLabel);
void CollectJars(const std::wstring& dir, std::vector<std::wstring>& jars);
void CollectManifestLibraryJars(
    const std::wstring& manifestPath,
    const std::wstring& runtimeRoot,
    const std::wstring& packageDir,
    std::vector<std::wstring>& jars);
bool PublishCoreWindowProperty(ABI::Windows::UI::Core::ICoreWindow* window);

using LaunchProgressCallback = std::function<void(const wchar_t* status, const wchar_t* detail, float progress)>;

bool EmbeddedJvmAlreadyUsed();

bool RunEmbeddedMinecraft(
    const std::wstring& exeDir,
    const std::wstring& packageDir,
    const std::wstring& jreDir,
    const std::wstring& packagedJreRelativeDir,
    const std::wstring& javaBasePatchName,
    const std::wstring& javaZipfsPatchName,
    const std::wstring& gameDir,
    const std::wstring& assetsDir,
    const std::wstring& nativesDir,
    const std::wstring& bundledModsDir,
    const std::wstring& userModsDir,
    const std::wstring& clientJar,
    const std::wstring& javaLog,
    const std::wstring& argsPath,
    const std::wstring& classPath,
    const std::wstring& launchVersion,
    const std::wstring& assetIndex,
    const std::wstring& minecraftVersion,
    const std::wstring& loader,
    const std::wstring& loaderVersion,
    const std::wstring& mainClassName,
    const std::vector<std::wstring>& extraJvmArgs,
    const std::vector<std::wstring>& extraGameArgs,
    const std::wstring& neoFormVersion,
    const std::wstring& neoForgeInstallToolsVersion,
    const std::wstring& neoForgeJarSplitterVersion,
    const std::wstring& neoForgeBinaryPatcherVersion,
    const std::wstring& neoForgeAutoRenamingToolVersion,
    const LaunchAuthConfig& authConfig,
    LaunchProgressCallback progress = {});
