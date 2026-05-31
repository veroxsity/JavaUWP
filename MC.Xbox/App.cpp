#include <windows.h>
#include <wchar.h>
#include <stdio.h>
#include <string>
#include <vector>
#include <roapi.h>
#include <wrl.h>
#include <wrl/wrappers/corewrappers.h>
#include <windows.applicationmodel.core.h>
#include <windows.ui.core.h>
#include <windows.system.h>
#include <windows.foundation.h>
#include <windows.foundation.collections.h>
#include <windows.storage.h>
#include <jni.h>
#include <io.h>
#include <fcntl.h>
#include <share.h>
#include <errno.h>
#include <atomic>
#include <chrono>
#include <thread>
#include <fstream>
#include <algorithm>
#include <cctype>
#include <cwctype>
#include <functional>
#include <new>
#include <cmath>
#include <mutex>
#include <d2d1_1.h>
#include <dwrite.h>
#include <d3d11_1.h>
#include <dxgi1_3.h>
#include <wincodec.h>
#include <sstream>
#include <iomanip>
#include <winhttp.h>
#include <bcrypt.h>
#include <map>
#include <set>
#include <winrt/base.h>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Graphics.Display.h>
#include <winrt/Windows.Security.Credentials.h>
#include <winrt/Windows.Security.ExchangeActiveSyncProvisioning.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.Web.Http.h>
#include <winrt/Windows.Web.Http.Headers.h>
#include <winrt/Windows.UI.Core.h>
#include <winrt/Windows.UI.Text.Core.h>
#include <winrt/Windows.UI.ViewManagement.h>
#include <winrt/Windows.System.h>

#include "runtime_config.h"
#include "qr_code.h"
#include "third_party/miniz/miniz.h"

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
using namespace ABI::Windows::ApplicationModel::Core;
using namespace ABI::Windows::ApplicationModel;
using namespace ABI::Windows::Storage;
using namespace ABI::Windows::UI::Core;
using namespace ABI::Windows::Foundation;
using namespace ABI::Windows::Foundation::Collections;

static std::wstring g_logDir;
static bool g_setWindowCalled = false;
static HRESULT g_windowInteropHr = E_NOTIMPL;
static HRESULT g_getWindowHandleHr = E_NOTIMPL;
static HWND g_windowHandle = NULL;
static ComPtr<ICoreWindow> g_authWindow;
static std::atomic<bool> g_minecraftRunning{ false };
using CoreWindowClosedHandler = ABI::Windows::Foundation::__FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CCoreWindowEventArgs_t;
using CoreWindowVisibilityHandler = ABI::Windows::Foundation::__FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CVisibilityChangedEventArgs_t;
static ComPtr<CoreWindowClosedHandler> g_coreWindowClosedHandler;
static ComPtr<CoreWindowVisibilityHandler> g_coreWindowVisibilityHandler;
static EventRegistrationToken g_coreWindowClosedToken = {};
static EventRegistrationToken g_coreWindowVisibilityToken = {};
static bool g_coreWindowLifecycleHooksInstalled = false;
static volatile LONG g_logTailerRunning = 0;
static HANDLE g_logTailerThreads[8] = {};
static int g_logTailerThreadCount = 0;
static constexpr wchar_t kEGLNativeWindowTypeProperty[] = L"EGLNativeWindowTypeProperty";
static constexpr char kMicrosoftAuthClientId[] = "c36a9fb6-4f2a-41ff-90bd-ae7cc92031eb";
static constexpr char kMicrosoftAuthScopes[] = "XboxLive.signin offline_access";
static constexpr wchar_t kRefreshTokenResource[] = L"MinecraftJavaUWP.MicrosoftRefreshToken";
static constexpr wchar_t kRefreshTokenUser[] = L"default";

typedef jint(JNICALL* JNI_CreateJavaVM_t)(JavaVM**, void**, void*);

static void WriteLog(const wchar_t* msg);
static void WriteLogF(const wchar_t* fmt, ...);
static std::string w2a(const std::wstring& w);
static std::wstring a2w(const char* utf8);

struct LogTailerConfig {
    std::wstring path;
    std::wstring label;
};

static std::wstring GetExecutableDir() {
    wchar_t buf[MAX_PATH];
    const DWORD len = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) return std::wstring();

    wchar_t* sl = wcsrchr(buf, L'\\');
    if (sl) *sl = L'\0';
    return std::wstring(buf);
}

static std::wstring HStringToWString(HSTRING value) {
    UINT32 len = 0;
    const wchar_t* raw = WindowsGetStringRawBuffer(value, &len);
    return raw ? std::wstring(raw, len) : std::wstring();
}

static std::wstring GetLocalStateDir() {
    ComPtr<IApplicationDataStatics> appDataStatics;
    HRESULT hr = GetActivationFactory(
        HStringReference(RuntimeClass_Windows_Storage_ApplicationData).Get(),
        &appDataStatics);
    if (FAILED(hr)) {
        WriteLogF(L"ApplicationData activation failed hr=0x%08X", hr);
        return std::wstring();
    }

    ComPtr<IApplicationData> appData;
    hr = appDataStatics->get_Current(appData.GetAddressOf());
    if (FAILED(hr)) {
        WriteLogF(L"ApplicationData.Current failed hr=0x%08X", hr);
        return std::wstring();
    }

    ComPtr<IStorageFolder> localFolder;
    hr = appData->get_LocalFolder(localFolder.GetAddressOf());
    if (FAILED(hr)) {
        WriteLogF(L"ApplicationData.LocalFolder failed hr=0x%08X", hr);
        return std::wstring();
    }

    ComPtr<IStorageItem> localItem;
    hr = localFolder.As(&localItem);
    if (FAILED(hr)) {
        WriteLogF(L"LocalFolder As(IStorageItem) failed hr=0x%08X", hr);
        return std::wstring();
    }

    HSTRING path = nullptr;
    hr = localItem->get_Path(&path);
    if (FAILED(hr)) {
        WriteLogF(L"LocalFolder.Path failed hr=0x%08X", hr);
        return std::wstring();
    }

    std::wstring result = HStringToWString(path);
    WindowsDeleteString(path);
    return result;
}

static bool EnsureDirectoryTree(const std::wstring& path) {
    if (path.empty()) return false;
    if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES) return true;

    std::wstring current;
    size_t start = 0;
    if (path.size() >= 2 && path[1] == L':') {
        current = path.substr(0, 2);
        start = 2;
    }

    while (start < path.size()) {
        size_t next = path.find(L'\\', start);
        std::wstring part = path.substr(
            start,
            next == std::wstring::npos ? path.size() - start : next - start);
        if (!part.empty()) {
            if (!current.empty() && current.back() != L'\\') current += L'\\';
            current += part;
            if (GetFileAttributesW(current.c_str()) == INVALID_FILE_ATTRIBUTES) {
                if (!CreateDirectoryW(current.c_str(), nullptr) &&
                    GetLastError() != ERROR_ALREADY_EXISTS) {
                    return false;
                }
            }
        }
        if (next == std::wstring::npos) break;
        start = next + 1;
    }

    return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

static std::wstring GetParentDir(const std::wstring& path) {
    const size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? std::wstring() : path.substr(0, slash);
}

using RuntimeSeedProgressCallback = std::function<void(const wchar_t*, const wchar_t*, float)>;

static std::wstring FileStamp(const std::wstring& path) {
    WIN32_FILE_ATTRIBUTE_DATA data = {};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data)) {
        return L"missing";
    }

    wchar_t stamp[96] = {};
    swprintf_s(stamp, L"%08X%08X:%08X%08X",
        data.ftLastWriteTime.dwHighDateTime,
        data.ftLastWriteTime.dwLowDateTime,
        data.nFileSizeHigh,
        data.nFileSizeLow);
    return stamp;
}

static bool ReadTextFile(const std::wstring& path, std::wstring& out) {
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"rb") != 0 || !f) return false;

    std::string bytes;
    char buffer[4096];
    while (true) {
        const size_t read = fread(buffer, 1, sizeof(buffer), f);
        if (read > 0) bytes.append(buffer, read);
        if (read < sizeof(buffer)) break;
    }
    fclose(f);

    out = a2w(bytes.c_str());
    return true;
}

static bool WriteTextFile(const std::wstring& path, const std::wstring& value) {
    EnsureDirectoryTree(GetParentDir(path));
    SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_NORMAL);
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"wb") != 0 || !f) return false;

    const std::string bytes = w2a(value);
    const bool ok = bytes.empty() || fwrite(bytes.data(), 1, bytes.size(), f) == bytes.size();
    fclose(f);
    return ok;
}

static std::wstring RuntimeSeedStamp(const std::wstring& packageDir) {
    return std::wstring(L"seedVersion=3\n") +
        L"packageDir=" + packageDir + L"\n" +
        L"exe=" + FileStamp(packageDir + L"\\MC.Xbox.exe") + L"\n" +
        L"manifest=" + FileStamp(packageDir + L"\\AppxManifest.xml") + L"\n" +
        L"downloadManifest=" + FileStamp(packageDir + L"\\download_manifest.tsv") + L"\n" +
        L"minecraft=" + std::wstring(kMinecraftVersionW) + L"\n" +
        L"jreRelease=" + FileStamp(packageDir + L"\\jre\\release") + L"\n" +
        L"jvm=" + FileStamp(packageDir + L"\\jre\\bin\\server\\jvm.dll") + L"\n" +
        L"securityPatch=" + FileStamp(packageDir + L"\\java-base-security-realpath.jar") + L"\n" +
        L"patchedFabricLoader=" + FileStamp(packageDir + L"\\runtime\\libraries\\net\\fabricmc\\fabric-loader\\" + a2w(kFabricLoaderVersion) + L"\\fabric-loader-" + a2w(kFabricLoaderVersion) + L".jar") + L"\n" +
        L"bundledMods=" + FileStamp(packageDir + L"\\runtime\\bundled-mods") + L"\n" +
        L"logConfig=" + FileStamp(packageDir + L"\\runtime\\log_configs\\client-uwp.xml") + L"\n" +
        L"nativeGlfw=" + FileStamp(packageDir + L"\\natives\\glfw.dll") + L"\n" +
        L"nativeLwjgl=" + FileStamp(packageDir + L"\\natives\\lwjgl.dll") + L"\n" +
        L"mesaOpenGl=" + FileStamp(packageDir + L"\\graphics\\mesa\\opengl32.dll") + L"\n" +
        L"xboxOneOpenGl=" + FileStamp(packageDir + L"\\graphics\\xboxone\\opengl32.dll") + L"\n";
}

static bool IsLocalRuntimeSeedCurrent(const std::wstring& packageDir, const std::wstring& localDir) {
    const std::wstring markerPath = localDir + L"\\.runtime_seed";
    std::wstring marker;
    if (!ReadTextFile(markerPath, marker)) {
        return false;
    }
    if (marker != RuntimeSeedStamp(packageDir)) {
        return false;
    }

    const bool hasGameSupport =
        GetFileAttributesW((localDir + L"\\game\\mods").c_str()) != INVALID_FILE_ATTRIBUTES &&
        GetFileAttributesW((localDir + L"\\game\\log_configs\\client-uwp.xml").c_str()) != INVALID_FILE_ATTRIBUTES;
    const bool hasNatives = GetFileAttributesW((localDir + L"\\natives").c_str()) != INVALID_FILE_ATTRIBUTES;
    const bool hasGraphics = GetFileAttributesW((localDir + L"\\graphics").c_str()) != INVALID_FILE_ATTRIBUTES ||
        GetFileAttributesW((localDir + L"\\natives\\opengl32.dll").c_str()) != INVALID_FILE_ATTRIBUTES;
    const bool hasJre =
        GetFileAttributesW((localDir + L"\\jre\\bin\\server\\jvm.dll").c_str()) != INVALID_FILE_ATTRIBUTES &&
        GetFileAttributesW((localDir + L"\\jre\\conf\\security\\java.security").c_str()) != INVALID_FILE_ATTRIBUTES;
    const bool hasJavaSecurityPatch =
        GetFileAttributesW((localDir + L"\\java-base-security-realpath.jar").c_str()) != INVALID_FILE_ATTRIBUTES;
    return hasGameSupport && hasNatives && hasGraphics && hasJre && hasJavaSecurityPatch;
}

static void MarkLocalRuntimeSeedCurrent(const std::wstring& packageDir, const std::wstring& localDir) {
    const std::wstring markerPath = localDir + L"\\.runtime_seed";
    if (WriteTextFile(markerPath, RuntimeSeedStamp(packageDir))) {
        WriteLog(L"LocalState runtime seed marker written");
    } else {
        WriteLogF(L"Failed to write LocalState runtime seed marker err=%u", GetLastError());
    }
}

static void CopyFileIfNeeded(const std::wstring& src, const std::wstring& dst) {
    if (GetFileAttributesW(src.c_str()) == INVALID_FILE_ATTRIBUTES) return;

    WIN32_FILE_ATTRIBUTE_DATA srcData = {};
    WIN32_FILE_ATTRIBUTE_DATA dstData = {};
    const bool hasDst = GetFileAttributesExW(dst.c_str(), GetFileExInfoStandard, &dstData);
    if (hasDst && GetFileAttributesExW(src.c_str(), GetFileExInfoStandard, &srcData)) {
        if (srcData.nFileSizeHigh == dstData.nFileSizeHigh &&
            srcData.nFileSizeLow == dstData.nFileSizeLow &&
            CompareFileTime(&srcData.ftLastWriteTime, &dstData.ftLastWriteTime) <= 0) {
            return;
        }
    }

    EnsureDirectoryTree(GetParentDir(dst));
    CopyFileW(src.c_str(), dst.c_str(), FALSE);
}

static void CopyDirectoryContentsIfNeeded(const std::wstring& src, const std::wstring& dst) {
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((src + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;

    EnsureDirectoryTree(dst);
    do {
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
        const std::wstring srcPath = src + L"\\" + fd.cFileName;
        const std::wstring dstPath = dst + L"\\" + fd.cFileName;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            CopyDirectoryContentsIfNeeded(srcPath, dstPath);
        } else {
            CopyFileIfNeeded(srcPath, dstPath);
        }
    } while (FindNextFileW(h, &fd));

    FindClose(h);
}

static bool SeedLocalRuntime(
    const std::wstring& packageDir,
    const std::wstring& localDir,
    const RuntimeSeedProgressCallback& progress = RuntimeSeedProgressCallback()) {
    if (packageDir.empty() || localDir.empty()) return false;

    EnsureDirectoryTree(localDir);
    WriteLogF(L"Seeding LocalState runtime from %s", packageDir.c_str());
    if (progress) {
        progress(L"Copying launcher files", L"Preparing mods and log configuration", 0.12f);
    }
    CopyDirectoryContentsIfNeeded(packageDir + L"\\runtime\\bundled-mods", localDir + L"\\game\\mods");
    CopyDirectoryContentsIfNeeded(packageDir + L"\\runtime\\log_configs", localDir + L"\\game\\log_configs");
    if (progress) {
        progress(L"Copying Java runtime", L"Preparing JVM files", 0.52f);
    }
    CopyDirectoryContentsIfNeeded(packageDir + L"\\jre", localDir + L"\\jre");
    std::wstring xboxSecurityProperties;
    if (ReadTextFile(packageDir + L"\\xbox_security.properties", xboxSecurityProperties)) {
        const std::wstring localSecurityDir = localDir + L"\\jre\\conf\\security";
        if (!WriteTextFile(localSecurityDir + L"\\java.security", xboxSecurityProperties)) {
            WriteLogF(L"Failed to rewrite LocalState java.security err=%u", GetLastError());
        }
        if (!WriteTextFile(localSecurityDir + L"\\xbox.properties", xboxSecurityProperties)) {
            WriteLogF(L"Failed to write LocalState xbox.properties err=%u", GetLastError());
        }
    } else {
        WriteLogF(L"Failed to read packaged xbox_security.properties err=%u", GetLastError());
    }
    if (progress) {
        progress(L"Copying native libraries", L"Preparing graphics and input runtime", 0.80f);
    }
    CopyDirectoryContentsIfNeeded(packageDir + L"\\natives", localDir + L"\\natives");
    CopyDirectoryContentsIfNeeded(packageDir + L"\\graphics", localDir + L"\\graphics");
    if (progress) {
        progress(L"Finalizing runtime", L"Writing launch configuration", 0.96f);
    }
    CopyFileIfNeeded(packageDir + L"\\xbox_security.properties", localDir + L"\\xbox_security.properties");
    CopyFileIfNeeded(packageDir + L"\\java-base-security-realpath.jar", localDir + L"\\java-base-security-realpath.jar");
    if (progress) {
        progress(L"Runtime ready", L"Starting Minecraft", 1.0f);
    }
    MarkLocalRuntimeSeedCurrent(packageDir, localDir);
    WriteLog(L"LocalState runtime seed complete");
    return true;
}

static void WriteLog(const wchar_t* msg) {
    if (g_logDir.empty()) {
        g_logDir = GetExecutableDir();
    }
    if (g_logDir.empty()) return;

    EnsureDirectoryTree(g_logDir);

    wchar_t path[MAX_PATH];
    swprintf_s(path, L"%s\\mc_launch.log", g_logDir.c_str());
    FILE* f = nullptr;
    _wfopen_s(&f, path, L"a");
    if (f) {
        SYSTEMTIME st;
        GetLocalTime(&st);
        fwprintf(f, L"[%02d:%02d:%02d.%03d] %s\n",
            st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, msg);
        fclose(f);
    }
}

static void WriteLogF(const wchar_t* fmt, ...) {
    va_list args;
    va_start(args, fmt);

    va_list sizeArgs;
    va_copy(sizeArgs, args);
    const int needed = _vscwprintf(fmt, sizeArgs);
    va_end(sizeArgs);

    if (needed <= 0) {
        va_end(args);
        WriteLog(L"WriteLogF: failed to format message");
        return;
    }

    std::vector<wchar_t> buf(static_cast<size_t>(needed) + 1);
    vswprintf_s(buf.data(), buf.size(), fmt, args);
    va_end(args);
    WriteLog(buf.data());
}

static void LogLifecycleEvent(const wchar_t* reason) {
    WriteLogF(L"%s minecraftRunning=%d",
        reason ? reason : L"Lifecycle event",
        g_minecraftRunning.load() ? 1 : 0);
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
            WriteLogF(L"CoreWindow VisibilityChanged visible=%d minecraftRunning=%d",
                visible ? 1 : 0,
                g_minecraftRunning.load() ? 1 : 0);
            return S_OK;
        });

    hr = window->add_VisibilityChanged(g_coreWindowVisibilityHandler.Get(), &g_coreWindowVisibilityToken);
    if (FAILED(hr)) {
        WriteLogF(L"CoreWindow add_VisibilityChanged failed hr=0x%08X", hr);
    }

    g_coreWindowLifecycleHooksInstalled = true;
    WriteLog(L"CoreWindow lifecycle handlers installed");
}

static void LogTextFileTail(const std::wstring& path, const wchar_t* label, DWORD maxBytes = 16384) {
    int fd = -1;
    errno_t openErr = _wsopen_s(&fd, path.c_str(), _O_RDONLY | _O_BINARY, _SH_DENYNO, _S_IREAD);
    if (openErr != 0 || fd < 0) {
        WriteLogF(L"%s unavailable: %s errno=%d winerr=%u", label ? label : L"log file", path.c_str(), openErr, GetLastError());
        return;
    }

    const __int64 size = _lseeki64(fd, 0, SEEK_END);
    if (size <= 0) {
        _close(fd);
        WriteLogF(L"%s empty: %s", label ? label : L"log file", path.c_str());
        return;
    }

    const DWORD bytesToRead = static_cast<DWORD>(size < maxBytes ? size : maxBytes);
    _lseeki64(fd, size - bytesToRead, SEEK_SET);

    std::string data(bytesToRead, '\0');
    const int bytesRead = _read(fd, data.data(), bytesToRead);
    _close(fd);
    if (bytesRead <= 0) {
        WriteLogF(L"%s read failed: %s errno=%d", label ? label : L"log file", path.c_str(), errno);
        return;
    }

    data.resize(static_cast<size_t>(bytesRead));
    for (char& ch : data) {
        if (ch == '\0') ch = ' ';
    }

    const int wideLen = MultiByteToWideChar(CP_UTF8, 0, data.c_str(), static_cast<int>(data.size()), nullptr, 0);
    std::wstring wide;
    if (wideLen > 0) {
        wide.resize(wideLen);
        MultiByteToWideChar(CP_UTF8, 0, data.c_str(), static_cast<int>(data.size()), wide.data(), wideLen);
    } else {
        wide = a2w(data.c_str());
    }

    WriteLogF(L"%s tail (%u bytes):\n%s", label ? label : L"log file", static_cast<unsigned>(bytesRead), wide.c_str());
}

static void LogUtf8Chunk(const std::wstring& label, const char* data, DWORD length) {
    if (!data || length == 0) return;

    UINT codePage = CP_UTF8;
    int wideLen = MultiByteToWideChar(codePage, 0, data, static_cast<int>(length), nullptr, 0);
    if (wideLen <= 0) {
        codePage = CP_ACP;
        wideLen = MultiByteToWideChar(codePage, 0, data, static_cast<int>(length), nullptr, 0);
    }
    if (wideLen <= 0) return;

    std::wstring wide(static_cast<size_t>(wideLen), L'\0');
    MultiByteToWideChar(codePage, 0, data, static_cast<int>(length), wide.data(), wideLen);
    for (wchar_t& ch : wide) {
        if (ch == L'\0') ch = L' ';
    }

    while (!wide.empty() && (wide.back() == L'\r' || wide.back() == L'\n')) {
        wide.pop_back();
    }
    if (!wide.empty()) {
        WriteLogF(L"%s:\n%s", label.empty() ? L"log" : label.c_str(), wide.c_str());
    }
}

static DWORD WINAPI LogTailerThreadProc(LPVOID param) {
    std::wstring path;
    std::wstring label;
    if (param) {
        LogTailerConfig* config = static_cast<LogTailerConfig*>(param);
        path = config->path;
        label = config->label;
        delete config;
    }

    LARGE_INTEGER offset = {};
    char buffer[4096];
    bool attached = false;

    while (InterlockedCompareExchange(&g_logTailerRunning, 1, 1) == 1) {
        HANDLE file = CreateFile2(
            path.c_str(),
            GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            OPEN_EXISTING,
            nullptr);

        if (file != INVALID_HANDLE_VALUE) {
            if (!attached) {
                WriteLogF(L"log tailer attached: %s -> %s", label.c_str(), path.c_str());
                attached = true;
            }

            LARGE_INTEGER fileSize = {};
            if (GetFileSizeEx(file, &fileSize)) {
                if (offset.QuadPart > fileSize.QuadPart) {
                    offset.QuadPart = 0;
                }

                SetFilePointerEx(file, offset, nullptr, FILE_BEGIN);
                for (;;) {
                    DWORD bytesRead = 0;
                    if (!ReadFile(file, buffer, sizeof(buffer), &bytesRead, nullptr) || bytesRead == 0) {
                        break;
                    }
                    offset.QuadPart += bytesRead;
                    LogUtf8Chunk(label, buffer, bytesRead);
                }
            }
            CloseHandle(file);
        }

        Sleep(250);
    }

    return 0;
}

static void StartOneLogTailer(const LogTailerConfig& config) {
    if (config.path.empty() ||
        g_logTailerThreadCount >= static_cast<int>(sizeof(g_logTailerThreads) / sizeof(g_logTailerThreads[0]))) {
        return;
    }

    LogTailerConfig* threadConfig = new (std::nothrow) LogTailerConfig(config);
    if (!threadConfig) {
        WriteLogF(L"Failed to allocate log tailer config for %s", config.label.c_str());
        return;
    }

    HANDLE thread = CreateThread(nullptr, 0, LogTailerThreadProc, threadConfig, 0, nullptr);
    if (!thread) {
        delete threadConfig;
        WriteLogF(L"Failed to start log tailer for %s err=%u", config.label.c_str(), GetLastError());
        return;
    }

    g_logTailerThreads[g_logTailerThreadCount++] = thread;
}

static bool StartLogTailers(const std::vector<LogTailerConfig>& configs) {
    if (configs.empty()) return false;
    if (InterlockedCompareExchange(&g_logTailerRunning, 1, 0) != 0) return false;

    g_logTailerThreadCount = 0;
    for (const auto& config : configs) {
        StartOneLogTailer(config);
    }

    if (g_logTailerThreadCount == 0) {
        InterlockedExchange(&g_logTailerRunning, 0);
        return false;
    }

    WriteLogF(L"started %d log tailer(s)", g_logTailerThreadCount);
    return true;
}

static void StopLogTailers() {
    if (InterlockedExchange(&g_logTailerRunning, 0) == 0) return;
    for (int i = 0; i < g_logTailerThreadCount; ++i) {
        if (!g_logTailerThreads[i]) continue;
        WaitForSingleObject(g_logTailerThreads[i], 1000);
        CloseHandle(g_logTailerThreads[i]);
        g_logTailerThreads[i] = NULL;
    }
    g_logTailerThreadCount = 0;
}

struct LogTailerGuard {
    bool active;

    explicit LogTailerGuard(bool started) : active(started) {}
    ~LogTailerGuard() {
        if (active) {
            StopLogTailers();
        }
    }
};

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

static void CollectJars(const std::wstring& dir, std::vector<std::wstring>& jars) {
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
        std::wstring full = dir + L"\\" + fd.cFileName;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            CollectJars(full, jars);
        } else {
            size_t len = wcslen(fd.cFileName);
            if (len > 4 && _wcsicmp(fd.cFileName + len - 4, L".jar") == 0) {
                if (!wcsstr(fd.cFileName, L"sources") && !wcsstr(fd.cFileName, L"javadoc")) {
                    jars.push_back(full);
                }
            }
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

static std::wstring fwd(const std::wstring& s) {
    std::wstring r = s;
    for (auto& c : r) {
        if (c == L'\\') c = L'/';
    }
    return r;
}

static std::string w2a(const std::wstring& w) {
    if (w.empty()) return {};
    int sz = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string s(sz, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, &s[0], sz, nullptr, nullptr);
    if (!s.empty() && s.back() == 0) s.pop_back();
    return s;
}

static std::wstring a2w(const char* utf8) {
    if (!utf8 || !*utf8) return {};
    int sz = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, nullptr, 0);
    if (sz <= 0) return {};

    std::wstring w(sz, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8, -1, &w[0], sz);
    if (!w.empty() && w.back() == L'\0') w.pop_back();
    return w;
}

static std::wstring GetEnvVarString(const wchar_t* name) {
    const DWORD len = GetEnvironmentVariableW(name, nullptr, 0);
    if (len == 0) return std::wstring();

    std::wstring value(len, L'\0');
    if (GetEnvironmentVariableW(name, value.data(), len) == 0) return std::wstring();
    if (!value.empty() && value.back() == L'\0') value.pop_back();
    return value;
}

struct DownloadManifestEntry {
    std::wstring relativePath;
    std::wstring url;
    std::string sha1;
    unsigned long long size = 0;
};

using DownloadProgressCallback = std::function<void(const wchar_t*, const wchar_t*, float)>;

struct DownloadOptions {
    bool forceRepair = false;
    unsigned workerCount = 6;
};

static std::wstring TrimTrailingSlash(std::wstring path) {
    while (!path.empty() && (path.back() == L'\\' || path.back() == L'/')) {
        path.pop_back();
    }
    return path;
}

static std::wstring JoinRuntimeRelativePath(const std::wstring& root, std::wstring relativePath) {
    for (wchar_t& ch : relativePath) {
        if (ch == L'/') ch = L'\\';
    }

    if (relativePath.empty() ||
        relativePath[0] == L'\\' ||
        relativePath.find(L":") != std::wstring::npos) {
        return std::wstring();
    }

    size_t start = 0;
    while (start <= relativePath.size()) {
        const size_t slash = relativePath.find(L'\\', start);
        const std::wstring segment = relativePath.substr(
            start,
            slash == std::wstring::npos ? std::wstring::npos : slash - start);
        if (segment.empty() || segment == L"." || segment == L"..") {
            return std::wstring();
        }
        if (slash == std::wstring::npos) break;
        start = slash + 1;
    }

    return TrimTrailingSlash(root) + L"\\" + relativePath;
}

static std::string ToLowerAscii(std::string value) {
    for (char& ch : value) {
        if (ch >= 'A' && ch <= 'Z') {
            ch = static_cast<char>((ch - 'A') + 'a');
        }
    }
    return value;
}

static std::string BytesToHex(const unsigned char* data, size_t length) {
    static const char* digits = "0123456789abcdef";
    std::string out;
    out.reserve(length * 2);
    for (size_t i = 0; i < length; ++i) {
        out.push_back(digits[(data[i] >> 4) & 0xF]);
        out.push_back(digits[data[i] & 0xF]);
    }
    return out;
}

static bool Sha1File(const std::wstring& path, std::string* outHex) {
    if (!outHex) return false;

    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"rb") != 0 || !f) {
        return false;
    }

    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    DWORD objectLength = 0;
    DWORD dataLength = 0;
    std::vector<unsigned char> hashObject;
    unsigned char hashBytes[20] = {};
    bool ok = false;

    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA1_ALGORITHM, nullptr, 0) != 0) goto done;
    if (BCryptGetProperty(alg, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&objectLength),
        sizeof(objectLength), &dataLength, 0) != 0 || objectLength == 0) goto done;

    hashObject.resize(objectLength);
    if (BCryptCreateHash(alg, &hash, hashObject.data(), objectLength, nullptr, 0, 0) != 0) goto done;

    {
        unsigned char buffer[64 * 1024];
        for (;;) {
            const size_t read = fread(buffer, 1, sizeof(buffer), f);
            if (read > 0 && BCryptHashData(hash, buffer, static_cast<ULONG>(read), 0) != 0) goto done;
            if (read < sizeof(buffer)) {
                if (ferror(f)) goto done;
                break;
            }
        }
    }

    if (BCryptFinishHash(hash, hashBytes, sizeof(hashBytes), 0) != 0) goto done;
    *outHex = BytesToHex(hashBytes, sizeof(hashBytes));
    ok = true;

done:
    if (hash) BCryptDestroyHash(hash);
    if (alg) BCryptCloseAlgorithmProvider(alg, 0);
    fclose(f);
    return ok;
}

static bool FileMatchesSha1(const std::wstring& path, const std::string& expectedSha1) {
    if (expectedSha1.empty()) {
        return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
    }

    std::string actual;
    if (!Sha1File(path, &actual)) return false;
    return ToLowerAscii(actual) == ToLowerAscii(expectedSha1);
}

static bool ReadDownloadManifest(const std::wstring& path, std::vector<DownloadManifestEntry>& entries) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return false;

    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;

        std::vector<std::string> parts;
        size_t start = 0;
        for (;;) {
            const size_t tab = line.find('\t', start);
            parts.push_back(line.substr(start, tab == std::string::npos ? std::string::npos : tab - start));
            if (tab == std::string::npos) break;
            start = tab + 1;
        }

        if (parts.size() < 4) {
            WriteLogF(L"Skipping malformed download manifest line: %s", a2w(line.c_str()).c_str());
            continue;
        }

        unsigned long long size = 0;
        try {
            size = parts[2].empty() ? 0 : std::stoull(parts[2]);
        } catch (...) {
            size = 0;
        }

        entries.push_back(DownloadManifestEntry{
            a2w(parts[0].c_str()),
            a2w(parts[3].c_str()),
            ToLowerAscii(parts[1]),
            size
        });
    }

    return true;
}

