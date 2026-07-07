#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <wchar.h>
#include <stdio.h>
#include <string>
#include <vector>
#include <atomic>
#include <chrono>
#include <thread>

#include <roapi.h>
#include <wrl.h>
#include <wrl/wrappers/corewrappers.h>
#include <windows.applicationmodel.core.h>
#include <windows.applicationmodel.h>
#include <windows.ui.core.h>
#include <windows.foundation.h>

#include <winrt/base.h>
#include <winrt/Windows.Graphics.Display.h>

#include "runtime_config.h"
#include "launcher_common.h"
#include "crash_report.h"
#include "mod_defaults.h"
#include "minecraft_auth.h"
#include "profiles.h"
#include "auth_screen.h"
#include "launcher_ui.h"
#include "web_relay_server.h"
#include "app_globals.h"
#include "loader.h"
#include "runtime_manager.h"
#include "minecraft_launch.h"

// ICoreWindowInterop is forward-declared without a GUID, so IID_PPV_ARGS
// cannot use it directly. Redeclare it with the correct uuid here.
MIDL_INTERFACE("45D64A29-A63B-4948-AE11-979AC0A4C806")
ICoreWindowInterop : public IUnknown
{
public:
    virtual HRESULT STDMETHODCALLTYPE get_WindowHandle(HWND* hwnd) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_MessageHandled(unsigned char value) = 0;
};

using namespace Microsoft::WRL;
using namespace Microsoft::WRL::Wrappers;
using namespace ABI::Windows::ApplicationModel;
using namespace ABI::Windows::ApplicationModel::Core;
using namespace ABI::Windows::UI::Core;
using namespace ABI::Windows::Foundation;
using namespace ABI::Windows::Foundation::Collections;

static bool g_setWindowCalled = false;
static HRESULT g_windowInteropHr = E_NOTIMPL;
static HRESULT g_getWindowHandleHr = E_NOTIMPL;
static HWND g_windowHandle = NULL;
ComPtr<ICoreWindow> g_authWindow;
using CoreWindowClosedHandler = ABI::Windows::Foundation::__FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CCoreWindowEventArgs_t;
using CoreWindowVisibilityHandler = ABI::Windows::Foundation::__FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CVisibilityChangedEventArgs_t;
using CoreWindowActivatedHandler = ABI::Windows::Foundation::__FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CWindowActivatedEventArgs_t;
static ComPtr<CoreWindowClosedHandler> g_coreWindowClosedHandler;
static ComPtr<CoreWindowVisibilityHandler> g_coreWindowVisibilityHandler;
static ComPtr<CoreWindowActivatedHandler> g_coreWindowActivatedHandler;
static EventRegistrationToken g_coreWindowClosedToken = {};
static EventRegistrationToken g_coreWindowVisibilityToken = {};
static EventRegistrationToken g_coreWindowActivatedToken = {};
static bool g_coreWindowLifecycleHooksInstalled = false;
static std::atomic<bool> g_coreWindowVisibleForInput{ true };
static std::atomic<bool> g_coreWindowActivatedForInput{ true };
static std::atomic<unsigned long long> g_coreWindowInputStateChangedMs{ 0 };

static void LogLifecycleEvent(const wchar_t* reason) {
    WriteLogF(L"%s minecraftRunning=%d",
        reason ? reason : L"Lifecycle event",
        g_minecraftRunning.load() ? 1 : 0);
}

static void MarkCoreWindowInputStateChanged() {
    g_coreWindowInputStateChangedMs.store(static_cast<unsigned long long>(GetTickCount64()));
}

bool CoreWindowAcceptsInput() {
    if (!g_coreWindowVisibleForInput.load() || !g_coreWindowActivatedForInput.load()) {
        return false;
    }
    const unsigned long long changed = g_coreWindowInputStateChangedMs.load();
    return changed == 0 || (static_cast<unsigned long long>(GetTickCount64()) - changed) >= 250ULL;
}

