#pragma once

#include <string>
#include <vector>

bool IsSafeWorldName(const std::wstring& name);

std::wstring WorldExportsDir(const std::wstring& runtimeRoot);
std::wstring DefaultWorldExportPath(const std::wstring& runtimeRoot, const std::wstring& profileId, const std::wstring& worldName);

std::vector<std::wstring> ListProfileWorlds(const std::wstring& runtimeRoot, const std::wstring& profileId);

bool ExportWorldZip(
    const std::wstring& runtimeRoot,
    const std::wstring& profileId,
    const std::wstring& worldName,
    const std::wstring& outputPath,
    std::wstring& error);

bool ImportWorldFromZip(
    const std::wstring& zipPath,
    const std::wstring& runtimeRoot,
    const std::wstring& profileId,
    const std::wstring& worldName,
    bool replaceExisting,
    std::wstring& error);
