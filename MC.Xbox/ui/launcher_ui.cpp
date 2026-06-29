#include "launcher_ui.h"

#include "mods_browser.h"
#include "auth_screen.h"
#include "launcher_mouse.h"
#include "launcher_common.h"
#include "minecraft_auth.h"
#include "minecraft_launch.h"
#include "profiles.h"
#include "remote_file_server.h"
#include "qr_code.h"

#include <chrono>
#include <thread>

#include <wrl.h>

using Microsoft::WRL::ComPtr;
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
        if (mouse.Visible()) {
            renderer->SetCursor(mouse.X(), mouse.Y(), true);
        }
        renderer->Render(state);
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
    state.title = L"Preparing Minecraft";
    state.showDeviceCode = false;
    state.status = status ? status : L"Preparing runtime";
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
    state.title = L"Preparing Minecraft";
    state.showDeviceCode = false;
    state.showLaunchLog = true;
    state.isError = false;
    state.secondsRemaining = 0;
    state.status = L"Starting Minecraft";
    state.detail = loaderLabel.empty()
        ? L"Preparing launch files."
        : (L"Preparing " + loaderLabel + L" and launch files.");
    state.progress = 0.12f;
    state.launchLogText = ReadLaunchLogTailForUi(launchLogPaths);
    RenderAuth(renderer, state);

    LaunchProgressCallback progress = [&](const wchar_t* status, const wchar_t* detail, float value) {
        state.status = status ? status : L"Starting Minecraft";
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
            state.status = L"Remote file manager is running";
            state.detail = L"Open " + RemoteFileServerUrl() + L"\nPIN: " + a2w(RemoteFileServerPin().c_str());
        } else {
            state.status = L"Remote file manager is not running";
            state.detail = L"Could not start the LAN server on port 27632. Check that another copy is not already running.";
            state.isError = true;
        }

        state.animation = static_cast<float>((GetTickCount64() % 100000) / 1000.0);
        RenderAuth(renderer, state);

        const bool backDown = AnyVirtualKeyDown(window, {
            ABI::Windows::System::VirtualKey_Escape,
            ABI::Windows::System::VirtualKey_GamepadB
        });
        if (backDown && !backWasDown) {
            StopRemoteFileServer();
            state.showRemoteFiles = false;
            state.showMainMenu = true;
            state.isError = false;
            state.status = L"Remote file manager stopped";
            state.detail = L"";
            WriteLog(L"Remote files page closed");
            return;
        }

        backWasDown = backDown;
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
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
    state.status = L"Signed in as " + a2w(authConfig.username.c_str());
    EnsureProfilesInitialized(runtimeRoot);
    {
        const std::wstring activeId = GetActiveProfileId(runtimeRoot);
        const Profile activeProfile = GetProfileById(runtimeRoot, activeId);
        const LaunchTarget activeTarget = ResolveProfileTarget(runtimeRoot, activeProfile);
        state.detail = L"Active profile: " + activeProfile.name + L" - " + TargetProfileText(activeTarget);
    }

    int selected = 0;
    bool upWasDown = false;
    bool downWasDown = false;
    bool selectWasDown = false;

    WriteLog(L"Main menu opened");
    while (true) {
        LauncherMouse& mouse = LauncherMouseInstance();
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
            if (hovered >= 0) {
                selected = hovered;
            }
        }
        state.selectedMenuIndex = selected;
        state.animation = static_cast<float>((GetTickCount64() % 100000) / 1000.0);
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
            selected = (selected + 4) % 5;
            state.detail = L"";
        }
        if (downDown && !downWasDown) {
            selected = (selected + 1) % 5;
            state.detail = L"";
        }
        const bool mouseClicked = mouse.TakeClick();
        if ((selectDown && !selectWasDown) || (mouseClicked && hovered >= 0)) {
            if (selected == 0) {
                WriteLog(L"Main menu: Play selected");
                StopRemoteFileServer();
                return MainMenuAction::Play;
            }
            if (selected == 1) {
                WriteLog(L"Main menu: Mods selected");
                ShowModsPage(window, renderer, state, runtimeRoot);
                state.detail = L"Browse installed, popular, and latest Fabric mods.";
            } else if (selected == 2) {
                WriteLog(L"Main menu: Remote files selected");
                ShowRemoteFilesPage(window, renderer, state, runtimeRoot);
            } else if (selected == 3) {
                WriteLog(L"Main menu: Repair downloads selected");
                StopRemoteFileServer();
                state.status = L"Repairing downloaded files";
                state.detail = L"Downloaded Minecraft files will be rechecked";
                RenderAuth(renderer, state);
                SleepWithAuthUi(renderer, state, 350);
                return MainMenuAction::RepairDownloads;
            } else {
                WriteLog(L"Main menu: Sign out selected");
                StopRemoteFileServer();
                state.status = L"Signing out";
                state.detail = L"Clearing saved Microsoft session";
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
    state.title = L"Signing in";
    state.showDeviceCode = false;
    state.progress = 0.12f;
    state.verificationUri = L"microsoft.com/link";
    state.status = L"Checking saved Microsoft session";
    RenderAuth(renderer, state);

    const std::string savedRefreshToken = LoadRefreshToken();
    if (!savedRefreshToken.empty()) {
        MicrosoftTokenResponse refreshed;
        std::string error;
        if (RefreshMicrosoftToken(savedRefreshToken, refreshed, error)) {
            if (!refreshed.refreshToken.empty()) {
                SaveRefreshToken(refreshed.refreshToken);
            }
            state.status = L"Verifying Minecraft ownership";
            state.detail = L"Using saved Microsoft session";
            state.progress = 0.58f;
            RenderAuth(renderer, state);
            if (BuildMinecraftAuth(refreshed.accessToken, out, error)) {
                state.status = L"Signed in as " + a2w(out.username.c_str());
                state.detail = L"";
                state.progress = 1.0f;
                RenderAuth(renderer, state);
                SleepWithAuthUi(renderer, state, 700);
                return true;
            }
        }

        WriteLogF(L"Saved auth failed: %s", a2w(error.c_str()).c_str());
        ClearRefreshToken();
    }

    DeviceCodeResponse device;
    std::string error;
    if (!RequestDeviceCode(device, error)) {
        state.status = L"Microsoft sign-in failed";
        state.detail = a2w(error.c_str());
        state.isError = true;
        RenderAuth(renderer, state);
        SleepWithAuthUi(renderer, state, 5000);
        return false;
    }

    state.userCode = a2w(device.userCode.c_str());
    state.verificationUri = a2w(device.verificationUri.c_str());
    state.qr = GenerateLoginQrMatrix("https://www.microsoft.com/link?otc=" + device.userCode);
    state.title = L"Microsoft sign-in";
    state.showDeviceCode = true;
    state.progress = -1.0f;
    state.status = L"Waiting for Microsoft sign-in";
    state.detail = L"Use the account that owns Minecraft Java Edition.";
    state.secondsRemaining = device.expiresIn;
    RenderAuth(renderer, state);

    auto expiresAt = std::chrono::steady_clock::now() + std::chrono::seconds(device.expiresIn);
    int interval = device.interval;
    while (std::chrono::steady_clock::now() < expiresAt) {
        state.secondsRemaining = static_cast<int>(
            std::chrono::duration_cast<std::chrono::seconds>(expiresAt - std::chrono::steady_clock::now()).count());
        state.status = L"Waiting for Microsoft sign-in";
        state.detail = L"Use the account that owns Minecraft Java Edition.";
        SleepWithAuthUi(renderer, state, interval * 1000);

        state.status = L"Checking sign-in";
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
            state.status = L"Microsoft sign-in failed";
            state.detail = a2w(poll.error.c_str());
            state.isError = true;
            RenderAuth(renderer, state);
            SleepWithAuthUi(renderer, state, 7000);
            return false;
        }

        if (!poll.token.refreshToken.empty()) {
            SaveRefreshToken(poll.token.refreshToken);
        }

        state.status = L"Verifying Minecraft ownership";
        state.detail.clear();
        RenderAuth(renderer, state);

        if (BuildMinecraftAuth(poll.token.accessToken, out, error)) {
            state.status = L"Signed in as " + a2w(out.username.c_str());
            state.secondsRemaining = 0;
            state.detail.clear();
            RenderAuth(renderer, state);
            SleepWithAuthUi(renderer, state, 900);
            return true;
        }

        state.status = L"Minecraft sign-in failed";
        state.detail = a2w(error.c_str());
        state.isError = true;
        RenderAuth(renderer, state);
        SleepWithAuthUi(renderer, state, 8000);
        return false;
    }

    state.status = L"Microsoft sign-in expired";
    state.detail = L"Restart the app to request a new code.";
    state.isError = true;
    state.secondsRemaining = 0;
    RenderAuth(renderer, state);
    SleepWithAuthUi(renderer, state, 5000);
    return false;
}