static void RegisterLifecycleHandlers(ICoreApplication* coreApp) {
    if (!coreApp) return;

    EventRegistrationToken token = {};
    HRESULT hr = coreApp->add_Suspending(
        Callback<IEventHandler<SuspendingEventArgs*>>(
            [](IInspectable*, ISuspendingEventArgs*) -> HRESULT {
                LogLifecycleEvent(L"CoreApplication Suspending");
                return S_OK;
            }).Get(),
        &token);
    if (FAILED(hr)) {
        WriteLogF(L"CoreApplication add_Suspending failed hr=0x%08X", hr);
    }

    hr = coreApp->add_Resuming(
        Callback<IEventHandler<IInspectable*>>(
            [](IInspectable*, IInspectable*) -> HRESULT {
                WriteLog(L"CoreApplication Resuming");
                return S_OK;
            }).Get(),
        &token);
    if (FAILED(hr)) {
        WriteLogF(L"CoreApplication add_Resuming failed hr=0x%08X", hr);
    }

    ComPtr<ICoreApplication2> coreApp2;
    hr = coreApp->QueryInterface(IID_PPV_ARGS(&coreApp2));
    if (FAILED(hr)) {
        WriteLogF(L"CoreApplication2 unavailable hr=0x%08X", hr);
        return;
    }

    hr = coreApp2->add_EnteredBackground(
        Callback<IEventHandler<EnteredBackgroundEventArgs*>>(
            [](IInspectable*, IEnteredBackgroundEventArgs*) -> HRESULT {
                LogLifecycleEvent(L"CoreApplication EnteredBackground");
                return S_OK;
            }).Get(),
        &token);
    if (FAILED(hr)) {
        WriteLogF(L"CoreApplication add_EnteredBackground failed hr=0x%08X", hr);
    }

    hr = coreApp2->add_LeavingBackground(
        Callback<IEventHandler<LeavingBackgroundEventArgs*>>(
            [](IInspectable*, ILeavingBackgroundEventArgs*) -> HRESULT {
                WriteLog(L"CoreApplication LeavingBackground");
                return S_OK;
            }).Get(),
        &token);
    if (FAILED(hr)) {
        WriteLogF(L"CoreApplication add_LeavingBackground failed hr=0x%08X", hr);
    }
}

static void RegisterCoreWindowLifecycleHandlers(ICoreWindow* window) {
    if (!window || g_coreWindowLifecycleHooksInstalled) return;

    g_coreWindowClosedHandler = Callback<CoreWindowClosedHandler>(
        [](ICoreWindow*, ICoreWindowEventArgs*) -> HRESULT {
            LogLifecycleEvent(L"CoreWindow Closed");
            return S_OK;
        });

    HRESULT hr = window->add_Closed(g_coreWindowClosedHandler.Get(), &g_coreWindowClosedToken);
    if (FAILED(hr)) {
        WriteLogF(L"CoreWindow add_Closed failed hr=0x%08X", hr);
    }

    g_coreWindowVisibilityHandler = Callback<CoreWindowVisibilityHandler>(
        [](ICoreWindow*, IVisibilityChangedEventArgs* args) -> HRESULT {
            boolean visible = true;
            if (args) {
                args->get_Visible(&visible);
            }
            const bool oldVisible = g_coreWindowVisibleForInput.exchange(visible ? true : false);
            if (oldVisible != (visible ? true : false)) {
                MarkCoreWindowInputStateChanged();
            }
            WriteLogF(L"CoreWindow VisibilityChanged visible=%d minecraftRunning=%d",
                visible ? 1 : 0,
                g_minecraftRunning.load() ? 1 : 0);
            return S_OK;
        });

    hr = window->add_VisibilityChanged(g_coreWindowVisibilityHandler.Get(), &g_coreWindowVisibilityToken);
    if (FAILED(hr)) {
        WriteLogF(L"CoreWindow add_VisibilityChanged failed hr=0x%08X", hr);
    }

    g_coreWindowActivatedHandler = Callback<CoreWindowActivatedHandler>(
        [](ICoreWindow*, IWindowActivatedEventArgs* args) -> HRESULT {
            CoreWindowActivationState state = CoreWindowActivationState_CodeActivated;
            if (args) {
                args->get_WindowActivationState(&state);
            }
            const bool active = state != CoreWindowActivationState_Deactivated;
            const bool oldActive = g_coreWindowActivatedForInput.exchange(active);
            if (oldActive != active) {
                MarkCoreWindowInputStateChanged();
            }
            WriteLogF(L"CoreWindow Activated state=%d active=%d minecraftRunning=%d",
                static_cast<int>(state),
                active ? 1 : 0,
                g_minecraftRunning.load() ? 1 : 0);
            return S_OK;
        });

    hr = window->add_Activated(g_coreWindowActivatedHandler.Get(), &g_coreWindowActivatedToken);
    if (FAILED(hr)) {
        WriteLogF(L"CoreWindow add_Activated failed hr=0x%08X", hr);
    }

    g_coreWindowLifecycleHooksInstalled = true;
    WriteLog(L"CoreWindow lifecycle handlers installed");
}
static bool WriteHwndFile(const std::wstring& dir, HWND hwnd) {
    if (dir.empty() || !hwnd) return false;

    EnsureDirectoryTree(dir);

    wchar_t hpath[MAX_PATH];
    swprintf_s(hpath, L"%s\\hwnd.txt", dir.c_str());
    FILE* hf = nullptr;
    _wfopen_s(&hf, hpath, L"w");
    if (!hf) return false;

    fprintf(hf, "%llu", (unsigned long long)(uintptr_t)hwnd);
    fclose(hf);
    return true;
}
class App : public RuntimeClass<RuntimeClassFlags<WinRtClassicComMix>, IFrameworkView>
{
public:
    HRESULT STDMETHODCALLTYPE Initialize(ICoreApplicationView*) override { return S_OK; }

