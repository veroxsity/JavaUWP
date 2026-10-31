#pragma once

#include <string>
#include <vector>

#include "loader.h"

void CleanroomFinalizeVersionInfo(
    MinecraftVersionInfo& info,
    const LaunchTarget& target,
    const LaunchTarget& defaultTarget);
void CleanroomBeforeLaunch(const LoaderPreLaunchContext& ctx);
void CleanroomAdjustClasspath(const LoaderJvmContext& ctx, std::wstring& classPath, LoaderJvmSetupResult& result);
void CleanroomAddJvmOptions(const LoaderJvmContext& ctx, std::vector<std::string>& vmOptions);
