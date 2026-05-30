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
#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Security.Credentials.h>
#include <winrt/Windows.Security.ExchangeActiveSyncProvisioning.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.Web.Http.h>
#include <winrt/Windows.Web.Http.Headers.h>

#include "runtime_config.h"
#include "qr_code.h"

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
    int selectedMenuIndex = 0;
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

        d2dContext_->CreateSolidColorBrush(D2D1::ColorF(0xF5F7F8), white.GetAddressOf());
        d2dContext_->CreateSolidColorBrush(D2D1::ColorF(0xA9B0B4), muted.GetAddressOf());
        d2dContext_->CreateSolidColorBrush(D2D1::ColorF(0x151718), panel.GetAddressOf());
        d2dContext_->CreateSolidColorBrush(D2D1::ColorF(0x70C486), accent.GetAddressOf());
        d2dContext_->CreateSolidColorBrush(D2D1::ColorF(0xE36A5C), danger.GetAddressOf());
        d2dContext_->CreateSolidColorBrush(D2D1::ColorF(0x000000), black.GetAddressOf());

        d2dContext_->BeginDraw();
        d2dContext_->Clear(D2D1::ColorF(0x070808));

        const float marginX = width_ * 0.075f;
        const float marginY = height_ * 0.11f;
        const D2D1_RECT_F frame = D2D1::RectF(marginX, marginY, width_ - marginX, height_ - marginY);
        d2dContext_->DrawRectangle(frame, white.Get(), 3.0f);

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
        if (state.showMainMenu) {
            const float left = frame.left + 36.0f;
            const float menuRight = frame.left + (frame.right - frame.left) * 0.34f;
            const float previewLeft = menuRight + 34.0f;
            const float previewRight = frame.right - 36.0f;
            const float top = frame.top + 34.0f;
            const float buttonH = 62.0f;
            const float buttonGap = 24.0f;
            const wchar_t* labels[] = { L"Play", L"Mods", L"Repair downloads", L"Sign out" };

            DrawText(title.c_str(), bodyFormat_.Get(), D2D1::RectF(left, top, menuRight, top + 42.0f), white.Get());

            for (int i = 0; i < 4; ++i) {
                const float y = top + 76.0f + i * (buttonH + buttonGap);
                const D2D1_RECT_F button = D2D1::RectF(left, y, menuRight, y + buttonH);
                if (i == state.selectedMenuIndex) {
                    d2dContext_->FillRectangle(button, panel.Get());
                    d2dContext_->DrawRectangle(button, accent.Get(), 4.0f);
                } else {
                    d2dContext_->DrawRectangle(button, white.Get(), 2.0f);
                }
                const D2D1_RECT_F textRect = D2D1::RectF(button.left + 18.0f, button.top + 12.0f, button.right - 12.0f, button.bottom - 8.0f);
                DrawText(labels[i], bodyFormat_.Get(), textRect, i == state.selectedMenuIndex ? accent.Get() : white.Get());
            }

            if (!state.status.empty()) {
                const D2D1_RECT_F statusRect = D2D1::RectF(left, frame.bottom - 88.0f, menuRight, frame.bottom - 28.0f);
                DrawText(state.status.c_str(), smallFormat_.Get(), statusRect, state.isError ? danger.Get() : muted.Get());
            }

            const D2D1_RECT_F preview = D2D1::RectF(previewLeft, top, previewRight, frame.bottom - 34.0f);
            d2dContext_->FillRectangle(preview, black.Get());
            d2dContext_->DrawRectangle(preview, white.Get(), 2.0f);
            const float inset = 8.0f;
            const D2D1_RECT_F pano = D2D1::RectF(preview.left + inset, preview.top + inset, preview.right - inset, preview.bottom - inset);
            DrawPanorama(pano, state.animation);

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
            DrawText(title.c_str(), bodyFormat_.Get(), titleRect, white.Get());

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
                d2dContext_->FillRectangle(track, panel.Get());
                d2dContext_->FillRectangle(fill, state.isError ? danger.Get() : accent.Get());

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
            white.Get(),
            3.0f);

        const D2D1_RECT_F titleRect = D2D1::RectF(frame.left + 42.0f, frame.top + 34.0f, dividerX - 42.0f, frame.top + 86.0f);
        DrawText(title.c_str(), bodyFormat_.Get(), titleRect, white.Get());

        const D2D1_RECT_F codeBox = D2D1::RectF(frame.left + 42.0f, frame.top + 102.0f, dividerX - 42.0f, frame.top + 190.0f);
        d2dContext_->DrawRectangle(codeBox, white.Get(), 2.0f);
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
    ComPtr<ID2D1Bitmap1> panoramaFaces_[4];
    ComPtr<ID2D1Bitmap1> panoramaOverlay_;
    bool panoramaLoadAttempted_ = false;
    bool panoramaLoaded_ = false;
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
            WriteLogF(L"Panorama decoder failed %s hr=0x%08X", path.c_str(), hr);
            return false;
        }

        ComPtr<IWICBitmapFrameDecode> frame;
        hr = decoder->GetFrame(0, frame.GetAddressOf());
        if (FAILED(hr)) {
            WriteLogF(L"Panorama frame failed %s hr=0x%08X", path.c_str(), hr);
            return false;
        }

        ComPtr<IWICFormatConverter> converter;
        hr = wicFactory_->CreateFormatConverter(converter.GetAddressOf());
        if (FAILED(hr)) {
            WriteLogF(L"Panorama converter create failed hr=0x%08X", hr);
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
            WriteLogF(L"Panorama converter init failed %s hr=0x%08X", path.c_str(), hr);
            return false;
        }

        const D2D1_BITMAP_PROPERTIES1 props = D2D1::BitmapProperties1(
            D2D1_BITMAP_OPTIONS_NONE,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
            96.0f,
            96.0f);
        hr = d2dContext_->CreateBitmapFromWicBitmap(converter.Get(), &props, out.GetAddressOf());
        if (FAILED(hr)) {
            WriteLogF(L"Panorama D2D bitmap failed %s hr=0x%08X", path.c_str(), hr);
            return false;
        }

        return true;
    }

    void EnsurePanoramaLoaded() {
        if (panoramaLoadAttempted_) return;
        panoramaLoadAttempted_ = true;

        const std::wstring dir = GetExecutableDir() + L"\\Assets\\panorama";
        int loaded = 0;
        for (int i = 0; i < 4; ++i) {
            const std::wstring path = dir + L"\\panorama_" + std::to_wstring(i) + L".png";
            if (LoadBitmapFromFile(path, panoramaFaces_[i])) {
                ++loaded;
            }
        }

        LoadBitmapFromFile(dir + L"\\panorama_overlay.png", panoramaOverlay_);
        panoramaLoaded_ = loaded == 4;
        WriteLogF(L"Panorama loaded faces=%d overlay=%d",
            loaded,
            panoramaOverlay_ ? 1 : 0);
    }

    void DrawPanoramaFallback(D2D1_RECT_F rect, float animation) {
        d2dContext_->PushAxisAlignedClip(rect, D2D1_ANTIALIAS_MODE_ALIASED);
        ComPtr<ID2D1SolidColorBrush> sky;
        ComPtr<ID2D1SolidColorBrush> ridge;
        ComPtr<ID2D1SolidColorBrush> water;
        d2dContext_->CreateSolidColorBrush(D2D1::ColorF(0x2F6F9F), sky.GetAddressOf());
        d2dContext_->CreateSolidColorBrush(D2D1::ColorF(0x315D35), ridge.GetAddressOf());
        d2dContext_->CreateSolidColorBrush(D2D1::ColorF(0x1B425A), water.GetAddressOf());
        d2dContext_->FillRectangle(rect, sky.Get());
        const float phase = static_cast<float>(static_cast<int>(animation * 45.0f) % 96);
        for (int i = -1; i < 8; ++i) {
            const float x = rect.left + i * 96.0f - phase;
            const float peak = rect.top + 96.0f + ((i % 2) ? 28.0f : 0.0f);
            d2dContext_->FillRectangle(D2D1::RectF(x, peak, x + 132.0f, rect.bottom - 72.0f), ridge.Get());
        }
        d2dContext_->FillRectangle(D2D1::RectF(rect.left, rect.bottom - 86.0f, rect.right, rect.bottom), water.Get());
        d2dContext_->PopAxisAlignedClip();
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

    void DrawPanorama(D2D1_RECT_F rect, float animation) {
        EnsurePanoramaLoaded();
        if (!panoramaLoaded_) {
            DrawPanoramaFallback(rect, animation);
            return;
        }

        d2dContext_->PushAxisAlignedClip(rect, D2D1_ANTIALIAS_MODE_ALIASED);

        const float segmentSeconds = 7.0f;
        const float raw = fmodf(animation / segmentSeconds, 6.0f);
        const int face = static_cast<int>(floorf(raw)) % 4;
        const int nextFace = (face + 1) % 4;
        const float phase = raw - floorf(raw);
        const float fade = phase > 0.82f ? (phase - 0.82f) / 0.18f : 0.0f;
        const float easedFade = fade * fade * (3.0f - 2.0f * fade);
        const float panX = sinf(animation * 0.11f) * 0.45f;
        const float panY = cosf(animation * 0.08f) * 0.18f;
        const float zoom = 1.08f + 0.025f * sinf(animation * 0.17f);

        DrawBitmapCover(panoramaFaces_[face].Get(), rect, 1.0f, zoom, panX, panY);
        if (easedFade > 0.0f) {
            DrawBitmapCover(
                panoramaFaces_[nextFace].Get(),
                rect,
                easedFade,
                1.08f + 0.025f * sinf((animation + segmentSeconds) * 0.17f),
                sinf((animation + segmentSeconds) * 0.11f) * 0.45f,
                cosf((animation + segmentSeconds) * 0.08f) * 0.18f);
        }

        if (panoramaOverlay_) {
            d2dContext_->DrawBitmap(panoramaOverlay_.Get(), rect, 0.48f, D2D1_INTERPOLATION_MODE_LINEAR);
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

static MainMenuAction ShowMainMenu(ICoreWindow* window, const LaunchAuthConfig& authConfig) {
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
                WriteLog(L"Main menu: Mods placeholder selected");
                state.detail = L"Mods are not implemented yet.";
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

            const MainMenuAction menuAction = ShowMainMenu(g_authWindow.Get(), authConfig);
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
        const std::wstring userModsDir = gameDir + L"\\user-mods";
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
