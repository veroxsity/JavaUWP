#pragma once

#include <string>

#include <windows.ui.core.h>

#include "auth_ui_state.h"
#include "mods_page.h"
#include "profiles.h"

class AuthScreenRenderer;

int PurgeBlockedModsFromDir(const std::wstring& runtimeRoot, const std::wstring& modsDir);
bool IsBlockedModFileName(const std::wstring& fileName);

void ShowModsPage(
    ABI::Windows::UI::Core::ICoreWindow* window,
    AuthScreenRenderer* renderer,
    AuthUiState& state,
    const std::wstring& runtimeRoot);