static bool DeleteDirectoryTree(const std::wstring& path) {
    if (path.empty() || path.size() < 4) return false;

    const DWORD attrs = GetFileAttributesW(path.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES) return true;
    if ((attrs & FILE_ATTRIBUTE_DIRECTORY) == 0) {
        SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_NORMAL);
        return DeleteFileW(path.c_str()) || GetLastError() == ERROR_FILE_NOT_FOUND;
    }

    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((path + L"\\*").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
            const std::wstring child = path + L"\\" + fd.cFileName;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                DeleteDirectoryTree(child);
            } else {
                SetFileAttributesW(child.c_str(), FILE_ATTRIBUTE_NORMAL);
                DeleteFileW(child.c_str());
            }
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }

    SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_NORMAL);
    return RemoveDirectoryW(path.c_str()) || GetLastError() == ERROR_FILE_NOT_FOUND;
}

static std::wstring DownloadMarkerPath(const std::wstring& runtimeRoot) {
    return runtimeRoot + L"\\.download_manifest";
}

static std::wstring BuildDownloadMarker(const std::wstring& manifestPath) {
    std::string sha1;
    Sha1File(manifestPath, &sha1);
    return std::wstring(L"markerVersion=1\n") +
        L"minecraft=" + std::wstring(kMinecraftVersionW) + L"\n" +
        L"fabricLoader=" + a2w(kFabricLoaderVersion) + L"\n" +
        L"assetIndex=" + a2w(kMinecraftAssetIndex) + L"\n" +
        L"manifestSha1=" + a2w(sha1.c_str()) + L"\n";
}

static void CleanupDownloadedRuntimeFiles(const std::wstring& runtimeRoot, const wchar_t* reason) {
    WriteLogF(L"Cleaning downloaded runtime files reason=%s", reason ? reason : L"unknown");
    DeleteDirectoryTree(runtimeRoot + L"\\assets");
    DeleteDirectoryTree(runtimeRoot + L"\\game\\libraries");
    DeleteDirectoryTree(runtimeRoot + L"\\game\\versions");
    DeleteFileW(DownloadMarkerPath(runtimeRoot).c_str());
}

static bool EnsureDownloadMarkerMatches(
    const std::wstring& manifestPath,
    const std::wstring& runtimeRoot,
    bool forceRepair) {
    if (forceRepair) {
        CleanupDownloadedRuntimeFiles(runtimeRoot, L"repair requested");
        return true;
    }

    const std::wstring expected = BuildDownloadMarker(manifestPath);
    std::wstring actual;
    if (!ReadTextFile(DownloadMarkerPath(runtimeRoot), actual)) {
        return true;
    }
    if (actual == expected) {
        return true;
    }

    CleanupDownloadedRuntimeFiles(runtimeRoot, L"manifest marker changed");
    return true;
}

static void MarkDownloadManifestCurrent(const std::wstring& manifestPath, const std::wstring& runtimeRoot) {
    if (WriteTextFile(DownloadMarkerPath(runtimeRoot), BuildDownloadMarker(manifestPath))) {
        WriteLog(L"Download manifest marker written");
    } else {
        WriteLogF(L"Failed to write download manifest marker err=%u", GetLastError());
    }
}

static constexpr int kDownloadFileAttempts = 5;
static constexpr DWORD kDownloadRetryBaseDelayMs = 750;

static std::wstring BuildRedirectUrl(const std::wstring& currentUrl, const std::wstring& location) {
    if (location.find(L"://") != std::wstring::npos) {
        return location;
    }

    URL_COMPONENTS uc = {};
    uc.dwStructSize = sizeof(uc);
    uc.dwSchemeLength = static_cast<DWORD>(-1);
    uc.dwHostNameLength = static_cast<DWORD>(-1);
    uc.dwUrlPathLength = static_cast<DWORD>(-1);
    uc.dwExtraInfoLength = static_cast<DWORD>(-1);

    std::wstring mutableUrl = currentUrl;
    if (!WinHttpCrackUrl(mutableUrl.c_str(), 0, 0, &uc)) {
        return location;
    }

    std::wstring scheme(uc.lpszScheme, uc.dwSchemeLength);
    std::wstring host(uc.lpszHostName, uc.dwHostNameLength);
    std::wstring path(uc.lpszUrlPath, uc.dwUrlPathLength);
    std::wstring extra;
    if (uc.dwExtraInfoLength > 0) {
        extra.assign(uc.lpszExtraInfo, uc.dwExtraInfoLength);
    }

    if (!location.empty() && location[0] == L'/') {
        return scheme + L"://" + host + location;
    }

    const size_t slash = path.find_last_of(L'/');
    path = slash == std::wstring::npos ? L"/" : path.substr(0, slash + 1);
    return scheme + L"://" + host + path + location;
}

static bool DownloadUrlToFile(
    const std::wstring& url,
    const std::wstring& destination,
    const std::function<void(unsigned long long)>& progressCallback) {
    std::wstring currentUrl = url;

    for (int redirect = 0; redirect < 6; ++redirect) {
        URL_COMPONENTS uc = {};
        uc.dwStructSize = sizeof(uc);
        uc.dwSchemeLength = static_cast<DWORD>(-1);
        uc.dwHostNameLength = static_cast<DWORD>(-1);
        uc.dwUrlPathLength = static_cast<DWORD>(-1);
        uc.dwExtraInfoLength = static_cast<DWORD>(-1);

        std::wstring mutableUrl = currentUrl;
        if (!WinHttpCrackUrl(mutableUrl.c_str(), 0, 0, &uc)) {
            WriteLogF(L"WinHttpCrackUrl failed err=%u url=%s", GetLastError(), currentUrl.c_str());
            return false;
        }

        std::wstring host(uc.lpszHostName, uc.dwHostNameLength);
        std::wstring objectName;
        if (uc.dwUrlPathLength > 0) objectName.assign(uc.lpszUrlPath, uc.dwUrlPathLength);
        if (uc.dwExtraInfoLength > 0) objectName.append(uc.lpszExtraInfo, uc.dwExtraInfoLength);
        if (objectName.empty()) objectName = L"/";

        HINTERNET session = WinHttpOpen(
            L"MinecraftJavaUWP/1.0",
            WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
            WINHTTP_NO_PROXY_NAME,
            WINHTTP_NO_PROXY_BYPASS,
            0);
        if (!session) {
            WriteLogF(L"WinHttpOpen failed err=%u", GetLastError());
            return false;
        }
        WinHttpSetTimeouts(session, 15000, 15000, 30000, 30000);

        HINTERNET connect = WinHttpConnect(session, host.c_str(), uc.nPort, 0);
        if (!connect) {
            WriteLogF(L"WinHttpConnect failed err=%u host=%s", GetLastError(), host.c_str());
            WinHttpCloseHandle(session);
            return false;
        }

        const DWORD flags = (uc.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
        HINTERNET request = WinHttpOpenRequest(
            connect,
            L"GET",
            objectName.c_str(),
            nullptr,
            WINHTTP_NO_REFERER,
            WINHTTP_DEFAULT_ACCEPT_TYPES,
            flags);
        if (!request) {
            WriteLogF(L"WinHttpOpenRequest failed err=%u path=%s", GetLastError(), objectName.c_str());
            WinHttpCloseHandle(connect);
            WinHttpCloseHandle(session);
            return false;
        }

        BOOL sent = WinHttpSendRequest(
            request,
            WINHTTP_NO_ADDITIONAL_HEADERS,
            0,
            WINHTTP_NO_REQUEST_DATA,
            0,
            0,
            0);
        BOOL received = sent ? WinHttpReceiveResponse(request, nullptr) : FALSE;
        if (!sent || !received) {
            WriteLogF(L"WinHttp request failed err=%u url=%s", GetLastError(), currentUrl.c_str());
            WinHttpCloseHandle(request);
            WinHttpCloseHandle(connect);
            WinHttpCloseHandle(session);
            return false;
        }

        DWORD status = 0;
        DWORD statusSize = sizeof(status);
        if (!WinHttpQueryHeaders(
            request,
            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX,
            &status,
            &statusSize,
            WINHTTP_NO_HEADER_INDEX)) {
            WriteLogF(L"WinHttpQueryHeaders(status) failed err=%u", GetLastError());
            WinHttpCloseHandle(request);
            WinHttpCloseHandle(connect);
            WinHttpCloseHandle(session);
            return false;
        }

        if (status == 301 || status == 302 || status == 303 || status == 307 || status == 308) {
            DWORD locationBytes = 0;
            WinHttpQueryHeaders(
                request,
                WINHTTP_QUERY_LOCATION,
                WINHTTP_HEADER_NAME_BY_INDEX,
                WINHTTP_NO_OUTPUT_BUFFER,
                &locationBytes,
                WINHTTP_NO_HEADER_INDEX);
            std::vector<wchar_t> location((locationBytes / sizeof(wchar_t)) + 1);
            if (locationBytes > 0 &&
                WinHttpQueryHeaders(
                    request,
                    WINHTTP_QUERY_LOCATION,
                    WINHTTP_HEADER_NAME_BY_INDEX,
                    location.data(),
                    &locationBytes,
                    WINHTTP_NO_HEADER_INDEX)) {
                currentUrl = BuildRedirectUrl(currentUrl, location.data());
                WinHttpCloseHandle(request);
                WinHttpCloseHandle(connect);
                WinHttpCloseHandle(session);
                continue;
            }
        }

        if (status != 200) {
            WriteLogF(L"Download failed HTTP %u url=%s", status, currentUrl.c_str());
            WinHttpCloseHandle(request);
            WinHttpCloseHandle(connect);
            WinHttpCloseHandle(session);
            return false;
        }

        EnsureDirectoryTree(GetParentDir(destination));
        FILE* out = nullptr;
        if (_wfopen_s(&out, destination.c_str(), L"wb") != 0 || !out) {
            WriteLogF(L"Could not open download output %s err=%u", destination.c_str(), GetLastError());
            WinHttpCloseHandle(request);
            WinHttpCloseHandle(connect);
            WinHttpCloseHandle(session);
            return false;
        }

        bool ok = true;
        unsigned long long fileBytesRead = 0;
        unsigned char buffer[64 * 1024];
        for (;;) {
            DWORD bytesRead = 0;
            if (!WinHttpReadData(request, buffer, sizeof(buffer), &bytesRead)) {
                WriteLogF(L"WinHttpReadData failed err=%u url=%s", GetLastError(), currentUrl.c_str());
                ok = false;
                break;
            }
            if (bytesRead == 0) break;
            if (fwrite(buffer, 1, bytesRead, out) != bytesRead) {
                WriteLogF(L"fwrite failed while downloading %s", destination.c_str());
                ok = false;
                break;
            }
            fileBytesRead += bytesRead;
            if (progressCallback) {
                progressCallback(fileBytesRead);
            }
        }

        fclose(out);
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return ok;
    }

    WriteLogF(L"Too many redirects url=%s", url.c_str());
    return false;
}

static bool EnsureRuntimeDownloads(
    const std::wstring& manifestPath,
    const std::wstring& runtimeRoot,
    const DownloadProgressCallback& progress = DownloadProgressCallback(),
    const DownloadOptions& options = DownloadOptions()) {
    if (GetFileAttributesW(manifestPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
        WriteLogF(L"No download manifest found at %s", manifestPath.c_str());
        return false;
    }
    if (!EnsureDownloadMarkerMatches(manifestPath, runtimeRoot, options.forceRepair)) {
        return false;
    }

    std::vector<DownloadManifestEntry> entries;
    if (!ReadDownloadManifest(manifestPath, entries)) {
        WriteLogF(L"Failed to read download manifest: %s", manifestPath.c_str());
        return false;
    }
    if (entries.empty()) {
        WriteLog(L"Download manifest is empty");
        if (progress) progress(L"No downloads needed", L"Launching Minecraft", 1.0f);
        return true;
    }

    unsigned long long totalBytes = 0;
    for (const auto& entry : entries) {
        totalBytes += entry.size;
    }

    size_t verified = 0;
    size_t downloaded = 0;
    unsigned long long completedBytes = 0;
    std::vector<size_t> missing;
    const std::wstring totalText = std::to_wstring(entries.size()) + L" files, " +
        std::to_wstring(totalBytes / (1024ULL * 1024ULL)) + L" MB";
    auto formatDownloadDetail = [&](size_t fileIndex, unsigned long long bytesDone) {
        return std::to_wstring(fileIndex) + L"/" + std::to_wstring(entries.size()) +
            L" files  " + std::to_wstring(bytesDone / (1024ULL * 1024ULL)) +
            L"/" + std::to_wstring(totalBytes / (1024ULL * 1024ULL)) + L" MB";
    };
    WriteLogF(L"Download manifest entries=%zu totalBytes=%llu", entries.size(), totalBytes);
    if (progress) progress(L"Preparing download", totalText.c_str(), 0.0f);

    for (size_t i = 0; i < entries.size(); ++i) {
        const auto& entry = entries[i];
        const std::wstring finalPath = JoinRuntimeRelativePath(runtimeRoot, entry.relativePath);
        if (finalPath.empty()) {
            WriteLogF(L"Invalid manifest relative path: %s", entry.relativePath.c_str());
            return false;
        }

        if (FileMatchesSha1(finalPath, entry.sha1)) {
            ++verified;
            completedBytes += entry.size;
            if (progress && (verified < 25 || verified % 100 == 0 || verified == entries.size())) {
                const float ratio = totalBytes > 0
                    ? static_cast<float>(static_cast<double>(completedBytes) / static_cast<double>(totalBytes))
                    : static_cast<float>(static_cast<double>(verified) / static_cast<double>(entries.size()));
                const std::wstring detail = formatDownloadDetail(verified, completedBytes);
                progress(L"Checking installed files", detail.c_str(), ratio);
            }
            continue;
        }

        missing.push_back(i);
    }

    if (missing.empty()) {
        WriteLogF(L"Download pass complete verified=%zu downloaded=0", verified);
        MarkDownloadManifestCurrent(manifestPath, runtimeRoot);
        if (progress) progress(L"Download complete", L"Launching Minecraft", 1.0f);
        return true;
    }

    const unsigned workerCount = (std::max)(1u, (std::min)(8u, options.workerCount));
    const unsigned workersToStart = (std::min<unsigned>)(workerCount, static_cast<unsigned>(missing.size()));
    WriteLogF(L"Downloading missing runtime files missing=%zu workers=%u", missing.size(), workersToStart);

    std::mutex stateMutex;
    std::vector<unsigned long long> inProgressBytes(entries.size(), 0);
    size_t nextMissing = 0;
    bool failed = false;
    std::wstring failureStatus;
    std::wstring failureDetail;
    int activeWorkers = static_cast<int>(workersToStart);

    auto workerProc = [&]() {
        for (;;) {
            size_t entryIndex = 0;
            {
                std::lock_guard<std::mutex> lock(stateMutex);
                if (failed || nextMissing >= missing.size()) {
                    break;
                }
                entryIndex = missing[nextMissing++];
                inProgressBytes[entryIndex] = 0;
            }

            const auto& entry = entries[entryIndex];
            const std::wstring finalPath = JoinRuntimeRelativePath(runtimeRoot, entry.relativePath);
            const std::wstring tempPath = finalPath + L".download";
            DeleteFileW(tempPath.c_str());

            if (entryIndex < 25 || entryIndex % 100 == 0) {
                WriteLogF(L"Downloading [%zu/%zu] %s", entryIndex + 1, entries.size(), entry.relativePath.c_str());
            }

            auto progressCallback = [&](unsigned long long fileBytesRead) {
                const unsigned long long cappedFileBytes = entry.size > 0
                    ? std::min<unsigned long long>(fileBytesRead, entry.size)
                    : fileBytesRead;
                std::lock_guard<std::mutex> lock(stateMutex);
                inProgressBytes[entryIndex] = cappedFileBytes;
            };

            bool downloadedOk = false;
            for (int attempt = 1; attempt <= kDownloadFileAttempts; ++attempt) {
                {
                    std::lock_guard<std::mutex> lock(stateMutex);
                    if (failed) {
                        break;
                    }
                    inProgressBytes[entryIndex] = 0;
                }

                DeleteFileW(tempPath.c_str());
                if (attempt > 1) {
                    const DWORD delayMs = kDownloadRetryBaseDelayMs * static_cast<DWORD>(attempt - 1);
                    WriteLogF(L"Retrying download attempt=%d/%d delayMs=%u file=%s",
                        attempt,
                        kDownloadFileAttempts,
                        delayMs,
                        entry.relativePath.c_str());
                    std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
                }

                if (DownloadUrlToFile(entry.url, tempPath, progressCallback)) {
                    downloadedOk = true;
                    break;
                }

                WriteLogF(L"Download attempt failed attempt=%d/%d file=%s",
                    attempt,
                    kDownloadFileAttempts,
                    entry.relativePath.c_str());
            }

            if (!downloadedOk) {
                DeleteFileW(tempPath.c_str());
                std::lock_guard<std::mutex> lock(stateMutex);
                failed = true;
                failureStatus = L"Download failed after retries";
                failureDetail = entry.relativePath;
                break;
            }

            if (!FileMatchesSha1(tempPath, entry.sha1)) {
                std::string actual;
                Sha1File(tempPath, &actual);
                WriteLogF(L"SHA1 mismatch for %s expected=%s actual=%s",
                    entry.relativePath.c_str(),
                    a2w(entry.sha1.c_str()).c_str(),
                    a2w(actual.c_str()).c_str());
                DeleteFileW(tempPath.c_str());
                std::lock_guard<std::mutex> lock(stateMutex);
                failed = true;
                failureStatus = L"File verification failed";
                failureDetail = entry.relativePath;
                break;
            }

            EnsureDirectoryTree(GetParentDir(finalPath));
            DeleteFileW(finalPath.c_str());
            if (!MoveFileExW(tempPath.c_str(), finalPath.c_str(), MOVEFILE_REPLACE_EXISTING)) {
                WriteLogF(L"MoveFileEx failed for %s err=%u", finalPath.c_str(), GetLastError());
                DeleteFileW(tempPath.c_str());
                std::lock_guard<std::mutex> lock(stateMutex);
                failed = true;
                failureStatus = L"Download install failed";
                failureDetail = entry.relativePath;
                break;
            }

            {
                std::lock_guard<std::mutex> lock(stateMutex);
                inProgressBytes[entryIndex] = 0;
                completedBytes += entry.size;
                ++downloaded;
                ++verified;
            }
        }

        {
            std::lock_guard<std::mutex> lock(stateMutex);
            --activeWorkers;
        }
    };

    std::vector<std::thread> workers;
    workers.reserve(workersToStart);
    for (unsigned i = 0; i < workersToStart; ++i) {
        workers.emplace_back(workerProc);
    }

    while (true) {
        bool done = false;
        bool failedSnapshot = false;
        std::wstring failureStatusSnapshot;
        std::wstring failureDetailSnapshot;
        size_t verifiedSnapshot = 0;
        unsigned long long bytesSnapshot = 0;
        {
            std::lock_guard<std::mutex> lock(stateMutex);
            done = activeWorkers == 0;
            failedSnapshot = failed;
            failureStatusSnapshot = failureStatus;
            failureDetailSnapshot = failureDetail;
            verifiedSnapshot = verified;
            bytesSnapshot = completedBytes;
            for (unsigned long long bytes : inProgressBytes) {
                bytesSnapshot += bytes;
            }
        }

        if (progress) {
            const float ratio = totalBytes > 0
                ? static_cast<float>(static_cast<double>(bytesSnapshot) / static_cast<double>(totalBytes))
                : static_cast<float>(static_cast<double>(verifiedSnapshot) / static_cast<double>(entries.size()));
            if (failedSnapshot) {
                progress(
                    failureStatusSnapshot.empty() ? L"Download failed" : failureStatusSnapshot.c_str(),
                    failureDetailSnapshot.c_str(),
                    ratio);
            } else {
                const std::wstring detail = formatDownloadDetail(verifiedSnapshot, bytesSnapshot);
                progress(L"Downloading Minecraft files", detail.c_str(), ratio);
            }
        }

        if (done) {
            break;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    for (std::thread& worker : workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }

    if (failed) {
        return false;
    }

    WriteLogF(L"Download pass complete verified=%zu downloaded=%zu", verified, downloaded);
    MarkDownloadManifestCurrent(manifestPath, runtimeRoot);
    if (progress) progress(L"Download complete", L"Launching Minecraft", 1.0f);
    return true;
}

static bool ContainsInsensitive(const std::wstring& value, const wchar_t* needle) {
    if (!needle || !*needle) return false;

    std::wstring haystack = value;
    std::wstring target = needle;
    std::transform(haystack.begin(), haystack.end(), haystack.begin(),
        [](wchar_t c) { return static_cast<wchar_t>(towlower(c)); });
    std::transform(target.begin(), target.end(), target.begin(),
        [](wchar_t c) { return static_cast<wchar_t>(towlower(c)); });
    return haystack.find(target) != std::wstring::npos;
}

static std::wstring DetectGraphicsRuntimeName() {
    const std::wstring overrideValue = GetEnvVarString(L"MC_GRAPHICS_RUNTIME");
    if (!overrideValue.empty()) {
        WriteLogF(L"Graphics runtime override: %s", overrideValue.c_str());
        return overrideValue;
    }

    try {
        using namespace winrt::Windows::Security::ExchangeActiveSyncProvisioning;
        EasClientDeviceInformation info;
        const std::wstring manufacturer = info.SystemManufacturer().c_str();
        const std::wstring productName = info.SystemProductName().c_str();
        const std::wstring sku = info.SystemSku().c_str();
        const std::wstring friendlyName = info.FriendlyName().c_str();
        const std::wstring probe = manufacturer + L" " + productName + L" " + sku + L" " + friendlyName;

        WriteLogF(L"Device manufacturer: %s", manufacturer.c_str());
        WriteLogF(L"Device product: %s", productName.c_str());
        WriteLogF(L"Device SKU: %s", sku.c_str());
        WriteLogF(L"Device friendly name: %s", friendlyName.c_str());

        if (ContainsInsensitive(probe, L"xbox one") ||
            ContainsInsensitive(probe, L"xboxone") ||
            ContainsInsensitive(probe, L"durango")) {
            return L"xboxone";
        }

        if (ContainsInsensitive(probe, L"xbox series") ||
            ContainsInsensitive(probe, L"scarlett") ||
            ContainsInsensitive(probe, L"anaconda") ||
            ContainsInsensitive(probe, L"lockhart")) {
            return L"mesa";
        }
    } catch (...) {
        WriteLog(L"Device graphics runtime detection failed; defaulting to Mesa");
    }

    return L"mesa";
}

struct LaunchAuthConfig {
    std::string username;
    std::string uuid;
    std::string accessToken;
};

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
    bool isModpack = false;
};

static std::mutex g_modsSearchMutex;
static std::wstring g_modsSearchBuffer;
static int g_modsCaret = 0;
static bool g_modsSearchDirty = false;
static std::atomic<bool> g_modsSearchCapturing{false};
static std::atomic<bool> g_modsEditFocusRemoved{false};
static std::atomic<bool> g_modsSearchSubmit{false};

// render computes how many card rows fit; the input loop reads this to keep the
// selection on screen without duplicating the layout math
static std::atomic<int> g_modsRowsVisible{1};

static std::string ExtractJsonStringValue(const std::string& content, const char* key) {
    if (!key || !*key) return {};
    const std::string needle = std::string("\"") + key + "\"";
    const size_t keyPos = content.find(needle);
    if (keyPos == std::string::npos) return {};

    size_t colonPos = content.find(':', keyPos + needle.size());
    if (colonPos == std::string::npos) return {};
    size_t valueStart = content.find('"', colonPos + 1);
    if (valueStart == std::string::npos) return {};
    ++valueStart;

    std::string value;
    for (size_t i = valueStart; i < content.size(); ++i) {
        const char c = content[i];
        if (c == '\\') {
            if (i + 1 < content.size()) {
                value.push_back(content[i + 1]);
                ++i;
            }
            continue;
        }
        if (c == '"') {
            return value;
        }
        value.push_back(c);
    }

    return {};
}

static int ExtractJsonIntValue(const std::string& content, const char* key, int fallback = 0) {
    if (!key || !*key) return fallback;
    const std::string needle = std::string("\"") + key + "\"";
    const size_t keyPos = content.find(needle);
    if (keyPos == std::string::npos) return fallback;

    size_t pos = content.find(':', keyPos + needle.size());
    if (pos == std::string::npos) return fallback;
    ++pos;
    while (pos < content.size() && isspace(static_cast<unsigned char>(content[pos]))) {
        ++pos;
    }

    const size_t start = pos;
    if (pos < content.size() && content[pos] == '-') {
        ++pos;
    }
    while (pos < content.size() && isdigit(static_cast<unsigned char>(content[pos]))) {
        ++pos;
    }
    if (pos == start) return fallback;

    try {
        return std::stoi(content.substr(start, pos - start));
    } catch (...) {
        return fallback;
    }
}

static std::string JsonEscape(const std::string& value) {
    std::string result;
    result.reserve(value.size() + 8);
    for (unsigned char c : value) {
        switch (c) {
        case '\\': result += "\\\\"; break;
        case '"':  result += "\\\""; break;
        case '\b': result += "\\b"; break;
        case '\f': result += "\\f"; break;
        case '\n': result += "\\n"; break;
        case '\r': result += "\\r"; break;
        case '\t': result += "\\t"; break;
        default:
            if (c < 0x20) {
                char buf[7] = {};
                sprintf_s(buf, "\\u%04x", c);
                result += buf;
            } else {
                result.push_back(static_cast<char>(c));
            }
            break;
        }
    }
    return result;
}

static std::string FormUrlEncode(const std::string& value) {
    std::ostringstream encoded;
    encoded << std::uppercase << std::hex;
    for (unsigned char c : value) {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            encoded << static_cast<char>(c);
        } else if (c == ' ') {
            encoded << '+';
        } else {
            encoded << '%' << std::setw(2) << std::setfill('0') << static_cast<int>(c);
        }
    }
    return encoded.str();
}

static std::string MakeFormBody(std::initializer_list<std::pair<std::string, std::string>> fields) {
    std::string body;
    bool first = true;
    for (const auto& field : fields) {
        if (!first) body += '&';
        first = false;
        body += FormUrlEncode(field.first);
        body += '=';
        body += FormUrlEncode(field.second);
    }
    return body;
}

static std::string NormalizeMinecraftUuid(const std::string& value) {
    std::string compact;
    compact.reserve(value.size());
    for (char c : value) {
        if (c != '-') compact.push_back(c);
    }
    if (compact.size() != 32) {
        return value;
    }
    return compact.substr(0, 8) + "-" +
        compact.substr(8, 4) + "-" +
        compact.substr(12, 4) + "-" +
        compact.substr(16, 4) + "-" +
        compact.substr(20, 12);
}

struct HttpResult {
    int status = 0;
    std::string body;

    bool success() const {
        return status >= 200 && status < 300;
    }
};

static HttpResult HttpPostString(const wchar_t* url, const std::string& body, const wchar_t* mediaType) {
    HttpResult result;
    try {
        using namespace winrt::Windows::Foundation;
        using namespace winrt::Windows::Storage::Streams;
        using namespace winrt::Windows::Web::Http;

        HttpClient client;
        HttpStringContent content(winrt::to_hstring(body), UnicodeEncoding::Utf8, mediaType);
        HttpResponseMessage response = client.PostAsync(winrt::Windows::Foundation::Uri(url), content).get();
        result.status = static_cast<int>(response.StatusCode());
        result.body = winrt::to_string(response.Content().ReadAsStringAsync().get());
    } catch (const winrt::hresult_error& ex) {
        WriteLogF(L"HTTP POST failed url=%s hr=0x%08X msg=%s",
            url, static_cast<unsigned int>(ex.code()), ex.message().c_str());
    }
    return result;
}

static HttpResult HttpGetBearer(const wchar_t* url, const std::string& token) {
    HttpResult result;
    try {
        using namespace winrt::Windows::Foundation;
        using namespace winrt::Windows::Web::Http;
        using namespace winrt::Windows::Web::Http::Headers;

        HttpClient client;
        HttpRequestMessage request(HttpMethod::Get(), winrt::Windows::Foundation::Uri(url));
        request.Headers().Authorization(HttpCredentialsHeaderValue(L"Bearer", winrt::to_hstring(token)));
        HttpResponseMessage response = client.SendRequestAsync(request).get();
        result.status = static_cast<int>(response.StatusCode());
        result.body = winrt::to_string(response.Content().ReadAsStringAsync().get());
    } catch (const winrt::hresult_error& ex) {
        WriteLogF(L"HTTP GET failed url=%s hr=0x%08X msg=%s",
            url, static_cast<unsigned int>(ex.code()), ex.message().c_str());
    }
    return result;
}

static HttpResult HttpGetString(const wchar_t* url) {
    HttpResult result;
    try {
        using namespace winrt::Windows::Foundation;
        using namespace winrt::Windows::Web::Http;

        HttpClient client;
        HttpRequestMessage request(HttpMethod::Get(), winrt::Windows::Foundation::Uri(url));
        request.Headers().UserAgent().ParseAdd(L"BanditVault-BanditLauncher/1.0");
        HttpResponseMessage response = client.SendRequestAsync(request).get();
        result.status = static_cast<int>(response.StatusCode());
        result.body = winrt::to_string(response.Content().ReadAsStringAsync().get());
    } catch (const winrt::hresult_error& ex) {
        WriteLogF(L"HTTP GET failed url=%s hr=0x%08X msg=%s",
            url, static_cast<unsigned int>(ex.code()), ex.message().c_str());
    }
    return result;
}

static std::wstring JsonStringOrEmpty(const winrt::Windows::Data::Json::JsonObject& obj, const wchar_t* key) {
    using namespace winrt::Windows::Data::Json;
    if (!key || !obj.HasKey(key)) return {};
    try {
        const auto value = obj.GetNamedValue(key);
        if (value.ValueType() == JsonValueType::String) {
            return std::wstring(obj.GetNamedString(key).c_str());
        }
    } catch (...) {
    }
    return {};
}

static int JsonIntOrZero(const winrt::Windows::Data::Json::JsonObject& obj, const wchar_t* key) {
    using namespace winrt::Windows::Data::Json;
    if (!key || !obj.HasKey(key)) return 0;
    try {
        const auto value = obj.GetNamedValue(key);
        if (value.ValueType() == JsonValueType::Number) {
            return static_cast<int>(obj.GetNamedNumber(key));
        }
    } catch (...) {
    }
    return 0;
}

static bool JsonBoolOrFalse(const winrt::Windows::Data::Json::JsonObject& obj, const wchar_t* key) {
    using namespace winrt::Windows::Data::Json;
    if (!key || !obj.HasKey(key)) return false;
    try {
        const auto value = obj.GetNamedValue(key);
        if (value.ValueType() == JsonValueType::Boolean) {
            return obj.GetNamedBoolean(key);
        }
    } catch (...) {
    }
    return false;
}

static std::wstring SafeFileName(std::wstring value) {
    for (wchar_t& ch : value) {
        if (ch < 32 || wcschr(L"<>:\"/\\|?*", ch)) {
            ch = L'_';
        }
    }
    if (value.empty()) value = L"mod";
    return value;
}

static std::wstring ModIconCachePath(const std::wstring& runtimeRoot, const std::wstring& projectId) {
    return runtimeRoot + L"\\mod-icons\\" + SafeFileName(projectId) + L".img";
}

static std::wstring CacheModIcon(const std::wstring& runtimeRoot, const std::wstring& projectId, const std::wstring& iconUrl) {
    if (runtimeRoot.empty() || projectId.empty() || iconUrl.empty()) return {};
    const std::wstring path = ModIconCachePath(runtimeRoot, projectId);
    if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES) {
        return path;
    }

    WriteLogF(L"Downloading Modrinth icon project=%s", projectId.c_str());
    if (DownloadUrlToFile(iconUrl, path, nullptr)) {
        return path;
    }

    DeleteFileW(path.c_str());
    WriteLogF(L"Modrinth icon download failed project=%s url=%s", projectId.c_str(), iconUrl.c_str());
    return {};
}

static std::wstring BuildModrinthSearchUrl(const char* index, int limit, int offset, const std::wstring& query, const char* projectType) {
    const std::string facets =
        std::string("[[\"project_type:") + projectType +
        "\"],[\"categories:fabric\"],[\"versions:" +
        std::string(kMinecraftVersion) +
        "\"],[\"client_side:required\",\"client_side:optional\"]]";
    std::wstring url = L"https://api.modrinth.com/v2/search?limit=" +
        std::to_wstring(limit) +
        L"&offset=" + std::to_wstring(offset) +
        L"&index=" + a2w(index) +
        L"&facets=" + a2w(FormUrlEncode(facets).c_str());
    if (!query.empty()) {
        url += L"&query=" + a2w(FormUrlEncode(w2a(query)).c_str());
    }
    return url;
}

struct IconJob {
    std::wstring url;
    std::wstring path;
};

static std::mutex g_iconMutex;
static std::vector<IconJob> g_iconJobs;
static size_t g_iconCursor = 0;
static std::atomic<bool> g_iconWorkerRun{false};
static std::thread g_iconWorker;

// icons stream in on a worker so the grid renders instantly and a large result
// set doesn't block the UI on sequential downloads
static void IconWorkerLoop() {
    while (g_iconWorkerRun.load()) {
        IconJob job;
        bool have = false;
        {
            std::lock_guard<std::mutex> lk(g_iconMutex);
            if (g_iconCursor < g_iconJobs.size()) {
                job = g_iconJobs[g_iconCursor++];
                have = true;
            }
        }
        if (!have) {
            std::this_thread::sleep_for(std::chrono::milliseconds(60));
            continue;
        }
        if (GetFileAttributesW(job.path.c_str()) != INVALID_FILE_ATTRIBUTES) continue;
        DownloadUrlToFile(job.url, job.path, nullptr);
    }
}

static void StartIconWorker() {
    if (g_iconWorkerRun.load()) return;
    g_iconWorkerRun.store(true);
    g_iconWorker = std::thread(IconWorkerLoop);
}

static void StopIconWorker() {
    g_iconWorkerRun.store(false);
    if (g_iconWorker.joinable()) g_iconWorker.join();
    std::lock_guard<std::mutex> lk(g_iconMutex);
    g_iconJobs.clear();
    g_iconCursor = 0;
}

static void QueueModIcons(const std::vector<ModCard>& cards) {
    std::lock_guard<std::mutex> lk(g_iconMutex);
    g_iconJobs.clear();
    g_iconCursor = 0;
    for (const auto& c : cards) {
        if (!c.iconUrl.empty() && !c.iconPath.empty() &&
            GetFileAttributesW(c.iconPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
            g_iconJobs.push_back({ c.iconUrl, c.iconPath });
        }
    }
}

static void QueueModIconsAppend(const std::vector<ModCard>& cards, size_t startIndex) {
    std::lock_guard<std::mutex> lk(g_iconMutex);
    for (size_t i = startIndex; i < cards.size(); ++i) {
        const auto& c = cards[i];
        if (!c.iconUrl.empty() && !c.iconPath.empty() &&
            GetFileAttributesW(c.iconPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
            g_iconJobs.push_back({ c.iconUrl, c.iconPath });
        }
    }
}

static std::wstring ModMetaPath(const std::wstring& runtimeRoot, const std::wstring& fileName) {
    return runtimeRoot + L"\\mod-meta\\" + SafeFileName(fileName) + L".meta";
}

static std::wstring StripNewlines(std::wstring value) {
    for (wchar_t& ch : value) {
        if (ch == L'\r' || ch == L'\n' || ch == L'\t') ch = L' ';
    }
    return value;
}

static void WriteModMeta(const std::wstring& runtimeRoot, const std::wstring& fileName, const ModCard& card) {
    const std::wstring path = ModMetaPath(runtimeRoot, fileName);
    EnsureDirectoryTree(GetParentDir(path));
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return;
    const std::string body =
        "title\t" + w2a(StripNewlines(card.title)) + "\n" +
        "desc\t" + w2a(StripNewlines(card.description)) + "\n" +
        "icon\t" + w2a(card.iconPath) + "\n";
    f.write(body.data(), static_cast<std::streamsize>(body.size()));
}

static bool ReadModMeta(const std::wstring& runtimeRoot, const std::wstring& fileName, ModCard& card) {
    const std::wstring path = ModMetaPath(runtimeRoot, fileName);
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::string line;
    bool any = false;
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const size_t tab = line.find('\t');
        if (tab == std::string::npos) continue;
        const std::string key = line.substr(0, tab);
        const std::wstring value = a2w(line.substr(tab + 1).c_str());
        if (key == "title" && !value.empty()) { card.title = value; any = true; }
        else if (key == "desc" && !value.empty()) { card.description = value; any = true; }
        else if (key == "icon") {
            if (!value.empty() && GetFileAttributesW(value.c_str()) != INVALID_FILE_ATTRIBUTES) {
                card.iconPath = value;
            }
            any = true;
        }
    }
    return any;
}

static bool ResolveInstalledModMeta(const std::wstring& runtimeRoot, const std::wstring& jarPath, ModCard& card) {
    using namespace winrt::Windows::Data::Json;
    std::string sha1;
    if (!Sha1File(jarPath, &sha1) || sha1.empty()) return false;

    const std::wstring versionUrl =
        L"https://api.modrinth.com/v2/version_file/" + a2w(sha1.c_str()) + L"?algorithm=sha1";
    const HttpResult versionResp = HttpGetString(versionUrl.c_str());
    if (!versionResp.success()) return false;

    std::wstring projectId;
    try {
        JsonObject version = JsonObject::Parse(winrt::to_hstring(versionResp.body));
        projectId = JsonStringOrEmpty(version, L"project_id");
    } catch (...) {
        return false;
    }
    if (projectId.empty()) return false;

    const std::wstring projectUrl =
        L"https://api.modrinth.com/v2/project/" + a2w(FormUrlEncode(w2a(projectId)).c_str());
    const HttpResult projectResp = HttpGetString(projectUrl.c_str());
    if (!projectResp.success()) return false;

    try {
        JsonObject project = JsonObject::Parse(winrt::to_hstring(projectResp.body));
        const std::wstring title = JsonStringOrEmpty(project, L"title");
        const std::wstring desc = JsonStringOrEmpty(project, L"description");
        const std::wstring iconUrl = JsonStringOrEmpty(project, L"icon_url");
        if (!title.empty()) card.title = title;
        if (!desc.empty()) card.description = desc;
        card.projectId = projectId;
        card.iconPath = CacheModIcon(runtimeRoot, projectId, iconUrl);
    } catch (...) {
        return false;
    }
    return true;
}

static bool LoadInstalledMods(const std::wstring& runtimeRoot, const std::wstring& userModsDir, std::vector<ModCard>& out) {
    out.clear();
    EnsureDirectoryTree(userModsDir);

    WIN32_FIND_DATAW fd = {};
    HANDLE h = FindFirstFileW((userModsDir + L"\\*.jar").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) {
        return true;
    }

    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        std::wstring name = fd.cFileName;
        ModCard card;
        card.title = name;
        card.description = L"Installed in user-mods";
        card.filePath = userModsDir + L"\\" + name;
        card.status = L"Installed";
        card.installed = true;

        if (!ReadModMeta(runtimeRoot, name, card)) {
            if (!ResolveInstalledModMeta(runtimeRoot, card.filePath, card)) {
                card.title = name;
                card.description = L"Installed in user-mods";
            }
            WriteModMeta(runtimeRoot, name, card);
        }

        out.push_back(card);
    } while (FindNextFileW(h, &fd));
    FindClose(h);

    std::sort(out.begin(), out.end(), [](const ModCard& a, const ModCard& b) {
        return _wcsicmp(a.title.c_str(), b.title.c_str()) < 0;
    });
    return true;
}

static const int kModPageSize = 50;

static bool FetchModrinthMods(const std::wstring& runtimeRoot, const char* index, const std::wstring& query, int offset, int limit, std::vector<ModCard>& out, int& totalHits, std::wstring& error, const char* projectType) {
    using namespace winrt::Windows::Data::Json;
    error.clear();
    const bool modpack = projectType && std::strcmp(projectType, "modpack") == 0;

    const std::wstring url = BuildModrinthSearchUrl(index, limit, offset, query, projectType);
    WriteLogF(L"Modrinth search url=%s", url.c_str());
    const HttpResult response = HttpGetString(url.c_str());
    if (!response.success()) {
        error = L"Modrinth search failed HTTP " + std::to_wstring(response.status);
        WriteLogF(L"%s", error.c_str());
        return false;
    }

    try {
        JsonObject root = JsonObject::Parse(winrt::to_hstring(response.body));
        if (!root.HasKey(L"hits") || root.GetNamedValue(L"hits").ValueType() != JsonValueType::Array) {
            error = L"Modrinth search returned no hits";
            return false;
        }
        totalHits = JsonIntOrZero(root, L"total_hits");

        JsonArray hits = root.GetNamedArray(L"hits");
        for (uint32_t i = 0; i < hits.Size(); ++i) {
            auto value = hits.GetAt(i);
            if (value.ValueType() != JsonValueType::Object) continue;
            JsonObject hit = value.GetObject();

            ModCard card;
            card.isModpack = modpack;
            card.projectId = JsonStringOrEmpty(hit, L"project_id");
            card.slug = JsonStringOrEmpty(hit, L"slug");
            card.title = JsonStringOrEmpty(hit, L"title");
            card.description = JsonStringOrEmpty(hit, L"description");
            const std::wstring iconUrl = JsonStringOrEmpty(hit, L"icon_url");
            if (card.title.empty()) card.title = card.slug.empty() ? card.projectId : card.slug;
            if (card.description.empty()) {
                card.description = (modpack ? L"Fabric modpack for Minecraft " : L"Fabric mod for Minecraft ") + a2w(kMinecraftVersion);
            }
            card.status = std::to_wstring(JsonIntOrZero(hit, L"downloads")) + L" downloads";
            if (!iconUrl.empty()) {
                card.iconUrl = iconUrl;
                card.iconPath = ModIconCachePath(runtimeRoot, card.projectId.empty() ? card.slug : card.projectId);
            }
            out.push_back(card);
        }
    } catch (const winrt::hresult_error& ex) {
        error = L"Could not parse Modrinth search response";
        WriteLogF(L"Modrinth search parse failed hr=0x%08X msg=%s",
            static_cast<unsigned int>(ex.code()), ex.message().c_str());
        return false;
    }

    return true;
}

static std::atomic<bool> g_installRunning{false};
static std::atomic<int> g_installDone{0};
static std::atomic<int> g_installTotal{0};
static std::atomic<bool> g_installResultReady{false};
static std::atomic<bool> g_installResultOk{false};
static std::mutex g_installStatusMutex;
static std::wstring g_installStatus;

static void SetInstallStatus(const std::wstring& s) {
    std::lock_guard<std::mutex> lk(g_installStatusMutex);
    g_installStatus = s;
}

static std::wstring GetInstallStatus() {
    std::lock_guard<std::mutex> lk(g_installStatusMutex);
    return g_installStatus;
}

static std::wstring MbStr(unsigned long long bytes) {
    wchar_t buf[32];
    swprintf(buf, 32, L"%.1f", static_cast<double>(bytes) / 1048576.0);
    return buf;
}

static std::function<void(unsigned long long)> MakeInstallProgress(const std::wstring& label, unsigned long long total) {
    return [label, total](unsigned long long done) {
        std::wstring s = label;
        if (total > 0) s += L"  " + MbStr((std::min)(done, total)) + L" / " + MbStr(total) + L" MB";
        else s += L"  " + MbStr(done) + L" MB";
        SetInstallStatus(s);
    };
}

static bool ExtractPrimaryModrinthFile(
    const winrt::Windows::Data::Json::JsonObject& version,
    std::wstring& url,
    std::wstring& filename,
    std::string& sha1,
    unsigned long long& size) {
    using namespace winrt::Windows::Data::Json;
    size = 0;
    if (!version.HasKey(L"files") || version.GetNamedValue(L"files").ValueType() != JsonValueType::Array) {
        return false;
    }

    JsonArray files = version.GetNamedArray(L"files");
    JsonObject selected = nullptr;
    for (uint32_t i = 0; i < files.Size(); ++i) {
        auto value = files.GetAt(i);
        if (value.ValueType() != JsonValueType::Object) continue;
        JsonObject file = value.GetObject();
        if (!selected || JsonBoolOrFalse(file, L"primary")) {
            selected = file;
            if (JsonBoolOrFalse(file, L"primary")) break;
        }
    }
    if (!selected) return false;

    url = JsonStringOrEmpty(selected, L"url");
    filename = JsonStringOrEmpty(selected, L"filename");
    if (selected.HasKey(L"size") && selected.GetNamedValue(L"size").ValueType() == JsonValueType::Number) {
        size = static_cast<unsigned long long>(selected.GetNamedNumber(L"size"));
    }
    if (selected.HasKey(L"hashes") &&
        selected.GetNamedValue(L"hashes").ValueType() == JsonValueType::Object) {
        sha1 = w2a(JsonStringOrEmpty(selected.GetNamedObject(L"hashes"), L"sha1"));
    }
    return !url.empty() && !filename.empty();
}

static bool InstallModrinthProjectRecursive(
    const std::wstring& projectIdOrSlug,
    const std::wstring& runtimeRoot,
    const std::wstring& userModsDir,
    std::set<std::wstring>& visited,
    std::vector<std::wstring>& installed,
    const ModCard* topMeta,
    std::wstring& error) {
    using namespace winrt::Windows::Data::Json;
    if (projectIdOrSlug.empty()) {
        error = L"Missing Modrinth project id";
        return false;
    }
    if (visited.find(projectIdOrSlug) != visited.end()) {
        return true;
    }
    visited.insert(projectIdOrSlug);

    const std::string project = w2a(projectIdOrSlug);
    const std::string loaders = FormUrlEncode("[\"fabric\"]");
    const std::string versions = FormUrlEncode("[\"" + std::string(kMinecraftVersion) + "\"]");
    const std::wstring url =
        L"https://api.modrinth.com/v2/project/" + a2w(FormUrlEncode(project).c_str()) +
        L"/version?loaders=" + a2w(loaders.c_str()) +
        L"&game_versions=" + a2w(versions.c_str()) +
        L"&include_changelog=false";

    WriteLogF(L"Modrinth versions url=%s", url.c_str());
    const HttpResult response = HttpGetString(url.c_str());
    if (!response.success()) {
        error = L"Modrinth version lookup failed HTTP " + std::to_wstring(response.status);
        WriteLogF(L"%s project=%s", error.c_str(), projectIdOrSlug.c_str());
        return false;
    }

    try {
        JsonArray versionsArray = JsonArray::Parse(winrt::to_hstring(response.body));
        if (versionsArray.Size() == 0) {
            error = L"No Fabric " + a2w(kMinecraftVersion) + L" version was found";
            return false;
        }

        JsonObject version = nullptr;
        for (uint32_t i = 0; i < versionsArray.Size(); ++i) {
            auto value = versionsArray.GetAt(i);
            if (value.ValueType() != JsonValueType::Object) continue;
            JsonObject candidate = value.GetObject();
            if (JsonStringOrEmpty(candidate, L"version_type") == L"release") {
                version = candidate;
                break;
            }
            if (!version) version = candidate;
        }
        if (!version) {
            error = L"No installable Modrinth version was found";
            return false;
        }

        if (version.HasKey(L"dependencies") &&
            version.GetNamedValue(L"dependencies").ValueType() == JsonValueType::Array) {
            JsonArray dependencies = version.GetNamedArray(L"dependencies");
            for (uint32_t i = 0; i < dependencies.Size(); ++i) {
                auto depValue = dependencies.GetAt(i);
                if (depValue.ValueType() != JsonValueType::Object) continue;
                JsonObject dep = depValue.GetObject();
                if (JsonStringOrEmpty(dep, L"dependency_type") != L"required") continue;
                const std::wstring depProject = JsonStringOrEmpty(dep, L"project_id");
                if (!depProject.empty() &&
                    !InstallModrinthProjectRecursive(depProject, runtimeRoot, userModsDir, visited, installed, nullptr, error)) {
                    return false;
                }
            }
        }

        std::wstring downloadUrl;
        std::wstring filename;
        std::string sha1;
        unsigned long long fileSize = 0;
        if (!ExtractPrimaryModrinthFile(version, downloadUrl, filename, sha1, fileSize)) {
            error = L"Modrinth version did not include a downloadable file";
            return false;
        }

        EnsureDirectoryTree(userModsDir);
        const std::wstring destination = userModsDir + L"\\" + SafeFileName(filename);
        if (FileMatchesSha1(destination, sha1)) {
            WriteLogF(L"Mod already installed: %s", destination.c_str());
            return true;
        }

        const std::wstring tempPath = destination + L".download";
        DeleteFileW(tempPath.c_str());
        WriteLogF(L"Downloading Modrinth mod %s", filename.c_str());
        SetInstallStatus(L"Installing " + filename);
        if (!DownloadUrlToFile(downloadUrl, tempPath, MakeInstallProgress(L"Installing " + filename, fileSize))) {
            DeleteFileW(tempPath.c_str());
            error = L"Mod download failed: " + filename;
            return false;
        }

        if (!FileMatchesSha1(tempPath, sha1)) {
            DeleteFileW(tempPath.c_str());
            error = L"Mod verification failed: " + filename;
            return false;
        }

        DeleteFileW(destination.c_str());
        if (!MoveFileExW(tempPath.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING)) {
            DeleteFileW(tempPath.c_str());
            error = L"Could not install mod: " + filename;
            WriteLogF(L"MoveFileEx failed for mod %s err=%u", destination.c_str(), GetLastError());
            return false;
        }

        installed.push_back(filename);
        if (topMeta) {
            ModCard meta = *topMeta;
            WriteModMeta(runtimeRoot, filename, meta);
        }
        WriteLogF(L"Installed Modrinth mod %s", destination.c_str());
        return true;
    } catch (const winrt::hresult_error& ex) {
        error = L"Could not parse Modrinth version response";
        WriteLogF(L"Modrinth version parse failed hr=0x%08X msg=%s",
            static_cast<unsigned int>(ex.code()), ex.message().c_str());
        return false;
    }
}

static bool InstallModrinthProject(
    const ModCard& card,
    const std::wstring& runtimeRoot,
    const std::wstring& userModsDir,
    std::vector<std::wstring>& installed,
    std::wstring& error) {
    std::set<std::wstring> visited;
    const std::wstring id = !card.projectId.empty() ? card.projectId : card.slug;
    return InstallModrinthProjectRecursive(id, runtimeRoot, userModsDir, visited, installed, &card, error);
}

// mods that are known to break this runtime; matched case-insensitively as a
// substring of the jar filename. essential's transformer crashes on launch.
static const wchar_t* kModpackBlockers[] = {
    L"essential",
};

static std::wstring ToLowerW(std::wstring v) {
    std::transform(v.begin(), v.end(), v.begin(), [](wchar_t c) { return static_cast<wchar_t>(towlower(c)); });
    return v;
}

static bool IsBlockedModFile(const std::wstring& fileName) {
    const std::wstring lower = ToLowerW(fileName);
    for (const wchar_t* b : kModpackBlockers) {
        if (lower.find(ToLowerW(b)) != std::wstring::npos) return true;
    }
    return false;
}

static bool WriteAllBytes(const std::wstring& path, const void* data, size_t size) {
    EnsureDirectoryTree(GetParentDir(path));
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    if (size) f.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size));
    return f.good();
}

