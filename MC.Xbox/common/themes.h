#pragma once

#include <windows.h>

#include <string>
#include <vector>

namespace themes {

struct Palette {
    UINT32 bg;
    UINT32 surface;
    UINT32 raised;
    UINT32 text;
    UINT32 textMuted;
    UINT32 accent;
    UINT32 ok;
    UINT32 danger;
    UINT32 onLight;
    UINT32 shade;
};

struct Theme {
    std::wstring id;
    std::wstring name;
    Palette palette;
    bool custom = false;
};

void Reload();
void Reload(const std::wstring& folder);

std::vector<Theme> Themes();

Palette Current();
std::wstring CurrentId();
std::wstring CurrentName();

bool SetTheme(const std::wstring& id);

std::wstring Folder();

}
