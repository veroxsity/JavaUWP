#pragma once

#include <string>

#include "loader.h"

std::wstring NeoForgeVersionFromLaunchVersion(const std::wstring& launchVersion);
std::wstring NeoForgeSrgJarSummary(const std::wstring& zipPath);

void NeoForgeFinalizeVersionInfo(
    MinecraftVersionInfo& info,
    const LaunchTarget& target,
    const LaunchTarget& defaultTarget);
void NeoForgeBeforeLaunch(const LoaderPreLaunchContext& ctx);
void NeoForgeAdjustClasspath(const LoaderJvmContext& ctx, std::wstring& classPath, LoaderJvmSetupResult& result);
void NeoForgeAddJvmOptions(const LoaderJvmContext& ctx, std::vector<std::string>& vmOptions);
bool NeoForgePrepareArtifactsAfterJvm(
    JNIEnv* env,
    const LoaderJvmContext& ctx,
    std::wstring& effectiveClassPath,
    bool neoForgeStartedWithGameClassPath);
