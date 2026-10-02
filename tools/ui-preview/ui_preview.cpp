#include "auth_screen.h"

#include <objbase.h>

#include <cstdio>
#include <functional>
#include <string>
#include <vector>

// the real one pumps the CoreWindow and lives in launcher_ui.cpp. offscreen Render returns before calling it
void ProcessAuthUiEvents() {}

namespace {

struct Scene {
    std::wstring name;
    std::function<AuthUiState()> build;
};

ModCard Card(ContentKind kind, const wchar_t* title, const wchar_t* description, const wchar_t* status) {
    ModCard card;
    card.kind = kind;
    card.projectId = title;
    card.title = title;
    card.description = description;
    card.status = status;
    return card;
}

std::vector<ModCard> CardsFor(int tab) {
    switch (tab) {
    case modstab::kProfiles:
        return {
            Card(ContentKind::Mod, L"+ New profile", L"Create 1.21.11 Fabric profile", L""),
            Card(ContentKind::Mod, L"Survival", L"1.21.11 Fabric - 12 mods", L"\u25CF Playing this"),
            Card(ContentKind::Mod, L"Vanilla", L"1.21.11 Fabric - Pure vanilla, no mods", L""),
        };
    case modstab::kModpacks:
        return {
            Card(ContentKind::Modpack, L"Fabulously Optimized", L"Improved performance and graphics with a vanilla feel", L"9120334 downloads"),
            Card(ContentKind::Modpack, L"Simply Optimized", L"The simple, light optimization pack", L"2110440 downloads"),
        };
    case modstab::kResourcePacks:
        return {
            Card(ContentKind::ResourcePack, L"Fresh Animations", L"Entity animations that bring the world to life", L"6213904 downloads"),
            Card(ContentKind::ResourcePack, L"Stay True", L"A vanilla friendly texture overhaul with a little extra", L"1843002 downloads"),
            Card(ContentKind::ResourcePack, L"A Long Pack Name That Has To Wrap Or Clip In The Card", L"Checks how a long title and a long description behave when both run past the edge of the card", L"12 downloads"),
        };
    case modstab::kShaders:
        return {
            Card(ContentKind::Shader, L"Complementary Reimagined", L"Vanilla inspired shaders with plenty of options", L"8801233 downloads"),
            Card(ContentKind::Shader, L"BSL Shaders", L"Bright and smooth lighting", L"7421988 downloads"),
        };
    default:
        return {
            Card(ContentKind::Mod, L"Sodium", L"The fastest rendering optimization mod", L"60113002 downloads"),
            Card(ContentKind::Mod, L"Iris Shaders", L"Shader support that works with Sodium", L"41022331 downloads"),
            Card(ContentKind::Mod, L"Mod Menu", L"Adds a mod menu to view the list of mods", L"38810220 downloads"),
            Card(ContentKind::Mod, L"Fabric API", L"Core API library for the Fabric toolchain", L"91200500 downloads"),
        };
    }
}

LaunchTarget Target(const wchar_t* mc, const wchar_t* loader, const wchar_t* loaderVersion) {
    LaunchTarget target;
    target.minecraftVersion = mc;
    target.loader = loader;
    target.loaderVersion = loaderVersion;
    target.targetId = std::wstring(mc) + L"-" + loader + L"-" + loaderVersion;
    target.displayName = std::wstring(mc) + L" " + loader;
    return target;
}

AuthUiState Screen() {
    AuthUiState state;
    state.showDeviceCode = false;
    state.title = L"Bandit Launcher";
    return state;
}

AuthUiState ModsPage(int tab) {
    AuthUiState state = Screen();
    state.showModsPage = true;
    state.selectedModsTab = tab;
    state.activeProfileName = L"Survival";
    state.modsTargets = {
        Target(L"1.21.11", L"fabric", L"0.19.3"),
        Target(L"1.21.11", L"neoforge", L"21.11.20"),
        Target(L"1.21.1", L"fabric", L"0.19.3"),
        Target(L"1.20.1", L"forge", L"47.4.0"),
        Target(L"26.2", L"fabric", L"0.19.3"),
    };
    state.modsBrowseTargetId = state.modsTargets.front().targetId;
    state.modsCards = CardsFor(tab);
    state.status = std::to_wstring(state.modsCards.size()) + L" of 1812";
    return state;
}

AuthUiState Detail(int tab) {
    AuthUiState state = ModsPage(tab);
    state.modsDetailOpen = true;
    state.modsDetailCard = state.modsCards.front();
    state.modsDetailMeta = L"Decoration, Utility  -  " + state.modsDetailCard.status;
    state.modsDetailBody = L"About\nA sample description so the body text has something to wrap.";
    return state;
}

AuthUiState Settings(bool reporting, ModSource source, const wchar_t* keyHint, const wchar_t* note, bool configured) {
    AuthUiState state = Screen();
    state.showSettings = true;
    state.settingsReportingOn = reporting;
    state.settingsConfigured = configured;
    state.settingsInstallId = L"Reporting id 7f3c9a12-4be0-4c51-9d2e-0a61b5e8c3f4";
    state.settingsModSource = source;
    state.settingsCurseForgeKeyHint = keyHint;
    state.settingsNote = note;
    return state;
}

AuthUiState Crash(bool details, bool consent) {
    AuthUiState state = Screen();
    state.showCrashScreen = true;
    state.crashHeadline = L"Minecraft crashed while loading mods";
    state.crashSuspectLine = L"Likely cause: sodium-fabric-0.6.13+mc1.21.11.jar\nMixin apply failed for net.minecraft.client.render.WorldRenderer";
    state.crashTrace =
        L"java.lang.RuntimeException: Mixin transformation of net.minecraft.class_761 failed\n"
        L"\tat net.fabricmc.loader.impl.launch.knot.KnotClassDelegate.getPostMixinClassByteArray(KnotClassDelegate.java:427)\n"
        L"\tat net.fabricmc.loader.impl.launch.knot.KnotClassDelegate.tryLoadClass(KnotClassDelegate.java:323)\n"
        L"Caused by: org.spongepowered.asm.mixin.throwables.MixinApplyError\n";
    state.crashConsentPayload = L"fingerprint 3f9a1c22b8e04d71\nphase mod_load\nloader fabric 0.19.3\n" + state.crashTrace;
    state.crashDetailsOpen = details;
    state.crashAskConsent = consent;
    state.crashButtonCount = consent ? 3 : 2;
    state.crashFootnote = consent ? L"" : L"Reported automatically. Turn reporting off in Settings.";
    return state;
}

std::vector<Scene> Scenes() {
    std::vector<Scene> scenes;

    scenes.push_back({ L"signin-device-code", [] {
        AuthUiState state;
        state.userCode = L"QXR7-2KWM";
        state.verificationUri = L"microsoft.com/link";
        state.status = L"Waiting for you to sign in";
        state.secondsRemaining = 842;
        state.qr = GenerateLoginQrMatrix("https://www.microsoft.com/link?otc=QXR7-2KWM");
        return state;
    } });
    scenes.push_back({ L"signin-loading", [] {
        AuthUiState state = Screen();
        state.status = L"Downloading Minecraft 1.21.11 libraries";
        state.detail = L"412 of 980 files, 188 MB";
        state.progress = 0.42f;
        return state;
    } });
    scenes.push_back({ L"signin-launch-log", [] {
        AuthUiState state = Screen();
        state.status = L"Starting Minecraft";
        state.detail = L"1.21.11 Fabric 0.19.3, profile Survival";
        state.showLaunchLog = true;
        state.animation = 0.3f;
        state.launchLogText =
            L"[19:47:13] JNI_CreateJavaVM ok\n[19:47:14] Loading Minecraft 1.21.11 with Fabric Loader 0.19.3\n"
            L"[19:47:16] Loading 14 mods\n[19:47:21] Mixins applied\n[19:47:24] Backend library: LWJGL version 3.3.3";
        return state;
    } });

    scenes.push_back({ L"main-menu", [] {
        AuthUiState state = Screen();
        state.showMainMenu = true;
        state.title = L"Steve";
        state.status = L"Ready to play 1.21.11 Fabric";
        state.detail = L"Profile Survival, 12 mods";
        return state;
    } });
    scenes.push_back({ L"remote-files", [] {
        AuthUiState state = Screen();
        state.showRemoteFiles = true;
        state.status = L"http://192.168.1.50:8080";
        state.detail = L"PIN 705143";
        return state;
    } });

    scenes.push_back({ L"settings-modrinth", [] { return Settings(true, ModSource::Modrinth, L"", L"", true); } });
    scenes.push_back({ L"settings-curseforge-no-key", [] { return Settings(false, ModSource::CurseForge, L"", L"", true); } });
    scenes.push_back({ L"settings-curseforge-key", [] {
        return Settings(true, ModSource::CurseForge, L"ends 9f2a", L"Mod source set to CurseForge", true);
    } });
    scenes.push_back({ L"settings-unconfigured", [] { return Settings(false, ModSource::Modrinth, L"", L"", false); } });

    scenes.push_back({ L"crash-summary", [] { return Crash(false, false); } });
    scenes.push_back({ L"crash-details", [] { return Crash(true, false); } });
    scenes.push_back({ L"crash-consent", [] { return Crash(false, true); } });

    for (int tab = 0; tab < modstab::kCount; ++tab) {
        std::wstring label = ModsTabAt(tab).label;
        for (wchar_t& c : label) c = c == L' ' ? L'-' : static_cast<wchar_t>(towlower(c));
        scenes.push_back({ L"mods-" + std::to_wstring(tab) + L"-" + label, [tab] { return ModsPage(tab); } });
    }
    scenes.push_back({ L"mods-detail-modpack", [] { return Detail(modstab::kModpacks); } });
    scenes.push_back({ L"mods-detail-resource-pack", [] { return Detail(modstab::kResourcePacks); } });
    scenes.push_back({ L"mods-detail-shader", [] { return Detail(modstab::kShaders); } });
    scenes.push_back({ L"mods-profile-open", [] {
        AuthUiState state = ModsPage(modstab::kProfiles);
        state.status.clear();
        state.modsProfileOpen = true;
        state.modsProfileId = L"survival";
        state.modsProfileName = L"Survival";
        state.modsProfileTargetText = L"1.21.11 Fabric 0.19.3";
        state.modsProfileMods = {
            L"sodium-fabric-0.6.13+mc1.21.11.jar", L"iris-fabric-1.8.12+mc1.21.11.jar", L"modmenu-15.0.0.jar",
            L"fabric-api-0.130.0+1.21.11.jar", L"lithium-fabric-0.15.1+mc1.21.11.jar", L"entityculling-fabric-1.8.2-mc1.21.11.jar",
        };
        return state;
    } });
    scenes.push_back({ L"mods-target-open", [] {
        AuthUiState state = ModsPage(modstab::kPopular);
        state.modsFocus = 3;
        state.modsTargetOpen = true;
        state.modsTargetSel = 1;
        return state;
    } });
    scenes.push_back({ L"mods-empty", [] {
        AuthUiState state = ModsPage(modstab::kShaders);
        state.modsCards.clear();
        state.modsSearchQuery = L"nothing matches this";
        state.status = L"No shaders found";
        return state;
    } });
    scenes.push_back({ L"mods-error", [] {
        AuthUiState state = ModsPage(modstab::kPopular);
        state.modsSource = ModSource::CurseForge;
        state.modsCards.clear();
        state.status = L"No CurseForge API key is set. Add one in Remote Files.";
        state.isError = true;
        return state;
    } });
    scenes.push_back({ L"mods-search-typing", [] {
        AuthUiState state = ModsPage(modstab::kResourcePacks);
        state.modsFocus = 1;
        state.modsSearchEditing = true;
        state.modsSearchQuery = L"fresh anim";
        return state;
    } });

    return scenes;
}

// every com object the renderer holds has to be released before CoUninitialize, so it lives in here
int RenderAll(const std::wstring& outDir) {
    // the console reports a 1920x1080 view at scale 1, and the layout and font sizes are fixed to it
    AuthScreenRenderer renderer;
    if (!renderer.InitializeOffscreen(1920.0f, 1080.0f, 1.0f)) {
        fwprintf(stderr, L"could not create the offscreen renderer\n");
        return 1;
    }

    int failures = 0;
    for (const Scene& scene : Scenes()) {
        renderer.Render(scene.build());
        const std::wstring path = outDir + L"\\" + scene.name + L".png";
        if (renderer.SaveFramePng(path)) {
            wprintf(L"%s\n", path.c_str());
        } else {
            fwprintf(stderr, L"could not write %s\n", path.c_str());
            ++failures;
        }
    }
    return failures;
}

}

int wmain(int argc, wchar_t** argv) {
    if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) return 1;
    const std::wstring outDir = argc > 1 ? argv[1] : L"ui-preview-out";
    CreateDirectoryW(outDir.c_str(), nullptr);
    const int failures = RenderAll(outDir);
    CoUninitialize();
    return failures == 0 ? 0 : 1;
}
