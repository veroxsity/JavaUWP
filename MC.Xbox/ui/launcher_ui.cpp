#include "launcher_ui.h"

#include "mod_source.h"

#include "mods_browser.h"
#include "mods_ui_globals.h"
#include "auth_screen.h"
#include "launcher_mouse.h"
#include "i18n.h"
#include "launcher_common.h"
#include "minecraft_auth.h"
#include "minecraft_launch.h"
#include "profiles.h"
#include "remote_file_server.h"
#include "qr_code.h"
#include "telemetry.h"
#include "update_check.h"

#include <chrono>
#include <thread>

#include <wrl.h>

using Microsoft::WRL::ComPtr;
using i18n::Tr;
using i18n::TrFormat;
using namespace ABI::Windows::UI::Core;

extern ComPtr<ICoreWindow> g_authWindow;

static bool IsVirtualKeyDown(ICoreWindow* window, ABI::Windows::System::VirtualKey key) {
    if (!CoreWindowAcceptsInput()) return false;
    if (!window) return false;
    CoreVirtualKeyStates state = CoreVirtualKeyStates_None;
    if (FAILED(window->GetKeyState(key, &state))) {
        return false;
    }
    return (state & CoreVirtualKeyStates_Down) == CoreVirtualKeyStates_Down;
}

bool AnyVirtualKeyDown(ICoreWindow* window, std::initializer_list<ABI::Windows::System::VirtualKey> keys) {
    for (const auto key : keys) {
        if (IsVirtualKeyDown(window, key)) {
            return true;
        }
    }
    return false;
}
void ProcessAuthUiEvents() {
    if (!g_authWindow) return;
    static bool dispatcherErrorLogged = false;

    ComPtr<ICoreDispatcher> dispatcher;
    HRESULT hr = g_authWindow->get_Dispatcher(dispatcher.GetAddressOf());
    if (FAILED(hr) || !dispatcher) {
        if (!dispatcherErrorLogged) {
            dispatcherErrorLogged = true;
            WriteLogF(L"Auth screen get_Dispatcher failed hr=0x%08X", hr);
        }
        return;
    }

    boolean hasThreadAccess = false;
    hr = dispatcher->get_HasThreadAccess(&hasThreadAccess);
    if (FAILED(hr) || !hasThreadAccess) {
        if (!dispatcherErrorLogged) {
            dispatcherErrorLogged = true;
            WriteLogF(L"Auth screen dispatcher access unavailable hr=0x%08X access=%d",
                hr, hasThreadAccess ? 1 : 0);
        }
        return;
    }

    hr = dispatcher->ProcessEvents(CoreProcessEventsOption_ProcessAllIfPresent);
    if (FAILED(hr) && !dispatcherErrorLogged) {
        dispatcherErrorLogged = true;
        WriteLogF(L"Auth screen ProcessEvents failed hr=0x%08X", hr);
    }
}

LauncherMouse& LauncherMouseInstance() {
    static LauncherMouse instance;
    return instance;
}

void RenderAuth(AuthScreenRenderer* renderer, const AuthUiState& state) {
    ProcessAuthUiEvents();
    if (renderer) {
        LauncherMouse& mouse = LauncherMouseInstance();
        mouse.Update(renderer->Width(), renderer->Height());
        renderer->SetCursor(mouse.X(), mouse.Y(), mouse.Visible());
        if (renderer->NeedsRecovery()) {
            AuthUiState recovery = state;
            recovery.status = Tr(L"Display interrupted. Retrying");
            recovery.isError = true;
            renderer->Render(recovery);
        } else {
            renderer->Render(state);
        }
    }
}

