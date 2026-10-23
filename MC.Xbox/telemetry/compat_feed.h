#pragma once

#include <string>

namespace compat {

struct Flag {
    bool found = false;
    bool versionSpecific = false;
    std::wstring slug;
    std::wstring severity;
    std::wstring note;
};

// refreshes stale cache without blocking the mods page
void RefreshAsync();

Flag Lookup(
    const std::wstring& slug,
    const std::wstring& projectId,
    const std::wstring& mcVersion,
    const std::wstring& loader,
    const std::wstring& modVersion = L"");

}