static bool ResolveModpackMrpack(const std::wstring& idOrSlug, std::wstring& url, std::wstring& filename, std::string& sha1, unsigned long long& size, std::wstring& error) {
    using namespace winrt::Windows::Data::Json;
    const std::string project = w2a(idOrSlug);
    const std::string loaders = FormUrlEncode("[\"fabric\"]");
    const std::string versions = FormUrlEncode("[\"" + std::string(kMinecraftVersion) + "\"]");
    const std::wstring vurl =
        L"https://api.modrinth.com/v2/project/" + a2w(FormUrlEncode(project).c_str()) +
        L"/version?loaders=" + a2w(loaders.c_str()) +
        L"&game_versions=" + a2w(versions.c_str()) +
        L"&include_changelog=false";
    const HttpResult response = HttpGetString(vurl.c_str());
    if (!response.success()) {
        error = L"Modpack version lookup failed HTTP " + std::to_wstring(response.status);
        return false;
    }
    try {
        JsonArray arr = JsonArray::Parse(winrt::to_hstring(response.body));
        if (arr.Size() == 0) {
            error = L"No Fabric " + a2w(kMinecraftVersion) + L" build of this pack";
            return false;
        }
        JsonObject version = nullptr;
        for (uint32_t i = 0; i < arr.Size(); ++i) {
            auto v = arr.GetAt(i);
            if (v.ValueType() != JsonValueType::Object) continue;
            JsonObject c = v.GetObject();
            if (JsonStringOrEmpty(c, L"version_type") == L"release") { version = c; break; }
            if (!version) version = c;
        }
        if (!version) { error = L"No installable pack version found"; return false; }
        if (!ExtractPrimaryModrinthFile(version, url, filename, sha1, size)) {
            error = L"Pack version had no downloadable file";
            return false;
        }
        return true;
    } catch (const winrt::hresult_error&) {
        error = L"Could not parse pack version response";
        return false;
    }
}

static std::wstring ModpackDestForRelative(const std::wstring& relRaw, const std::wstring& gameDir, const std::wstring& userModsDir) {
    std::wstring rel = relRaw;
    std::replace(rel.begin(), rel.end(), L'/', L'\\');
    const std::wstring lower = ToLowerW(rel);
    const size_t slash = rel.find_last_of(L'\\');
    const std::wstring base = slash == std::wstring::npos ? rel : rel.substr(slash + 1);
    if (lower.rfind(L"mods\\", 0) == 0) {
        return userModsDir + L"\\" + base;
    }
    return gameDir + L"\\" + rel;
}

static bool InstallModpack(const ModCard& card, const std::wstring& runtimeRoot, const std::wstring& userModsDir, std::wstring& error) {
    using namespace winrt::Windows::Data::Json;
    const std::wstring gameDir = runtimeRoot + L"\\game";
    const std::wstring idOrSlug = !card.projectId.empty() ? card.projectId : card.slug;

    SetInstallStatus(L"Resolving " + card.title + L"...");
    std::wstring mrUrl, mrName;
    std::string mrSha1;
    unsigned long long mrSize = 0;
    if (!ResolveModpackMrpack(idOrSlug, mrUrl, mrName, mrSha1, mrSize, error)) return false;

    const std::wstring cacheDir = runtimeRoot + L"\\.modpack-cache";
    EnsureDirectoryTree(cacheDir);
    const std::wstring mrPath = cacheDir + L"\\" + SafeFileName(mrName);
    DeleteFileW(mrPath.c_str());
    if (!DownloadUrlToFile(mrUrl, mrPath, MakeInstallProgress(L"Downloading " + card.title, mrSize))) { error = L"Pack download failed"; return false; }
    if (!mrSha1.empty() && !FileMatchesSha1(mrPath, mrSha1)) {
        DeleteFileW(mrPath.c_str());
        error = L"Pack verification failed";
        return false;
    }

    std::vector<unsigned char> packBytes;
    {
        std::ifstream f(mrPath, std::ios::binary | std::ios::ate);
        if (!f) { error = L"Could not open downloaded pack"; return false; }
        const std::streamoff sz = f.tellg();
        f.seekg(0);
        if (sz > 0) {
            packBytes.resize(static_cast<size_t>(sz));
            f.read(reinterpret_cast<char*>(packBytes.data()), sz);
        }
    }
    if (packBytes.empty()) { error = L"Downloaded pack was empty"; return false; }

    mz_zip_archive zip{};
    if (!mz_zip_reader_init_mem(&zip, packBytes.data(), packBytes.size(), 0)) {
        error = L"Pack is not a valid .mrpack archive";
        return false;
    }

    std::string indexJson;
    {
        const int idx = mz_zip_reader_locate_file(&zip, "modrinth.index.json", nullptr, 0);
        if (idx < 0) { mz_zip_reader_end(&zip); error = L"Pack is missing modrinth.index.json"; return false; }
        size_t outSize = 0;
        void* p = mz_zip_reader_extract_to_heap(&zip, static_cast<mz_uint>(idx), &outSize, 0);
        if (!p) { mz_zip_reader_end(&zip); error = L"Could not read pack index"; return false; }
        indexJson.assign(static_cast<const char*>(p), outSize);
        mz_free(p);
    }

    struct PackFile { std::wstring path; std::wstring url; std::string sha1; unsigned long long size = 0; };
    std::vector<PackFile> jobs;
    int skipped = 0;
    try {
        JsonObject root = JsonObject::Parse(winrt::to_hstring(indexJson));
        JsonArray files{ nullptr };
        if (root.HasKey(L"files") && root.GetNamedValue(L"files").ValueType() == JsonValueType::Array) {
            files = root.GetNamedArray(L"files");
        }
        const uint32_t fcount = files ? files.Size() : 0;
        for (uint32_t i = 0; i < fcount; ++i) {
            auto v = files.GetAt(i);
            if (v.ValueType() != JsonValueType::Object) continue;
            JsonObject fo = v.GetObject();
            if (fo.HasKey(L"env") && fo.GetNamedValue(L"env").ValueType() == JsonValueType::Object) {
                if (JsonStringOrEmpty(fo.GetNamedObject(L"env"), L"client") == L"unsupported") continue;
            }
            const std::wstring path = JsonStringOrEmpty(fo, L"path");
            if (path.empty()) continue;
            std::wstring durl;
            if (fo.HasKey(L"downloads") && fo.GetNamedValue(L"downloads").ValueType() == JsonValueType::Array) {
                JsonArray dls = fo.GetNamedArray(L"downloads");
                if (dls.Size() > 0 && dls.GetAt(0).ValueType() == JsonValueType::String) {
                    durl = dls.GetAt(0).GetString().c_str();
                }
            }
            if (durl.empty()) continue;
            std::string sha1;
            if (fo.HasKey(L"hashes") && fo.GetNamedValue(L"hashes").ValueType() == JsonValueType::Object) {
                sha1 = w2a(JsonStringOrEmpty(fo.GetNamedObject(L"hashes"), L"sha1"));
            }
            unsigned long long fsize = 0;
            if (fo.HasKey(L"fileSize") && fo.GetNamedValue(L"fileSize").ValueType() == JsonValueType::Number) {
                fsize = static_cast<unsigned long long>(fo.GetNamedNumber(L"fileSize"));
            }
            const size_t slash = path.find_last_of(L"/\\");
            const std::wstring base = slash == std::wstring::npos ? path : path.substr(slash + 1);
            if (IsBlockedModFile(base)) { ++skipped; continue; }
            jobs.push_back({ path, durl, sha1, fsize });
        }
    } catch (const winrt::hresult_error&) {
        mz_zip_reader_end(&zip);
        error = L"Could not parse pack index";
        return false;
    }

    g_installTotal.store(static_cast<int>(jobs.size()));
    g_installDone.store(0);
    std::wstring firstError;
    int done = 0;
    for (const PackFile& job : jobs) {
        const std::wstring dest = ModpackDestForRelative(job.path, gameDir, userModsDir);
        const size_t bslash = job.path.find_last_of(L"/\\");
        const std::wstring base = bslash == std::wstring::npos ? job.path : job.path.substr(bslash + 1);
        const std::wstring label = std::to_wstring(done + 1) + L" of " + std::to_wstring(jobs.size()) + L"  " + base;
        SetInstallStatus(label);
        if (!job.sha1.empty() && FileMatchesSha1(dest, job.sha1)) { ++done; g_installDone.store(done); continue; }
        EnsureDirectoryTree(GetParentDir(dest));
        const std::wstring tmp = dest + L".download";
        DeleteFileW(tmp.c_str());
        if (DownloadUrlToFile(job.url, tmp, MakeInstallProgress(label, job.size)) && (job.sha1.empty() || FileMatchesSha1(tmp, job.sha1))) {
            DeleteFileW(dest.c_str());
            if (!MoveFileExW(tmp.c_str(), dest.c_str(), MOVEFILE_REPLACE_EXISTING)) {
                DeleteFileW(tmp.c_str());
                if (firstError.empty()) firstError = L"Some pack files could not be saved";
            }
        } else {
            DeleteFileW(tmp.c_str());
            if (firstError.empty()) firstError = L"Some pack files failed to download";
        }
        ++done;
        g_installDone.store(done);
    }

    SetInstallStatus(L"Applying overrides...");
    const mz_uint entryCount = mz_zip_reader_get_num_files(&zip);
    for (mz_uint i = 0; i < entryCount; ++i) {
        if (mz_zip_reader_is_file_a_directory(&zip, i)) continue;
        mz_zip_archive_file_stat st;
        if (!mz_zip_reader_file_stat(&zip, i, &st)) continue;
        std::string name = st.m_filename;
        std::string prefix;
        if (name.rfind("overrides/", 0) == 0) prefix = "overrides/";
        else if (name.rfind("client-overrides/", 0) == 0) prefix = "client-overrides/";
        else continue;
        const std::string relA = name.substr(prefix.size());
        if (relA.empty()) continue;
        const std::wstring rel = a2w(relA.c_str());
        const std::wstring dest = ModpackDestForRelative(rel, gameDir, userModsDir);
        const size_t slash = dest.find_last_of(L'\\');
        const std::wstring base = slash == std::wstring::npos ? dest : dest.substr(slash + 1);
        if (ToLowerW(dest).find(ToLowerW(userModsDir)) == 0 && IsBlockedModFile(base)) { ++skipped; continue; }
        size_t outSize = 0;
        void* p = mz_zip_reader_extract_to_heap(&zip, i, &outSize, 0);
        if (!p) continue;
        WriteAllBytes(dest, p, outSize);
        mz_free(p);
    }

    mz_zip_reader_end(&zip);
    DeleteFileW(mrPath.c_str());

    {
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileW((userModsDir + L"\\*.jar").c_str(), &fd);
        if (h != INVALID_HANDLE_VALUE) {
            do {
                if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
                if (IsBlockedModFile(fd.cFileName)) {
                    DeleteFileW((userModsDir + L"\\" + fd.cFileName).c_str());
                    DeleteFileW(ModMetaPath(runtimeRoot, fd.cFileName).c_str());
                    ++skipped;
                }
            } while (FindNextFileW(h, &fd));
            FindClose(h);
        }
    }

    WriteLogF(L"Modpack install done: %d files, %d blocked", done, skipped);
    if (jobs.empty() && firstError.empty()) { error = L"Pack had no installable client files"; return false; }
    if (!firstError.empty()) { error = firstError; return false; }
    return true;
}

static void CleanInlineMd(const std::wstring& s, std::wstring& out,
                          std::vector<std::pair<UINT32, UINT32>>& bold,
                          bool& boldOpen, UINT32& boldStart) {
    const size_t npos = std::wstring::npos;
    size_t i = 0;
    while (i < s.size()) {
        const wchar_t c = s[i];
        if (c == L'<') { const size_t e = s.find(L'>', i); if (e == npos) break; i = e + 1; continue; }
        if (c == L'!' && i + 1 < s.size() && s[i + 1] == L'[') {
            const size_t rb = s.find(L']', i + 2);
            if (rb != npos) {
                size_t j = rb + 1;
                if (j < s.size() && s[j] == L'(') { const size_t rp = s.find(L')', j); if (rp != npos) j = rp + 1; }
                i = j; continue;
            }
        }
        if (c == L'[') {
            const size_t rb = s.find(L']', i + 1);
            if (rb != npos) {
                size_t j = rb + 1; bool hasUrl = false; size_t rp = npos;
                if (j < s.size() && s[j] == L'(') { rp = s.find(L')', j); if (rp != npos) hasUrl = true; }
                CleanInlineMd(s.substr(i + 1, rb - (i + 1)), out, bold, boldOpen, boldStart);
                i = hasUrl ? rp + 1 : rb + 1; continue;
            }
        }
        if (c == L'`') { i++; continue; }
        if (c == L'~' && i + 1 < s.size() && s[i + 1] == L'~') { i += 2; continue; }
        if ((c == L'*' || c == L'_') && i + 1 < s.size() && s[i + 1] == c) {
            if (!boldOpen) { boldOpen = true; boldStart = static_cast<UINT32>(out.size()); }
            else { boldOpen = false; bold.push_back({ boldStart, static_cast<UINT32>(out.size()) - boldStart }); }
            i += 2; continue;
        }
        if (c == L'*' || c == L'_') { i++; continue; }
        out.push_back(c); i++;
    }
}

