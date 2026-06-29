#pragma once

#include <string>
#include <utility>
#include <vector>

#include <windows.h>

#include "mod_types.h"
#include "profiles.h"
#include "qr_code.h"

struct AuthUiState {
    std::wstring title;
    std::wstring userCode;
    std::wstring verificationUri;
    std::wstring status;
    std::wstring detail;
    int secondsRemaining = 0;
    bool isError = false;
    bool showDeviceCode = true;
    bool showMainMenu = false;
    bool showRemoteFiles = false;
    bool showModsPage = false;
    int selectedMenuIndex = 0;
    int selectedModsTab = 0;
    int selectedModIndex = 0;
    int modsFocus = 0;
    int modsScrollRow = 0;
    int modsHoverTab = -1;
    int modsTotalHits = 0;
    bool modsExhausted = false;
    bool modsSearchEditing = false;
    std::wstring modsSearchQuery;
    bool modsDetailOpen = false;
    ModCard modsDetailCard;
    std::wstring modsDetailBody;
    std::wstring modsDetailMeta;
    std::vector<std::pair<UINT32, UINT32>> modsDetailBold;
    std::vector<std::pair<UINT32, UINT32>> modsDetailHead;
    bool modsDetailLoading = false;
    int modsDetailScroll = 0;
    unsigned modsDetailReqId = 0;
    std::wstring activeProfileName;
    std::wstring activeProfileId;
    std::vector<LaunchTarget> modsTargets;
    std::wstring modsBrowseTargetId;
    bool modsTargetOpen = false;
    int modsTargetSel = 0;
    bool modsProfileOpen = false;
    std::wstring modsProfileId;
    std::wstring modsProfileName;
    std::wstring modsProfileTargetText;
    bool modsProfileBuiltin = false;
    std::vector<std::wstring> modsProfileMods;
    int modsProfileScroll = 0;
    int modsProfileFocus = 0;
    int modsProfileSel = 0;
    bool modsRenaming = false;
    std::wstring modsRenameText;
    std::vector<ModCard> modsCards;
    float animation = 0.0f;
    float progress = -1.0f;
    QrMatrix qr;
    bool showLaunchLog = false;
    std::wstring launchLogText;
};
