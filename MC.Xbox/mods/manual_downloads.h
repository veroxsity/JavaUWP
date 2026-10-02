#pragma once

#include <string>
#include <vector>

// files a pack needed but curseforge would not serve, kept until the file turns up in the profile
struct ManualDownload {
    std::wstring fileName;
    std::wstring modName;
    std::wstring url;
    // relative to the profile game dir, mods, resourcepacks or shaderpacks
    std::wstring folder = L"mods";
};

bool RecordManualDownloads(const std::wstring& profileId, const std::vector<ManualDownload>& items);

std::vector<ManualDownload> PendingManualDownloads(const std::wstring& runtimeRoot, const std::wstring& profileId);

bool ClearManualDownloads(const std::wstring& profileId);