static std::wstring CleanMarkdown(const std::wstring& in,
                                  std::vector<std::pair<UINT32, UINT32>>& bold,
                                  std::vector<std::pair<UINT32, UINT32>>& head) {
    const size_t npos = std::wstring::npos;
    std::wstring src; src.reserve(in.size());
    for (wchar_t c : in) if (c != L'\r') src.push_back(c);

    std::wstring out;
    bool boldOpen = false; UINT32 boldStart = 0;
    int blankRun = 0;
    size_t pos = 0;
    while (true) {
        const size_t nl = src.find(L'\n', pos);
        std::wstring line = src.substr(pos, (nl == npos ? src.size() : nl) - pos);

        const size_t a = line.find_first_not_of(L" \t");
        std::wstring work = (a == npos) ? L"" : line.substr(a);

        bool isHr = false;
        if (work.size() >= 3) {
            const wchar_t h = work[0];
            if (h == L'-' || h == L'*' || h == L'_') {
                isHr = true;
                for (wchar_t ch : work) if (ch != h && ch != L' ') { isHr = false; break; }
            }
        }
        if (isHr) {
            out += L"\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\n";
            blankRun = 0;
            if (nl == npos) break; pos = nl + 1; continue;
        }

        bool heading = false;
        if (!work.empty() && work[0] == L'#') {
            size_t hc = 0; while (hc < work.size() && work[hc] == L'#') hc++;
            if (hc <= 6 && hc < work.size() && work[hc] == L' ') { heading = true; const size_t t = work.find_first_not_of(L" ", hc); work = (t == npos) ? L"" : work.substr(t); }
            else if (hc <= 6 && hc == work.size()) { work = L""; }
        }
        while (!work.empty() && work[0] == L'>') { work.erase(work.begin()); if (!work.empty() && work[0] == L' ') work.erase(work.begin()); }

        std::wstring prefix;
        if (work.size() >= 2 && (work[0] == L'-' || work[0] == L'*' || work[0] == L'+') && work[1] == L' ') { prefix = L"\u2022 "; work = work.substr(2); }

        if (work.empty() && prefix.empty()) {
            if (blankRun < 1) out += L"\n";
            blankRun++;
        } else {
            blankRun = 0;
            const UINT32 lineStart = static_cast<UINT32>(out.size());
            out += prefix;
            CleanInlineMd(work, out, bold, boldOpen, boldStart);
            if (heading) head.push_back({ lineStart, static_cast<UINT32>(out.size()) - lineStart });
            out += L"\n";
        }

        if (nl == npos) break;
        pos = nl + 1;
    }
    if (boldOpen) bold.push_back({ boldStart, static_cast<UINT32>(out.size()) - boldStart });
    while (!out.empty() && (out.back() == L'\n' || out.back() == L' ')) out.pop_back();
    return out;
}

static void FetchProjectDetail(const std::wstring& idOrSlug, std::wstring& body, std::wstring& meta, std::vector<std::pair<UINT32, UINT32>>& bold, std::vector<std::pair<UINT32, UINT32>>& head) {
    using namespace winrt::Windows::Data::Json;
    const std::wstring url = L"https://api.modrinth.com/v2/project/" + a2w(FormUrlEncode(w2a(idOrSlug)).c_str());
    const HttpResult response = HttpGetString(url.c_str());
    if (!response.success()) { body = L"Could not load description."; return; }
    try {
        JsonObject root = JsonObject::Parse(winrt::to_hstring(response.body));
        std::wstring cats;
        if (root.HasKey(L"categories") && root.GetNamedValue(L"categories").ValueType() == JsonValueType::Array) {
            JsonArray a = root.GetNamedArray(L"categories");
            for (uint32_t i = 0; i < a.Size() && i < 6; ++i) {
                if (a.GetAt(i).ValueType() != JsonValueType::String) continue;
                if (!cats.empty()) cats += L", ";
                cats += a.GetAt(i).GetString().c_str();
            }
        }
        const int downloads = JsonIntOrZero(root, L"downloads");
        meta = cats;
        if (!meta.empty()) meta += L"  -  ";
        meta += std::to_wstring(downloads) + L" downloads";
        std::wstring raw = JsonStringOrEmpty(root, L"body");
        if (raw.empty()) raw = JsonStringOrEmpty(root, L"description");
        if (raw.size() > 6000) raw.resize(6000);
        body = CleanMarkdown(raw, bold, head);
        if (body.empty()) body = L"No description provided.";
    } catch (const winrt::hresult_error&) {
        body = L"Could not parse description.";
    }
}

static std::atomic<unsigned> g_detailReqId{0};
static std::atomic<bool> g_detailFetching{false};
static std::mutex g_detailMutex;
static unsigned g_detailReadyId = 0;
static std::wstring g_detailBody;
static std::wstring g_detailMeta;
static std::vector<std::pair<UINT32, UINT32>> g_detailBold;
static std::vector<std::pair<UINT32, UINT32>> g_detailHead;
static std::atomic<int> g_detailMaxScroll{0};
static std::atomic<int> g_profileMaxScroll{0};
static std::atomic<int> g_profileRowsVisible{1};

static unsigned StartDetailFetch(const ModCard& card) {
    const unsigned id = ++g_detailReqId;
    const std::wstring idOrSlug = !card.projectId.empty() ? card.projectId : card.slug;
    g_detailFetching.store(true);
    std::thread([id, idOrSlug]() {
        std::wstring body, meta;
        std::vector<std::pair<UINT32, UINT32>> bold, head;
        FetchProjectDetail(idOrSlug, body, meta, bold, head);
        std::lock_guard<std::mutex> lk(g_detailMutex);
        if (id == g_detailReqId.load()) {
            g_detailBody = body;
            g_detailMeta = meta;
            g_detailBold = bold;
            g_detailHead = head;
            g_detailReadyId = id;
        }
        g_detailFetching.store(false);
    }).detach();
    return id;
}

static const wchar_t* kVanillaProfileId = L"vanilla";

struct Profile {
    std::wstring id;
    std::wstring name;
    bool builtin = false;
};

static std::wstring ProfilesRoot(const std::wstring& runtimeRoot) { return runtimeRoot + L"\\profiles"; }
static std::wstring ProfileDir(const std::wstring& runtimeRoot, const std::wstring& id) { return ProfilesRoot(runtimeRoot) + L"\\" + id; }
static std::wstring ProfileModsDir(const std::wstring& runtimeRoot, const std::wstring& id) { return ProfileDir(runtimeRoot, id) + L"\\mods"; }
static std::wstring ProfilesManifestPath(const std::wstring& runtimeRoot) { return ProfilesRoot(runtimeRoot) + L"\\profiles.txt"; }
static std::wstring ActiveProfilePath(const std::wstring& runtimeRoot) { return ProfilesRoot(runtimeRoot) + L"\\active.txt"; }

static std::vector<Profile> LoadProfiles(const std::wstring& runtimeRoot) {
    std::vector<Profile> out;
    out.push_back({ kVanillaProfileId, L"Vanilla", true });
    std::ifstream f(ProfilesManifestPath(runtimeRoot), std::ios::binary);
    std::string line;
    while (f && std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        const size_t tab = line.find('\t');
        const std::wstring id = a2w((tab == std::string::npos ? line : line.substr(0, tab)).c_str());
        const std::wstring name = tab == std::string::npos ? id : a2w(line.substr(tab + 1).c_str());
        if (id.empty() || id == kVanillaProfileId) continue;
        out.push_back({ id, name.empty() ? id : name, false });
    }
    return out;
}

static void SaveProfiles(const std::wstring& runtimeRoot, const std::vector<Profile>& profiles) {
    EnsureDirectoryTree(ProfilesRoot(runtimeRoot));
    std::ofstream f(ProfilesManifestPath(runtimeRoot), std::ios::binary | std::ios::trunc);
    if (!f) return;
    for (const auto& p : profiles) {
        if (p.builtin || p.id == kVanillaProfileId) continue;
        const std::string body = w2a(p.id) + "\t" + w2a(StripNewlines(p.name)) + "\n";
        f.write(body.data(), static_cast<std::streamsize>(body.size()));
    }
}

static std::wstring GetActiveProfileId(const std::wstring& runtimeRoot) {
    std::ifstream f(ActiveProfilePath(runtimeRoot), std::ios::binary);
    std::string line;
    if (f && std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const std::wstring id = a2w(line.c_str());
        if (!id.empty()) {
            if (id == kVanillaProfileId) return id;
            if (GetFileAttributesW(ProfileDir(runtimeRoot, id).c_str()) != INVALID_FILE_ATTRIBUTES) return id;
        }
    }
    return kVanillaProfileId;
}

static void SetActiveProfileId(const std::wstring& runtimeRoot, const std::wstring& id) {
    EnsureDirectoryTree(ProfilesRoot(runtimeRoot));
    std::ofstream f(ActiveProfilePath(runtimeRoot), std::ios::binary | std::ios::trunc);
    if (f) { const std::string s = w2a(id); f.write(s.data(), static_cast<std::streamsize>(s.size())); }
}

static int ProfileModCount(const std::wstring& runtimeRoot, const std::wstring& id) {
    int n = 0;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((ProfileModsDir(runtimeRoot, id) + L"\\*.jar").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do { if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) ++n; } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    return n;
}

static std::wstring MakeProfileId(const std::wstring& runtimeRoot, const std::wstring& name) {
    std::wstring base = SafeFileName(name);
    if (base.empty()) base = L"profile";
    std::wstring id = base;
    int n = 2;
    while (id == kVanillaProfileId || GetFileAttributesW(ProfileDir(runtimeRoot, id).c_str()) != INVALID_FILE_ATTRIBUTES) {
        id = base + L"-" + std::to_wstring(n++);
    }
    return id;
}

static std::wstring CreateProfile(const std::wstring& runtimeRoot, const std::wstring& name) {
    std::vector<Profile> profiles = LoadProfiles(runtimeRoot);
    const std::wstring id = MakeProfileId(runtimeRoot, name);
    EnsureDirectoryTree(ProfileModsDir(runtimeRoot, id));
    profiles.push_back({ id, name.empty() ? id : StripNewlines(name), false });
    SaveProfiles(runtimeRoot, profiles);
    return id;
}

static std::wstring CreateAutoProfile(const std::wstring& runtimeRoot) {
    int n = 1;
    for (const Profile& p : LoadProfiles(runtimeRoot)) if (!p.builtin) ++n;
    return CreateProfile(runtimeRoot, L"Profile " + std::to_wstring(n));
}

static void DeleteProfile(const std::wstring& runtimeRoot, const std::wstring& id) {
    if (id.empty() || id == kVanillaProfileId) return;
    DeleteDirectoryTree(ProfileDir(runtimeRoot, id));
    std::vector<Profile> kept;
    for (const Profile& p : LoadProfiles(runtimeRoot)) if (p.id != id) kept.push_back(p);
    SaveProfiles(runtimeRoot, kept);
    if (GetActiveProfileId(runtimeRoot) == id) SetActiveProfileId(runtimeRoot, kVanillaProfileId);
}

static void RenameProfile(const std::wstring& runtimeRoot, const std::wstring& id, const std::wstring& newName) {
    if (id.empty() || id == kVanillaProfileId) return;
    std::vector<Profile> profiles = LoadProfiles(runtimeRoot);
    for (auto& p : profiles) if (p.id == id && !p.builtin) p.name = StripNewlines(newName);
    SaveProfiles(runtimeRoot, profiles);
}

static void EnsureProfilesInitialized(const std::wstring& runtimeRoot) {
    EnsureDirectoryTree(ProfileModsDir(runtimeRoot, kVanillaProfileId));
    if (GetFileAttributesW(ProfilesManifestPath(runtimeRoot).c_str()) != INVALID_FILE_ATTRIBUTES) return;

    const std::wstring legacy = runtimeRoot + L"\\game\\user-mods";
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((legacy + L"\\*.jar").c_str(), &fd);
    const bool hasLegacy = h != INVALID_HANDLE_VALUE;

    std::vector<Profile> profiles;
    profiles.push_back({ kVanillaProfileId, L"Vanilla", true });
    if (hasLegacy) {
        const std::wstring id = L"default";
        EnsureDirectoryTree(ProfileModsDir(runtimeRoot, id));
        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            MoveFileExW((legacy + L"\\" + fd.cFileName).c_str(),
                (ProfileModsDir(runtimeRoot, id) + L"\\" + fd.cFileName).c_str(), MOVEFILE_REPLACE_EXISTING);
        } while (FindNextFileW(h, &fd));
        FindClose(h);
        profiles.push_back({ id, L"Default", false });
        SaveProfiles(runtimeRoot, profiles);
        SetActiveProfileId(runtimeRoot, id);
    } else {
        SaveProfiles(runtimeRoot, profiles);
        SetActiveProfileId(runtimeRoot, kVanillaProfileId);
    }
}

static std::vector<std::wstring> ListProfileMods(const std::wstring& runtimeRoot, const std::wstring& id) {
    std::vector<std::wstring> out;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((ProfileModsDir(runtimeRoot, id) + L"\\*.jar").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do { if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) out.push_back(fd.cFileName); } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    std::sort(out.begin(), out.end());
    return out;
}

static std::wstring ProfileDisplayName(const std::wstring& runtimeRoot, const std::wstring& id) {
    for (const Profile& p : LoadProfiles(runtimeRoot)) if (p.id == id) return p.name;
    return id;
}
static void StartInstallJob(const ModCard& card, const std::wstring& runtimeRoot) {
    if (g_installRunning.load()) return;
    g_installRunning.store(true);
    g_installResultReady.store(false);
    g_installTotal.store(0);
    g_installDone.store(0);
    SetInstallStatus(L"Starting " + card.title + L"...");
    ModCard copy = card;
    std::wstring rootCopy = runtimeRoot;
    std::thread([copy, rootCopy]() {
        std::wstring err;
        bool ok;
        if (copy.isModpack) {
            const std::wstring pid = CreateProfile(rootCopy, copy.title);
            ok = InstallModpack(copy, rootCopy, ProfileModsDir(rootCopy, pid), err);
            if (ok) {
                SetActiveProfileId(rootCopy, pid);
                SetInstallStatus(L"Installed profile " + copy.title);
            } else {
                DeleteProfile(rootCopy, pid);
                SetInstallStatus(err.empty() ? L"Modpack install failed" : err);
            }
        } else {
            const std::wstring active = GetActiveProfileId(rootCopy);
            if (active == kVanillaProfileId) {
                ok = false;
                SetInstallStatus(L"Vanilla is read-only. Pick or make a profile first.");
            } else {
                std::vector<std::wstring> installed;
                ok = InstallModrinthProject(copy, rootCopy, ProfileModsDir(rootCopy, active), installed, err);
                SetInstallStatus(ok
                    ? (installed.empty() ? L"Already installed" : L"Installed " + std::to_wstring(installed.size()) + L" file(s)")
                    : (err.empty() ? L"Install failed" : err));
            }
        }
        g_installResultOk.store(ok);
        g_installRunning.store(false);
        g_installResultReady.store(true);
    }).detach();
}

static bool SaveRefreshToken(const std::string& refreshToken) {
    if (refreshToken.empty()) return false;
    try {
        winrt::Windows::Security::Credentials::PasswordVault vault;
        try {
            auto existing = vault.Retrieve(kRefreshTokenResource, kRefreshTokenUser);
            vault.Remove(existing);
        } catch (...) {
        }
        vault.Add(winrt::Windows::Security::Credentials::PasswordCredential(
            kRefreshTokenResource,
            kRefreshTokenUser,
            winrt::to_hstring(refreshToken)));
        WriteLog(L"Saved Microsoft refresh token to Credential Locker");
        return true;
    } catch (const winrt::hresult_error& ex) {
        WriteLogF(L"Failed to save refresh token hr=0x%08X msg=%s",
            static_cast<unsigned int>(ex.code()), ex.message().c_str());
        return false;
    }
}

static std::string LoadRefreshToken() {
    try {
        winrt::Windows::Security::Credentials::PasswordVault vault;
        auto credential = vault.Retrieve(kRefreshTokenResource, kRefreshTokenUser);
        credential.RetrievePassword();
        return winrt::to_string(credential.Password());
    } catch (...) {
        return {};
    }
}

static void ClearRefreshToken() {
    try {
        winrt::Windows::Security::Credentials::PasswordVault vault;
        auto credential = vault.Retrieve(kRefreshTokenResource, kRefreshTokenUser);
        vault.Remove(credential);
    } catch (...) {
    }
}

struct DeviceCodeResponse {
    std::string userCode;
    std::string deviceCode;
    std::string verificationUri;
    int expiresIn = 900;
    int interval = 5;
};

struct MicrosoftTokenResponse {
    std::string accessToken;
    std::string refreshToken;
    int expiresIn = 0;
};

struct XboxAuthResponse {
    std::string token;
    std::string userHash;
};

enum class DevicePollStatus {
    Pending,
    SlowDown,
    Success,
    Failed
};

struct DevicePollResult {
    DevicePollStatus status = DevicePollStatus::Failed;
    MicrosoftTokenResponse token;
    std::string error;
};

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
    bool showModsPage = false;
    int selectedMenuIndex = 0;
    int selectedModsTab = 0;
    int selectedModIndex = 0;
    int modsFocus = 0;
    int modsScrollRow = 0;
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
    bool modsProfileOpen = false;
    std::wstring modsProfileId;
    std::wstring modsProfileName;
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
};

static void ProcessAuthUiEvents();

class AuthScreenRenderer {
public:
    bool Initialize(ICoreWindow* window) {
        if (!window) return false;
        WriteLog(L"Auth screen Initialize started");
        window_ = window;

        Rect bounds = {};
        if (FAILED(window->get_Bounds(&bounds))) {
            bounds.Width = 1280;
            bounds.Height = 720;
        }
        width_ = bounds.Width > 0 ? bounds.Width : 1280;
        height_ = bounds.Height > 0 ? bounds.Height : 720;

        const UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
        D3D_FEATURE_LEVEL levels[] = {
            D3D_FEATURE_LEVEL_11_1,
            D3D_FEATURE_LEVEL_11_0,
            D3D_FEATURE_LEVEL_10_1,
            D3D_FEATURE_LEVEL_10_0
        };
        D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
        HRESULT hr = D3D11CreateDevice(
            nullptr,
            D3D_DRIVER_TYPE_HARDWARE,
            nullptr,
            flags,
            levels,
            ARRAYSIZE(levels),
            D3D11_SDK_VERSION,
            d3dDevice_.GetAddressOf(),
            &level,
            d3dContext_.GetAddressOf());
        if (FAILED(hr)) {
            WriteLogF(L"Auth screen D3D11CreateDevice hardware failed hr=0x%08X; trying WARP", hr);
            hr = D3D11CreateDevice(
                nullptr,
                D3D_DRIVER_TYPE_WARP,
                nullptr,
                flags,
                levels,
                ARRAYSIZE(levels),
                D3D11_SDK_VERSION,
                d3dDevice_.ReleaseAndGetAddressOf(),
                &level,
                d3dContext_.ReleaseAndGetAddressOf());
            if (FAILED(hr)) {
                WriteLogF(L"Auth screen D3D11CreateDevice failed hr=0x%08X", hr);
                return false;
            }
        }

        hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, d2dFactory_.GetAddressOf());
        if (FAILED(hr)) {
            WriteLogF(L"Auth screen D2D factory failed hr=0x%08X", hr);
            return false;
        }

        ComPtr<IDXGIDevice> dxgiDevice;
        hr = d3dDevice_.As(&dxgiDevice);
        if (FAILED(hr)) {
            WriteLogF(L"Auth screen IDXGIDevice query failed hr=0x%08X", hr);
            return false;
        }

        hr = d2dFactory_->CreateDevice(dxgiDevice.Get(), d2dDevice_.GetAddressOf());
        if (FAILED(hr)) {
            WriteLogF(L"Auth screen D2D device failed hr=0x%08X", hr);
            return false;
        }

