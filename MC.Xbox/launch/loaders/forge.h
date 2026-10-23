#pragma once

#include <string>
#include <vector>

#include "loader.h"

std::wstring ForgeMavenVersion(const std::wstring& launchVersion);

void ForgeFinalizeVersionInfo(
    MinecraftVersionInfo& info,
    const LaunchTarget& target,
    const LaunchTarget& defaultTarget);
void ForgeBeforeLaunch(const LoaderPreLaunchContext& ctx);
void ForgeAdjustClasspath(const LoaderJvmContext& ctx, std::wstring& classPath, LoaderJvmSetupResult& result);
void ForgeAddJvmOptions(const LoaderJvmContext& ctx, std::vector<std::string>& vmOptions);
bool ForgePrepareArtifactsAfterJvm(
    JNIEnv* env,
    const LoaderJvmContext& ctx,
    std::wstring& effectiveClassPath,
    bool forgeStartedWithGameClassPath);
