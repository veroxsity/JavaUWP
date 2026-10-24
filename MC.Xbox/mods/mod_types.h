#pragma once

#include "i18n.h"

#include <string>

enum class ModSource {
    Modrinth = 0,
    CurseForge = 1,
};

enum class ContentKind {
    Mod,
    Modpack,
    ResourcePack,
    Shader,
};

inline const wchar_t* ContentNoun(ContentKind kind, bool plural) {
    switch (kind) {
    case ContentKind::Modpack: return plural ? TR_NOOP(L"modpacks") : TR_NOOP(L"modpack");
    case ContentKind::ResourcePack: return plural ? TR_NOOP(L"resource packs") : TR_NOOP(L"resource pack");
    case ContentKind::Shader: return plural ? TR_NOOP(L"shaders") : TR_NOOP(L"shader");
    case ContentKind::Mod: break;
    }
    return plural ? TR_NOOP(L"mods") : TR_NOOP(L"mod");
}

inline const wchar_t* ContentLabel(ContentKind kind) {
    switch (kind) {
    case ContentKind::Modpack: return TR_NOOP(L"Modpack");
    case ContentKind::ResourcePack: return TR_NOOP(L"Resource pack");
    case ContentKind::Shader: return TR_NOOP(L"Shader");
    case ContentKind::Mod: break;
    }
    return TR_NOOP(L"Mod");
}

// relative to the profile game dir. a modpack becomes a profile of its own so it has no folder
inline const wchar_t* ContentFolder(ContentKind kind) {
    switch (kind) {
    case ContentKind::ResourcePack: return L"resourcepacks";
    case ContentKind::Shader: return L"shaderpacks";
    case ContentKind::Mod:
    case ContentKind::Modpack: break;
    }
    return L"mods";
}

struct ModCard {
    std::wstring projectId;
    std::wstring slug;
    std::wstring title;
    std::wstring description;
    std::wstring iconPath;
    std::wstring iconUrl;
    std::wstring filePath;
    std::wstring status;
    bool installed = false;
    ContentKind kind = ContentKind::Mod;
    ModSource source = ModSource::Modrinth;
};