    HRESULT STDMETHODCALLTYPE SetWindow(ICoreWindow* window) override {
        g_setWindowCalled = true;
        g_authWindow = window;
        if (g_logDir.empty()) {
            std::wstring localDir = GetLocalStateDir();
            if (localDir.empty()) localDir = GetExecutableDir();
            g_logDir = LogsCurrentDir(localDir);
        }
        EnsureDirectoryTree(g_logDir);
        SetEnvironmentVariableW(L"MC_LOG_DIR", g_logDir.c_str());

        try {
            const double rawPixelsPerViewPixel =
                winrt::Windows::Graphics::Display::DisplayInformation::GetForCurrentView()
                    .RawPixelsPerViewPixel();
            if (rawPixelsPerViewPixel >= 0.5 && rawPixelsPerViewPixel <= 8.0) {
                wchar_t scaleText[32] = {};
                swprintf_s(scaleText, L"%.6f", rawPixelsPerViewPixel);
                SetEnvironmentVariableW(L"MC_RAW_PIXELS_PER_VIEW_PIXEL", scaleText);
                WriteLogF(L"SetWindow: rawPixelsPerViewPixel=%s", scaleText);
            }
        } catch (const winrt::hresult_error& ex) {
            WriteLogF(L"SetWindow: DisplayInformation scale unavailable hr=0x%08X msg=%s",
                static_cast<unsigned int>(ex.code()), ex.message().c_str());
        }

        // On Xbox, the B button is also treated as a UWP Back request. If it is
        // not handled here, the shell can suspend/back out of the app before the
        // game sees the controller input.
        ComPtr<ISystemNavigationManagerStatics> navStatics;
        HRESULT navHr = GetActivationFactory(
            HStringReference(RuntimeClass_Windows_UI_Core_SystemNavigationManager).Get(),
            navStatics.GetAddressOf());
        if (SUCCEEDED(navHr)) {
            ComPtr<ISystemNavigationManager> navManager;
            navHr = navStatics->GetForCurrentView(navManager.GetAddressOf());
            if (SUCCEEDED(navHr)) {
                EventRegistrationToken token = {};
                navHr = navManager->add_BackRequested(
                    Callback<IEventHandler<BackRequestedEventArgs*>>(
                        [](IInspectable*, IBackRequestedEventArgs* args) -> HRESULT {
                            if (args) {
                                args->put_Handled(TRUE);
                            }
                            WriteLogF(L"SetWindow: BackRequested handled minecraftRunning=%d",
                                g_minecraftRunning.load() ? 1 : 0);
                            return S_OK;
                        }).Get(),
                    &token);
                if (SUCCEEDED(navHr)) {
                    WriteLog(L"SetWindow: BackRequested handler installed");
                } else {
                    WriteLogF(L"SetWindow: add_BackRequested failed hr=0x%08X", navHr);
                }
            } else {
                WriteLogF(L"SetWindow: GetForCurrentView failed hr=0x%08X", navHr);
            }
        } else {
            WriteLogF(L"SetWindow: SystemNavigationManager activation failed hr=0x%08X", navHr);
        }

        ComPtr<ICoreWindowInterop> interop;
        g_windowInteropHr = window->QueryInterface(IID_PPV_ARGS(&interop));
        if (SUCCEEDED(g_windowInteropHr)) {
            g_windowHandle = NULL;
            g_getWindowHandleHr = interop->get_WindowHandle(&g_windowHandle);
            if (FAILED(g_getWindowHandleHr)) {
                WriteLogF(L"SetWindow: get_WindowHandle failed hr=0x%08X", g_getWindowHandleHr);
            } else if (g_windowHandle) {
                if (WriteHwndFile(g_logDir, g_windowHandle)) {
                    WriteLogF(L"SetWindow: HWND=0x%p written to hwnd.txt", g_windowHandle);
                } else {
                    WriteLogF(L"SetWindow: failed to open hwnd.txt err=%u", GetLastError());
                }
            } else {
                WriteLog(L"SetWindow: get_WindowHandle returned null HWND");
            }
        } else {
            WriteLogF(L"SetWindow: failed to query ICoreWindowInterop hr=0x%08X", g_windowInteropHr);
        }
        RegisterCoreWindowLifecycleHandlers(window);
        PublishCoreWindowProperty(window);
        HRESULT activateHr = window->Activate();
        if (FAILED(activateHr)) {
            WriteLogF(L"SetWindow: CoreWindow.Activate failed hr=0x%08X", activateHr);
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Load(HSTRING) override { return S_OK; }

    HRESULT STDMETHODCALLTYPE Run() override
    {
        const std::wstring packageDir = GetExecutableDir();
        std::wstring exeDir = GetLocalStateDir();
        if (exeDir.empty()) {
            exeDir = packageDir;
        }

        g_logDir = LogsCurrentDir(exeDir);
        EnsureDirectoryTree(g_logDir);
        ArchiveCurrentLogsToPrevious(exeDir);
        EnsureDirectoryTree(g_logDir);
        SetCurrentDirectoryW(exeDir.c_str());
        SetEnvironmentVariableW(L"MC_RUNTIME_DIR", exeDir.c_str());
        SetEnvironmentVariableW(L"MC_LOG_DIR", g_logDir.c_str());
        const std::wstring graphicsRuntime = DetectGraphicsRuntimeName();
        SetEnvironmentVariableW(L"MC_GRAPHICS_RUNTIME", graphicsRuntime.c_str());
        SetEnvironmentVariableW(L"mesa_glthread", L"true");
        const std::wstring mesaCacheDir = exeDir + L"\\mesa_cache";
        if (EnsureDirectoryTree(mesaCacheDir)) {
            // UWP has no XDG_CACHE_HOME or HOME so mesa disables its shader disk cache unless the dir is set explicitly
            SetEnvironmentVariableW(L"MESA_SHADER_CACHE_DIR", mesaCacheDir.c_str());
            SetEnvironmentVariableW(L"MESA_SHADER_CACHE_MAX_SIZE", L"1G");
            WriteLogF(L"mesa shader cache dir=%s", mesaCacheDir.c_str());
        }
        WriteLog(L"mesa_glthread enabled");

        wchar_t lp[MAX_PATH];
        swprintf_s(lp, L"%s\\mc_launch.log", g_logDir.c_str());
        ArchivePreviousCrashIfNeeded(exeDir);
        FILE* clf = nullptr;
        _wfopen_s(&clf, lp, L"w");
        if (clf) fclose(clf);

        WriteLog(L"=== MC.App Run() started ===");
        WriteLogF(L"graphicsRuntime=%s", graphicsRuntime.c_str());
        WriteLogF(L"SetWindow called=%d", g_setWindowCalled ? 1 : 0);
        WriteLogF(L"SetWindow QueryInterface hr=0x%08X", g_windowInteropHr);
        WriteLogF(L"SetWindow get_WindowHandle hr=0x%08X", g_getWindowHandleHr);
        WriteLogF(L"Stored HWND=0x%p", g_windowHandle);
        wchar_t cwd[MAX_PATH] = {};
        GetCurrentDirectoryW(MAX_PATH, cwd);
        WriteLogF(L"cwd=%s", cwd);
        if (g_windowHandle) {
            if (WriteHwndFile(g_logDir, g_windowHandle)) {
                WriteLog(L"Run: rewrote hwnd.txt from stored HWND");
            } else {
                WriteLogF(L"Run: failed to rewrite hwnd.txt err=%u", GetLastError());
            }
        }

        LaunchAuthConfig authConfig;
        bool authConfigReady = false;
        for (;;) {
        bool repairDownloads = false;
        while (true) {
            if (!authConfigReady && !ResolveLaunchAuthConfig(g_authWindow.Get(), authConfig)) {
                WriteLog(L"Dynamic authentication failed");
                return E_FAIL;
            }
            authConfigReady = true;

            const MainMenuAction menuAction = ShowMainMenu(g_authWindow.Get(), authConfig, exeDir);
            if (menuAction == MainMenuAction::Play) {
                break;
            }
            if (menuAction == MainMenuAction::RepairDownloads) {
                repairDownloads = true;
                break;
            }

            ClearRefreshToken();
            authConfig = LaunchAuthConfig{};
            authConfigReady = false;
            WriteLog(L"Saved Microsoft refresh token cleared by sign out");
        }

        EnsureProfilesInitialized(exeDir);
        const Profile selectedProfile = GetProfileById(exeDir, GetActiveProfileId(exeDir));
        const LaunchTarget selectedTarget = ResolveProfileTarget(exeDir, selectedProfile);
        WriteLogF(L"Selected profile id=%s name=%s target=%s minecraft=%s loader=%s loaderVersion=%s",
            selectedProfile.id.c_str(),
            selectedProfile.name.c_str(),
            selectedTarget.targetId.c_str(),
            selectedTarget.minecraftVersion.c_str(),
            selectedTarget.loader.c_str(),
            selectedTarget.loaderVersion.c_str());
        const MinecraftVersionInfo selectedInfo = ResolveVersionInfo(packageDir, exeDir, selectedTarget);
        if (!selectedInfo.supported) {
            WriteLogF(L"Unsupported launch target: %s manifest=%s assetIndex=%s launchVersion=%s mainClass=%s loaderJar=%s bundledMods=%s",
                selectedTarget.targetId.c_str(),
                selectedInfo.manifestPath.empty() ? L"(none)" : selectedInfo.manifestPath.c_str(),
                selectedInfo.assetIndex.empty() ? L"(none)" : selectedInfo.assetIndex.c_str(),
                selectedInfo.launchVersion.empty() ? L"(none)" : selectedInfo.launchVersion.c_str(),
                selectedInfo.mainClass.empty() ? L"(none)" : selectedInfo.mainClass.c_str(),
                selectedInfo.loaderJar.empty() ? L"(none)" : selectedInfo.loaderJar.c_str(),
                selectedInfo.bundledModsDir.empty() ? L"(none)" : selectedInfo.bundledModsDir.c_str());
            std::wstring unsupportedDetail = TargetProfileText(selectedTarget) + L" is in the catalog, but ";
            if (selectedInfo.manifestPath.empty()) {
                unsupportedDetail += L"its download manifest is missing from the installed package. Rebuild/reinstall the launcher with per-version manifests enabled.";
            } else if (selectedInfo.assetIndex.empty() || selectedInfo.launchVersion.empty() || selectedInfo.mainClass.empty()) {
                unsupportedDetail += L"its download manifest is incomplete. Reinstall the latest launcher build.";
            } else {
                unsupportedDetail += L"its launch provider is not available in this build yet.";
            }
            AuthScreenRenderer unsupportedRendererInstance;
            AuthScreenRenderer* unsupportedRenderer = nullptr;
            if (unsupportedRendererInstance.Initialize(g_authWindow.Get())) {
                unsupportedRenderer = &unsupportedRendererInstance;
            }
            AuthUiState unsupportedState;
            RenderPreparationProgress(
                unsupportedRenderer,
                unsupportedState,
                L"Unsupported launch target",
                unsupportedDetail.c_str(),
                1.0f);
            SleepWithAuthUi(unsupportedRenderer, unsupportedState, 8000);
            continue;
        }

        if (exeDir != packageDir && !IsLocalRuntimeSeedCurrent(packageDir, exeDir)) {
            AuthScreenRenderer prepRendererInstance;
            AuthScreenRenderer* prepRenderer = nullptr;
            if (prepRendererInstance.Initialize(g_authWindow.Get())) {
                prepRenderer = &prepRendererInstance;
            }

            AuthUiState prepState;
            RenderPreparationProgress(
                prepRenderer,
                prepState,
                L"Preparing local runtime",
                L"Copying packaged files into writable app storage",
                0.04f);

            SeedLocalRuntime(packageDir, exeDir,
                [&](const wchar_t* status, const wchar_t* detail, float progress) {
                    RenderPreparationProgress(prepRenderer, prepState, status, detail, progress);
                });

            SleepWithAuthUi(prepRenderer, prepState, 350);
        } else if (exeDir != packageDir) {
            WriteLog(L"LocalState runtime seed is current; skipping copy");
        }

        EnsureProfilesInitialized(exeDir);
        const LaunchTarget launchTarget = ResolveProfileTarget(exeDir, GetProfileById(exeDir, GetActiveProfileId(exeDir)));
        const MinecraftVersionInfo versionInfo = ResolveVersionInfo(packageDir, exeDir, launchTarget);
        WriteLogF(L"Launch target=%s manifest=%s assetIndex=%s launchVersion=%s mainClass=%s loaderJar=%s bundledMods=%s supported=%d",
            versionInfo.targetId.c_str(),
            versionInfo.manifestPath.empty() ? L"(none)" : versionInfo.manifestPath.c_str(),
            versionInfo.assetIndex.empty() ? L"(none)" : versionInfo.assetIndex.c_str(),
            versionInfo.launchVersion.empty() ? L"(none)" : versionInfo.launchVersion.c_str(),
            versionInfo.mainClass.empty() ? L"(none)" : versionInfo.mainClass.c_str(),
            versionInfo.loaderJar.empty() ? L"(none)" : versionInfo.loaderJar.c_str(),
            versionInfo.bundledModsDir.empty() ? L"(none)" : versionInfo.bundledModsDir.c_str(),
            versionInfo.supported ? 1 : 0);

        {
            AuthScreenRenderer downloadRendererInstance;
            AuthScreenRenderer* downloadRenderer = nullptr;
            if (downloadRendererInstance.Initialize(g_authWindow.Get())) {
                downloadRenderer = &downloadRendererInstance;
            }

            AuthUiState downloadState;
            const std::wstring manifestPath = versionInfo.manifestPath.empty()
                ? (packageDir + L"\\download_manifest.tsv")
                : versionInfo.manifestPath;
            RenderPreparationProgress(
                downloadRenderer,
                downloadState,
                L"Checking installed files",
                L"Validating Minecraft downloads",
                0.0f);

            int downloadAttempt = 0;
            for (;;) {
                ++downloadAttempt;
                DownloadOptions downloadOptions;
                downloadOptions.forceRepair = repairDownloads && downloadAttempt == 1;
                downloadOptions.workerCount = 6;

                if (EnsureRuntimeDownloads(
                    manifestPath,
                    exeDir,
                    versionInfo.targetId,
                    [&](const wchar_t* status, const wchar_t* detail, float progress) {
                        RenderPreparationProgress(downloadRenderer, downloadState, status, detail, progress);
                    },
                    downloadOptions)) {
                    break;
                }

                WriteLogF(L"Runtime download/bootstrap failed attempt=%d; retrying", downloadAttempt);
                RenderPreparationProgress(
                    downloadRenderer,
                    downloadState,
                    L"Download failed",
                    L"Could not prepare Minecraft files. Retrying in 10 seconds",
                    1.0f);
                SleepWithAuthUi(downloadRenderer, downloadState, 10000);
                RenderPreparationProgress(
                    downloadRenderer,
                    downloadState,
                    L"Retrying download",
                    L"Checking Minecraft files again",
                    0.0f);
            }

            SleepWithAuthUi(downloadRenderer, downloadState, 250);
        }

        const JavaRuntimeInfo javaRuntime = ResolveJavaRuntimeInfo(packageDir, exeDir, versionInfo.javaRuntime);
        const std::wstring jreDir = javaRuntime.selectedDir;
        EnsureProfilesInitialized(exeDir);
        const std::wstring activeProfile = GetActiveProfileId(exeDir);
        const Profile activeProfileInfo = GetProfileById(exeDir, activeProfile);
        EnsureProfileGameDataInitialized(exeDir, activeProfile);
        const std::wstring sharedGameDir = exeDir + L"\\game";
        const std::wstring gameDir = ProfileGameDir(exeDir, activeProfile);
        const std::wstring javaExe = jreDir + L"\\bin\\java.exe";
        const std::wstring assetsDir = exeDir + L"\\assets";
        const std::wstring localNativesDir = exeDir + L"\\natives";
        const std::wstring packageNativesDir = packageDir + L"\\natives";
        const std::wstring effManifestPath = versionInfo.manifestPath.empty()
            ? (packageDir + L"\\download_manifest.tsv")
            : versionInfo.manifestPath;
        std::wstring targetNativesDir;
        std::wstring nativesDir;
        if (PrepareTargetNativeDir(effManifestPath, exeDir, packageDir, versionInfo.targetId, targetNativesDir)) {
            nativesDir = targetNativesDir;
        } else {
            nativesDir =
                GetFileAttributesW((packageNativesDir + L"\\lwjgl.dll").c_str()) != INVALID_FILE_ATTRIBUTES &&
                GetFileAttributesW((packageNativesDir + L"\\glfw.dll").c_str()) != INVALID_FILE_ATTRIBUTES
                    ? packageNativesDir
                    : localNativesDir;
            WriteLogF(L"Falling back to default natives for target=%s", versionInfo.targetId.c_str());
        }
        const std::wstring minecraftVersion = versionInfo.minecraftVersion;
        const std::wstring packageRuntimeDir = packageDir + L"\\runtime";
        const std::wstring packagedLibrariesDir = packageRuntimeDir + L"\\libraries";
        const std::wstring userModsDir = ProfileModsDir(exeDir, activeProfile);
        const std::wstring bundledModsDir = PrepareEffectiveBundledModsDir(
            exeDir,
            versionInfo.targetId,
            versionInfo.bundledModsDir,
            activeProfileInfo.controllerModEnabled);
        const std::wstring clientJar = sharedGameDir + L"\\versions\\" + minecraftVersion + L"\\" + minecraftVersion + L".jar";
        const std::wstring argsPath = g_logDir + L"\\java_args.txt";
        const std::wstring javaLog = g_logDir + L"\\java_output.log";

        WriteLogF(L"exeDir: %s", exeDir.c_str());
        WriteLogF(L"target java runtime: requested=%s resolved=%s packageRelative=%s javaBasePatch=%s zipfsPatch=%s",
            versionInfo.javaRuntime.c_str(),
            javaRuntime.runtimeId.c_str(),
            javaRuntime.packageRelativeDir.c_str(),
            javaRuntime.javaBasePatchName.c_str(),
            javaRuntime.zipfsPatchName.c_str());
        WriteLogF(L"jreDir: %s", jreDir.c_str());
        WriteLogF(L"jre release stamp: %s", FileStamp(jreDir + L"\\release").c_str());
        WriteLogF(L"local target jre release stamp: %s", FileStamp(javaRuntime.localDir + L"\\release").c_str());
        WriteLogF(L"package target jre release stamp: %s", FileStamp(javaRuntime.packageDir + L"\\release").c_str());
        WriteLogF(L"nativesDir: %s", nativesDir.c_str());
        WriteLogF(L"packagedLibrariesDir: %s", packagedLibrariesDir.c_str());
        WriteLogF(L"sharedGameDir: %s", sharedGameDir.c_str());
        WriteLogF(L"profileGameDir: %s", gameDir.c_str());
        WriteLogF(L"downloadedLibrariesDir: %s", (sharedGameDir + L"\\libraries").c_str());
        WriteLogF(L"profile controller mod enabled=%d", activeProfileInfo.controllerModEnabled ? 1 : 0);
        WriteLogF(L"bundledModsDir: %s", bundledModsDir.c_str());
        WriteLogF(L"userModsDir: %s", userModsDir.c_str());
        if (!activeProfileInfo.controllerModEnabled) {
            const int controllerRemoved = DeleteBanditControllerModsFromDir(userModsDir);
            if (controllerRemoved > 0) {
                WriteLogF(L"Removed %d Bandit controller mod(s) from active profile before launch", controllerRemoved);
            }
        } else {
            const int controlifyRemoved = DeleteControlifyModsFromDir(userModsDir);
            if (controlifyRemoved > 0) {
                WriteLogF(L"Removed %d Controlify mod(s) from active profile before launch", controlifyRemoved);
            }
        }
        const int blockedRemoved = PurgeBlockedModsFromDir(exeDir, userModsDir);
        if (blockedRemoved > 0) {
            WriteLogF(L"Removed %d blocked mod(s) from active profile before launch", blockedRemoved);
        }
        WriteLogF(L"java.exe  exists=%d", GetFileAttributesW(javaExe.c_str()) != INVALID_FILE_ATTRIBUTES);
        WriteLogF(L"gameDir   exists=%d", GetFileAttributesW(gameDir.c_str()) != INVALID_FILE_ATTRIBUTES);
        WriteLogF(L"clientJar exists=%d", GetFileAttributesW(clientJar.c_str()) != INVALID_FILE_ATTRIBUTES);
        LoaderPreLaunchContext preLaunchCtx;
        preLaunchCtx.exeDir = exeDir;
        preLaunchCtx.packageDir = packageDir;
        preLaunchCtx.sharedGameDir = sharedGameDir;
        preLaunchCtx.gameDir = gameDir;
        preLaunchCtx.minecraftVersion = minecraftVersion;
        preLaunchCtx.packagedLibrariesDir = packagedLibrariesDir;
        preLaunchCtx.versionInfo = versionInfo;
        LoaderBeforeLaunch(preLaunchCtx);

        const bool legacyOpenGlContext = CompareVersionNumbers(w2a(minecraftVersion), "1.17") < 0;
        SetEnvironmentVariableW(L"MC_LEGACY_OPENGL_CONTEXT", legacyOpenGlContext ? L"1" : L"0");
        WriteLogF(L"legacy OpenGL compatibility context=%d", legacyOpenGlContext ? 1 : 0);

        std::vector<std::wstring> jars;
        if (!versionInfo.loaderJar.empty()) {
            jars.push_back(versionInfo.loaderJar);
        } else {
            LoaderCollectExtraClasspathJars(preLaunchCtx, jars);
        }
        CollectManifestLibraryJars(effManifestPath, exeDir, packageDir, jars);
        jars.push_back(clientJar);
        WriteLogF(L"JAR count: %zu", jars.size());

        std::wstring cp;
        for (size_t i = 0; i < jars.size(); i++) {
            if (i > 0) cp += L";";
            cp += fwd(jars[i]);
        }
        WriteLog(L"Launching embedded JVM");
        const std::vector<std::wstring> launchLogPaths = {
            g_logDir + L"\\mc_launch.log",
            g_logDir + L"\\java_output.log",
            g_logDir + L"\\stderr_stream.log"
        };
        AuthScreenRenderer launchRendererInstance;
        AuthScreenRenderer* launchRenderer = nullptr;
        // mesa d3d12 needs exclusive CoreWindow swapchain ownership; holding a
        // launch-screen D3D11 swapchain during the run trips wglCreateContext err=183
        (void)launchRendererInstance;
        AuthUiState launchState;
        DeleteFileW((g_logDir + L"\\glfw_uwp.log").c_str());

        std::wstring loaderLabel = L"Minecraft";
        if (versionInfo.loader == L"fabric") {
            loaderLabel = L"Fabric";
        } else if (versionInfo.loader == L"neoforge") {
            loaderLabel = L"NeoForge";
        } else if (versionInfo.loader == L"forge") {
            loaderLabel = L"Forge";
        }

        g_minecraftRunning.store(true);
        const std::wstring effLaunchVersion = versionInfo.launchVersion.empty() ? a2w(kFabricLaunchVersion) : versionInfo.launchVersion;
        const std::wstring effAssetIndex = versionInfo.assetIndex.empty() ? a2w(kMinecraftAssetIndex) : versionInfo.assetIndex;
        const bool launched = RunLaunchTaskWithLiveUi(
            launchRenderer,
            launchState,
            launchLogPaths,
            loaderLabel,
            [&](LaunchProgressCallback progress) {
                return RunEmbeddedMinecraft(
                    exeDir,
                    packageDir,
                    jreDir,
                    javaRuntime.packageRelativeDir,
                    javaRuntime.javaBasePatchName,
                    javaRuntime.zipfsPatchName,
                    gameDir,
                    assetsDir,
                    nativesDir,
                    bundledModsDir,
                    userModsDir,
                    clientJar,
                    javaLog,
                    argsPath,
                    cp,
                    effLaunchVersion,
                    effAssetIndex,
                    minecraftVersion,
                    versionInfo.loader,
                    versionInfo.loaderVersion,
                    versionInfo.mainClass,
                    versionInfo.extraJvmArgs,
                    versionInfo.extraGameArgs,
                    versionInfo.neoFormVersion,
                    versionInfo.neoForgeInstallToolsVersion,
                    versionInfo.neoForgeJarSplitterVersion,
                    versionInfo.neoForgeBinaryPatcherVersion,
                    versionInfo.neoForgeAutoRenamingToolVersion,
                    authConfig,
                    progress);
            });
        if (!launched) {
            g_minecraftRunning.store(false);
            WriteLog(L"Embedded JVM launch failed");
            AuthScreenRenderer failedRendererInstance;
            AuthScreenRenderer* failedRenderer = nullptr;
            if (failedRendererInstance.Initialize(g_authWindow.Get())) {
                failedRenderer = &failedRendererInstance;
            }
            AuthUiState failedState;
            RenderPreparationProgress(
                failedRenderer,
                failedState,
                L"Launch failed",
                L"Minecraft could not start. Check logs for details.",
                1.0f);
            SleepWithAuthUi(failedRenderer, failedState, 6000);
            continue;
        }
        g_minecraftRunning.store(false);
        WriteLog(L"Minecraft session ended; returning to main menu");
        SetCurrentDirectoryW(exeDir.c_str());
        PublishCoreWindowProperty(g_authWindow.Get());
        if (g_authWindow) {
            g_authWindow->Activate();
        }
        ProcessAuthUiEvents();
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
        ProcessAuthUiEvents();
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Uninitialize() override { return S_OK; }
};

class AppSource : public RuntimeClass<RuntimeClassFlags<WinRtClassicComMix>, IFrameworkViewSource>
{
public:
    HRESULT STDMETHODCALLTYPE CreateView(IFrameworkView** view) override
    {
        return Make<App>().CopyTo(view);
    }
};

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    RoInitialize(RO_INIT_MULTITHREADED);

    typedef void (*MouseSupportVoidProc)();
    MouseSupportVoidProc mouseSupportShutdown = nullptr;
    HMODULE mouseSupportModule = LoadPackagedLibrary(L"mouse_support.dll", 0);
    if (mouseSupportModule) {
        MouseSupportVoidProc mouseSupportInit =
            reinterpret_cast<MouseSupportVoidProc>(GetProcAddress(mouseSupportModule, "MouseSupport_Init"));
        mouseSupportShutdown =
            reinterpret_cast<MouseSupportVoidProc>(GetProcAddress(mouseSupportModule, "MouseSupport_Shutdown"));
        if (mouseSupportInit) {
            mouseSupportInit();
            WriteLog(L"mouse_support.dll initialized at launcher startup");
        }
    } else {
        WriteLogF(L"mouse_support.dll not loaded err=%lu", GetLastError());
    }

    StartWebRelayServer();

    ComPtr<ICoreApplication> coreApp;
    GetActivationFactory(
        HStringReference(RuntimeClass_Windows_ApplicationModel_Core_CoreApplication).Get(),
        &coreApp);
    RegisterLifecycleHandlers(coreApp.Get());
    coreApp->Run(Make<AppSource>().Get());
    if (mouseSupportShutdown) {
        mouseSupportShutdown();
    }
    StopWebRelayServer();
    RoUninitialize();
    return 0;
}
