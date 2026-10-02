#pragma once

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
    case ContentKind::Modpack: return plural ? L"modpacks" : L"modpack";
    case ContentKind::ResourcePack: return plural ? L"resource packs" : L"resource pack";
    case ContentKind::Shader: return plural ? L"shaders" : L"shader";
    case ContentKind::Mod: break;
    }
    return plural ? L"mods" : L"mod";
}

inline const wchar_t* ContentLabel(ContentKind kind) {
    switch (kind) {
    case ContentKind::Modpack: return L"Modpack";
    case ContentKind::ResourcePack: return L"Resource pack";
    case ContentKind::Shader: return L"Shader";
    case ContentKind::Mod: break;
    }
    return L"Mod";
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
