#pragma once

#include <functional>
#include <string>
#include <vector>

#include <jni.h>

#include "loader_common.h"
#include "profiles.h"
#include "runtime_manager.h"

struct LoaderPreLaunchContext {
    std::wstring exeDir;
    std::wstring packageDir;
    std::wstring sharedGameDir;
    std::wstring gameDir;
    std::wstring minecraftVersion;
    std::wstring packagedLibrariesDir;
    MinecraftVersionInfo versionInfo;
};

struct LoaderJvmContext {
    LoaderId loader = LoaderId::Unknown;
    std::wstring mainClass;
    std::wstring prepClassPath;
    std::wstring exeDir;
    std::wstring packageDir;
    std::wstring gameDir;
    std::wstring clientJar;
    std::wstring bundledModsDir;
    std::wstring userModsDir;
    std::wstring launchVersion;
    std::wstring loaderVersion;
    std::wstring minecraftVersion;
    std::wstring neoFormVersion;
    std::wstring neoForgeInstallToolsVersion;
    std::wstring neoForgeBinaryPatcherVersion;
    std::vector<std::wstring> extraGameArgs;
    std::wstring launcherOverrideDir;
    std::wstring launcherLogDir;
    std::wstring fabricLogPath;
    std::wstring libraryDir;
    std::wstring neoForgeMinecraftSrgJar;
    std::function<std::wstring(std::wstring)> expandLaunchArg;
};

struct LoaderJvmSetupResult {
    std::wstring effectiveClassPath;
    bool neoForgeStartedWithGameClassPath = false;
};

void LoaderFinalizeVersionInfo(
    MinecraftVersionInfo& info,
    const LaunchTarget& target,
    const std::wstring& packageDir,
    const LaunchTarget& defaultTarget);
void LoaderBeforeLaunch(const LoaderPreLaunchContext& ctx);
std::wstring LoaderDefaultMainClass(LoaderId loader, const std::wstring& mainClassName);
const wchar_t* LoaderTailLogLabel(LoaderId loader);
void LoaderAdjustClasspath(const LoaderJvmContext& ctx, std::wstring& classPath, LoaderJvmSetupResult& result);
void LoaderAddJvmOptions(const LoaderJvmContext& ctx, std::vector<std::string>& vmOptions);
bool LoaderPrepareArtifactsAfterJvm(
    JNIEnv* env,
    const LoaderJvmContext& ctx,
    std::wstring& effectiveClassPath,
    bool neoForgeStartedWithGameClassPath);