        hr = d2dDevice_->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, d2dContext_.GetAddressOf());
        if (FAILED(hr)) {
            WriteLogF(L"Auth screen D2D context failed hr=0x%08X", hr);
            return false;
        }

        ComPtr<IDXGIAdapter> adapter;
        hr = dxgiDevice->GetAdapter(adapter.GetAddressOf());
        if (FAILED(hr)) {
            WriteLogF(L"Auth screen DXGI adapter failed hr=0x%08X", hr);
            return false;
        }

        ComPtr<IDXGIFactory2> dxgiFactory;
        hr = adapter->GetParent(IID_PPV_ARGS(dxgiFactory.GetAddressOf()));
        if (FAILED(hr)) {
            WriteLogF(L"Auth screen DXGI factory failed hr=0x%08X", hr);
            return false;
        }

        DXGI_SWAP_CHAIN_DESC1 desc = {};
        desc.Width = static_cast<UINT>(width_);
        desc.Height = static_cast<UINT>(height_);
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.Stereo = FALSE;
        desc.SampleDesc.Count = 1;
        desc.SampleDesc.Quality = 0;
        desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.BufferCount = 2;
        desc.Scaling = DXGI_SCALING_STRETCH;
        desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
        desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;

        hr = dxgiFactory->CreateSwapChainForCoreWindow(
            d3dDevice_.Get(),
            reinterpret_cast<IUnknown*>(window),
            &desc,
            nullptr,
            swapChain_.GetAddressOf());
        if (FAILED(hr)) {
            WriteLogF(L"Auth screen swap chain failed hr=0x%08X", hr);
            return false;
        }

        ComPtr<IDXGISurface> backBuffer;
        hr = swapChain_->GetBuffer(0, IID_PPV_ARGS(backBuffer.GetAddressOf()));
        if (FAILED(hr)) {
            WriteLogF(L"Auth screen back buffer failed hr=0x%08X", hr);
            return false;
        }

        D2D1_BITMAP_PROPERTIES1 props = D2D1::BitmapProperties1(
            D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE),
            96.0f,
            96.0f);
        hr = d2dContext_->CreateBitmapFromDxgiSurface(backBuffer.Get(), &props, targetBitmap_.GetAddressOf());
        if (FAILED(hr)) {
            WriteLogF(L"Auth screen target bitmap failed hr=0x%08X", hr);
            return false;
        }
        d2dContext_->SetTarget(targetBitmap_.Get());

        hr = DWriteCreateFactory(
            DWRITE_FACTORY_TYPE_SHARED,
            __uuidof(IDWriteFactory),
            reinterpret_cast<IUnknown**>(dwriteFactory_.GetAddressOf()));
        if (FAILED(hr)) {
            WriteLogF(L"Auth screen DWrite factory failed hr=0x%08X", hr);
            return false;
        }

        hr = CoCreateInstance(
            CLSID_WICImagingFactory,
            nullptr,
            CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(wicFactory_.GetAddressOf()));
        if (FAILED(hr)) {
            WriteLogF(L"Auth screen WIC factory failed hr=0x%08X", hr);
        }

        CreateTextFormats();
        WriteLogF(L"Auth screen initialized %.0fx%.0f featureLevel=0x%X",
            width_, height_, static_cast<unsigned int>(level));
        return true;
    }

    void Render(const AuthUiState& state) {
        if (!d2dContext_ || !swapChain_) return;

        ComPtr<ID2D1SolidColorBrush> white;
        ComPtr<ID2D1SolidColorBrush> muted;
        ComPtr<ID2D1SolidColorBrush> panel;
        ComPtr<ID2D1SolidColorBrush> accent;
        ComPtr<ID2D1SolidColorBrush> danger;
        ComPtr<ID2D1SolidColorBrush> black;
        ComPtr<ID2D1SolidColorBrush> softEdge;
        ComPtr<ID2D1SolidColorBrush> surfaceFill;
        ComPtr<ID2D1SolidColorBrush> accentSoft;

        d2dContext_->CreateSolidColorBrush(D2D1::ColorF(0xF5F7F8), white.GetAddressOf());
        d2dContext_->CreateSolidColorBrush(D2D1::ColorF(0xA9B0B4), muted.GetAddressOf());
        d2dContext_->CreateSolidColorBrush(D2D1::ColorF(0x151718), panel.GetAddressOf());
        d2dContext_->CreateSolidColorBrush(D2D1::ColorF(0x70C486), accent.GetAddressOf());
        d2dContext_->CreateSolidColorBrush(D2D1::ColorF(0xE36A5C), danger.GetAddressOf());
        d2dContext_->CreateSolidColorBrush(D2D1::ColorF(0x000000), black.GetAddressOf());
        d2dContext_->CreateSolidColorBrush(D2D1::ColorF(0xFFFFFF, 0.12f), softEdge.GetAddressOf());
        d2dContext_->CreateSolidColorBrush(D2D1::ColorF(0x161B1F, 0.75f), surfaceFill.GetAddressOf());
        d2dContext_->CreateSolidColorBrush(D2D1::ColorF(0x70C486, 0.14f), accentSoft.GetAddressOf());

        d2dContext_->BeginDraw();
        d2dContext_->Clear(D2D1::ColorF(0x05080B));
        FillVerticalGradient(D2D1::RectF(0.0f, 0.0f, width_, height_), 0x0E1726, 0x05080B);

        const float marginX = width_ * 0.075f;
        const float marginY = height_ * 0.11f;
        const D2D1_RECT_F frame = D2D1::RectF(marginX, marginY, width_ - marginX, height_ - marginY);
        {
            ComPtr<ID2D1SolidColorBrush> surface, surfaceEdge;
            d2dContext_->CreateSolidColorBrush(D2D1::ColorF(0x0C0F12, 0.92f), surface.GetAddressOf());
            d2dContext_->CreateSolidColorBrush(D2D1::ColorF(0xFFFFFF, 0.10f), surfaceEdge.GetAddressOf());
            FillRound(frame, surface.Get(), 22.0f);
            StrokeRound(frame, surfaceEdge.Get(), 22.0f, 1.5f);
        }

        auto finishDraw = [&]() {
            HRESULT hr = d2dContext_->EndDraw();
            if (FAILED(hr)) {
                WriteLogF(L"Auth screen EndDraw failed hr=0x%08X", hr);
            }
            hr = swapChain_->Present(1, 0);
            if (FAILED(hr)) {
                WriteLogF(L"Auth screen Present failed hr=0x%08X", hr);
            }
            ProcessAuthUiEvents();
        };

        const std::wstring title = state.title.empty() ? L"Microsoft sign-in" : state.title;
        if (state.showModsPage) {
            if (state.modsDetailOpen) {
                const ModCard& card = state.modsDetailCard;
                const float left = frame.left + 40.0f;
                const float right = frame.right - 40.0f;
                const float top = frame.top + 34.0f;

                const float iconSide = 132.0f;
                const D2D1_RECT_F iconRect = D2D1::RectF(left, top, left + iconSide, top + iconSide);
                ComPtr<ID2D1Bitmap1> icon = GetCachedBitmap(card.iconPath);
                if (icon) {
                    DrawBitmapCover(icon.Get(), iconRect, 1.0f, 1.0f, 0.0f, 0.0f);
                    StrokeRound(iconRect, softEdge.Get(), 14.0f, 1.0f);
                } else {
                    ComPtr<ID2D1SolidColorBrush> ph;
                    d2dContext_->CreateSolidColorBrush(D2D1::ColorF(0x05080B), ph.GetAddressOf());
                    FillRound(iconRect, ph.Get(), 14.0f);
                    StrokeRound(iconRect, softEdge.Get(), 14.0f, 1.0f);
                    DrawIcon(card.isModpack ? L"\uE7B8" : L"\uE74C", iconRect, muted.Get(), true);
                }

                const float headLeft = iconRect.right + 24.0f;
                DrawText(card.title.c_str(), titleFormat_.Get(),
                    D2D1::RectF(headLeft, top, right, top + 48.0f), white.Get());
                DrawText(card.isModpack ? L"Modpack" : L"Mod", captionFormat_.Get(),
                    D2D1::RectF(headLeft, top + 50.0f, headLeft + 200.0f, top + 74.0f), accent.Get());
                const std::wstring metaLine = !state.modsDetailMeta.empty() ? state.modsDetailMeta : card.status;
                DrawText(metaLine.c_str(), smallFormat_.Get(),
                    D2D1::RectF(headLeft, top + 76.0f, right, top + 102.0f), muted.Get());
                DrawText(card.description.c_str(), smallFormat_.Get(),
                    D2D1::RectF(headLeft, top + 104.0f, right, top + iconSide), muted.Get());

                const float btnW = 240.0f;
                const float btnH = 54.0f;
                const bool installing = g_installRunning.load();
                const D2D1_RECT_F installBtn = D2D1::RectF(right - btnW, iconRect.bottom + 16.0f, right, iconRect.bottom + 16.0f + btnH);
                if (!installing) GlowSelect(installBtn, 12.0f);
                FillRound(installBtn, installing ? panel.Get() : accent.Get(), 12.0f);
                StrokeRound(installBtn, accent.Get(), 12.0f, 2.0f);
                DrawIcon(installing ? L"\uE895" : L"\uE896", D2D1::RectF(installBtn.left + 18.0f, installBtn.top, installBtn.left + 46.0f, installBtn.bottom), installing ? muted.Get() : black.Get());
                DrawText(installing ? L"Installing..." : (card.isModpack ? L"Install pack" : L"Install"),
                    bodyMid_.Get(), D2D1::RectF(installBtn.left + 52.0f, installBtn.top, installBtn.right - 8.0f, installBtn.bottom), installing ? muted.Get() : black.Get());

                DrawIcon(L"\uE72B", D2D1::RectF(left, iconRect.bottom + 26.0f, left + 26.0f, iconRect.bottom + 26.0f + 30.0f), muted.Get());
                DrawText(L"Back", smallFormat_.Get(),
                    D2D1::RectF(left + 30.0f, iconRect.bottom + 30.0f, left + 220.0f, iconRect.bottom + 30.0f + 28.0f), muted.Get());

                if (!state.status.empty()) {
                    DrawText(state.status.c_str(), smallFormat_.Get(),
                        D2D1::RectF(left, installBtn.bottom + 6.0f, right, installBtn.bottom + 32.0f),
                        state.isError ? danger.Get() : muted.Get());
                }

                const float bodyTop = installBtn.bottom + 42.0f;
                const D2D1_RECT_F bodyRect = D2D1::RectF(left, bodyTop, right, frame.bottom - 30.0f);
                FillRound(bodyRect, surfaceFill.Get(), 16.0f);
                StrokeRound(bodyRect, softEdge.Get(), 16.0f, 1.0f);
                const float padb = 16.0f;
                const D2D1_RECT_F bodyInner = D2D1::RectF(bodyRect.left + padb, bodyRect.top + padb, bodyRect.right - padb, bodyRect.bottom - padb);
                const float lineStep = 34.0f;
                const float scrollPx = static_cast<float>(state.modsDetailScroll) * lineStep;

                const std::wstring bodyText = (state.modsDetailLoading || state.modsDetailBody.empty())
                    ? std::wstring(L"Loading description...")
                    : state.modsDetailBody;
                ComPtr<IDWriteTextLayout> layout;
                if (SUCCEEDED(dwriteFactory_->CreateTextLayout(
                        bodyText.c_str(), static_cast<UINT32>(bodyText.size()), smallFormat_.Get(),
                        bodyInner.right - bodyInner.left, 100000.0f, layout.GetAddressOf()))) {
                    if (!state.modsDetailLoading && !state.modsDetailBody.empty()) {
                        for (const auto& b : state.modsDetailBold) {
                            layout->SetFontWeight(DWRITE_FONT_WEIGHT_BOLD, DWRITE_TEXT_RANGE{ b.first, b.second });
                            layout->SetDrawingEffect(white.Get(), DWRITE_TEXT_RANGE{ b.first, b.second });
                        }
                        for (const auto& hh : state.modsDetailHead) {
                            layout->SetFontWeight(DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_RANGE{ hh.first, hh.second });
                            layout->SetFontSize(27.0f, DWRITE_TEXT_RANGE{ hh.first, hh.second });
                            layout->SetDrawingEffect(white.Get(), DWRITE_TEXT_RANGE{ hh.first, hh.second });
                        }
                    }
                    DWRITE_TEXT_METRICS tm{};
                    const float viewH = bodyInner.bottom - bodyInner.top;
                    if (SUCCEEDED(layout->GetMetrics(&tm))) {
                        const int maxScroll = tm.height > viewH ? static_cast<int>(ceilf((tm.height - viewH) / lineStep)) : 0;
                        g_detailMaxScroll.store(maxScroll);
                    }
                    d2dContext_->PushAxisAlignedClip(bodyInner, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
                    d2dContext_->DrawTextLayout(D2D1::Point2F(bodyInner.left, bodyInner.top - scrollPx), layout.Get(), muted.Get());
                    d2dContext_->PopAxisAlignedClip();
                }

                finishDraw();
                return;
            }
            if (state.modsProfileOpen) {
                const float left = frame.left + 40.0f;
                const float right = frame.right - 40.0f;
                const float top = frame.top + 34.0f;
                const bool isActive = !state.modsProfileId.empty() && state.modsProfileId == state.activeProfileId;

                const std::wstring nameShown = state.modsRenaming ? (state.modsRenameText + L"_") : state.modsProfileName;
                DrawText(nameShown.c_str(), titleFormat_.Get(),
                    D2D1::RectF(left, top, right - 380.0f, top + 48.0f), state.modsRenaming ? accent.Get() : white.Get());
                const std::wstring sub = state.modsProfileBuiltin
                    ? std::wstring(L"Pure vanilla, always available")
                    : (std::to_wstring(state.modsProfileMods.size()) + (state.modsProfileMods.size() == 1 ? L" mod installed" : L" mods installed"));
                DrawText(sub.c_str(), captionFormat_.Get(),
                    D2D1::RectF(left, top + 50.0f, right - 380.0f, top + 74.0f), muted.Get());

                const float btnW = 168.0f;
                const float btnH = 54.0f;
                const D2D1_RECT_F playBtn = D2D1::RectF(right - btnW, top, right, top + btnH);
                const bool playFocus = state.modsProfileFocus == 0;
                if (playFocus) GlowSelect(playBtn, 12.0f);
                FillRound(playBtn, isActive ? panel.Get() : accent.Get(), 12.0f);
                StrokeRound(playBtn, accent.Get(), 12.0f, playFocus ? 3.0f : 2.0f);
                DrawIcon(L"\uE768", D2D1::RectF(playBtn.left + 16.0f, playBtn.top, playBtn.left + 44.0f, playBtn.bottom), isActive ? muted.Get() : black.Get());
                DrawText(isActive ? L"Playing" : L"Play this", bodyMid_.Get(),
                    D2D1::RectF(playBtn.left + 50.0f, playBtn.top, playBtn.right - 8.0f, playBtn.bottom), isActive ? muted.Get() : black.Get());

                if (!state.modsProfileBuiltin) {
                    const D2D1_RECT_F delBtn = D2D1::RectF(right - btnW * 2.0f - 16.0f, top, right - btnW - 16.0f, top + btnH);
                    const bool delFocus = state.modsProfileFocus == 1;
                    if (delFocus) GlowSelect(delBtn, 12.0f);
                    FillRound(delBtn, surfaceFill.Get(), 12.0f);
                    StrokeRound(delBtn, danger.Get(), 12.0f, delFocus ? 3.0f : 2.0f);
                    DrawIcon(L"\uE74D", D2D1::RectF(delBtn.left + 16.0f, delBtn.top, delBtn.left + 44.0f, delBtn.bottom), danger.Get());
                    DrawText(L"Delete", bodyMid_.Get(),
                        D2D1::RectF(delBtn.left + 50.0f, delBtn.top, delBtn.right - 8.0f, delBtn.bottom), danger.Get());
                }

                const bool gridFocus = state.modsProfileFocus == 2;
                DrawText(state.modsRenaming
                            ? L"Type a name, then close the keyboard to save"
                            : (state.modsProfileBuiltin
                                ? L"B  Back"
                                : (gridFocus ? L"B  Back      X  Remove mod      Y  Rename"
                                             : L"B  Back      Down to mods      Y  Rename")),
                    smallFormat_.Get(),
                    D2D1::RectF(left, top + btnH + 12.0f, right, top + btnH + 40.0f),
                    state.modsRenaming ? accent.Get() : muted.Get());

                if (!state.status.empty()) {
                    DrawText(state.status.c_str(), smallFormat_.Get(),
                        D2D1::RectF(left, top + btnH + 42.0f, right, top + btnH + 68.0f),
                        state.isError ? danger.Get() : accent.Get());
                }

                const float bodyTop = top + btnH + 78.0f;
                const D2D1_RECT_F bodyRect = D2D1::RectF(left, bodyTop, right, frame.bottom - 30.0f);
                FillRound(bodyRect, surfaceFill.Get(), 16.0f);
                StrokeRound(bodyRect, softEdge.Get(), 16.0f, 1.0f);
                const float padb = 16.0f;
                const D2D1_RECT_F bodyInner = D2D1::RectF(bodyRect.left + padb, bodyRect.top + padb, bodyRect.right - padb, bodyRect.bottom - padb);
                const int total = static_cast<int>(state.modsProfileMods.size());

                if (total == 0) {
                    g_profileMaxScroll.store(0);
                    g_profileRowsVisible.store(1);
                    DrawText(state.modsProfileBuiltin ? L"No mods. This is the clean vanilla game."
                                                      : L"No mods yet. Set this profile active, then install mods from the other tabs.",
                        smallFormat_.Get(), D2D1::RectF(bodyInner.left, bodyInner.top, bodyInner.right, bodyInner.top + 28.0f), muted.Get());
                } else {
                    const float colGap = 14.0f;
                    const float cardGap = 12.0f;
                    const float cardW = (bodyInner.right - bodyInner.left - colGap) * 0.5f;
                    const float cardH = 58.0f;
                    const float gridH = bodyInner.bottom - bodyInner.top;
                    const int rowsVisible = (std::max)(1, static_cast<int>((gridH + cardGap) / (cardH + cardGap)));
                    g_profileRowsVisible.store(rowsVisible);
                    const int totalRows = (total + 1) / 2;
                    const int maxScroll = (std::max)(0, totalRows - rowsVisible);
                    g_profileMaxScroll.store(maxScroll);
                    const int scroll = (std::min)((std::max)(0, state.modsProfileScroll), maxScroll);

                    d2dContext_->PushAxisAlignedClip(bodyInner, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
                    for (int row = scroll; row < scroll + rowsVisible && row < totalRows; ++row) {
                        for (int col = 0; col < 2; ++col) {
                            const int i = row * 2 + col;
                            if (i >= total) break;
                            const float x = bodyInner.left + col * (cardW + colGap);
                            const float y = bodyInner.top + (row - scroll) * (cardH + cardGap);
                            const D2D1_RECT_F card = D2D1::RectF(x, y, x + cardW, y + cardH);
                            const bool sel = gridFocus && i == state.modsProfileSel;
                            if (sel) GlowSelect(card, 12.0f);
                            FillRound(card, panel.Get(), 12.0f);
                            StrokeRound(card, sel ? accent.Get() : softEdge.Get(), 12.0f, sel ? 3.0f : 1.0f);
                            DrawIcon(L"\uE74C", D2D1::RectF(card.left + 10.0f, card.top, card.left + 44.0f, card.bottom), sel ? accent.Get() : muted.Get());
                            std::wstring nm = state.modsProfileMods[static_cast<size_t>(i)];
                            if (nm.size() > 4 && nm.compare(nm.size() - 4, 4, L".jar") == 0) nm = nm.substr(0, nm.size() - 4);
                            DrawText(nm.c_str(), smallMid_.Get(),
                                D2D1::RectF(card.left + 48.0f, card.top, card.right - 12.0f, card.bottom), white.Get());
                        }
                    }
                    d2dContext_->PopAxisAlignedClip();

                    if (maxScroll > 0) {
                        const std::wstring pos = L"row " + std::to_wstring(scroll + 1) + L" / " + std::to_wstring(totalRows);
                        DrawText(pos.c_str(), smallFormat_.Get(),
                            D2D1::RectF(bodyRect.right - 140.0f, bodyRect.top - 26.0f, bodyRect.right, bodyRect.top), muted.Get());
                    }
                }

                finishDraw();
                return;
            }
            const float left = frame.left + 36.0f;
            const float tabsRight = frame.left + (frame.right - frame.left) * 0.22f;
            const float cardsLeft = tabsRight + 34.0f;
            const float cardsRight = frame.right - 36.0f;
            const float top = frame.top + 34.0f;
            const float buttonH = 58.0f;
            const float buttonGap = 22.0f;
            const wchar_t* tabs[] = { L"Profiles", L"Popular", L"Latest", L"Recommended", L"Modpacks" };

            DrawText(L"Mods", titleFormat_.Get(), D2D1::RectF(left, top, tabsRight, top + 48.0f), white.Get());

            const wchar_t* tabIcons[] = { L"\uE8B7", L"\uE735", L"\uE823", L"\uEB52", L"\uE7B8" };
            for (int i = 0; i < 5; ++i) {
                const float y = top + 76.0f + i * (buttonH + buttonGap);
                const D2D1_RECT_F tab = D2D1::RectF(left, y, tabsRight, y + buttonH);
                const bool selected = i == state.selectedModsTab && state.modsFocus == 0;
                const bool active = i == state.selectedModsTab;
                if (selected) GlowSelect(tab, 14.0f);
                FillRound(tab, active ? accentSoft.Get() : surfaceFill.Get(), 14.0f);
                StrokeRound(tab, (selected || active) ? accent.Get() : softEdge.Get(), 14.0f, selected ? 3.0f : (active ? 2.0f : 1.0f));
                DrawIcon(tabIcons[i], D2D1::RectF(tab.left + 8.0f, tab.top, tab.left + 46.0f, tab.bottom), active ? accent.Get() : muted.Get());
                DrawText(tabs[i], bodyMid_.Get(),
                    D2D1::RectF(tab.left + 52.0f, tab.top, tab.right - 10.0f, tab.bottom),
                    active ? accent.Get() : white.Get());
            }

            {
                const float infoY = top + 76.0f + 5 * (buttonH + buttonGap) + 8.0f;
                const D2D1_RECT_F infoBox = D2D1::RectF(left, infoY, tabsRight, infoY + 74.0f);
                FillRound(infoBox, surfaceFill.Get(), 12.0f);
                StrokeRound(infoBox, softEdge.Get(), 12.0f, 1.0f);
                DrawIcon(L"\uE768", D2D1::RectF(infoBox.left + 8.0f, infoBox.top + 6.0f, infoBox.left + 34.0f, infoBox.top + 30.0f), muted.Get());
                DrawText(L"INSTALLS GO TO", captionFormat_.Get(),
                    D2D1::RectF(infoBox.left + 36.0f, infoBox.top + 9.0f, infoBox.right - 8.0f, infoBox.top + 30.0f), muted.Get());
                const std::wstring who = state.activeProfileName.empty() ? std::wstring(L"Vanilla") : state.activeProfileName;
                DrawText(who.c_str(), bodyFormat_.Get(),
                    D2D1::RectF(infoBox.left + 12.0f, infoBox.top + 32.0f, infoBox.right - 8.0f, infoBox.bottom - 6.0f), accent.Get());
            }

            if (!state.status.empty()) {
                DrawText(state.status.c_str(), smallFormat_.Get(),
                    D2D1::RectF(left, frame.bottom - 112.0f, tabsRight, frame.bottom - 30.0f),
                    state.isError ? danger.Get() : muted.Get());
            }

            const D2D1_RECT_F list = D2D1::RectF(cardsLeft, top, cardsRight, frame.bottom - 34.0f);
            FillRound(list, surfaceFill.Get(), 16.0f);
            StrokeRound(list, softEdge.Get(), 16.0f, 1.0f);

            const float pad = 16.0f;
            const D2D1_RECT_F inner = D2D1::RectF(list.left + pad, list.top + pad, list.right - pad, list.bottom - pad);

            const float searchH = 46.0f;
            const D2D1_RECT_F search = D2D1::RectF(inner.left, inner.top, inner.right, inner.top + searchH);
            const bool searchFocused = state.modsFocus == 1;
            if (searchFocused) GlowSelect(search, 12.0f);
            FillRound(search, searchFocused ? accentSoft.Get() : panel.Get(), 12.0f);
            StrokeRound(search, searchFocused ? accent.Get() : softEdge.Get(), 12.0f, searchFocused ? 3.0f : 1.0f);
            DrawIcon(L"\uE721", D2D1::RectF(search.left + 8.0f, search.top, search.left + 40.0f, search.bottom), searchFocused ? accent.Get() : muted.Get());
            {
                const bool placeholder = state.modsSearchQuery.empty() && !state.modsSearchEditing;
                const wchar_t* hint = state.selectedModsTab == 4 ? L"Search modpacks" : L"Search mods";
                std::wstring shown = placeholder ? std::wstring(hint) : state.modsSearchQuery;
                if (state.modsSearchEditing) shown += L"_";
                DrawText(shown.c_str(), smallMid_.Get(),
                    D2D1::RectF(search.left + 44.0f, search.top, search.right - 12.0f, search.bottom),
                    placeholder ? muted.Get() : white.Get());
            }

            const float gridTop = search.bottom + 16.0f;
            const float colGap = 16.0f;
            const float cardGap = 16.0f;
            const float cardW = (inner.right - inner.left - colGap) * 0.5f;
            const float gridH = inner.bottom - gridTop;
            const float desiredCardH = 150.0f;
            int rowsVisible = static_cast<int>((gridH + cardGap) / (desiredCardH + cardGap) + 0.5f);
            if (rowsVisible < 1) rowsVisible = 1;
            const float cardH = (gridH - cardGap * (rowsVisible - 1)) / static_cast<float>(rowsVisible);
            g_modsRowsVisible.store(rowsVisible);

            const int count = static_cast<int>(state.modsCards.size());
            const int totalRows = (count + 1) / 2;
            const int maxScroll = totalRows > rowsVisible ? totalRows - rowsVisible : 0;
            int scroll = state.modsScrollRow;
            if (scroll < 0) scroll = 0;
            if (scroll > maxScroll) scroll = maxScroll;

            for (int row = scroll; row < scroll + rowsVisible && row < totalRows; ++row) {
                for (int col = 0; col < 2; ++col) {
                    const int i = row * 2 + col;
                    if (i >= count) break;
                    const float x = inner.left + col * (cardW + colGap);
                    const float y = gridTop + (row - scroll) * (cardH + cardGap);
                    const D2D1_RECT_F card = D2D1::RectF(x, y, x + cardW, y + cardH);
                    const bool selected = state.modsFocus == 2 && i == state.selectedModIndex;

                    if (selected) GlowSelect(card, 14.0f);
                    FillRound(card, panel.Get(), 14.0f);
                    StrokeRound(card, selected ? accent.Get() : softEdge.Get(), 14.0f, selected ? 3.0f : 1.0f);

                    const float imageSide = (std::min)(cardH - 24.0f, 112.0f);
                    const float imageTop = card.top + (cardH - imageSide) * 0.5f;
                    const D2D1_RECT_F imageRect = D2D1::RectF(card.left + 14.0f, imageTop, card.left + 14.0f + imageSide, imageTop + imageSide);
                    ComPtr<ID2D1Bitmap1> icon = GetCachedBitmap(state.modsCards[i].iconPath);
                    if (icon) {
                        DrawBitmapCover(icon.Get(), imageRect, 1.0f, 1.0f, 0.0f, 0.0f);
                        StrokeRound(imageRect, softEdge.Get(), 8.0f, 1.0f);
                    } else {
                        ComPtr<ID2D1SolidColorBrush> ph;
                        d2dContext_->CreateSolidColorBrush(D2D1::ColorF(0x05080B), ph.GetAddressOf());
                        FillRound(imageRect, ph.Get(), 8.0f);
                        StrokeRound(imageRect, softEdge.Get(), 8.0f, 1.0f);
                        const wchar_t* g = state.selectedModsTab == 0
                            ? (state.modsCards[i].projectId == L"__new__" ? L"\uE710" : L"\uE8B7")
                            : (state.modsCards[i].isModpack ? L"\uE7B8" : L"\uE74C");
                        DrawIcon(g, imageRect, muted.Get(), true);
                    }

                    const float textLeft = imageRect.right + 16.0f;
                    const float textRight = card.right - 14.0f;
                    DrawText(state.modsCards[i].title.c_str(), cardTitleFormat_.Get(),
                        D2D1::RectF(textLeft, card.top + 12.0f, textRight, card.top + 44.0f),
                        white.Get());
                    DrawText(state.modsCards[i].description.c_str(), captionFormat_.Get(),
                        D2D1::RectF(textLeft, card.top + 44.0f, textRight, card.bottom - 40.0f),
                        muted.Get());
                    if (!state.modsCards[i].status.empty()) {
                        DrawText(state.modsCards[i].status.c_str(), smallMid_.Get(),
                            D2D1::RectF(textLeft, card.bottom - 40.0f, textRight, card.bottom - 6.0f),
                            state.modsCards[i].installed ? accent.Get() : muted.Get());
                    }
                }
            }

            if (maxScroll > 0) {
                const D2D1_RECT_F track = D2D1::RectF(inner.right - 6.0f, gridTop, inner.right - 2.0f, inner.bottom);
                FillRound(track, panel.Get(), 2.0f);
                const float trackH = track.bottom - track.top;
                const float thumbH = trackH * (static_cast<float>(rowsVisible) / static_cast<float>(totalRows));
                const float thumbY = track.top + (trackH - thumbH) * (static_cast<float>(scroll) / static_cast<float>(maxScroll));
                FillRound(D2D1::RectF(track.left, thumbY, track.right, thumbY + thumbH), accent.Get(), 2.0f);
            }

            if (state.modsCards.empty()) {
                const std::wstring emptyText = state.selectedModsTab == 0
                    ? L"No installed mods"
                    : (state.selectedModsTab == 4 ? L"No modpacks found" : L"No mods found");
                DrawText(emptyText.c_str(), bodyFormat_.Get(),
                    D2D1::RectF(inner.left + 8.0f, gridTop + 8.0f, inner.right - 8.0f, gridTop + 70.0f),
                    muted.Get());
            }

            finishDraw();
            return;
        }

        if (state.showMainMenu) {
            const float left = frame.left + 36.0f;
            const float menuRight = frame.left + (frame.right - frame.left) * 0.34f;
            const float previewLeft = menuRight + 34.0f;
            const float previewRight = frame.right - 36.0f;
            const float top = frame.top + 34.0f;
            const float buttonH = 62.0f;
            const float buttonGap = 24.0f;
            const wchar_t* labels[] = { L"Play", L"Mods", L"Repair downloads", L"Sign out" };

            DrawText(title.c_str(), titleFormat_.Get(), D2D1::RectF(left, top, menuRight, top + 48.0f), white.Get());

            const wchar_t* menuIcons[] = { L"\uE768", L"\uE74C", L"\uE72C", L"\uE7E8" };
            for (int i = 0; i < 4; ++i) {
                const float y = top + 76.0f + i * (buttonH + buttonGap);
                const D2D1_RECT_F button = D2D1::RectF(left, y, menuRight, y + buttonH);
                const bool sel = i == state.selectedMenuIndex;
                if (sel) GlowSelect(button, 14.0f);
                FillRound(button, sel ? accentSoft.Get() : surfaceFill.Get(), 14.0f);
                StrokeRound(button, sel ? accent.Get() : softEdge.Get(), 14.0f, sel ? 3.0f : 1.0f);
                DrawIcon(menuIcons[i], D2D1::RectF(button.left + 16.0f, button.top, button.left + 50.0f, button.bottom), sel ? accent.Get() : white.Get());
                const D2D1_RECT_F textRect = D2D1::RectF(button.left + 56.0f, button.top, button.right - 12.0f, button.bottom);
                DrawText(labels[i], bodyMid_.Get(), textRect, sel ? accent.Get() : white.Get());
            }

            if (!state.status.empty()) {
                const D2D1_RECT_F statusRect = D2D1::RectF(left, frame.bottom - 88.0f, menuRight, frame.bottom - 28.0f);
                DrawText(state.status.c_str(), smallFormat_.Get(), statusRect, state.isError ? danger.Get() : muted.Get());
            }

            const D2D1_RECT_F preview = D2D1::RectF(previewLeft, top, previewRight, frame.bottom - 34.0f);
            FillRound(preview, black.Get(), 16.0f);
            StrokeRound(preview, softEdge.Get(), 16.0f, 1.0f);
            const float inset = 8.0f;
            const D2D1_RECT_F pano = D2D1::RectF(preview.left + inset, preview.top + inset, preview.right - inset, preview.bottom - inset);
            DrawScreenshots(pano);

            if (!state.detail.empty()) {
                const D2D1_RECT_F detailRect = D2D1::RectF(preview.left + 26.0f, preview.bottom - 82.0f, preview.right - 26.0f, preview.bottom - 24.0f);
                DrawText(state.detail.c_str(), smallFormat_.Get(), detailRect, muted.Get());
            }

            finishDraw();
            return;
        }

        if (!state.showDeviceCode) {
            const float left = frame.left + 54.0f;
            const float right = frame.right - 54.0f;
            const D2D1_RECT_F titleRect = D2D1::RectF(left, frame.top + 72.0f, right, frame.top + 130.0f);
            DrawText(title.c_str(), titleFormat_.Get(), titleRect, white.Get());

            const D2D1_RECT_F statusRect = D2D1::RectF(left, frame.top + 178.0f, right, frame.top + 240.0f);
            DrawText(state.status.c_str(), bodyFormat_.Get(), statusRect, state.isError ? danger.Get() : white.Get());

            if (!state.detail.empty()) {
                const D2D1_RECT_F detailRect = D2D1::RectF(left, frame.top + 248.0f, right, frame.top + 306.0f);
                DrawText(state.detail.c_str(), smallFormat_.Get(), detailRect, muted.Get());
            }

            if (state.progress >= 0.0f) {
                const float progress = (std::max)(0.0f, (std::min)(1.0f, state.progress));
                const float barTop = frame.bottom - 130.0f;
                const float barHeight = 18.0f;
                const D2D1_RECT_F track = D2D1::RectF(left, barTop, right, barTop + barHeight);
                const D2D1_RECT_F fill = D2D1::RectF(left, barTop, left + (right - left) * progress, barTop + barHeight);
                FillRound(track, panel.Get(), 9.0f);
                FillRound(fill, state.isError ? danger.Get() : accent.Get(), 9.0f);

                wchar_t percent[32] = {};
                swprintf_s(percent, L"%d%%", static_cast<int>(progress * 100.0f + 0.5f));
                const D2D1_RECT_F percentRect = D2D1::RectF(left, barTop + 28.0f, left + 140.0f, barTop + 68.0f);
                DrawText(percent, smallFormat_.Get(), percentRect, muted.Get());
            }

            finishDraw();
            return;
        }

        const float dividerX = frame.left + (frame.right - frame.left) * 0.52f;
        d2dContext_->DrawLine(
            D2D1::Point2F(dividerX, frame.top + 32.0f),
            D2D1::Point2F(dividerX, frame.bottom - 32.0f),
            softEdge.Get(),
            1.5f);

        const D2D1_RECT_F titleRect = D2D1::RectF(frame.left + 42.0f, frame.top + 34.0f, dividerX - 42.0f, frame.top + 86.0f);
        DrawText(title.c_str(), titleFormat_.Get(), titleRect, white.Get());

        const D2D1_RECT_F codeBox = D2D1::RectF(frame.left + 42.0f, frame.top + 102.0f, dividerX - 42.0f, frame.top + 190.0f);
        FillRound(codeBox, accentSoft.Get(), 14.0f);
        StrokeRound(codeBox, accent.Get(), 14.0f, 2.0f);
        if (!state.userCode.empty()) {
            DrawText(state.userCode.c_str(), codeFormat_.Get(), codeBox, white.Get());
        }

        std::wstring instruction = L"Enter this code at";
        std::wstring url = state.verificationUri.empty() ? L"microsoft.com/link" : state.verificationUri;
        const D2D1_RECT_F bodyRect = D2D1::RectF(frame.left + 46.0f, frame.top + 218.0f, dividerX - 48.0f, frame.top + 326.0f);
        DrawText((instruction + L"\n" + url).c_str(), bodyFormat_.Get(), bodyRect, white.Get());

        std::wstring status = state.status;
        if (state.secondsRemaining > 0) {
            status += L"\nCode expires in " + std::to_wstring(state.secondsRemaining) + L" seconds";
        }
        const D2D1_RECT_F statusRect = D2D1::RectF(frame.left + 46.0f, frame.bottom - 116.0f, dividerX - 48.0f, frame.bottom - 38.0f);
        DrawText(status.c_str(), smallFormat_.Get(), statusRect, state.isError ? danger.Get() : muted.Get());

        if (!state.detail.empty()) {
            const D2D1_RECT_F detailRect = D2D1::RectF(frame.left + 46.0f, frame.bottom - 160.0f, dividerX - 48.0f, frame.bottom - 118.0f);
            DrawText(state.detail.c_str(), smallFormat_.Get(), detailRect, muted.Get());
        }

        const float qrSide = (std::min)((frame.right - dividerX) * 0.55f, (frame.bottom - frame.top) * 0.58f);
        const float qrLeft = dividerX + ((frame.right - dividerX) - qrSide) * 0.5f;
        const float qrTop = frame.top + ((frame.bottom - frame.top) - qrSide) * 0.43f;
        const D2D1_RECT_F qrRect = D2D1::RectF(qrLeft, qrTop, qrLeft + qrSide, qrTop + qrSide);
        DrawQr(state.qr, qrRect, white.Get(), black.Get(), muted.Get());

        const D2D1_RECT_F qrLabel = D2D1::RectF(qrLeft, qrRect.bottom + 18.0f, qrLeft + qrSide, qrRect.bottom + 54.0f);
        DrawText(L"Scan QR", smallFormat_.Get(), qrLabel, muted.Get());

        finishDraw();
    }

private:
    ComPtr<ICoreWindow> window_;
    ComPtr<ID3D11Device> d3dDevice_;
    ComPtr<ID3D11DeviceContext> d3dContext_;
    ComPtr<IDXGISwapChain1> swapChain_;
    ComPtr<ID2D1Factory1> d2dFactory_;
    ComPtr<ID2D1Device> d2dDevice_;
    ComPtr<ID2D1DeviceContext> d2dContext_;
    ComPtr<ID2D1Bitmap1> targetBitmap_;
    ComPtr<IDWriteFactory> dwriteFactory_;
    ComPtr<IWICImagingFactory> wicFactory_;
    ComPtr<IDWriteTextFormat> codeFormat_;
    ComPtr<IDWriteTextFormat> bodyFormat_;
    ComPtr<IDWriteTextFormat> smallFormat_;
    ComPtr<IDWriteTextFormat> cardTitleFormat_;
    ComPtr<IDWriteTextFormat> titleFormat_;
    ComPtr<IDWriteTextFormat> captionFormat_;
    ComPtr<IDWriteTextFormat> bodyMid_;
    ComPtr<IDWriteTextFormat> smallMid_;
    ComPtr<IDWriteTextFormat> iconFormat_;
    ComPtr<IDWriteTextFormat> iconLgFormat_;
    std::map<std::wstring, ComPtr<ID2D1Bitmap1>> bitmapCache_;
    std::vector<std::wstring> screenshotPaths_;
    ULONGLONG screenshotsScanTick_ = 0;
    float width_ = 1280.0f;
    float height_ = 720.0f;

    void CreateTextFormats() {
        if (!dwriteFactory_) return;
        dwriteFactory_->CreateTextFormat(
            L"Consolas", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, 48.0f, L"en-US", codeFormat_.GetAddressOf());
        dwriteFactory_->CreateTextFormat(
            L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_REGULAR, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, 30.0f, L"en-US", bodyFormat_.GetAddressOf());
        dwriteFactory_->CreateTextFormat(
            L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_REGULAR, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, 21.0f, L"en-US", smallFormat_.GetAddressOf());
        dwriteFactory_->CreateTextFormat(
            L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, 22.0f, L"en-US", cardTitleFormat_.GetAddressOf());

        if (codeFormat_) {
            codeFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            codeFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        }
        if (bodyFormat_) {
            bodyFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
            bodyFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
        }
        if (smallFormat_) {
            smallFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
            smallFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
        }
        if (cardTitleFormat_) {
            cardTitleFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
            cardTitleFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
            cardTitleFormat_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        }
        dwriteFactory_->CreateTextFormat(
            L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, 40.0f, L"en-US", titleFormat_.GetAddressOf());
        dwriteFactory_->CreateTextFormat(
            L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_REGULAR, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, 17.0f, L"en-US", captionFormat_.GetAddressOf());
        dwriteFactory_->CreateTextFormat(
            L"Segoe MDL2 Assets", nullptr, DWRITE_FONT_WEIGHT_REGULAR, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, 22.0f, L"en-US", iconFormat_.GetAddressOf());
        dwriteFactory_->CreateTextFormat(
            L"Segoe MDL2 Assets", nullptr, DWRITE_FONT_WEIGHT_REGULAR, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, 32.0f, L"en-US", iconLgFormat_.GetAddressOf());
        if (titleFormat_) {
            titleFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
            titleFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
            titleFormat_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        }
        if (captionFormat_) {
            captionFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
            captionFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
        }
        dwriteFactory_->CreateTextFormat(
            L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_REGULAR, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, 30.0f, L"en-US", bodyMid_.GetAddressOf());
        dwriteFactory_->CreateTextFormat(
            L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_REGULAR, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, 21.0f, L"en-US", smallMid_.GetAddressOf());
        for (IDWriteTextFormat* mf : { bodyMid_.Get(), smallMid_.Get() }) {
            if (!mf) continue;
            mf->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
            mf->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            mf->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        }
        for (IDWriteTextFormat* icf : { iconFormat_.Get(), iconLgFormat_.Get() }) {
            if (!icf) continue;
            icf->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            icf->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            icf->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        }
    }

    void DrawText(const wchar_t* text, IDWriteTextFormat* format, D2D1_RECT_F rect, ID2D1Brush* brush) {
        if (!text || !format || !brush) return;
        d2dContext_->DrawText(
            text,
            static_cast<UINT32>(wcslen(text)),
            format,
            rect,
            brush,
            D2D1_DRAW_TEXT_OPTIONS_CLIP);
    }

    void FillRound(D2D1_RECT_F r, ID2D1Brush* b, float radius) {
        if (b) d2dContext_->FillRoundedRectangle(D2D1::RoundedRect(r, radius, radius), b);
    }

    void StrokeRound(D2D1_RECT_F r, ID2D1Brush* b, float radius, float width) {
        if (b) d2dContext_->DrawRoundedRectangle(D2D1::RoundedRect(r, radius, radius), b, width);
    }

    void DrawIcon(const wchar_t* glyph, D2D1_RECT_F rect, ID2D1Brush* brush, bool large = false) {
        DrawText(glyph, large ? iconLgFormat_.Get() : iconFormat_.Get(), rect, brush);
    }

    void GlowSelect(D2D1_RECT_F r, float radius) {
        ComPtr<ID2D1SolidColorBrush> g1, g2;
        d2dContext_->CreateSolidColorBrush(D2D1::ColorF(0x70C486, 0.10f), g1.GetAddressOf());
        d2dContext_->CreateSolidColorBrush(D2D1::ColorF(0x70C486, 0.20f), g2.GetAddressOf());
        if (g1) d2dContext_->FillRoundedRectangle(
            D2D1::RoundedRect(D2D1::RectF(r.left - 10.0f, r.top - 10.0f, r.right + 10.0f, r.bottom + 10.0f), radius + 9.0f, radius + 9.0f), g1.Get());
        if (g2) d2dContext_->FillRoundedRectangle(
            D2D1::RoundedRect(D2D1::RectF(r.left - 5.0f, r.top - 5.0f, r.right + 5.0f, r.bottom + 5.0f), radius + 5.0f, radius + 5.0f), g2.Get());
    }

    void FillVerticalGradient(D2D1_RECT_F r, UINT32 topColor, UINT32 bottomColor) {
        D2D1_GRADIENT_STOP stops[2];
        stops[0].position = 0.0f; stops[0].color = D2D1::ColorF(topColor);
        stops[1].position = 1.0f; stops[1].color = D2D1::ColorF(bottomColor);
        ComPtr<ID2D1GradientStopCollection> coll;
        if (FAILED(d2dContext_->CreateGradientStopCollection(stops, 2, coll.GetAddressOf()))) return;
        D2D1_LINEAR_GRADIENT_BRUSH_PROPERTIES props{};
        props.startPoint = D2D1::Point2F(r.left, r.top);
        props.endPoint = D2D1::Point2F(r.left, r.bottom);
        ComPtr<ID2D1LinearGradientBrush> br;
        if (FAILED(d2dContext_->CreateLinearGradientBrush(props, coll.Get(), br.GetAddressOf()))) return;
        d2dContext_->FillRectangle(r, br.Get());
    }

    ComPtr<ID2D1Bitmap1> GetCachedBitmap(const std::wstring& path) {
        if (path.empty()) return nullptr;
        auto found = bitmapCache_.find(path);
        if (found != bitmapCache_.end()) {
            return found->second;
        }

        if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
            return nullptr;
        }

        ComPtr<ID2D1Bitmap1> bitmap;
        if (LoadBitmapFromFile(path, bitmap)) {
            bitmapCache_[path] = bitmap;
            return bitmap;
        }

        return nullptr;
    }

    bool LoadBitmapFromFile(const std::wstring& path, ComPtr<ID2D1Bitmap1>& out) {
        if (!wicFactory_ || !d2dContext_) return false;

        ComPtr<IWICBitmapDecoder> decoder;
        HRESULT hr = wicFactory_->CreateDecoderFromFilename(
            path.c_str(),
            nullptr,
            GENERIC_READ,
            WICDecodeMetadataCacheOnLoad,
            decoder.GetAddressOf());
        if (FAILED(hr)) {
            WriteLogF(L"Bitmap decoder failed %s hr=0x%08X", path.c_str(), hr);
            return false;
        }

        ComPtr<IWICBitmapFrameDecode> frame;
        hr = decoder->GetFrame(0, frame.GetAddressOf());
        if (FAILED(hr)) {
            WriteLogF(L"Bitmap frame failed %s hr=0x%08X", path.c_str(), hr);
            return false;
        }

        ComPtr<IWICFormatConverter> converter;
        hr = wicFactory_->CreateFormatConverter(converter.GetAddressOf());
        if (FAILED(hr)) {
            WriteLogF(L"Bitmap converter create failed hr=0x%08X", hr);
            return false;
        }

        hr = converter->Initialize(
            frame.Get(),
            GUID_WICPixelFormat32bppPBGRA,
            WICBitmapDitherTypeNone,
            nullptr,
            0.0,
            WICBitmapPaletteTypeCustom);
        if (FAILED(hr)) {
            WriteLogF(L"Bitmap converter init failed %s hr=0x%08X", path.c_str(), hr);
            return false;
        }

        const D2D1_BITMAP_PROPERTIES1 props = D2D1::BitmapProperties1(
            D2D1_BITMAP_OPTIONS_NONE,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
            96.0f,
            96.0f);
        hr = d2dContext_->CreateBitmapFromWicBitmap(converter.Get(), &props, out.GetAddressOf());
        if (FAILED(hr)) {
            WriteLogF(L"Bitmap D2D bitmap failed %s hr=0x%08X", path.c_str(), hr);
            return false;
        }

        return true;
    }


    void ScanScreenshots() {
        screenshotPaths_.clear();
        const std::wstring dir = GetExecutableDir() + L"\\Assets\\screenshots";
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileW((dir + L"\\*.png").c_str(), &fd);
        if (h != INVALID_HANDLE_VALUE) {
            do {
                if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
                screenshotPaths_.push_back(dir + L"\\" + fd.cFileName);
            } while (FindNextFileW(h, &fd));
            FindClose(h);
        }
        std::sort(screenshotPaths_.begin(), screenshotPaths_.end());
    }


    void DrawBitmapCover(ID2D1Bitmap1* bitmap, D2D1_RECT_F rect, float opacity, float zoom, float panX, float panY) {
        if (!bitmap) return;

        const D2D1_SIZE_F size = bitmap->GetSize();
        const float srcW = size.width;
        const float srcH = size.height;
        if (srcW <= 0.0f || srcH <= 0.0f) return;

        const float destW = rect.right - rect.left;
        const float destH = rect.bottom - rect.top;
        if (destW <= 0.0f || destH <= 0.0f) return;

        const float destAspect = destW / destH;
        const float srcAspect = srcW / srcH;
        float cropW = srcW;
        float cropH = srcH;
        if (srcAspect > destAspect) {
            cropW = srcH * destAspect;
        } else {
            cropH = srcW / destAspect;
        }

        zoom = (std::max)(1.0f, zoom);
        cropW /= zoom;
        cropH /= zoom;

        const float maxX = (std::max)(0.0f, (srcW - cropW) * 0.5f);
        const float maxY = (std::max)(0.0f, (srcH - cropH) * 0.5f);
        const float centerX = srcW * 0.5f + maxX * (std::max)(-1.0f, (std::min)(1.0f, panX));
        const float centerY = srcH * 0.5f + maxY * (std::max)(-1.0f, (std::min)(1.0f, panY));
        const D2D1_RECT_F source = D2D1::RectF(
            centerX - cropW * 0.5f,
            centerY - cropH * 0.5f,
            centerX + cropW * 0.5f,
            centerY + cropH * 0.5f);

        d2dContext_->DrawBitmap(bitmap, rect, opacity, D2D1_INTERPOLATION_MODE_LINEAR, source);
    }

    void DrawScreenshots(D2D1_RECT_F rect) {
        const ULONGLONG nowMs = GetTickCount64();
        if (screenshotPaths_.empty() || nowMs - screenshotsScanTick_ > 10000) {
            ScanScreenshots();
            screenshotsScanTick_ = nowMs;
        }

        d2dContext_->PushAxisAlignedClip(rect, D2D1_ANTIALIAS_MODE_ALIASED);

        if (screenshotPaths_.empty()) {
            ComPtr<ID2D1SolidColorBrush> dim, hint;
            d2dContext_->CreateSolidColorBrush(D2D1::ColorF(0x070A0E), dim.GetAddressOf());
            d2dContext_->CreateSolidColorBrush(D2D1::ColorF(0x5A6168), hint.GetAddressOf());
            d2dContext_->FillRectangle(rect, dim.Get());
            DrawText(L"No images in Assets\\screenshots", smallMid_.Get(),
                D2D1::RectF(rect.left + 28.0f, rect.top, rect.right - 28.0f, rect.bottom), hint.Get());
            d2dContext_->PopAxisAlignedClip();
            return;
        }

        const double now = nowMs / 1000.0;
        const double hold = 6.0;
        const int total = static_cast<int>(screenshotPaths_.size());
        const double t = now / hold;
        const long long base = static_cast<long long>(floor(t));
        const int idx = static_cast<int>(((base % total) + total) % total);
        const int nextIdx = (idx + 1) % total;
        const double frac = t - static_cast<double>(base);
        const double fade = frac > 0.8 ? (frac - 0.8) / 0.2 : 0.0;
        const float easedFade = static_cast<float>(fade * fade * (3.0 - 2.0 * fade));
        const float panX = sinf(static_cast<float>(now) * 0.05f) * 0.4f;
        const float panY = cosf(static_cast<float>(now) * 0.037f) * 0.16f;
        const float zoom = 1.05f + 0.02f * sinf(static_cast<float>(now) * 0.08f);

        ComPtr<ID2D1Bitmap1> cur = GetCachedBitmap(screenshotPaths_[static_cast<size_t>(idx)]);
        if (cur) DrawBitmapCover(cur.Get(), rect, 1.0f, zoom, panX, panY);
        if (easedFade > 0.0f && total > 1) {
            ComPtr<ID2D1Bitmap1> nxt = GetCachedBitmap(screenshotPaths_[static_cast<size_t>(nextIdx)]);
            if (nxt) DrawBitmapCover(nxt.Get(), rect, easedFade, zoom, -panX, panY);
        }
        d2dContext_->PopAxisAlignedClip();
    }

    void DrawQr(const QrMatrix& qr, D2D1_RECT_F rect, ID2D1Brush* white, ID2D1Brush* black, ID2D1Brush* muted) {
        d2dContext_->FillRectangle(rect, white);
        if (qr.empty()) {
            DrawText(L"QR", codeFormat_.Get(), rect, muted);
            return;
        }

        constexpr int quiet = 4;
        const float module = (std::min)(
            (rect.right - rect.left) / static_cast<float>(qr.size + quiet * 2),
            (rect.bottom - rect.top) / static_cast<float>(qr.size + quiet * 2));
        const float qrDraw = module * static_cast<float>(qr.size + quiet * 2);
        const float startX = rect.left + ((rect.right - rect.left) - qrDraw) * 0.5f + module * quiet;
        const float startY = rect.top + ((rect.bottom - rect.top) - qrDraw) * 0.5f + module * quiet;

        for (int y = 0; y < qr.size; ++y) {
            for (int x = 0; x < qr.size; ++x) {
                if (!qr.at(x, y)) continue;
                const D2D1_RECT_F moduleRect = D2D1::RectF(
                    startX + x * module,
                    startY + y * module,
                    startX + (x + 1) * module + 0.25f,
                    startY + (y + 1) * module + 0.25f);
                d2dContext_->FillRectangle(moduleRect, black);
            }
        }
    }
};

