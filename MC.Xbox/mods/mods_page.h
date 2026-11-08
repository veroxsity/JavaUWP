#pragma once

#include "auth_ui_state.h"
#include "mod_types.h"
#include "profiles.h"

namespace modstab {
constexpr int kProfiles = 0;
constexpr int kPopular = 1;
constexpr int kLatest = 2;
constexpr int kRecommended = 3;
constexpr int kModpacks = 4;
constexpr int kResourcePacks = 5;
constexpr int kShaders = 6;
constexpr int kCount = 7;
}

struct ModsTabInfo {
    const wchar_t* label;
    const wchar_t* icon;
    ContentKind kind;
    // paged search against the active source. profiles and recommended are local lists
    bool searchesSource;
    bool sortByDownloads;
};

inline const ModsTabInfo& ModsTabAt(int tab) {
    static const ModsTabInfo kTabs[modstab::kCount] = {
        { L"Profiles",       L"\uE8B7", ContentKind::Mod,          false, false },
        { L"Popular",        L"\uE735", ContentKind::Mod,          true,  true  },
        { L"Latest",         L"\uE823", ContentKind::Mod,          true,  false },
        { L"Recommended",    L"\uEB52", ContentKind::Mod,          false, false },
        { L"Modpacks",       L"\uE7B8", ContentKind::Modpack,      true,  true  },
        { L"Resource packs", L"\uE790", ContentKind::ResourcePack, true,  true  },
        { L"Shaders",        L"\uE706", ContentKind::Shader,       true,  true  },
    };
    return kTabs[(tab >= 0 && tab < modstab::kCount) ? tab : modstab::kProfiles];
}

inline const wchar_t* ContentIcon(ContentKind kind) {
    switch (kind) {
    case ContentKind::Modpack: return L"\uE7B8";
    case ContentKind::ResourcePack: return L"\uE790";
    case ContentKind::Shader: return L"\uE706";
    case ContentKind::Mod: break;
    }
    return L"\uE74C";
}

inline int ModsTargetIndex(const AuthUiState& state) {
    for (size_t i = 0; i < state.modsTargets.size(); ++i) {
        if (state.modsTargets[i].targetId == state.modsBrowseTargetId) return static_cast<int>(i);
    }
    return -1;
}

inline LaunchTarget CurrentModsTarget(const AuthUiState& state) {
    const int idx = ModsTargetIndex(state);
    if (idx >= 0) return state.modsTargets[static_cast<size_t>(idx)];
    return DefaultLaunchTarget();
}
