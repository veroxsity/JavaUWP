#pragma once

#include <functional>
#include <string>

std::wstring ProfileExportsDir(const std::wstring& runtimeRoot);
std::wstring DefaultProfileExportPath(const std::wstring& runtimeRoot, const std::wstring& profileId);

bool ExportProfileMrpack(
    const std::wstring& runtimeRoot,
    const std::wstring& profileId,
    const std::wstring& outputPath,
    std::wstring& error);

// returns a byte progress sink for one file, so the caller decides how to show it
using ModpackProgressFactory =
    std::function<std::function<void(unsigned long long)>(const std::wstring& label, unsigned long long total)>;

bool InstallModpackFromFile(
    const std::wstring& mrpackPath,
    const std::wstring& runtimeRoot,
    const std::wstring& profileId,
    std::wstring& error,
    std::wstring* skippedNote = nullptr,
    const ModpackProgressFactory& progressFor = nullptr);