void SleepWithAuthUi(AuthScreenRenderer* renderer, AuthUiState& state, int milliseconds) {
    const auto start = std::chrono::steady_clock::now();
    while (true) {
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start).count();
        if (elapsed >= milliseconds) break;

        RenderAuth(renderer, state);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

void RenderPreparationProgress(
    AuthScreenRenderer* renderer,
    AuthUiState& state,
    const wchar_t* status,
    const wchar_t* detail,
    float progress) {
    state.title = Tr(L"Preparing Minecraft");
    state.showDeviceCode = false;
    state.status = status ? status : Tr(L"Preparing runtime");
    state.detail = detail ? detail : L"";
    state.progress = progress;
    state.secondsRemaining = 0;
    state.isError = false;
    RenderAuth(renderer, state);
}

bool RunLaunchTaskWithLiveUi(
    AuthScreenRenderer* renderer,
    AuthUiState& state,
    const std::vector<std::wstring>& launchLogPaths,
    const std::wstring& loaderLabel,
    const std::function<bool(LaunchProgressCallback progress)>& launchTask) {
    state.title = Tr(L"Preparing Minecraft");
    state.showDeviceCode = false;
    state.showLaunchLog = true;
    state.isError = false;
    state.secondsRemaining = 0;
    state.status = Tr(L"Starting Minecraft");
    state.detail = loaderLabel.empty()
        ? Tr(L"Preparing launch files.")
        : TrFormat(L"Preparing {0} and launch files.", { loaderLabel });
    state.progress = 0.12f;
    state.launchLogText = ReadLaunchLogTailForUi(launchLogPaths);
    RenderAuth(renderer, state);

    LaunchProgressCallback progress = [&](const wchar_t* status, const wchar_t* detail, float value) {
        state.status = status ? status : Tr(L"Starting Minecraft");
        state.detail = detail ? detail : L"";
        state.progress = value;
        state.launchLogText = ReadLaunchLogTailForUi(launchLogPaths);
        state.animation = static_cast<float>((GetTickCount64() % 100000) / 1000.0);
        RenderAuth(renderer, state);
    };

    return launchTask(progress);
}

static void ShowRemoteFilesPage(ICoreWindow* window, AuthScreenRenderer* renderer, AuthUiState& state, const std::wstring& runtimeRoot) {
    WriteLog(L"Remote files page opened");
    StartRemoteFileServer(runtimeRoot);

    state.showMainMenu = false;
    state.showModsPage = false;
    state.showRemoteFiles = true;
    state.showDeviceCode = false;
    state.isError = false;

    bool backWasDown = false;
    while (true) {
        if (RemoteFileServerRunning()) {
            const std::wstring url = RemoteFileServerUrl();
            state.isError = url.empty();
            state.status = url.empty() ? Tr(L"Remote Files has no LAN address") : Tr(L"Remote file manager is running");
            state.detail = url.empty()
                ? Tr(L"Could not find a connected IPv4 LAN address. Connect to the local network and try again.")
                : TrFormat(L"Open {0}\nPIN: {1}", { url, a2w(RemoteFileServerPin().c_str()) });
            if (RemoteFileImportRunning()) state.detail += L"\n" + std::wstring(Tr(L"Import in progress. Wait for it to finish before leaving."));
        } else {
            state.status = Tr(L"Remote file manager is not running");
            state.detail = Tr(L"Could not start the LAN server on port 27632. Check that another copy is not already running.");
            state.isError = true;
        }

        state.animation = static_cast<float>((GetTickCount64() % 100000) / 1000.0);
        RenderAuth(renderer, state);

        LauncherMouse& mouse = LauncherMouseInstance();
        const bool clickBack = mouse.Visible() && mouse.TakeClick() &&
            renderer->HitTest(mouse.X(), mouse.Y()) == launchhit::kBack;

        const bool backDown = AnyVirtualKeyDown(window, {
            ABI::Windows::System::VirtualKey_Escape,
            ABI::Windows::System::VirtualKey_GamepadB
        });
        if (((backDown && !backWasDown) || clickBack) && !RemoteFileImportRunning()) {
            StopRemoteFileServer();
            state.showRemoteFiles = false;
            state.showMainMenu = true;
            state.isError = false;
            state.status = Tr(L"Remote file manager stopped");
            state.detail = L"";
            WriteLog(L"Remote files page closed");
            return;
        }

        backWasDown = backDown;
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
}

// wait for release before the menu resets its edge detection
static void WaitForMenuKeysReleased(ICoreWindow* window, AuthScreenRenderer* renderer, const AuthUiState& state) {
    while (AnyVirtualKeyDown(window, {
        ABI::Windows::System::VirtualKey_Enter,
        ABI::Windows::System::VirtualKey_Space,
        ABI::Windows::System::VirtualKey_GamepadA,
        ABI::Windows::System::VirtualKey_Escape,
        ABI::Windows::System::VirtualKey_GamepadB
    })) {
        RenderAuth(renderer, state);
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
}

static std::wstring CrashHeadline(const telemetry::CrashRecord& record) {
    if (record.repeatCount >= 3) {
        return TrFormat(L"Minecraft has crashed {0} launches in a row", { std::to_wstring(record.repeatCount) });
    }
    if (record.exception.find(L"unknown_") != std::wstring::npos) {
        return Tr(L"Minecraft closed unexpectedly");
    }
    if (record.phase == L"mod_load") return Tr(L"Minecraft crashed while loading mods");
    if (record.phase == L"jvm_init") return Tr(L"Minecraft crashed while starting up");
    if (record.phase == L"ingame") return Tr(L"Minecraft crashed while playing");
    return Tr(L"Minecraft closed unexpectedly");
}

static std::wstring CrashSuspectLine(const telemetry::CrashRecord& record) {
    if (!record.suspectedMod.empty()) {
        std::wstring line = TrFormat(L"Suspected: {0}", { record.suspectedMod });
        if (!record.detail.targetMethod.empty()) {
            line += L"\n" + std::wstring(Tr(L"It asked for something this version of Minecraft does not have."));
        }
        return line;
    }
    if (!record.detail.symbol.empty()) {
        // ole32 is com, and jna cannot LoadLibrary a system dll from a uwp package
        if (record.detail.symbol.find("Ole32") != std::string::npos) {
            return Tr(L"A mod asked Windows for hardware details, which this console blocks.\n"
                   L"Mods that probe the GPU this way cannot run here yet.");
        }
        return TrFormat(L"A mod asked for {0}, which this console does not provide.",
            { a2w(record.detail.symbol.c_str()) });
    }
    if (record.exception.find(L"heap_exhausted") != std::wstring::npos) {
        return TrFormat(L"This pack ran out of memory ({0}).\n"
                        L"The console has about 5 GB for everything, so very large packs cannot load.",
            { record.message });
    }
    if (record.exception.find(L"unknown_") != std::wstring::npos) {
        return Tr(L"Nothing readable was left behind, so there is no diagnosis for this one.");
    }
    if (!record.message.empty()) return record.message;
    return record.exception;
}

static std::wstring CrashTraceText(const telemetry::CrashRecord& record) {
    std::wstring text = record.exception;
    if (!record.message.empty()) text += L"\n" + record.message;
    text += L"\n";
    for (const std::wstring& frame : record.frames) {
        text += L"\n  at " + frame;
    }
    if (record.frames.empty()) text += L"\n" + std::wstring(Tr(L"No stack trace was recovered."));
    text += L"\n\nfingerprint " + record.fingerprint;
    text += L"\nbuild " + record.launcherBuild + L", " + record.mcVersion + L" " + record.loader;
    if (!record.zip.empty()) {
        text += L"\n\n" + std::wstring(Tr(L"The full logs are zipped at:")) + L"\n" + record.zip +
            L"\n" + Tr(L"Open Remote Files on the main menu to read it on a phone or PC.");
    }
    return text;
}

static void ShowCrashScreen(ICoreWindow* window, AuthScreenRenderer* renderer, AuthUiState& state) {
    const telemetry::CrashRecord record = telemetry::ReadLastCrash();
    if (!record.found) return;

    WriteLogF(L"Crash screen shown for %s, repeat %d",
        record.fingerprint.c_str(), record.repeatCount);

    const bool showConsent =
        telemetry::Configured() && telemetry::Consent() == telemetry::ConsentState::Unanswered;

    const bool wasShowingMenu = state.showMainMenu;
    state.showMainMenu = false;
    state.showModsPage = false;
    state.showRemoteFiles = false;
    state.showDeviceCode = false;
    state.showCrashScreen = true;
    state.crashHeadline = CrashHeadline(record);
    state.crashSuspectLine = CrashSuspectLine(record);
    state.crashTrace = CrashTraceText(record);
    state.crashConsentPayload = telemetry::ConsentPayloadPreview(record);
    state.crashAskConsent = showConsent;
    state.crashDetailsOpen = false;
    state.crashDetailScroll = 0;
    state.crashSelected = 0;
    state.crashButtonCount = showConsent ? 3 : 2;
    state.crashFootnote = showConsent
        ? Tr(L"You can change this later, and reading the crash never sends anything.")
        : Tr(L"Press B or select Dismiss to carry on.");

    bool leftWas = false;
    bool rightWas = false;
    bool selectWas = false;
    bool backWas = false;
    bool upWas = false;
    bool downWas = false;

    while (true) {
        state.animation = static_cast<float>((GetTickCount64() % 100000) / 1000.0);
        RenderAuth(renderer, state);

        LauncherMouse& mouse = LauncherMouseInstance();
        int clicked = -1;
        if (mouse.Visible() && mouse.TakeClick()) {
            const int hit = renderer->HitTest(mouse.X(), mouse.Y());
            if (hit >= launchhit::kCrashButtonBase && hit < launchhit::kCrashButtonBase + 3) {
                clicked = hit - launchhit::kCrashButtonBase;
            }
        }

        const bool leftDown = AnyVirtualKeyDown(window, {
            ABI::Windows::System::VirtualKey_Left,
            ABI::Windows::System::VirtualKey_GamepadDPadLeft,
            ABI::Windows::System::VirtualKey_GamepadLeftThumbstickLeft
        });
        const bool rightDown = AnyVirtualKeyDown(window, {
            ABI::Windows::System::VirtualKey_Right,
            ABI::Windows::System::VirtualKey_GamepadDPadRight,
            ABI::Windows::System::VirtualKey_GamepadLeftThumbstickRight
        });
        const bool upDown = AnyVirtualKeyDown(window, {
            ABI::Windows::System::VirtualKey_Up,
            ABI::Windows::System::VirtualKey_GamepadDPadUp,
            ABI::Windows::System::VirtualKey_GamepadLeftThumbstickUp
        });
        const bool downDown = AnyVirtualKeyDown(window, {
            ABI::Windows::System::VirtualKey_Down,
            ABI::Windows::System::VirtualKey_GamepadDPadDown,
            ABI::Windows::System::VirtualKey_GamepadLeftThumbstickDown
        });
        const bool selectDown = AnyVirtualKeyDown(window, {
            ABI::Windows::System::VirtualKey_Enter,
            ABI::Windows::System::VirtualKey_Space,
            ABI::Windows::System::VirtualKey_GamepadA
        });
        const bool backDown = AnyVirtualKeyDown(window, {
            ABI::Windows::System::VirtualKey_Escape,
            ABI::Windows::System::VirtualKey_GamepadB
        });

        if (leftDown && !leftWas && state.crashButtonCount > 0) {
            state.crashSelected = (state.crashSelected + state.crashButtonCount - 1) % state.crashButtonCount;
        }
        if (rightDown && !rightWas && state.crashButtonCount > 0) {
            state.crashSelected = (state.crashSelected + 1) % state.crashButtonCount;
        }
        if (upDown && !upWas && state.crashDetailScroll > 0) --state.crashDetailScroll;
        if (downDown && !downWas) {
            const std::wstring& body = state.crashDetailsOpen
                ? state.crashTrace
                : state.crashConsentPayload;
            int lines = 1;
            for (const wchar_t ch : body) {
                if (ch == L'\n') ++lines;
            }
            if (state.crashDetailScroll < lines - 4) ++state.crashDetailScroll;
        }

        if (backDown && !backWas) break;

        const int pressed = clicked >= 0 ? clicked : ((selectDown && !selectWas) ? state.crashSelected : -1);
        if (pressed < 0) {
            leftWas = leftDown;
            rightWas = rightDown;
            upWas = upDown;
            downWas = downDown;
            selectWas = selectDown;
            backWas = backDown;
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
            continue;
        }

        if (state.crashAskConsent) {
            if (pressed == 0) {
                telemetry::SendLastCrashOnce(record);
                state.crashFootnote = Tr(L"Sending this one report. Reporting stays off.");
            } else if (pressed == 1) {
                if (telemetry::SetConsent(telemetry::ConsentState::Always)) {
                    telemetry::SendLastCrashOnce(record);
                    telemetry::FlushQueueAsync();
                    state.crashFootnote = Tr(L"Reporting is on. Turn it off any time from Settings.");
                } else {
                    state.crashFootnote = Tr(L"Reporting could not be turned on. Nothing was sent.");
                }
            } else {
                if (telemetry::SetConsent(telemetry::ConsentState::Never)) {
                    telemetry::ClearQueue();
                    state.crashFootnote = Tr(L"Nothing will be sent. Crashes are still explained here.");
                } else {
                    state.crashFootnote = Tr(L"Reporting could not be turned off.");
                }
            }
            state.crashAskConsent = false;
            state.crashSelected = 0;
            state.crashButtonCount = 2;
            state.crashDetailScroll = 0;
        } else if (pressed == 0) {
            state.crashDetailsOpen = !state.crashDetailsOpen;
            state.crashDetailScroll = 0;
        } else {
            break;
        }

        leftWas = leftDown;
        rightWas = rightDown;
        upWas = upDown;
        downWas = downDown;
        selectWas = selectDown;
        backWas = backDown;
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }

    telemetry::ClearLastCrash();

    WaitForMenuKeysReleased(window, renderer, state);

    state.showCrashScreen = false;
    state.showMainMenu = wasShowingMenu;
    WriteLog(L"Crash screen dismissed");
}

static void ShowChangelogScreen(
    ICoreWindow* window,
    AuthScreenRenderer* renderer,
    AuthUiState& state,
    const updates::Offer* offer) {
    WriteLog(offer ? L"Update notice shown" : L"Changelog opened");

    bool fromGitHub = false;
    const std::wstring markdown = updates::ChangelogMarkdown(&fromGitHub);
    std::wstring body;
    updates::TextRanges bold;
    updates::TextRanges head;
    updates::FormatChangelog(markdown, body, bold, head);

    const std::wstring previousStatus = state.status;
    const std::wstring previousDetail = state.detail;

    state.showMainMenu = false;
    state.showModsPage = false;
    state.showRemoteFiles = false;
    state.showSettings = false;
    state.showDeviceCode = false;
    state.showChangelog = true;
    state.changelogOffer = offer != nullptr;
    state.changelogBody = body;
    state.changelogBold = bold;
    state.changelogHead = head;
    state.changelogScroll = 0;
    state.changelogSelected = 0;
    state.isError = false;
    if (offer) {
        state.changelogHeadline = TrFormat(L"Version {0} is available", { offer->latest });
        state.changelogSub = TrFormat(L"You are running {0}. Updating is up to you.", { offer->current });
        state.changelogUrl = updates::ReleasesUrl();
        state.qr = GenerateLoginQrMatrix(w2a(state.changelogUrl));
    } else {
        state.changelogHeadline = Tr(L"What's new");
        state.changelogSub = fromGitHub
            ? std::wstring(Tr(L"The latest changes, fetched from GitHub."))
            : TrFormat(L"The changes that came with this version ({0}).", { telemetry::LauncherBuild() });
        std::wstring newest;
        if (updates::NewerAvailable(newest)) {
            state.changelogSub += L" " + TrFormat(L"Version {0} is out. Get it from {1}.", { newest, updates::ReleasesUrl() });
        }
    }

    bool leftWas = false;
    bool rightWas = false;
    bool selectWas = false;
    bool backWas = false;
    int scrollDirWas = 0;
    ULONGLONG nextScrollAt = 0;

    while (true) {
        state.animation = static_cast<float>((GetTickCount64() % 100000) / 1000.0);
        RenderAuth(renderer, state);

        LauncherMouse& mouse = LauncherMouseInstance();
        int clicked = -1;
        bool clickedBack = false;
        if (mouse.Visible() && mouse.TakeClick()) {
            const int hit = renderer->HitTest(mouse.X(), mouse.Y());
            if (hit == launchhit::kBack) clickedBack = true;
            if (hit >= launchhit::kChangelogButtonBase && hit < launchhit::kChangelogButtonBase + 2) {
                clicked = hit - launchhit::kChangelogButtonBase;
            }
        }

        const bool leftDown = AnyVirtualKeyDown(window, {
            ABI::Windows::System::VirtualKey_Left,
            ABI::Windows::System::VirtualKey_GamepadDPadLeft,
            ABI::Windows::System::VirtualKey_GamepadLeftThumbstickLeft
        });
        const bool rightDown = AnyVirtualKeyDown(window, {
            ABI::Windows::System::VirtualKey_Right,
            ABI::Windows::System::VirtualKey_GamepadDPadRight,
            ABI::Windows::System::VirtualKey_GamepadLeftThumbstickRight
        });
        const bool upDown = AnyVirtualKeyDown(window, {
            ABI::Windows::System::VirtualKey_Up,
            ABI::Windows::System::VirtualKey_GamepadDPadUp,
            ABI::Windows::System::VirtualKey_GamepadLeftThumbstickUp
        });
        const bool downDown = AnyVirtualKeyDown(window, {
            ABI::Windows::System::VirtualKey_Down,
            ABI::Windows::System::VirtualKey_GamepadDPadDown,
            ABI::Windows::System::VirtualKey_GamepadLeftThumbstickDown
        });
        const bool selectDown = AnyVirtualKeyDown(window, {
            ABI::Windows::System::VirtualKey_Enter,
            ABI::Windows::System::VirtualKey_Space,
            ABI::Windows::System::VirtualKey_GamepadA
        });
        const bool backDown = AnyVirtualKeyDown(window, {
            ABI::Windows::System::VirtualKey_Escape,
            ABI::Windows::System::VirtualKey_GamepadB
        });

        // a long changelog is slow to read one line per press, so holding the stick keeps scrolling
        const int scrollDir = (downDown ? 1 : 0) - (upDown ? 1 : 0);
        const ULONGLONG now = GetTickCount64();
        if (scrollDir != 0 && (scrollDir != scrollDirWas || now >= nextScrollAt)) {
            const int maxScroll = g_changelogMaxScroll.load();
            state.changelogScroll = scrollDir > 0
                ? (std::min)(maxScroll, state.changelogScroll + 1)
                : (std::max)(0, state.changelogScroll - 1);
            nextScrollAt = now + (scrollDir != scrollDirWas ? 320 : 55);
        }
        scrollDirWas = scrollDir;

        if (mouse.Visible()) {
            const float wheel = mouse.TakeWheel();
            if (wheel != 0.0f) {
                const int notches = static_cast<int>(wheel > 0.0f ? (wheel + 0.5f) : (wheel - 0.5f));
                state.changelogScroll = (std::clamp)(
                    state.changelogScroll - notches * 3, 0, g_changelogMaxScroll.load());
            }
        }

        if (offer && ((leftDown && !leftWas) || (rightDown && !rightWas))) {
            state.changelogSelected = 1 - state.changelogSelected;
        }

        if ((backDown && !backWas) || clickedBack) break;

        const int pressed = clicked >= 0
            ? clicked
            : ((selectDown && !selectWas && offer) ? state.changelogSelected : -1);
        if (pressed == 0) {
            updates::SkipVersion(offer->latest);
            break;
        }
        if (pressed == 1) break;

        leftWas = leftDown;
        rightWas = rightDown;
        selectWas = selectDown;
        backWas = backDown;
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }

    WaitForMenuKeysReleased(window, renderer, state);

    state.showChangelog = false;
    state.changelogOffer = false;
    state.changelogBody.clear();
    state.changelogBold.clear();
    state.changelogHead.clear();
    state.qr = QrMatrix();
    state.showMainMenu = true;
    state.status = previousStatus;
    state.detail = previousDetail;
    WriteLog(L"Changelog closed");
}

static void ShowSettingsPage(ICoreWindow* window, AuthScreenRenderer* renderer, AuthUiState& state) {
    constexpr int kSettingsRows = 7;
    WriteLog(L"Settings page opened");

    state.showMainMenu = false;
    state.showModsPage = false;
    state.showRemoteFiles = false;
    state.showDeviceCode = false;
    state.showSettings = true;
    state.settingsSelected = 0;
    state.settingsNote.clear();
    const MouseSensitivity sensitivity = LoadMouseSensitivity();
    state.settingsMenuMouseSpeed = sensitivity.menu;
    state.settingsGameMouseSpeed = sensitivity.game;
    state.settingsConfigured = telemetry::Configured();
    state.settingsReportingOn = telemetry::ConsentGranted();
    state.settingsInstallId = telemetry::InstallIdText();
    state.settingsModSource = modsource::Current();
    state.settingsCurseForgeKeyHint = modsource::CurseForgeKeyHint();
    state.settingsUpdateCheckOn = updates::Enabled();
    for (const i18n::Language& language : i18n::Languages()) {
        if (language.code == i18n::CurrentCode()) state.settingsLanguageName = language.name;
    }

    bool upWas = false;
    bool downWas = false;
    bool leftWas = false;
    bool rightWas = false;
    bool selectWas = false;
    bool backWas = false;

    while (true) {
        state.animation = static_cast<float>((GetTickCount64() % 100000) / 1000.0);
        RenderAuth(renderer, state);

        LauncherMouse& mouse = LauncherMouseInstance();
        int clicked = -1;
        bool clickedBack = false;
        if (mouse.Visible() && mouse.TakeClick()) {
            const int hit = renderer->HitTest(mouse.X(), mouse.Y());
            if (hit == launchhit::kBack) clickedBack = true;
            if (hit >= launchhit::kSettingsRowBase && hit < launchhit::kSettingsRowBase + kSettingsRows) {
                clicked = hit - launchhit::kSettingsRowBase;
            }
        }

        const bool upDown = AnyVirtualKeyDown(window, {
            ABI::Windows::System::VirtualKey_Up,
            ABI::Windows::System::VirtualKey_GamepadDPadUp,
            ABI::Windows::System::VirtualKey_GamepadLeftThumbstickUp
        });
        const bool downDown = AnyVirtualKeyDown(window, {
            ABI::Windows::System::VirtualKey_Down,
            ABI::Windows::System::VirtualKey_GamepadDPadDown,
            ABI::Windows::System::VirtualKey_GamepadLeftThumbstickDown
        });
        const bool leftDown = AnyVirtualKeyDown(window, {
            ABI::Windows::System::VirtualKey_Left,
            ABI::Windows::System::VirtualKey_GamepadDPadLeft,
            ABI::Windows::System::VirtualKey_GamepadLeftThumbstickLeft
        });
        const bool rightDown = AnyVirtualKeyDown(window, {
            ABI::Windows::System::VirtualKey_Right,
            ABI::Windows::System::VirtualKey_GamepadDPadRight,
            ABI::Windows::System::VirtualKey_GamepadLeftThumbstickRight
        });
        const bool selectDown = AnyVirtualKeyDown(window, {
            ABI::Windows::System::VirtualKey_Enter,
            ABI::Windows::System::VirtualKey_Space,
            ABI::Windows::System::VirtualKey_GamepadA
        });
        const bool backDown = AnyVirtualKeyDown(window, {
            ABI::Windows::System::VirtualKey_Escape,
            ABI::Windows::System::VirtualKey_GamepadB
        });

        if (upDown && !upWas) {
            state.settingsSelected = (state.settingsSelected + kSettingsRows - 1) % kSettingsRows;
        }
        if (downDown && !downWas) {
            state.settingsSelected = (state.settingsSelected + 1) % kSettingsRows;
        }

        if ((backDown && !backWas) || clickedBack) break;

        const int pressed = clicked >= 0 ? clicked : ((selectDown && !selectWas) ? state.settingsSelected : -1);
        const int speedRow = clicked >= 0 ? clicked : state.settingsSelected;
        const int speedStep = leftDown && !leftWas ? -1 :
            (rightDown && !rightWas ? 1 : (pressed == speedRow ? 1 : 0));
        if (speedRow < 2 && speedStep != 0) {
            MouseSensitivity next{state.settingsMenuMouseSpeed, state.settingsGameMouseSpeed};
            int& percent = speedRow == 0 ? next.menu : next.game;
            percent += speedStep * 25;
            if (percent > 300) percent = 25;
            if (percent < 25) percent = 300;
            if (SaveMouseSensitivity(next)) {
                state.settingsMenuMouseSpeed = next.menu;
                state.settingsGameMouseSpeed = next.game;
                ApplyMouseSensitivity(next);
                state.settingsNote = Tr(L"Mouse sensitivity saved.");
            } else {
                state.settingsNote = Tr(L"Mouse sensitivity could not be saved.");
            }
        } else if (pressed == 2) {
            const bool turnOn = !state.settingsReportingOn;
            if (telemetry::SetConsent(turnOn ? telemetry::ConsentState::Always : telemetry::ConsentState::Never)) {
                state.settingsReportingOn = turnOn;
                if (turnOn) {
                    telemetry::FlushQueueAsync();
                    state.settingsNote = Tr(L"Reporting is on. Anything waiting will be retried.");
                } else {
                    telemetry::ClearQueue();
                    state.settingsNote = Tr(L"Reporting is off. Pending reports were deleted.");
                }
            } else {
                state.settingsNote = turnOn
                    ? Tr(L"Reporting could not be turned on.")
                    : Tr(L"Reporting could not be turned off.");
            }
        } else if (pressed == 3) {
            if (telemetry::ResetInstallId()) {
                state.settingsInstallId = telemetry::InstallIdText();
                state.settingsNote = Tr(L"A new reporting id was created.");
            } else {
                state.settingsNote = Tr(L"The reporting id could not be reset.");
            }
        } else if (pressed == 4) {
            const ModSource next = state.settingsModSource == ModSource::CurseForge
                ? ModSource::Modrinth
                : ModSource::CurseForge;
            if (modsource::SetCurrent(next)) {
                state.settingsModSource = next;
                state.settingsCurseForgeKeyHint = modsource::CurseForgeKeyHint();
                if (next == ModSource::CurseForge && state.settingsCurseForgeKeyHint.empty()) {
                    state.settingsNote = Tr(L"CurseForge selected, but no API key is set. Add one in Remote Files.");
                } else {
                    state.settingsNote = TrFormat(L"Mods will come from {0}.", { modsource::DisplayName(next) });
                }
            } else {
                state.settingsNote = Tr(L"The mod source could not be changed.");
            }
        } else if (pressed == 5) {
            const bool turnOn = !state.settingsUpdateCheckOn;
            if (!updates::HasReleaseChannel()) {
                state.settingsNote = Tr(L"This build is not published anywhere, so there is nothing to check.");
            } else if (updates::SetEnabled(turnOn)) {
                state.settingsUpdateCheckOn = turnOn;
                if (turnOn) {
                    updates::StartCheckAsync();
                    state.settingsNote = Tr(L"The launcher will check for new versions when it opens.");
                } else {
                    state.settingsNote = Tr(L"The launcher will not look for new versions.");
                }
            } else {
                state.settingsNote = Tr(L"The update setting could not be saved.");
            }
        } else if (pressed == 6) {
            const std::vector<i18n::Language> languages = i18n::Languages();
            size_t next = 0;
            for (size_t i = 0; i < languages.size(); ++i) {
                if (languages[i].code == i18n::CurrentCode()) next = (i + 1) % languages.size();
            }
            const bool saved = i18n::SetLanguage(languages[next].code);
            state.settingsLanguageName = languages[next].name;
            state.settingsNote = languages.size() == 1
                ? Tr(L"No other languages are installed yet.")
                : (saved ? Tr(L"Language changed.") : Tr(L"The language was changed but could not be saved."));
        }

        upWas = upDown;
        downWas = downDown;
        leftWas = leftDown;
        rightWas = rightDown;
        selectWas = selectDown;
        backWas = backDown;
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }

    state.showSettings = false;
    state.showMainMenu = true;
    state.status = telemetry::ConsentGranted()
        ? Tr(L"Crash reporting is on")
        : Tr(L"Crash reporting is off");
    state.detail = L"";
    WriteLog(L"Settings page closed");
}

MainMenuAction ShowMainMenu(ICoreWindow* window, const LaunchAuthConfig& authConfig, const std::wstring& runtimeRoot) {
    AuthScreenRenderer rendererInstance;
    AuthScreenRenderer* renderer = nullptr;
    if (rendererInstance.Initialize(window)) {
        renderer = &rendererInstance;
    } else {
        WriteLog(L"Main menu renderer failed; falling through to Play");
        return MainMenuAction::Play;
    }

    AuthUiState state;
    state.title = L"Bandit Launcher";
    state.showDeviceCode = false;
    state.showMainMenu = true;
    state.status = TrFormat(L"Signed in as {0}", { a2w(authConfig.username.c_str()) });
    EnsureProfilesInitialized(runtimeRoot);
    {
        const std::wstring activeId = GetActiveProfileId(runtimeRoot);
        const Profile activeProfile = GetProfileById(runtimeRoot, activeId);
        const LaunchTarget activeTarget = ResolveProfileTarget(runtimeRoot, activeProfile);
        state.detail = TrFormat(L"Active profile: {0} - {1}", { activeProfile.name, TargetProfileText(activeTarget) });
    }

    int selected = 0;
    bool upWasDown = false;
    bool downWasDown = false;
    bool selectWasDown = false;

    ShowCrashScreen(window, renderer, state);

    float previousMouseX = -1;
    float previousMouseY = -1;
    WriteLog(L"Main menu opened");
    while (true) {
        LauncherMouse& mouse = LauncherMouseInstance();
        mouse.Update(renderer->Width(), renderer->Height());
        const bool mouseClicked = mouse.TakeClick();
        const bool mouseMoved = mouse.X() != previousMouseX || mouse.Y() != previousMouseY;
        previousMouseX = mouse.X();
        previousMouseY = mouse.Y();
        int hovered = -1;
        if (mouse.Visible()) {
            for (int i = 0; i < renderer->MainMenuItemCount(); ++i) {
                D2D1_RECT_F r;
                if (renderer->MainMenuItemRect(i, r) &&
                    mouse.X() >= r.left && mouse.X() <= r.right &&
                    mouse.Y() >= r.top && mouse.Y() <= r.bottom) {
                    hovered = i;
                }
            }
            if (hovered >= 0 && (mouseMoved || mouseClicked)) {
                selected = hovered;
            }
        }
        state.selectedMenuIndex = selected;
        state.animation = static_cast<float>((GetTickCount64() % 100000) / 1000.0);
        {
            std::wstring newest;
            state.updateAvailable = updates::NewerAvailable(newest);
            updates::Offer offer;
            if (updates::TakeOffer(offer)) {
                ShowChangelogScreen(window, renderer, state, &offer);
                continue;
            }
        }
        RenderAuth(renderer, state);

        const bool upDown = AnyVirtualKeyDown(window, {
            ABI::Windows::System::VirtualKey_Up,
            ABI::Windows::System::VirtualKey_GamepadDPadUp,
            ABI::Windows::System::VirtualKey_GamepadLeftThumbstickUp
        });
        const bool downDown = AnyVirtualKeyDown(window, {
            ABI::Windows::System::VirtualKey_Down,
            ABI::Windows::System::VirtualKey_GamepadDPadDown,
            ABI::Windows::System::VirtualKey_GamepadLeftThumbstickDown
        });
        const bool selectDown = AnyVirtualKeyDown(window, {
            ABI::Windows::System::VirtualKey_Enter,
            ABI::Windows::System::VirtualKey_Space,
            ABI::Windows::System::VirtualKey_GamepadA
        });

        if (upDown && !upWasDown) {
            selected = (selected + kMainMenuItems - 1) % kMainMenuItems;
            state.detail = L"";
        }
        if (downDown && !downWasDown) {
            selected = (selected + 1) % kMainMenuItems;
            state.detail = L"";
        }
        if ((selectDown && !selectWasDown) || (mouseClicked && hovered >= 0)) {
            if (selected == 0) {
                WriteLog(L"Main menu: Play selected");
                StopRemoteFileServer();
                return MainMenuAction::Play;
            }
            if (selected == 1) {
                WriteLog(L"Main menu: Mods selected");
                ShowModsPage(window, renderer, state, runtimeRoot);
                state.detail = Tr(L"Browse installed, popular, and latest Fabric mods.");
            } else if (selected == 2) {
                WriteLog(L"Main menu: Remote files selected");
                ShowRemoteFilesPage(window, renderer, state, runtimeRoot);
            } else if (selected == 3) {
                WriteLog(L"Main menu: Settings selected");
                ShowSettingsPage(window, renderer, state);
            } else if (selected == 4) {
                WriteLog(L"Main menu: What's new selected");
                ShowChangelogScreen(window, renderer, state, nullptr);
            } else if (selected == 5) {
                WriteLog(L"Main menu: Repair downloads selected");
                StopRemoteFileServer();
                state.status = Tr(L"Repairing downloaded files");
                state.detail = Tr(L"Downloaded Minecraft files will be rechecked");
                RenderAuth(renderer, state);
                SleepWithAuthUi(renderer, state, 350);
                return MainMenuAction::RepairDownloads;
            } else {
                WriteLog(L"Main menu: Sign out selected");
                StopRemoteFileServer();
                state.status = Tr(L"Signing out");
                state.detail = Tr(L"Clearing saved Microsoft session");
                RenderAuth(renderer, state);
                SleepWithAuthUi(renderer, state, 350);
                return MainMenuAction::SignOut;
            }
        }

        upWasDown = upDown;
        downWasDown = downDown;
        selectWasDown = selectDown;
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
}

bool ResolveLaunchAuthConfig(ICoreWindow* window, LaunchAuthConfig& out) {
    AuthScreenRenderer rendererInstance;
    AuthScreenRenderer* renderer = nullptr;
    if (rendererInstance.Initialize(window)) {
        renderer = &rendererInstance;
    }

    AuthUiState state;
    state.title = Tr(L"Signing in");
    state.showDeviceCode = false;
    state.progress = 0.12f;
    state.verificationUri = L"microsoft.com/link";
    state.status = Tr(L"Checking saved Microsoft session");
    RenderAuth(renderer, state);

    const std::string savedRefreshToken = LoadRefreshToken();
    if (!savedRefreshToken.empty()) {
        MicrosoftTokenResponse refreshed;
        std::string error;
        bool invalidGrant = false;
        if (RefreshMicrosoftToken(savedRefreshToken, refreshed, error, &invalidGrant)) {
            if (!refreshed.refreshToken.empty()) {
                SaveRefreshToken(refreshed.refreshToken);
            }
            state.status = Tr(L"Verifying Minecraft ownership");
            state.detail = Tr(L"Using saved Microsoft session");
            state.progress = 0.58f;
            RenderAuth(renderer, state);
            if (BuildMinecraftAuth(refreshed.accessToken, out, error)) {
                state.status = TrFormat(L"Signed in as {0}", { a2w(out.username.c_str()) });
                state.detail = L"";
                state.progress = 1.0f;
                RenderAuth(renderer, state);
                SleepWithAuthUi(renderer, state, 700);
                return true;
            }
        }

        WriteLogF(L"Saved auth failed: %s", a2w(error.c_str()).c_str());
        if (invalidGrant) {
            ClearRefreshToken();
        } else {
            state.status = Tr(L"Saved sign-in could not be verified");
            state.detail = a2w(error.c_str());
            state.isError = true;
            RenderAuth(renderer, state);
            return false;
        }
    }

    DeviceCodeResponse device;
    std::string error;
    if (!RequestDeviceCode(device, error)) {
        state.status = Tr(L"Microsoft sign-in failed");
        state.detail = a2w(error.c_str());
        state.isError = true;
        RenderAuth(renderer, state);
        SleepWithAuthUi(renderer, state, 5000);
        return false;
    }

    state.userCode = a2w(device.userCode.c_str());
    state.verificationUri = a2w(device.verificationUri.c_str());
    state.qr = GenerateLoginQrMatrix("https://www.microsoft.com/link?otc=" + device.userCode);
    state.title = Tr(L"Microsoft sign-in");
    state.showDeviceCode = true;
    state.progress = -1.0f;
    state.status = Tr(L"Waiting for Microsoft sign-in");
    state.detail = Tr(L"Use the account that owns Minecraft Java Edition.");
    state.secondsRemaining = device.expiresIn;
    RenderAuth(renderer, state);

    auto expiresAt = std::chrono::steady_clock::now() + std::chrono::seconds(device.expiresIn);
    int interval = device.interval;
    while (std::chrono::steady_clock::now() < expiresAt) {
        state.secondsRemaining = static_cast<int>(
            std::chrono::duration_cast<std::chrono::seconds>(expiresAt - std::chrono::steady_clock::now()).count());
        state.status = Tr(L"Waiting for Microsoft sign-in");
        state.detail = Tr(L"Use the account that owns Minecraft Java Edition.");
        SleepWithAuthUi(renderer, state, interval * 1000);

        state.status = Tr(L"Checking sign-in");
        state.detail.clear();
        RenderAuth(renderer, state);

        DevicePollResult poll = PollDeviceToken(device.deviceCode);
        if (poll.status == DevicePollStatus::Pending) {
            continue;
        }
        if (poll.status == DevicePollStatus::SlowDown) {
            interval += 5;
            continue;
        }
        if (poll.status == DevicePollStatus::Failed) {
            state.status = Tr(L"Microsoft sign-in failed");
            state.detail = a2w(poll.error.c_str());
            state.isError = true;
            RenderAuth(renderer, state);
            SleepWithAuthUi(renderer, state, 7000);
            return false;
        }

        if (!poll.token.refreshToken.empty()) {
            SaveRefreshToken(poll.token.refreshToken);
        }

        state.status = Tr(L"Verifying Minecraft ownership");
        state.detail.clear();
        RenderAuth(renderer, state);

        if (BuildMinecraftAuth(poll.token.accessToken, out, error)) {
            state.status = TrFormat(L"Signed in as {0}", { a2w(out.username.c_str()) });
            state.secondsRemaining = 0;
            state.detail.clear();
            RenderAuth(renderer, state);
            SleepWithAuthUi(renderer, state, 900);
            return true;
        }

        state.status = Tr(L"Minecraft sign-in failed");
        state.detail = a2w(error.c_str());
        state.isError = true;
        RenderAuth(renderer, state);
        SleepWithAuthUi(renderer, state, 8000);
        return false;
    }

    state.status = Tr(L"Microsoft sign-in expired");
    state.detail = Tr(L"Restart the app to request a new code.");
    state.isError = true;
    state.secondsRemaining = 0;
    RenderAuth(renderer, state);
    SleepWithAuthUi(renderer, state, 5000);
    return false;
}