static void ProcessAuthUiEvents() {
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

static void RenderAuth(AuthScreenRenderer* renderer, const AuthUiState& state) {
    ProcessAuthUiEvents();
    if (renderer) {
        renderer->Render(state);
    }
}

static void SleepWithAuthUi(AuthScreenRenderer* renderer, AuthUiState& state, int milliseconds) {
    const auto start = std::chrono::steady_clock::now();
    while (true) {
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start).count();
        if (elapsed >= milliseconds) break;

        RenderAuth(renderer, state);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

static void RenderPreparationProgress(
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

enum class MainMenuAction {
    Play,
    RepairDownloads,
    SignOut
};

static bool IsVirtualKeyDown(ICoreWindow* window, ABI::Windows::System::VirtualKey key) {
    if (!window) return false;
    CoreVirtualKeyStates state = CoreVirtualKeyStates_None;
    if (FAILED(window->GetKeyState(key, &state))) {
        return false;
    }
    return (state & CoreVirtualKeyStates_Down) == CoreVirtualKeyStates_Down;
}

static bool AnyVirtualKeyDown(ICoreWindow* window, std::initializer_list<ABI::Windows::System::VirtualKey> keys) {
    for (const auto key : keys) {
        if (IsVirtualKeyDown(window, key)) {
            return true;
        }
    }
    return false;
}

static const wchar_t* kRecommendedSlugs[] = {
    L"sodium", L"modernfix", L"ferrite-core", L"c2me-fabric", L"scalablelux",
    L"asyncparticles", L"controlify", L"mcwifipnp", L"fpsdisplay", L"modmenu"
};

static bool FetchRecommendedMods(const std::wstring& runtimeRoot, std::vector<ModCard>& out, std::wstring& error) {
    using namespace winrt::Windows::Data::Json;
    error.clear();

    std::string ids = "[";
    bool first = true;
    for (const wchar_t* slug : kRecommendedSlugs) {
        if (!first) ids += ",";
        first = false;
        ids += "\"" + w2a(slug) + "\"";
    }
    ids += "]";

    const std::wstring url = L"https://api.modrinth.com/v2/projects?ids=" + a2w(FormUrlEncode(ids).c_str());
    const HttpResult response = HttpGetString(url.c_str());
    if (!response.success()) {
        error = L"Recommended fetch failed HTTP " + std::to_wstring(response.status);
        return false;
    }

    std::map<std::wstring, ModCard> bySlug;
    try {
        JsonArray arr = JsonArray::Parse(winrt::to_hstring(response.body));
        for (uint32_t i = 0; i < arr.Size(); ++i) {
            auto value = arr.GetAt(i);
            if (value.ValueType() != JsonValueType::Object) continue;
            JsonObject project = value.GetObject();

            ModCard card;
            card.projectId = JsonStringOrEmpty(project, L"id");
            card.slug = JsonStringOrEmpty(project, L"slug");
            card.title = JsonStringOrEmpty(project, L"title");
            card.description = JsonStringOrEmpty(project, L"description");
            const std::wstring iconUrl = JsonStringOrEmpty(project, L"icon_url");
            if (card.title.empty()) card.title = card.slug.empty() ? card.projectId : card.slug;
            card.status = std::to_wstring(JsonIntOrZero(project, L"downloads")) + L" downloads";
            if (!iconUrl.empty()) {
                card.iconUrl = iconUrl;
                card.iconPath = ModIconCachePath(runtimeRoot, card.projectId.empty() ? card.slug : card.projectId);
            }
            std::wstring key = card.slug;
            std::transform(key.begin(), key.end(), key.begin(),
                [](wchar_t c) { return static_cast<wchar_t>(towlower(c)); });
            bySlug[key] = card;
        }
    } catch (...) {
        error = L"Could not parse recommended response";
        return false;
    }

    for (const wchar_t* slug : kRecommendedSlugs) {
        std::wstring key = slug;
        std::transform(key.begin(), key.end(), key.begin(),
            [](wchar_t c) { return static_cast<wchar_t>(towlower(c)); });
        auto it = bySlug.find(key);
        if (it != bySlug.end()) out.push_back(it->second);
    }
    return true;
}

static void LoadModsTab(AuthUiState& state, const std::wstring& runtimeRoot, const std::wstring& userModsDir) {
    state.modsCards.clear();
    state.selectedModIndex = 0;
    state.modsScrollRow = 0;
    state.modsTotalHits = 0;
    state.modsExhausted = true;
    state.isError = false;
    state.activeProfileId = GetActiveProfileId(runtimeRoot);
    state.activeProfileName = ProfileDisplayName(runtimeRoot, state.activeProfileId);

    const std::wstring query = state.modsSearchQuery;

    if (state.selectedModsTab == 0) {
        EnsureProfilesInitialized(runtimeRoot);
        const std::wstring active = GetActiveProfileId(runtimeRoot);
        {
            ModCard add;
            add.projectId = L"__new__";
            add.title = L"+ New profile";
            add.description = L"Create an empty profile, then install mods into it";
            state.modsCards.push_back(add);
        }
        for (const Profile& p : LoadProfiles(runtimeRoot)) {
            ModCard c;
            c.projectId = L"__profile__";
            c.filePath = p.id;
            c.title = p.name;
            const bool isActive = (p.id == active);
            c.installed = isActive;
            if (p.builtin) {
                c.description = L"Pure vanilla, no mods";
            } else {
                const int n = ProfileModCount(runtimeRoot, p.id);
                c.description = std::to_wstring(n) + (n == 1 ? L" mod" : L" mods");
            }
            c.status = isActive ? L"\u25CF Playing this" : L"";
            state.modsCards.push_back(c);
        }
        state.status = L"A open profile  -  X delete  -  installs go to the active profile";
        return;
    }

    if (state.selectedModsTab == 3) {
        std::vector<ModCard> all;
        std::wstring error;
        if (!FetchRecommendedMods(runtimeRoot, all, error)) {
            state.status = error.empty() ? L"Could not load recommended" : error;
            state.isError = true;
            return;
        }
        if (query.empty()) {
            state.modsCards = std::move(all);
        } else {
            std::wstring lowerNeedle = query;
            std::transform(lowerNeedle.begin(), lowerNeedle.end(), lowerNeedle.begin(),
                [](wchar_t c) { return static_cast<wchar_t>(towlower(c)); });
            for (auto& card : all) {
                std::wstring hay = card.title + L" " + card.slug;
                std::transform(hay.begin(), hay.end(), hay.begin(),
                    [](wchar_t c) { return static_cast<wchar_t>(towlower(c)); });
                if (hay.find(lowerNeedle) != std::wstring::npos) {
                    state.modsCards.push_back(std::move(card));
                }
            }
        }
        QueueModIcons(state.modsCards);
        state.status = state.modsCards.empty()
            ? L"No recommended mods"
            : std::to_wstring(state.modsCards.size()) + L" recommended";
        return;
    }

    const char* projectType = state.selectedModsTab == 4 ? "modpack" : "mod";
    const char* index = !query.empty()
        ? "relevance"
        : ((state.selectedModsTab == 1 || state.selectedModsTab == 4) ? "downloads" : "newest");
    std::wstring error;
    int total = 0;
    if (!FetchModrinthMods(runtimeRoot, index, query, 0, kModPageSize, state.modsCards, total, error, projectType)) {
        state.status = error.empty() ? L"Could not load Modrinth" : error;
        state.isError = true;
        return;
    }

    state.modsTotalHits = total;
    state.modsExhausted = static_cast<int>(state.modsCards.size()) >= total;
    QueueModIcons(state.modsCards);
    state.status = state.modsCards.empty()
        ? (state.selectedModsTab == 4 ? L"No modpacks found" : L"No mods found")
        : std::to_wstring(state.modsCards.size()) + L" of " + std::to_wstring(total);
}

static winrt::Windows::UI::Core::CoreWindow g_modsCharWindow{nullptr};
static winrt::event_token g_modsCharToken{};
static bool g_modsCharRegistered = false;

static winrt::Windows::UI::Text::Core::CoreTextEditContext g_editContext{nullptr};
static winrt::Windows::UI::ViewManagement::InputPane g_inputPane{nullptr};
static winrt::event_token g_ecTextRequested{};
static winrt::event_token g_ecSelectionRequested{};
static winrt::event_token g_ecTextUpdating{};
static winrt::event_token g_ecSelectionUpdating{};
static winrt::event_token g_ecLayoutRequested{};
static winrt::event_token g_ecFocusRemoved{};
static winrt::event_token g_modsKeyDownToken{};
static bool g_modsKeyDownRegistered = false;
static bool g_modsUsingEditContext = false;

static void RegisterCharacterFallback() {
    if (!g_modsCharWindow || g_modsCharRegistered) return;
    try {
        g_modsCharToken = g_modsCharWindow.CharacterReceived(
            [](winrt::Windows::UI::Core::CoreWindow const&,
               winrt::Windows::UI::Core::CharacterReceivedEventArgs const& args) {
                if (!g_modsSearchCapturing.load()) return;
                const wchar_t ch = static_cast<wchar_t>(args.KeyCode());
                std::lock_guard<std::mutex> lk(g_modsSearchMutex);
                if (ch == 8) {
                    if (!g_modsSearchBuffer.empty()) {
                        g_modsSearchBuffer.pop_back();
                        g_modsSearchDirty = true;
                    }
                } else if (ch >= 32 && ch != 127 && g_modsSearchBuffer.size() < 64) {
                    g_modsSearchBuffer.push_back(ch);
                    g_modsSearchDirty = true;
                }
            });
        g_modsCharRegistered = true;
    } catch (...) {
    }
}

static void CreateModsEditContext() {
    using namespace winrt::Windows::UI::Text::Core;
    try {
        auto manager = CoreTextServicesManager::GetForCurrentView();
        g_editContext = manager.CreateEditContext();
        g_editContext.InputPaneDisplayPolicy(CoreTextInputPaneDisplayPolicy::Manual);
        g_editContext.InputScope(CoreTextInputScope::Search);

        g_ecTextRequested = g_editContext.TextRequested(
            [](auto&&, CoreTextTextRequestedEventArgs const& args) {
                auto request = args.Request();
                auto range = request.Range();
                std::lock_guard<std::mutex> lk(g_modsSearchMutex);
                const int len = static_cast<int>(g_modsSearchBuffer.size());
                const int s = (std::max)(0, (std::min)(range.StartCaretPosition, len));
                const int e = (std::max)(s, (std::min)(range.EndCaretPosition, len));
                request.Text(winrt::hstring(std::wstring_view(g_modsSearchBuffer).substr(s, e - s)));
            });

        g_ecSelectionRequested = g_editContext.SelectionRequested(
            [](auto&&, CoreTextSelectionRequestedEventArgs const& args) {
                CoreTextRange r{ g_modsCaret, g_modsCaret };
                args.Request().Selection(r);
            });

        g_ecTextUpdating = g_editContext.TextUpdating(
            [](auto&&, CoreTextTextUpdatingEventArgs const& args) {
                const auto range = args.Range();
                bool sawNewline = false;
                std::wstring incoming;
                for (wchar_t c : std::wstring_view(args.Text())) {
                    if (c == L'\r' || c == L'\n') { sawNewline = true; continue; }
                    if (c == L'\t') continue;
                    incoming.push_back(c);
                }
                {
                    std::lock_guard<std::mutex> lk(g_modsSearchMutex);
                    const int len = static_cast<int>(g_modsSearchBuffer.size());
                    const int s = (std::max)(0, (std::min)(range.StartCaretPosition, len));
                    const int e = (std::max)(s, (std::min)(range.EndCaretPosition, len));
                    g_modsSearchBuffer = g_modsSearchBuffer.substr(0, s) + incoming + g_modsSearchBuffer.substr(e);
                    if (g_modsSearchBuffer.size() > 64) g_modsSearchBuffer.resize(64);
                    const int newLen = static_cast<int>(g_modsSearchBuffer.size());
                    const auto sel = args.NewSelection();
                    g_modsCaret = (std::max)(0, (std::min)(sel.EndCaretPosition, newLen));
                    g_modsSearchDirty = true;
                }
                if (sawNewline) g_modsSearchSubmit.store(true);
                args.Result(CoreTextTextUpdatingResult::Succeeded);
            });

        g_ecSelectionUpdating = g_editContext.SelectionUpdating(
            [](auto&&, CoreTextSelectionUpdatingEventArgs const& args) {
                g_modsCaret = args.Selection().EndCaretPosition;
                args.Result(CoreTextSelectionUpdatingResult::Succeeded);
            });

        g_ecLayoutRequested = g_editContext.LayoutRequested(
            [](auto&&, CoreTextLayoutRequestedEventArgs const& args) {
                winrt::Windows::Foundation::Rect rect{ 0.0f, 0.0f, 1.0f, 1.0f };
                if (g_modsCharWindow) rect = g_modsCharWindow.Bounds();
                auto bounds = args.Request().LayoutBounds();
                bounds.TextBounds(rect);
                bounds.ControlBounds(rect);
            });

        g_ecFocusRemoved = g_editContext.FocusRemoved(
            [](auto&&, auto&&) { g_modsEditFocusRemoved.store(true); });

        // the Xbox on-screen keyboard delivers Backspace/Enter as key events rather
        // than through the edit context, so catch them here and keep the context in sync
        if (g_modsCharWindow) {
            g_modsKeyDownToken = g_modsCharWindow.KeyDown(
                [](winrt::Windows::UI::Core::CoreWindow const&,
                   winrt::Windows::UI::Core::KeyEventArgs const& args) {
                    if (!g_modsSearchCapturing.load()) return;
                    const auto vk = args.VirtualKey();
                    if (vk == winrt::Windows::System::VirtualKey::Back) {
                        int newCaret = 0;
                        bool changed = false;
                        {
                            std::lock_guard<std::mutex> lk(g_modsSearchMutex);
                            int caret = (std::max)(0, (std::min)(g_modsCaret, static_cast<int>(g_modsSearchBuffer.size())));
                            if (caret > 0) {
                                g_modsSearchBuffer.erase(caret - 1, 1);
                                g_modsCaret = caret - 1;
                                newCaret = g_modsCaret;
                                changed = true;
                                g_modsSearchDirty = true;
                            }
                        }
                        if (changed && g_editContext) {
                            try {
                                using namespace winrt::Windows::UI::Text::Core;
                                g_editContext.NotifyTextChanged(CoreTextRange{ newCaret, newCaret + 1 }, 0, CoreTextRange{ newCaret, newCaret });
                                g_editContext.NotifySelectionChanged(CoreTextRange{ newCaret, newCaret });
                            } catch (...) {}
                        }
                        args.Handled(true);
                    } else if (vk == winrt::Windows::System::VirtualKey::Enter) {
                        g_modsSearchSubmit.store(true);
                        args.Handled(true);
                    }
                });
            g_modsKeyDownRegistered = true;
        }

        try { g_inputPane = winrt::Windows::UI::ViewManagement::InputPane::GetForCurrentView(); } catch (...) {}
        g_modsUsingEditContext = true;
    } catch (...) {
        g_editContext = nullptr;
        g_modsUsingEditContext = false;
    }
}

static void BeginModsSearchCapture(ICoreWindow* window) {
    if (!g_modsCharWindow) {
        try {
            winrt::Windows::UI::Core::CoreWindow w{ nullptr };
            winrt::copy_from_abi(w, window);
            g_modsCharWindow = w;
        } catch (...) {
        }
    }
    CreateModsEditContext();
    if (!g_modsUsingEditContext) {
        RegisterCharacterFallback();
    }
}

static void EndModsSearchCapture() {
    g_modsSearchCapturing.store(false);
    if (g_modsUsingEditContext && g_editContext) {
        try { g_editContext.NotifyFocusLeave(); } catch (...) {}
        if (g_inputPane) { try { g_inputPane.TryHide(); } catch (...) {} }
        try {
            g_editContext.TextRequested(g_ecTextRequested);
            g_editContext.SelectionRequested(g_ecSelectionRequested);
            g_editContext.TextUpdating(g_ecTextUpdating);
            g_editContext.SelectionUpdating(g_ecSelectionUpdating);
            g_editContext.LayoutRequested(g_ecLayoutRequested);
            g_editContext.FocusRemoved(g_ecFocusRemoved);
        } catch (...) {}
    }
    g_editContext = nullptr;
    g_inputPane = nullptr;
    g_modsUsingEditContext = false;

    if (g_modsKeyDownRegistered && g_modsCharWindow) {
        try { g_modsCharWindow.KeyDown(g_modsKeyDownToken); } catch (...) {}
    }
    g_modsKeyDownRegistered = false;
    g_modsKeyDownToken = {};

    if (g_modsCharRegistered && g_modsCharWindow) {
        try { g_modsCharWindow.CharacterReceived(g_modsCharToken); } catch (...) {}
    }
    g_modsCharRegistered = false;
    g_modsCharToken = {};
    g_modsCharWindow = nullptr;

    std::lock_guard<std::mutex> lk(g_modsSearchMutex);
    g_modsSearchBuffer.clear();
    g_modsCaret = 0;
    g_modsSearchDirty = false;
}

static void ModsSearchBeginInput() {
    g_modsEditFocusRemoved.store(false);
    g_modsSearchSubmit.store(false);
    if (g_modsUsingEditContext && g_editContext) {
        int len = 0;
        { std::lock_guard<std::mutex> lk(g_modsSearchMutex); g_modsCaret = static_cast<int>(g_modsSearchBuffer.size()); len = g_modsCaret; }
        try {
            using namespace winrt::Windows::UI::Text::Core;
            g_editContext.NotifyFocusEnter();
            g_editContext.NotifyTextChanged(CoreTextRange{ 0, 0 }, len, CoreTextRange{ len, len });
            g_editContext.NotifySelectionChanged(CoreTextRange{ len, len });
        } catch (...) {}
        if (g_inputPane) { try { g_inputPane.TryShow(); } catch (...) {} }
    } else {
        g_modsSearchCapturing.store(true);
    }
}

static void ModsSearchEndInput() {
    if (g_modsUsingEditContext && g_editContext) {
        try { g_editContext.NotifyFocusLeave(); } catch (...) {}
        if (g_inputPane) { try { g_inputPane.TryHide(); } catch (...) {} }
    } else {
        g_modsSearchCapturing.store(false);
    }
}

static bool ModsOnScreenKeyboardVisible() {
    if (!g_modsUsingEditContext || !g_inputPane) return false;
    try { return g_inputPane.Visible(); } catch (...) { return false; }
}

static void ShowModsPage(
    ICoreWindow* window,
    AuthScreenRenderer* renderer,
    AuthUiState& state,
    const std::wstring& runtimeRoot) {
    const std::wstring userModsDir = runtimeRoot + L"\\game\\user-mods";
    state.showMainMenu = false;
    state.showModsPage = true;
    state.title = L"Bandit Launcher";
    state.selectedModsTab = 0;
    state.selectedModIndex = 0;
    state.modsFocus = 0;
    state.modsScrollRow = 0;
    state.modsSearchEditing = false;
    state.modsDetailOpen = false;
    state.modsDetailScroll = 0;
    state.modsSearchQuery.clear();
    state.status = L"Loading installed mods";
    LoadModsTab(state, runtimeRoot, userModsDir);

    StartIconWorker();
    BeginModsSearchCapture(window);
    std::wstring loadedQuery = state.modsSearchQuery;

    bool upWasDown = false;
    bool downWasDown = false;
    bool leftWasDown = false;
    bool rightWasDown = false;
    bool selectWasDown = false;
    bool enterWasDown = false;
    bool backWasDown = false;
    bool pageUpWasDown = false;
    bool pageDownWasDown = false;
    bool xWasDown = false;
    bool yWasDown = false;
    bool wasOskVisible = false;

    auto enterSearch = [&]() {
        {
            std::lock_guard<std::mutex> lk(g_modsSearchMutex);
            g_modsSearchBuffer = state.modsSearchQuery;
            g_modsSearchDirty = false;
        }
        state.modsFocus = 1;
        state.modsSearchEditing = false;
    };

    auto commitSearch = [&]() {
        std::wstring buf;
        {
            std::lock_guard<std::mutex> lk(g_modsSearchMutex);
            buf = g_modsSearchBuffer;
        }
        if (buf != loadedQuery) {
            state.modsSearchQuery = buf;
            LoadModsTab(state, runtimeRoot, userModsDir);
            loadedQuery = buf;
        }
    };

    auto ensureSelectionVisible = [&]() {
        const int rowsVisible = g_modsRowsVisible.load();
        const int selRow = state.selectedModIndex / 2;
        if (selRow < state.modsScrollRow) state.modsScrollRow = selRow;
        if (selRow >= state.modsScrollRow + rowsVisible) state.modsScrollRow = selRow - rowsVisible + 1;
        if (state.modsScrollRow < 0) state.modsScrollRow = 0;
    };

    auto loadMore = [&]() {
        const bool browseTab = state.selectedModsTab == 1 || state.selectedModsTab == 2 || state.selectedModsTab == 4;
        if (state.modsExhausted || !browseTab) return;
        const char* projectType = state.selectedModsTab == 4 ? "modpack" : "mod";
        const char* index = !state.modsSearchQuery.empty()
            ? "relevance"
            : ((state.selectedModsTab == 1 || state.selectedModsTab == 4) ? "downloads" : "newest");
        const int before = static_cast<int>(state.modsCards.size());
        state.status = L"Loading more...";
        RenderAuth(renderer, state);

        int total = state.modsTotalHits;
        std::wstring error;
        if (!FetchModrinthMods(runtimeRoot, index, state.modsSearchQuery, before, kModPageSize, state.modsCards, total, error, projectType)) {
            state.modsExhausted = true;
            return;
        }
        const int after = static_cast<int>(state.modsCards.size());
        state.modsTotalHits = total;
        state.modsExhausted = after == before || after >= total;
        QueueModIconsAppend(state.modsCards, static_cast<size_t>(before));
        state.status = std::to_wstring(after) + L" of " + std::to_wstring(total);
    };

    WriteLog(L"Mods page opened");
    while (true) {
        g_modsSearchCapturing.store(state.modsSearchEditing || state.modsRenaming);
        if (state.modsSearchEditing) {
            std::lock_guard<std::mutex> lk(g_modsSearchMutex);
            state.modsSearchQuery = g_modsSearchBuffer;
        }
        if (state.modsRenaming) {
            std::lock_guard<std::mutex> lk(g_modsSearchMutex);
            state.modsRenameText = g_modsSearchBuffer;
        }

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
        const bool enterDown = AnyVirtualKeyDown(window, {
            ABI::Windows::System::VirtualKey_Enter,
            ABI::Windows::System::VirtualKey_GamepadA
        });
        const bool backDown = AnyVirtualKeyDown(window, {
            ABI::Windows::System::VirtualKey_Escape,
            ABI::Windows::System::VirtualKey_GamepadB
        });
        const bool pageUpDown = AnyVirtualKeyDown(window, {
            ABI::Windows::System::VirtualKey_PageUp,
            ABI::Windows::System::VirtualKey_GamepadLeftShoulder
        });
        const bool pageDownDown = AnyVirtualKeyDown(window, {
            ABI::Windows::System::VirtualKey_PageDown,
            ABI::Windows::System::VirtualKey_GamepadRightShoulder
        });
        const bool xDown = AnyVirtualKeyDown(window, {
            ABI::Windows::System::VirtualKey_GamepadX,
            ABI::Windows::System::VirtualKey_Delete
        });
        const bool yDown = AnyVirtualKeyDown(window, {
            ABI::Windows::System::VirtualKey_GamepadY,
            ABI::Windows::System::VirtualKey_F2
        });

        if (backDown && !backWasDown && !state.modsDetailOpen && !state.modsProfileOpen && !g_installRunning.load()) {
            WriteLog(L"Mods page closed");
            EndModsSearchCapture();
            StopIconWorker();
            state.showModsPage = false;
            state.showMainMenu = true;
            state.modsCards.clear();
            state.isError = false;
            return;
        }

        const int count = static_cast<int>(state.modsCards.size());

        const bool oskVisible = ModsOnScreenKeyboardVisible();
        const bool oskFocusRemoved = g_modsEditFocusRemoved.exchange(false);
        const bool submitRequested = g_modsSearchSubmit.exchange(false);
        if (state.modsFocus == 1 && state.modsSearchEditing &&
            (submitRequested || (wasOskVisible && !oskVisible) || oskFocusRemoved)) {
            commitSearch();
            ModsSearchEndInput();
            state.modsSearchEditing = false;
            state.modsFocus = state.modsCards.empty() ? 0 : 2;
            state.selectedModIndex = 0;
            state.modsScrollRow = 0;
        }
        if (state.modsRenaming &&
            (submitRequested || (wasOskVisible && !oskVisible) || oskFocusRemoved)) {
            std::wstring buf;
            { std::lock_guard<std::mutex> lk(g_modsSearchMutex); buf = g_modsSearchBuffer; }
            while (!buf.empty() && (buf.front() == L' ' || buf.front() == L'\t')) buf.erase(buf.begin());
            while (!buf.empty() && (buf.back() == L' ' || buf.back() == L'\t' || buf.back() == L'\r' || buf.back() == L'\n')) buf.pop_back();
            ModsSearchEndInput();
            state.modsRenaming = false;
            if (!buf.empty() && !state.modsProfileBuiltin) {
                RenameProfile(runtimeRoot, state.modsProfileId, buf);
                state.modsProfileName = buf;
                if (state.activeProfileId == state.modsProfileId) state.activeProfileName = buf;
                state.status = L"Renamed to " + buf;
            }
        }
        wasOskVisible = oskVisible;
        if (oskVisible) {
            upWasDown = upDown; downWasDown = downDown; leftWasDown = leftDown;
            rightWasDown = rightDown; selectWasDown = selectDown; enterWasDown = enterDown;
            backWasDown = backDown; pageUpWasDown = pageUpDown; pageDownWasDown = pageDownDown; xWasDown = xDown; yWasDown = yDown;
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }

        if (g_installRunning.load()) {
            const std::wstring s = GetInstallStatus();
            state.status = s.empty() ? L"Installing..." : s;
            state.isError = false;
            upWasDown = upDown; downWasDown = downDown; leftWasDown = leftDown;
            rightWasDown = rightDown; selectWasDown = selectDown; enterWasDown = enterDown;
            backWasDown = backDown; pageUpWasDown = pageUpDown; pageDownWasDown = pageDownDown; xWasDown = xDown; yWasDown = yDown;
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }
        if (g_installResultReady.exchange(false)) {
            const bool ok = g_installResultOk.load();
            const std::wstring s = GetInstallStatus();
            state.status = !s.empty() ? s : (ok ? L"Installed" : L"Install failed");
            state.isError = !ok;
            if (ok && state.selectedModsTab == 0) {
                LoadModsTab(state, runtimeRoot, userModsDir);
                loadedQuery = state.modsSearchQuery;
            }
        }

        if (state.modsProfileOpen) {
            const int pmTotal = static_cast<int>(state.modsProfileMods.size());
            const int pmRows = (std::max)(1, g_profileRowsVisible.load());
            auto pmEnsureVisible = [&]() {
                const int selRow = state.modsProfileSel / 2;
                if (selRow < state.modsProfileScroll) state.modsProfileScroll = selRow;
                else if (selRow >= state.modsProfileScroll + pmRows) state.modsProfileScroll = selRow - pmRows + 1;
            };
            const bool gridFocus = state.modsProfileFocus == 2;
            if (backDown && !backWasDown) {
                state.modsProfileOpen = false;
                state.status.clear();
            } else if (yDown && !yWasDown && !state.modsProfileBuiltin) {
                {
                    std::lock_guard<std::mutex> lk(g_modsSearchMutex);
                    g_modsSearchBuffer = state.modsProfileName;
                    g_modsCaret = static_cast<int>(g_modsSearchBuffer.size());
                }
                state.modsRenameText = state.modsProfileName;
                state.modsRenaming = true;
                ModsSearchBeginInput();
            } else if (!gridFocus && leftDown && !leftWasDown) {
                if (!state.modsProfileBuiltin) state.modsProfileFocus = 1;
            } else if (!gridFocus && rightDown && !rightWasDown) {
                state.modsProfileFocus = 0;
            } else if (!gridFocus && (downDown && !downWasDown)) {
                if (pmTotal > 0) { state.modsProfileFocus = 2; state.modsProfileSel = 0; state.modsProfileScroll = 0; }
            } else if (gridFocus && (upDown && !upWasDown)) {
                if (state.modsProfileSel < 2) { state.modsProfileFocus = 0; }
                else { state.modsProfileSel -= 2; pmEnsureVisible(); }
            } else if (gridFocus && (downDown && !downWasDown)) {
                if (state.modsProfileSel + 2 < pmTotal) { state.modsProfileSel += 2; pmEnsureVisible(); }
            } else if (gridFocus && (leftDown && !leftWasDown)) {
                if (state.modsProfileSel > 0) { state.modsProfileSel -= 1; pmEnsureVisible(); }
            } else if (gridFocus && (rightDown && !rightWasDown)) {
                if (state.modsProfileSel + 1 < pmTotal) { state.modsProfileSel += 1; pmEnsureVisible(); }
            } else if (gridFocus && (pageDownDown && !pageDownWasDown)) {
                state.modsProfileSel = (std::min)(pmTotal - 1, state.modsProfileSel + pmRows * 2); pmEnsureVisible();
            } else if (gridFocus && (pageUpDown && !pageUpWasDown)) {
                state.modsProfileSel = (std::max)(0, state.modsProfileSel - pmRows * 2); pmEnsureVisible();
            } else if (xDown && !xWasDown) {
                if (gridFocus && pmTotal > 0 && state.modsProfileSel < pmTotal) {
                    const std::wstring jar = state.modsProfileMods[static_cast<size_t>(state.modsProfileSel)];
                    DeleteFileW((ProfileModsDir(runtimeRoot, state.modsProfileId) + L"\\" + jar).c_str());
                    state.modsProfileMods = ListProfileMods(runtimeRoot, state.modsProfileId);
                    const int newTotal = static_cast<int>(state.modsProfileMods.size());
                    if (state.modsProfileSel >= newTotal) state.modsProfileSel = (std::max)(0, newTotal - 1);
                    if (newTotal == 0) state.modsProfileFocus = 0;
                    pmEnsureVisible();
                    state.status = L"Removed " + jar;
                } else if (!state.modsProfileBuiltin) {
                    const std::wstring gone = state.modsProfileName;
                    DeleteProfile(runtimeRoot, state.modsProfileId);
                    state.modsProfileOpen = false;
                    LoadModsTab(state, runtimeRoot, userModsDir);
                    state.status = L"Deleted " + gone;
                }
            } else if ((selectDown && !selectWasDown) || (enterDown && !enterWasDown)) {
                if (state.modsProfileFocus == 1 && !state.modsProfileBuiltin) {
                    const std::wstring gone = state.modsProfileName;
                    DeleteProfile(runtimeRoot, state.modsProfileId);
                    state.modsProfileOpen = false;
                    LoadModsTab(state, runtimeRoot, userModsDir);
                    state.status = L"Deleted " + gone;
                } else if (state.modsProfileFocus == 0) {
                    SetActiveProfileId(runtimeRoot, state.modsProfileId);
                    state.activeProfileId = state.modsProfileId;
                    state.activeProfileName = state.modsProfileName;
                    state.status = L"Play will use " + state.modsProfileName;
                }
            }
            upWasDown = upDown; downWasDown = downDown; leftWasDown = leftDown;
            rightWasDown = rightDown; selectWasDown = selectDown; enterWasDown = enterDown;
            backWasDown = backDown; pageUpWasDown = pageUpDown; pageDownWasDown = pageDownDown; xWasDown = xDown; yWasDown = yDown;
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }

        if (state.modsDetailOpen) {
            {
                std::lock_guard<std::mutex> lk(g_detailMutex);
                if (g_detailReadyId == state.modsDetailReqId) {
                    state.modsDetailBody = g_detailBody;
                    state.modsDetailBold = g_detailBold;
                    state.modsDetailHead = g_detailHead;
                    state.modsDetailMeta = g_detailMeta;
                    state.modsDetailLoading = false;
                }
            }
            if (backDown && !backWasDown) {
                state.modsDetailOpen = false;
                state.status.clear();
            } else if ((selectDown && !selectWasDown) || (enterDown && !enterWasDown)) {
                StartInstallJob(state.modsDetailCard, runtimeRoot);
            } else if (upDown && !upWasDown) {
                state.modsDetailScroll = (std::max)(0, state.modsDetailScroll - 2);
            } else if (downDown && !downWasDown) {
                state.modsDetailScroll = (std::min)(g_detailMaxScroll.load(), state.modsDetailScroll + 2);
            } else if (pageDownDown && !pageDownWasDown) {
                state.modsDetailScroll = (std::min)(g_detailMaxScroll.load(), state.modsDetailScroll + 8);
            } else if (pageUpDown && !pageUpWasDown) {
                state.modsDetailScroll = (std::max)(0, state.modsDetailScroll - 8);
            }
            upWasDown = upDown; downWasDown = downDown; leftWasDown = leftDown;
            rightWasDown = rightDown; selectWasDown = selectDown; enterWasDown = enterDown;
            backWasDown = backDown; pageUpWasDown = pageUpDown; pageDownWasDown = pageDownDown; xWasDown = xDown; yWasDown = yDown;
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }

        if (state.modsFocus == 0) {
            if (upDown && !upWasDown) {
                state.selectedModsTab = (state.selectedModsTab + 4) % 5;
                LoadModsTab(state, runtimeRoot, userModsDir);
                loadedQuery = state.modsSearchQuery;
            }
            if (downDown && !downWasDown) {
                state.selectedModsTab = (state.selectedModsTab + 1) % 5;
                LoadModsTab(state, runtimeRoot, userModsDir);
                loadedQuery = state.modsSearchQuery;
            }
            if ((rightDown && !rightWasDown) || (selectDown && !selectWasDown)) {
                enterSearch();
            }
        } else if (state.modsFocus == 1) {
            if (!state.modsSearchEditing) {
                if ((selectDown && !selectWasDown) || (enterDown && !enterWasDown)) {
                    state.modsSearchEditing = true;
                    ModsSearchBeginInput();
                } else if ((upDown && !upWasDown) || (leftDown && !leftWasDown)) {
                    state.modsFocus = 0;
                } else if (downDown && !downWasDown) {
                    if (!state.modsCards.empty()) {
                        state.modsFocus = 2;
                        state.selectedModIndex = 0;
                        state.modsScrollRow = 0;
                    } else {
                        state.modsFocus = 0;
                    }
                }
            } else {
                bool leaving = false;
                int nextFocus = 1;
                if (enterDown && !enterWasDown) {
                    leaving = true;
                    nextFocus = state.modsCards.empty() ? 1 : 2;
                } else if ((upDown && !upWasDown) || (leftDown && !leftWasDown)) {
                    leaving = true;
                    nextFocus = 0;
                } else if (downDown && !downWasDown) {
                    leaving = true;
                    nextFocus = state.modsCards.empty() ? 0 : 2;
                }
                if (leaving) {
                    commitSearch();
                    ModsSearchEndInput();
                    state.modsSearchEditing = false;
                    state.modsFocus = nextFocus;
                    if (nextFocus == 2) {
                        state.selectedModIndex = 0;
                        state.modsScrollRow = 0;
                    }
                }
            }
        } else {
            if (leftDown && !leftWasDown) {
                if (state.selectedModIndex % 2 == 0) {
                    state.modsFocus = 0;
                } else {
                    --state.selectedModIndex;
                }
            }
            if (rightDown && !rightWasDown && state.selectedModIndex + 1 < count) {
                ++state.selectedModIndex;
            }
            if (upDown && !upWasDown) {
                if (state.selectedModIndex < 2) {
                    enterSearch();
                } else {
                    state.selectedModIndex -= 2;
                }
            }
            if (downDown && !downWasDown && state.selectedModIndex + 2 < count) {
                state.selectedModIndex += 2;
            }
            if (pageDownDown && !pageDownWasDown && count > 0) {
                const int step = g_modsRowsVisible.load() * 2;
                state.selectedModIndex = (std::min)(state.selectedModIndex + step, count - 1);
            }
            if (pageUpDown && !pageUpWasDown) {
                const int step = g_modsRowsVisible.load() * 2;
                state.selectedModIndex = (std::max)(state.selectedModIndex - step, 0);
            }

            if (!state.modsExhausted && (state.selectedModsTab == 1 || state.selectedModsTab == 2 || state.selectedModsTab == 4) && count > 0 &&
                ((downDown && !downWasDown) || (pageDownDown && !pageDownWasDown))) {
                const int lastRow = (count - 1) / 2;
                if (state.selectedModIndex / 2 >= lastRow) {
                    loadMore();
                }
            }

            if (state.modsFocus == 2) {
                ensureSelectionVisible();
            }

            if (state.selectedModsTab == 0 && xDown && !xWasDown &&
                state.selectedModIndex >= 0 && state.selectedModIndex < count) {
                const ModCard& sel = state.modsCards[static_cast<size_t>(state.selectedModIndex)];
                if (sel.projectId == L"__profile__" && sel.filePath != kVanillaProfileId) {
                    DeleteProfile(runtimeRoot, sel.filePath);
                    const std::wstring deleted = sel.title;
                    LoadModsTab(state, runtimeRoot, userModsDir);
                    if (state.selectedModIndex >= static_cast<int>(state.modsCards.size())) {
                        state.selectedModIndex = (std::max)(0, static_cast<int>(state.modsCards.size()) - 1);
                    }
                    ensureSelectionVisible();
                    state.status = L"Deleted " + deleted;
                }
            }

            if (selectDown && !selectWasDown && state.selectedModIndex >= 0 && state.selectedModIndex < count) {
                ModCard selected = state.modsCards[static_cast<size_t>(state.selectedModIndex)];
                if (state.selectedModsTab == 0) {
                    if (selected.projectId == L"__new__") {
                        const std::wstring pid = CreateAutoProfile(runtimeRoot);
                        SetActiveProfileId(runtimeRoot, pid);
                        LoadModsTab(state, runtimeRoot, userModsDir);
                        ensureSelectionVisible();
                        state.status = L"New profile ready. Browse mods to fill it.";
                    } else if (selected.projectId == L"__profile__") {
                        state.modsProfileOpen = true;
                        state.modsProfileId = selected.filePath;
                        state.modsProfileName = selected.title;
                        state.modsProfileBuiltin = (selected.filePath == kVanillaProfileId);
                        state.modsProfileMods = ListProfileMods(runtimeRoot, selected.filePath);
                        state.modsProfileScroll = 0;
                        state.modsProfileFocus = 0;
                        state.modsProfileSel = 0;
                        state.status.clear();
                    }
                } else {
                    state.modsDetailCard = selected;
                    state.modsDetailOpen = true;
                    state.modsDetailScroll = 0;
                    state.modsDetailBody.clear();
                    state.modsDetailBold.clear();
                    state.modsDetailHead.clear();
                    state.modsDetailMeta.clear();
                    state.modsDetailLoading = true;
                    state.modsDetailReqId = StartDetailFetch(selected);
                    state.status.clear();
                    state.isError = false;
                }
            }
        }

        upWasDown = upDown;
        downWasDown = downDown;
        leftWasDown = leftDown;
        rightWasDown = rightDown;
        selectWasDown = selectDown;
        enterWasDown = enterDown;
        backWasDown = backDown;
        pageUpWasDown = pageUpDown;
        pageDownWasDown = pageDownDown;
        xWasDown = xDown; yWasDown = yDown;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

static MainMenuAction ShowMainMenu(ICoreWindow* window, const LaunchAuthConfig& authConfig, const std::wstring& runtimeRoot) {
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
    state.detail = L"Mods placeholder - no mod manager is wired yet.";

    int selected = 0;
    bool upWasDown = false;
    bool downWasDown = false;
    bool selectWasDown = false;

    WriteLog(L"Main menu opened");
    while (true) {
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
            selected = (selected + 3) % 4;
            state.detail = L"";
        }
        if (downDown && !downWasDown) {
            selected = (selected + 1) % 4;
            state.detail = L"";
        }
        if (selectDown && !selectWasDown) {
            if (selected == 0) {
                WriteLog(L"Main menu: Play selected");
                return MainMenuAction::Play;
            }
            if (selected == 1) {
                WriteLog(L"Main menu: Mods selected");
                ShowModsPage(window, renderer, state, runtimeRoot);
                state.detail = L"Browse installed, popular, and latest Fabric mods.";
            } else if (selected == 2) {
                WriteLog(L"Main menu: Repair downloads selected");
                state.status = L"Repairing downloaded files";
                state.detail = L"Downloaded Minecraft files will be rechecked";
                RenderAuth(renderer, state);
                SleepWithAuthUi(renderer, state, 350);
                return MainMenuAction::RepairDownloads;
            } else {
                WriteLog(L"Main menu: Sign out selected");
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
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

static bool RequestDeviceCode(DeviceCodeResponse& out, std::string& error) {
    const std::string body = MakeFormBody({
        { "client_id", kMicrosoftAuthClientId },
        { "scope", kMicrosoftAuthScopes }
    });
    const HttpResult response = HttpPostString(
        L"https://login.microsoftonline.com/consumers/oauth2/v2.0/devicecode",
        body,
        L"application/x-www-form-urlencoded");
    if (!response.success()) {
        error = "Device code request failed: HTTP " + std::to_string(response.status) + " " + response.body;
        return false;
    }

    out.userCode = ExtractJsonStringValue(response.body, "user_code");
    out.deviceCode = ExtractJsonStringValue(response.body, "device_code");
    out.verificationUri = ExtractJsonStringValue(response.body, "verification_uri");
    out.expiresIn = ExtractJsonIntValue(response.body, "expires_in", 900);
    out.interval = (std::max)(1, ExtractJsonIntValue(response.body, "interval", 5));

    if (out.userCode.empty() || out.deviceCode.empty() || out.verificationUri.empty()) {
        error = "Device code response was missing required fields.";
        return false;
    }

    WriteLogF(L"Device auth code received user_code=%s expires=%d interval=%d",
        a2w(out.userCode.c_str()).c_str(), out.expiresIn, out.interval);
    return true;
}

static DevicePollResult PollDeviceToken(const std::string& deviceCode) {
    DevicePollResult result;
    const std::string body = MakeFormBody({
        { "grant_type", "urn:ietf:params:oauth:grant-type:device_code" },
        { "client_id", kMicrosoftAuthClientId },
        { "device_code", deviceCode }
    });
    const HttpResult response = HttpPostString(
        L"https://login.microsoftonline.com/consumers/oauth2/v2.0/token",
        body,
        L"application/x-www-form-urlencoded");

    if (response.success()) {
        result.status = DevicePollStatus::Success;
        result.token.accessToken = ExtractJsonStringValue(response.body, "access_token");
        result.token.refreshToken = ExtractJsonStringValue(response.body, "refresh_token");
        result.token.expiresIn = ExtractJsonIntValue(response.body, "expires_in", 0);
        if (result.token.accessToken.empty()) {
            result.status = DevicePollStatus::Failed;
            result.error = "Microsoft token response did not include access_token.";
        }
        return result;
    }

    const std::string code = ExtractJsonStringValue(response.body, "error");
    if (code == "authorization_pending") {
        result.status = DevicePollStatus::Pending;
    } else if (code == "slow_down") {
        result.status = DevicePollStatus::SlowDown;
    } else {
        result.status = DevicePollStatus::Failed;
        result.error = code.empty()
            ? "Microsoft token polling failed: HTTP " + std::to_string(response.status)
            : code + ": " + ExtractJsonStringValue(response.body, "error_description");
    }
    return result;
}

static bool RefreshMicrosoftToken(const std::string& refreshToken, MicrosoftTokenResponse& out, std::string& error) {
    const std::string body = MakeFormBody({
        { "grant_type", "refresh_token" },
        { "client_id", kMicrosoftAuthClientId },
        { "refresh_token", refreshToken },
        { "scope", kMicrosoftAuthScopes }
    });
    const HttpResult response = HttpPostString(
        L"https://login.microsoftonline.com/consumers/oauth2/v2.0/token",
        body,
        L"application/x-www-form-urlencoded");
    if (!response.success()) {
        error = "Saved Microsoft session expired.";
        return false;
    }

    out.accessToken = ExtractJsonStringValue(response.body, "access_token");
    out.refreshToken = ExtractJsonStringValue(response.body, "refresh_token");
    out.expiresIn = ExtractJsonIntValue(response.body, "expires_in", 0);
    if (out.accessToken.empty()) {
        error = "Microsoft refresh response did not include access_token.";
        return false;
    }
    return true;
}

static bool AuthenticateWithXboxLive(const std::string& microsoftAccessToken, XboxAuthResponse& out, std::string& error) {
    const std::string payload =
        "{\"Properties\":{\"AuthMethod\":\"RPS\",\"SiteName\":\"user.auth.xboxlive.com\",\"RpsTicket\":\"d=" +
        JsonEscape(microsoftAccessToken) +
        "\"},\"RelyingParty\":\"http://auth.xboxlive.com\",\"TokenType\":\"JWT\"}";
    const HttpResult response = HttpPostString(
        L"https://user.auth.xboxlive.com/user/authenticate",
        payload,
        L"application/json");
    if (!response.success()) {
        error = "Xbox Live auth failed: HTTP " + std::to_string(response.status) + " " + response.body;
        return false;
    }

    out.token = ExtractJsonStringValue(response.body, "Token");
    out.userHash = ExtractJsonStringValue(response.body, "uhs");
    if (out.token.empty() || out.userHash.empty()) {
        error = "Xbox Live auth response was missing token fields.";
        return false;
    }
    return true;
}

static bool AuthorizeWithXsts(const std::string& xboxToken, const char* relyingParty, XboxAuthResponse& out, std::string& error) {
    const std::string payload =
        "{\"Properties\":{\"SandboxId\":\"RETAIL\",\"UserTokens\":[\"" +
        JsonEscape(xboxToken) +
        "\"]},\"RelyingParty\":\"" +
        JsonEscape(relyingParty) +
        "\",\"TokenType\":\"JWT\"}";
    const HttpResult response = HttpPostString(
        L"https://xsts.auth.xboxlive.com/xsts/authorize",
        payload,
        L"application/json");
    if (!response.success()) {
        error = "XSTS auth failed: HTTP " + std::to_string(response.status) + " " + response.body;
        return false;
    }

    out.token = ExtractJsonStringValue(response.body, "Token");
    out.userHash = ExtractJsonStringValue(response.body, "uhs");
    if (out.token.empty() || out.userHash.empty()) {
        error = "XSTS response was missing token fields.";
        return false;
    }
    return true;
}

static bool LoginToMinecraft(const std::string& userHash, const std::string& xstsToken, MicrosoftTokenResponse& out, std::string& error) {
    const std::string identity = "XBL3.0 x=" + userHash + ";" + xstsToken;
    const std::string payload = "{\"identityToken\":\"" + JsonEscape(identity) + "\"}";
    const HttpResult response = HttpPostString(
        L"https://api.minecraftservices.com/authentication/login_with_xbox",
        payload,
        L"application/json");
    if (!response.success()) {
        error = "Minecraft login failed: HTTP " + std::to_string(response.status) + " " + response.body;
        return false;
    }

    out.accessToken = ExtractJsonStringValue(response.body, "access_token");
    out.expiresIn = ExtractJsonIntValue(response.body, "expires_in", 0);
    if (out.accessToken.empty()) {
        error = "Minecraft login response did not include access_token.";
        return false;
    }
    return true;
}

static bool EnsureMinecraftEntitlement(const std::string& minecraftAccessToken, std::string& error) {
    const HttpResult response = HttpGetBearer(
        L"https://api.minecraftservices.com/entitlements/mcstore",
        minecraftAccessToken);
    if (!response.success()) {
        error = "Minecraft entitlement check failed: HTTP " + std::to_string(response.status) + " " + response.body;
        return false;
    }

    if (response.body.find("\"game_minecraft\"") == std::string::npos &&
        response.body.find("\"product_minecraft\"") == std::string::npos) {
        error = "This Microsoft account does not appear to own Minecraft Java Edition.";
        return false;
    }
    return true;
}

static bool FetchMinecraftProfile(const std::string& minecraftAccessToken, LaunchAuthConfig& out, std::string& error) {
    const HttpResult response = HttpGetBearer(
        L"https://api.minecraftservices.com/minecraft/profile",
        minecraftAccessToken);
    if (!response.success()) {
        error = "Minecraft profile request failed: HTTP " + std::to_string(response.status) + " " + response.body;
        return false;
    }

    out.uuid = NormalizeMinecraftUuid(ExtractJsonStringValue(response.body, "id"));
    out.username = ExtractJsonStringValue(response.body, "name");
    out.accessToken = minecraftAccessToken;
    if (out.uuid.empty() || out.username.empty()) {
        error = "Minecraft profile response was missing id or name.";
        return false;
    }
    return true;
}

static bool BuildMinecraftAuth(const std::string& microsoftAccessToken, LaunchAuthConfig& out, std::string& error) {
    XboxAuthResponse xbl;
    if (!AuthenticateWithXboxLive(microsoftAccessToken, xbl, error)) {
        return false;
    }

    XboxAuthResponse xsts;
    if (!AuthorizeWithXsts(xbl.token, "rp://api.minecraftservices.com/", xsts, error)) {
        return false;
    }

    MicrosoftTokenResponse minecraftToken;
    if (!LoginToMinecraft(xsts.userHash, xsts.token, minecraftToken, error)) {
        return false;
    }

    if (!EnsureMinecraftEntitlement(minecraftToken.accessToken, error)) {
        return false;
    }

    if (!FetchMinecraftProfile(minecraftToken.accessToken, out, error)) {
        return false;
    }

    WriteLogF(L"Minecraft auth resolved username=%s uuid=%s",
        a2w(out.username.c_str()).c_str(),
        a2w(out.uuid.c_str()).c_str());
    return true;
}

static bool ResolveLaunchAuthConfig(ICoreWindow* window, LaunchAuthConfig& out) {
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

static bool RedirectStdStreams(const std::wstring& path) {
    int fd = -1;
    if (_wsopen_s(&fd, path.c_str(), _O_CREAT | _O_TRUNC | _O_WRONLY | _O_TEXT,
        _SH_DENYNO, _S_IREAD | _S_IWRITE) != 0 || fd < 0) {
        return false;
    }

    if (_dup2(fd, 1) != 0) {
        _close(fd);
        return false;
    }
    if (_dup2(fd, 2) != 0) {
        _close(fd);
        return false;
    }
    _close(fd);

    FILE* out = _fdopen(1, "w");
    FILE* err = _fdopen(2, "w");
    if (!out || !err) {
        return false;
    }
    *stdout = *out;
    *stderr = *err;
    setvbuf(stdout, nullptr, _IONBF, 0);
    setvbuf(stderr, nullptr, _IONBF, 0);
    return true;
}

static bool PublishCoreWindowProperty(ICoreWindow* window) {
    if (!window) return false;

    ComPtr<ICoreApplication> coreApp;
    HRESULT hr = GetActivationFactory(
        HStringReference(RuntimeClass_Windows_ApplicationModel_Core_CoreApplication).Get(),
        &coreApp);
    if (FAILED(hr)) {
        WriteLogF(L"CoreApplication activation failed hr=0x%08X", hr);
        return false;
    }

    ComPtr<IPropertySet> props;
    hr = coreApp->get_Properties(props.GetAddressOf());
    if (FAILED(hr)) {
        WriteLogF(L"CoreApplication.get_Properties failed hr=0x%08X", hr);
        return false;
    }

    ComPtr<IMap<HSTRING, IInspectable*>> propMap;
    hr = props.As(&propMap);
    if (FAILED(hr)) {
        WriteLogF(L"CoreApplication properties As(IMap) failed hr=0x%08X", hr);
        return false;
    }

    boolean replaced = false;
    hr = propMap->Insert(HStringReference(kEGLNativeWindowTypeProperty).Get(), window, &replaced);
    if (FAILED(hr)) {
        WriteLogF(L"CoreApplication properties insert failed hr=0x%08X", hr);
        return false;
    }

    Rect bounds = {};
    if (SUCCEEDED(window->get_Bounds(&bounds))) {
        WriteLogF(L"Published CoreWindow for EGL (%dx%d)%s",
            (int)bounds.Width, (int)bounds.Height, replaced ? L" [replaced]" : L"");
    } else {
        WriteLogF(L"Published CoreWindow for EGL%s", replaced ? L" [replaced]" : L"");
    }
    return true;
}

static bool PreloadJvm(const std::wstring& exeDir, const std::wstring& jreDir, const std::wstring& nativesDir, HMODULE* jvmModule) {
    const std::wstring jreBin = jreDir + L"\\bin";
    const std::wstring jreServer = jreBin + L"\\server";
    const std::wstring path = jreBin + L";" + jreServer + L";" + exeDir + L";" + nativesDir + L";" + GetEnvVarString(L"PATH");
    SetEnvironmentVariableW(L"PATH", path.c_str());
    SetEnvironmentVariableW(L"JAVA_HOME", jreDir.c_str());

    auto loadPackaged = [&](const wchar_t* relativePath, const wchar_t* label) -> HMODULE {
        HMODULE module = LoadPackagedLibrary(relativePath, 0);
        if (!module) {
            WriteLogF(L"LoadPackagedLibrary(%s) failed err=%u", label, GetLastError());
        }
        return module;
    };

    // Preload the JRE CRT/runtime DLLs from jre\bin so jvm.dll can resolve
    // its non-system imports while running inside the app package.
	loadPackaged(L"jre\\bin\\vcruntime140.dll", L"vcruntime140.dll");
    loadPackaged(L"jre\\bin\\vcruntime140_1.dll", L"vcruntime140_1.dll");
    loadPackaged(L"jre\\bin\\msvcp140.dll", L"msvcp140.dll");
    loadPackaged(L"jre\\bin\\jli.dll", L"jli.dll");

    *jvmModule = loadPackaged(L"jre\\bin\\server\\jvm.dll", L"jvm.dll");
    if (!*jvmModule) {
        return false;
    }
	
	loadPackaged(L"jre\\bin\\java.dll", L"java.dll");

    WriteLog(L"JVM DLLs loaded");
    return true;
}

static bool CheckAndLogJavaException(JNIEnv* env, const wchar_t* stage) {
    if (!env->ExceptionCheck()) return false;
    WriteLogF(L"Java exception during %s", stage);

    jthrowable throwable = env->ExceptionOccurred();
    env->ExceptionClear();

    if (!throwable) {
        WriteLog(L"Java exception object was null after ExceptionOccurred");
        return true;
    }

    jclass stringWriterClass = env->FindClass("java/io/StringWriter");
    jclass printWriterClass = env->FindClass("java/io/PrintWriter");
    jclass throwableClass = env->FindClass("java/lang/Throwable");
    if (!stringWriterClass || !printWriterClass || !throwableClass) {
        WriteLog(L"Unable to load Java exception formatting classes");
        env->ExceptionClear();
        env->DeleteLocalRef(throwable);
        return true;
    }

    jmethodID stringWriterCtor = env->GetMethodID(stringWriterClass, "<init>", "()V");
    jmethodID printWriterCtor = env->GetMethodID(printWriterClass, "<init>", "(Ljava/io/Writer;)V");
    jmethodID printStackTrace = env->GetMethodID(throwableClass, "printStackTrace", "(Ljava/io/PrintWriter;)V");
    jmethodID toString = env->GetMethodID(stringWriterClass, "toString", "()Ljava/lang/String;");
    if (!stringWriterCtor || !printWriterCtor || !printStackTrace || !toString || env->ExceptionCheck()) {
        WriteLog(L"Unable to resolve Java exception formatting methods");
        env->ExceptionClear();
        env->DeleteLocalRef(throwable);
        env->DeleteLocalRef(stringWriterClass);
        env->DeleteLocalRef(printWriterClass);
        env->DeleteLocalRef(throwableClass);
        return true;
    }

    jobject stringWriter = env->NewObject(stringWriterClass, stringWriterCtor);
    jobject printWriter = stringWriter ? env->NewObject(printWriterClass, printWriterCtor, stringWriter) : nullptr;
    if (!stringWriter || !printWriter || env->ExceptionCheck()) {
        WriteLog(L"Unable to create Java exception formatter");
        env->ExceptionClear();
        env->DeleteLocalRef(throwable);
        env->DeleteLocalRef(stringWriterClass);
        env->DeleteLocalRef(printWriterClass);
        env->DeleteLocalRef(throwableClass);
        return true;
    }

    env->CallVoidMethod(throwable, printStackTrace, printWriter);
    jstring trace = static_cast<jstring>(env->CallObjectMethod(stringWriter, toString));
    if (trace && !env->ExceptionCheck()) {
        const char* utf8 = env->GetStringUTFChars(trace, nullptr);
        if (utf8) {
            const std::wstring wideTrace = a2w(utf8);
            WriteLogF(L"Java exception stack:\n%s", wideTrace.c_str());
            env->ReleaseStringUTFChars(trace, utf8);
        }
    } else {
        WriteLog(L"Unable to stringify Java exception stack");
        env->ExceptionClear();
    }

    if (trace) env->DeleteLocalRef(trace);
    env->DeleteLocalRef(printWriter);
    env->DeleteLocalRef(stringWriter);
    env->DeleteLocalRef(throwable);
    env->DeleteLocalRef(stringWriterClass);
    env->DeleteLocalRef(printWriterClass);
    env->DeleteLocalRef(throwableClass);
    return true;
}

static std::wstring JStringToWide(JNIEnv* env, jstring value) {
    if (!env || !value) return std::wstring();
    const char* utf8 = env->GetStringUTFChars(value, nullptr);
    if (!utf8) {
        env->ExceptionClear();
        return std::wstring();
    }
    std::wstring wide = a2w(utf8);
    env->ReleaseStringUTFChars(value, utf8);
    return wide;
}

static std::wstring JavaObjectToWideString(JNIEnv* env, jobject object) {
    if (!env || !object) return std::wstring();
    jclass objectClass = env->FindClass("java/lang/Object");
    if (!objectClass) {
        env->ExceptionClear();
        return std::wstring();
    }
    jmethodID toString = env->GetMethodID(objectClass, "toString", "()Ljava/lang/String;");
    env->DeleteLocalRef(objectClass);
    if (!toString) {
        env->ExceptionClear();
        return std::wstring();
    }
    jstring stringValue = static_cast<jstring>(env->CallObjectMethod(object, toString));
    if (!stringValue || env->ExceptionCheck()) {
        env->ExceptionClear();
        return std::wstring();
    }
    std::wstring wide = JStringToWide(env, stringValue);
    env->DeleteLocalRef(stringValue);
    return wide;
}

static void DumpJavaThreadStacks(JavaVM* vm, const wchar_t* reason) {
    if (!vm) return;

    JNIEnv* env = nullptr;
    bool attached = false;
    jint envResult = vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_8);
    if (envResult == JNI_EDETACHED) {
        if (vm->AttachCurrentThread(reinterpret_cast<void**>(&env), nullptr) != JNI_OK || !env) {
            WriteLog(L"Java thread dump failed: AttachCurrentThread failed");
            return;
        }
        attached = true;
    } else if (envResult != JNI_OK || !env) {
        WriteLogF(L"Java thread dump failed: GetEnv => %d", envResult);
        return;
    }

    WriteLogF(L"Java thread dump begin: %s", reason ? reason : L"watchdog");

    jclass threadClass = env->FindClass("java/lang/Thread");
    jclass mapClass = env->FindClass("java/util/Map");
    jclass setClass = env->FindClass("java/util/Set");
    jclass iteratorClass = env->FindClass("java/util/Iterator");
    jclass entryClass = env->FindClass("java/util/Map$Entry");
    if (!threadClass || !mapClass || !setClass || !iteratorClass || !entryClass || env->ExceptionCheck()) {
        env->ExceptionClear();
        WriteLog(L"Java thread dump failed: class lookup failed");
        goto done;
    }

    {
        jmethodID getAllStackTraces = env->GetStaticMethodID(threadClass, "getAllStackTraces", "()Ljava/util/Map;");
        jmethodID getName = env->GetMethodID(threadClass, "getName", "()Ljava/lang/String;");
        jmethodID getState = env->GetMethodID(threadClass, "getState", "()Ljava/lang/Thread$State;");
        jmethodID entrySet = env->GetMethodID(mapClass, "entrySet", "()Ljava/util/Set;");
        jmethodID iterator = env->GetMethodID(setClass, "iterator", "()Ljava/util/Iterator;");
        jmethodID hasNext = env->GetMethodID(iteratorClass, "hasNext", "()Z");
        jmethodID next = env->GetMethodID(iteratorClass, "next", "()Ljava/lang/Object;");
        jmethodID getKey = env->GetMethodID(entryClass, "getKey", "()Ljava/lang/Object;");
        jmethodID getValue = env->GetMethodID(entryClass, "getValue", "()Ljava/lang/Object;");
        if (!getAllStackTraces || !getName || !getState || !entrySet || !iterator ||
            !hasNext || !next || !getKey || !getValue || env->ExceptionCheck()) {
            env->ExceptionClear();
            WriteLog(L"Java thread dump failed: method lookup failed");
            goto done;
        }

        jobject traces = env->CallStaticObjectMethod(threadClass, getAllStackTraces);
        jobject entries = traces ? env->CallObjectMethod(traces, entrySet) : nullptr;
        jobject iter = entries ? env->CallObjectMethod(entries, iterator) : nullptr;
        if (!iter || env->ExceptionCheck()) {
            env->ExceptionClear();
            WriteLog(L"Java thread dump failed: iterator creation failed");
            if (traces) env->DeleteLocalRef(traces);
            if (entries) env->DeleteLocalRef(entries);
            goto done;
        }

        int threadCount = 0;
        while (threadCount < 64 && env->CallBooleanMethod(iter, hasNext) == JNI_TRUE && !env->ExceptionCheck()) {
            jobject entry = env->CallObjectMethod(iter, next);
            jobject thread = entry ? env->CallObjectMethod(entry, getKey) : nullptr;
            jobjectArray frames = entry ? static_cast<jobjectArray>(env->CallObjectMethod(entry, getValue)) : nullptr;
            if (env->ExceptionCheck()) {
                env->ExceptionClear();
                WriteLog(L"Java thread dump stopped: entry read failed");
                if (entry) env->DeleteLocalRef(entry);
                break;
            }

            jstring nameString = thread ? static_cast<jstring>(env->CallObjectMethod(thread, getName)) : nullptr;
            jobject stateObject = thread ? env->CallObjectMethod(thread, getState) : nullptr;
            std::wstring name = JStringToWide(env, nameString);
            std::wstring state = JavaObjectToWideString(env, stateObject);
            const jsize frameCount = frames ? env->GetArrayLength(frames) : 0;

            WriteLogF(L"  Thread \"%s\" state=%s frames=%d",
                name.empty() ? L"?" : name.c_str(),
                state.empty() ? L"?" : state.c_str(),
                static_cast<int>(frameCount));

            const jsize framesToLog = frameCount < 12 ? frameCount : 12;
            for (jsize i = 0; i < framesToLog; ++i) {
                jobject frame = env->GetObjectArrayElement(frames, i);
                std::wstring frameText = JavaObjectToWideString(env, frame);
                WriteLogF(L"    at %s", frameText.empty() ? L"?" : frameText.c_str());
                if (frame) env->DeleteLocalRef(frame);
            }

            if (nameString) env->DeleteLocalRef(nameString);
            if (stateObject) env->DeleteLocalRef(stateObject);
            if (frames) env->DeleteLocalRef(frames);
            if (thread) env->DeleteLocalRef(thread);
            if (entry) env->DeleteLocalRef(entry);
            ++threadCount;
        }

        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            WriteLog(L"Java thread dump ended after clearing an exception");
        }
        WriteLogF(L"Java thread dump end: %d threads", threadCount);

        env->DeleteLocalRef(iter);
        env->DeleteLocalRef(entries);
        env->DeleteLocalRef(traces);
    }

done:
    if (entryClass) env->DeleteLocalRef(entryClass);
    if (iteratorClass) env->DeleteLocalRef(iteratorClass);
    if (setClass) env->DeleteLocalRef(setClass);
    if (mapClass) env->DeleteLocalRef(mapClass);
    if (threadClass) env->DeleteLocalRef(threadClass);
    if (attached) {
        vm->DetachCurrentThread();
    }
}

static bool RunEmbeddedMinecraft(const std::wstring& exeDir,
    const std::wstring& packageDir,
    const std::wstring& jreDir,
    const std::wstring& gameDir,
    const std::wstring& assetsDir,
    const std::wstring& nativesDir,
    const std::wstring& bundledModsDir,
    const std::wstring& userModsDir,
    const std::wstring& clientJar,
    const std::wstring& javaLog,
    const std::wstring& argsPath,
    const std::wstring& classPath,
    const LaunchAuthConfig& authConfig)
{
    const std::wstring jnaTmpDir = nativesDir;
    const std::wstring lwjglTmpDir = exeDir + L"\\tmp";
    const std::wstring packagedNativesDir = packageDir + L"\\natives";
    const std::wstring lwjglNativeDir =
        GetFileAttributesW((packagedNativesDir + L"\\lwjgl.dll").c_str()) != INVALID_FILE_ATTRIBUTES &&
        GetFileAttributesW((packagedNativesDir + L"\\glfw.dll").c_str()) != INVALID_FILE_ATTRIBUTES
            ? packagedNativesDir
            : nativesDir;
    const std::wstring lwjglGlfwDll = lwjglNativeDir + L"\\glfw.dll";
    const std::wstring logConfigPath = gameDir + L"\\log_configs\\client-uwp.xml";
    const std::wstring fabricLogPath = gameDir + L"\\logs\\fabric-loader.log";
    const std::wstring latestLogPath = gameDir + L"\\logs\\latest.log";
    const std::wstring xboxCompatLogPath = gameDir + L"\\xbox_compat.log";

    EnsureDirectoryTree(gameDir + L"\\logs");
    EnsureDirectoryTree(gameDir + L"\\crash-reports");
    EnsureDirectoryTree(userModsDir);
    EnsureDirectoryTree(lwjglTmpDir);
    DeleteFileW(fabricLogPath.c_str());
    DeleteFileW(latestLogPath.c_str());
    DeleteFileW(xboxCompatLogPath.c_str());

    if (!RedirectStdStreams(javaLog)) {
        WriteLogF(L"Failed to redirect stdout/stderr errno=%d winerr=%u", errno, GetLastError());
    } else {
        WriteLog(L"stdout/stderr redirected to java_output.log");
    }

    const std::vector<LogTailerConfig> tailerConfigs = {
        LogTailerConfig{ javaLog, L"java_output.log" },
        LogTailerConfig{ fabricLogPath, L"fabric-loader.log" },
        LogTailerConfig{ latestLogPath, L"latest.log" },
        LogTailerConfig{ xboxCompatLogPath, L"xbox_compat.log" }
    };
    LogTailerGuard logTailers(StartLogTailers(tailerConfigs));

    FILE* af = nullptr;
    _wfopen_s(&af, argsPath.c_str(), L"w");
    if (!af) {
        WriteLogF(L"FAILED args file err=%u", GetLastError());
        return false;
    }

    std::vector<std::string> vmOptionStorage;
    vmOptionStorage.reserve(16);
    vmOptionStorage.push_back("-Xmx4G");
    vmOptionStorage.push_back("-Xms512M");
    vmOptionStorage.push_back("--enable-native-access=ALL-UNNAMED");
    vmOptionStorage.push_back("--add-opens=jdk.zipfs/jdk.nio.zipfs=ALL-UNNAMED");
    const std::wstring localJavaSecurityPatch = exeDir + L"\\java-base-security-realpath.jar";
    const std::wstring packagedJavaSecurityPatch = packageDir + L"\\java-base-security-realpath.jar";
    const std::wstring javaSecurityPatch =
        GetFileAttributesW(localJavaSecurityPatch.c_str()) != INVALID_FILE_ATTRIBUTES
            ? localJavaSecurityPatch
            : packagedJavaSecurityPatch;
    if (GetFileAttributesW(javaSecurityPatch.c_str()) != INVALID_FILE_ATTRIBUTES) {
        vmOptionStorage.push_back("--patch-module=java.base=" + w2a(fwd(javaSecurityPatch)));
        WriteLogF(L"Java security realpath patch enabled: %s", javaSecurityPatch.c_str());
    } else {
        WriteLogF(L"Java security realpath patch missing: %s", javaSecurityPatch.c_str());
    }
    vmOptionStorage.push_back("-Djava.home=" + w2a(fwd(jreDir)));
    vmOptionStorage.push_back("-Djava.security.properties==" + w2a(fwd(jreDir + L"\\conf\\security\\xbox.properties")));
    vmOptionStorage.push_back("-Djava.security.egd=file:/dev/urandom");
    vmOptionStorage.push_back("-Dfabric.log.file=" + w2a(fwd(fabricLogPath)));
    vmOptionStorage.push_back("-Dfabric.log.level=debug");
    vmOptionStorage.push_back("-Dfabric.debug.throwDirectly=true");
    vmOptionStorage.push_back("-Dmixin.debug.verbose=true");
    vmOptionStorage.push_back("-Djava.io.tmpdir=" + w2a(fwd(jnaTmpDir)));
    vmOptionStorage.push_back("-Djna.tmpdir=" + w2a(fwd(jnaTmpDir)));
    vmOptionStorage.push_back("-Djna.nosys=true");
    vmOptionStorage.push_back("-Djna.nounpack=true");
    vmOptionStorage.push_back("-Djna.boot.library.name=jnidispatch");
    vmOptionStorage.push_back("-Djna.boot.library.path=" + w2a(fwd(nativesDir)));
    vmOptionStorage.push_back("-Djava.library.path=" + w2a(fwd(lwjglNativeDir)));
    vmOptionStorage.push_back("-Dorg.lwjgl.librarypath=" + w2a(fwd(lwjglNativeDir)));
    vmOptionStorage.push_back("-Dorg.lwjgl.util.Debug=true");
    vmOptionStorage.push_back("-Dorg.lwjgl.util.DebugLoader=true");
    vmOptionStorage.push_back("-Dorg.lwjgl.system.SharedLibraryExtractDirectory=" + w2a(fwd(lwjglTmpDir)));
    vmOptionStorage.push_back("-Dorg.lwjgl.glfw.libname=" + w2a(fwd(lwjglGlfwDll)));
    WriteLogF(L"LWJGL native directory: %s", lwjglNativeDir.c_str());
    WriteLogF(L"LWJGL GLFW library forced: %s", lwjglGlfwDll.c_str());
    std::wstring graphicsRuntime = GetEnvVarString(L"MC_GRAPHICS_RUNTIME");
    if (graphicsRuntime.empty()) {
        graphicsRuntime = L"mesa";
    }
    const std::wstring packagedOpenGl = packageDir + L"\\graphics\\" + graphicsRuntime + L"\\opengl32.dll";
    const std::wstring localOpenGl = exeDir + L"\\graphics\\" + graphicsRuntime + L"\\opengl32.dll";
    const std::wstring selectedOpenGl =
        GetFileAttributesW(packagedOpenGl.c_str()) != INVALID_FILE_ATTRIBUTES
            ? packagedOpenGl
            : localOpenGl;
    if (GetFileAttributesW(selectedOpenGl.c_str()) != INVALID_FILE_ATTRIBUTES) {
        vmOptionStorage.push_back("-Dorg.lwjgl.opengl.libname=" + w2a(fwd(selectedOpenGl)));
        WriteLogF(L"LWJGL OpenGL library forced: %s", selectedOpenGl.c_str());
    } else {
        WriteLogF(L"LWJGL OpenGL library override missing: %s", selectedOpenGl.c_str());
    }
    vmOptionStorage.push_back("-Dfabric.gameJarPath=" + w2a(fwd(clientJar)));
    vmOptionStorage.push_back("-Dfabric.modsFolder=" + w2a(fwd(userModsDir)));
    if (GetFileAttributesW(bundledModsDir.c_str()) != INVALID_FILE_ATTRIBUTES) {
        vmOptionStorage.push_back("-Dfabric.addMods=" + w2a(fwd(bundledModsDir)));
    }
    vmOptionStorage.push_back("-Djava.class.path=" + w2a(classPath));
    vmOptionStorage.push_back("-Duser.dir=" + w2a(fwd(gameDir)));
    vmOptionStorage.push_back("-Dlog4j.configurationFile=" + w2a(fwd(logConfigPath)));
    vmOptionStorage.push_back("-XX:ErrorFile=" + w2a(fwd(gameDir + L"\\hs_err_pid%p.log")));

    const std::vector<std::string> appArgs = {
        "--username", authConfig.username,
        "--version", kFabricLaunchVersion,
        "--gameDir", w2a(fwd(gameDir)),
        "--assetsDir", w2a(fwd(assetsDir)),
        "--assetIndex", kMinecraftAssetIndex,
        "--uuid", authConfig.uuid,
        "--accessToken", authConfig.accessToken,
        "--versionType", "release"
    };

    fprintf(af, "# JVM options\n");
    for (const auto& opt : vmOptionStorage) {
        fprintf(af, "%s\n", opt.c_str());
    }
    fprintf(af, "# Main class\nnet.fabricmc.loader.impl.launch.knot.KnotClient\n");
    fprintf(af, "# App args\n");
    for (const auto& arg : appArgs) {
        fprintf(af, "%s\n", arg.c_str());
    }
    fclose(af);
    WriteLog(L"Embedded JVM options written");

    HMODULE jvmModule = nullptr;
    if (!PreloadJvm(exeDir, jreDir, nativesDir, &jvmModule)) {
        return false;
    }

    auto createJavaVm = reinterpret_cast<JNI_CreateJavaVM_t>(GetProcAddress(jvmModule, "JNI_CreateJavaVM"));
    if (!createJavaVm) {
        WriteLogF(L"GetProcAddress(JNI_CreateJavaVM) failed err=%u", GetLastError());
        return false;
    }

    std::vector<JavaVMOption> vmOptions(vmOptionStorage.size());
    for (size_t i = 0; i < vmOptionStorage.size(); ++i) {
        vmOptions[i].optionString = const_cast<char*>(vmOptionStorage[i].c_str());
        vmOptions[i].extraInfo = nullptr;
    }

    JavaVMInitArgs vmArgs = {};
    vmArgs.version = JNI_VERSION_21;
    vmArgs.nOptions = static_cast<jint>(vmOptions.size());
    vmArgs.options = vmOptions.data();
    vmArgs.ignoreUnrecognized = JNI_FALSE;

    JavaVM* vm = nullptr;
    JNIEnv* env = nullptr;
    const jint createResult = createJavaVm(&vm, reinterpret_cast<void**>(&env), &vmArgs);
    WriteLogF(L"JNI_CreateJavaVM => %d", createResult);
    if (createResult != JNI_OK || !vm || !env) {
        return false;
    }

    jclass mainClass = env->FindClass("net/fabricmc/loader/impl/launch/knot/KnotClient");
    if (!mainClass || CheckAndLogJavaException(env, L"FindClass(KnotClient)")) {
        return false;
    }

    jmethodID mainMethod = env->GetStaticMethodID(mainClass, "main", "([Ljava/lang/String;)V");
    if (!mainMethod || CheckAndLogJavaException(env, L"GetStaticMethodID(main)")) {
        return false;
    }

    jclass stringClass = env->FindClass("java/lang/String");
    if (!stringClass || CheckAndLogJavaException(env, L"FindClass(String)")) {
        return false;
    }

    jobjectArray argv = env->NewObjectArray(static_cast<jsize>(appArgs.size()), stringClass, nullptr);
    if (!argv || CheckAndLogJavaException(env, L"NewObjectArray")) {
        return false;
    }

    for (jsize i = 0; i < static_cast<jsize>(appArgs.size()); ++i) {
        jstring value = env->NewStringUTF(appArgs[i].c_str());
        if (!value || CheckAndLogJavaException(env, L"NewStringUTF")) {
            return false;
        }
        env->SetObjectArrayElement(argv, i, value);
        env->DeleteLocalRef(value);
        if (CheckAndLogJavaException(env, L"SetObjectArrayElement")) {
            return false;
        }
    }

    WriteLog(L"Invoking KnotClient.main via embedded JVM");
    std::atomic<bool> javaMainRunning{ true };
    std::thread javaMainWatchdog([&javaMainRunning, vm]() {
        unsigned seconds = 0;
        while (javaMainRunning.load()) {
            std::this_thread::sleep_for(std::chrono::seconds(5));
            seconds += 5;
            if (javaMainRunning.load()) {
                WriteLogF(L"KnotClient.main still running after %u seconds", seconds);
                if (seconds == 15 || (seconds >= 30 && (seconds % 30) == 0)) {
                    DumpJavaThreadStacks(vm, L"KnotClient.main watchdog");
                }
            }
        }
    });

    env->CallStaticVoidMethod(mainClass, mainMethod, argv);
    javaMainRunning.store(false);
    if (javaMainWatchdog.joinable()) {
        javaMainWatchdog.join();
    }

    if (CheckAndLogJavaException(env, L"CallStaticVoidMethod(main)")) {
        LogTextFileTail(javaLog, L"java_output.log");
        WriteLog(L"Embedded JVM failed after startup; terminating host process to avoid JVM/native reuse");
        ExitProcess(1);
        return false;
    }

    WriteLog(L"KnotClient.main returned");
    g_minecraftRunning.store(false);
    WriteLog(L"Minecraft exited; terminating host process to avoid JVM/native reuse on relaunch");
    ExitProcess(0);
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
            g_logDir = GetExecutableDir();
        }
        EnsureDirectoryTree(g_logDir);

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

        g_logDir = exeDir;
        EnsureDirectoryTree(g_logDir);
        SetCurrentDirectoryW(exeDir.c_str());
        SetEnvironmentVariableW(L"MC_RUNTIME_DIR", exeDir.c_str());
        const std::wstring graphicsRuntime = DetectGraphicsRuntimeName();
        SetEnvironmentVariableW(L"MC_GRAPHICS_RUNTIME", graphicsRuntime.c_str());
        const std::wstring mobileGluesDir = exeDir + L"\\mobileglues";
        EnsureDirectoryTree(mobileGluesDir);
        SetEnvironmentVariableW(L"MG_DIR_PATH", mobileGluesDir.c_str());

        wchar_t lp[MAX_PATH];
        swprintf_s(lp, L"%s\\mc_launch.log", exeDir.c_str());
        FILE* clf = nullptr;
        _wfopen_s(&clf, lp, L"w");
        if (clf) fclose(clf);

        WriteLog(L"=== MC.App Run() started ===");
        WriteLogF(L"graphicsRuntime=%s", graphicsRuntime.c_str());
        WriteLogF(L"MG_DIR_PATH=%s", mobileGluesDir.c_str());
        WriteLogF(L"SetWindow called=%d", g_setWindowCalled ? 1 : 0);
        WriteLogF(L"SetWindow QueryInterface hr=0x%08X", g_windowInteropHr);
        WriteLogF(L"SetWindow get_WindowHandle hr=0x%08X", g_getWindowHandleHr);
        WriteLogF(L"Stored HWND=0x%p", g_windowHandle);
        wchar_t cwd[MAX_PATH] = {};
        GetCurrentDirectoryW(MAX_PATH, cwd);
        WriteLogF(L"cwd=%s", cwd);
        if (g_windowHandle) {
            if (WriteHwndFile(exeDir, g_windowHandle)) {
                WriteLog(L"Run: rewrote hwnd.txt from stored HWND");
            } else {
                WriteLogF(L"Run: failed to rewrite hwnd.txt err=%u", GetLastError());
            }
        }

        bool repairDownloads = false;
        LaunchAuthConfig authConfig;
        while (true) {
            if (!ResolveLaunchAuthConfig(g_authWindow.Get(), authConfig)) {
                WriteLog(L"Dynamic authentication failed");
                return E_FAIL;
            }

            const MainMenuAction menuAction = ShowMainMenu(g_authWindow.Get(), authConfig, exeDir);
            if (menuAction == MainMenuAction::Play) {
                break;
            }
            if (menuAction == MainMenuAction::RepairDownloads) {
                repairDownloads = true;
                break;
            }

            ClearRefreshToken();
            WriteLog(L"Saved Microsoft refresh token cleared by sign out");
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

        {
            AuthScreenRenderer downloadRendererInstance;
            AuthScreenRenderer* downloadRenderer = nullptr;
            if (downloadRendererInstance.Initialize(g_authWindow.Get())) {
                downloadRenderer = &downloadRendererInstance;
            }

            AuthUiState downloadState;
            const std::wstring manifestPath = packageDir + L"\\download_manifest.tsv";
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

        const std::wstring localJreDir = exeDir + L"\\jre";
        const std::wstring packageJreDir = packageDir + L"\\jre";
        const std::wstring jreDir =
            GetFileAttributesW((packageJreDir + L"\\bin\\java.exe").c_str()) != INVALID_FILE_ATTRIBUTES
                ? packageJreDir
                : localJreDir;
        const std::wstring gameDir = exeDir + L"\\game";
        const std::wstring javaExe = jreDir + L"\\bin\\java.exe";
        const std::wstring assetsDir = exeDir + L"\\assets";
        const std::wstring localNativesDir = exeDir + L"\\natives";
        const std::wstring packageNativesDir = packageDir + L"\\natives";
        const std::wstring nativesDir =
            GetFileAttributesW((packageNativesDir + L"\\lwjgl.dll").c_str()) != INVALID_FILE_ATTRIBUTES &&
            GetFileAttributesW((packageNativesDir + L"\\glfw.dll").c_str()) != INVALID_FILE_ATTRIBUTES
                ? packageNativesDir
                : localNativesDir;
        const std::wstring minecraftVersion = kMinecraftVersionW;
        const std::wstring packageRuntimeDir = packageDir + L"\\runtime";
        const std::wstring packagedLibrariesDir = packageRuntimeDir + L"\\libraries";
        const std::wstring bundledModsDir = gameDir + L"\\mods";
        EnsureProfilesInitialized(exeDir);
        const std::wstring activeProfile = GetActiveProfileId(exeDir);
        const std::wstring userModsDir = ProfileModsDir(exeDir, activeProfile);
        const std::wstring clientJar = gameDir + L"\\versions\\" + minecraftVersion + L"\\" + minecraftVersion + L".jar";
        const std::wstring argsPath = exeDir + L"\\java_args.txt";
        const std::wstring javaLog = exeDir + L"\\java_output.log";

        WriteLogF(L"exeDir: %s", exeDir.c_str());
        WriteLogF(L"jreDir: %s", jreDir.c_str());
        WriteLogF(L"jre release stamp: %s", FileStamp(jreDir + L"\\release").c_str());
        WriteLogF(L"local jre release stamp: %s", FileStamp(localJreDir + L"\\release").c_str());
        WriteLogF(L"package jre release stamp: %s", FileStamp(packageJreDir + L"\\release").c_str());
        WriteLogF(L"nativesDir: %s", nativesDir.c_str());
        WriteLogF(L"packagedLibrariesDir: %s", packagedLibrariesDir.c_str());
        WriteLogF(L"downloadedLibrariesDir: %s", (gameDir + L"\\libraries").c_str());
        WriteLogF(L"bundledModsDir: %s", bundledModsDir.c_str());
        WriteLogF(L"userModsDir: %s", userModsDir.c_str());
        WriteLogF(L"java.exe  exists=%d", GetFileAttributesW(javaExe.c_str()) != INVALID_FILE_ATTRIBUTES);
        WriteLogF(L"gameDir   exists=%d", GetFileAttributesW(gameDir.c_str()) != INVALID_FILE_ATTRIBUTES);
        WriteLogF(L"clientJar exists=%d", GetFileAttributesW(clientJar.c_str()) != INVALID_FILE_ATTRIBUTES);

        std::vector<std::wstring> jars;
        CollectJars(packagedLibrariesDir, jars);
        CollectJars(gameDir + L"\\libraries", jars);
        jars.push_back(clientJar);
        WriteLogF(L"JAR count: %zu", jars.size());

        std::wstring cp;
        for (size_t i = 0; i < jars.size(); i++) {
            if (i > 0) cp += L";";
            cp += fwd(jars[i]);
        }
        WriteLog(L"Launching embedded JVM");
        g_minecraftRunning.store(true);
        if (!RunEmbeddedMinecraft(exeDir, packageDir, jreDir, gameDir, assetsDir, nativesDir, bundledModsDir, userModsDir, clientJar, javaLog, argsPath, cp, authConfig)) {
            g_minecraftRunning.store(false);
            WriteLog(L"Embedded JVM launch failed");
            return E_FAIL;
        }
        g_minecraftRunning.store(false);
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
    ComPtr<ICoreApplication> coreApp;
    GetActivationFactory(
        HStringReference(RuntimeClass_Windows_ApplicationModel_Core_CoreApplication).Get(),
        &coreApp);
    RegisterLifecycleHandlers(coreApp.Get());
    coreApp->Run(Make<AppSource>().Get());
    RoUninitialize();
    return 0;
}
