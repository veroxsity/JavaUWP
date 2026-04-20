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
#include <windows.foundation.h>
#include <windows.foundation.collections.h>
#include <jni.h>
#include <io.h>
#include <fcntl.h>
#include <share.h>
#include <errno.h>

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
using namespace ABI::Windows::UI::Core;
using namespace ABI::Windows::Foundation;
using namespace ABI::Windows::Foundation::Collections;

static std::wstring g_logDir;
static bool g_setWindowCalled = false;
static HRESULT g_windowInteropHr = E_NOTIMPL;
static HRESULT g_getWindowHandleHr = E_NOTIMPL;
static HWND g_windowHandle = NULL;
static constexpr wchar_t kEGLNativeWindowTypeProperty[] = L"EGLNativeWindowTypeProperty";

typedef jint(JNICALL* JNI_CreateJavaVM_t)(JavaVM**, void**, void*);

static std::wstring GetExecutableDir() {
    wchar_t buf[MAX_PATH];
    const DWORD len = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) return std::wstring();

    wchar_t* sl = wcsrchr(buf, L'\\');
    if (sl) *sl = L'\0';
    return std::wstring(buf);
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

static void CopyFileIfMissing(const std::wstring& src, const std::wstring& dst) {
    if (GetFileAttributesW(src.c_str()) == INVALID_FILE_ATTRIBUTES) return;
    if (GetFileAttributesW(dst.c_str()) != INVALID_FILE_ATTRIBUTES) return;

    EnsureDirectoryTree(GetParentDir(dst));
    CopyFileW(src.c_str(), dst.c_str(), TRUE);
}

static void CopyDirectoryContentsIfMissing(const std::wstring& src, const std::wstring& dst) {
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((src + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;

    EnsureDirectoryTree(dst);
    do {
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
        const std::wstring srcPath = src + L"\\" + fd.cFileName;
        const std::wstring dstPath = dst + L"\\" + fd.cFileName;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            CopyDirectoryContentsIfMissing(srcPath, dstPath);
        } else {
            CopyFileIfMissing(srcPath, dstPath);
        }
    } while (FindNextFileW(h, &fd));

    FindClose(h);
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
    wchar_t buf[4096];
    va_list args;
    va_start(args, fmt);
    vswprintf_s(buf, fmt, args);
    va_end(args);
    WriteLog(buf);
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

static std::wstring GetEnvVarString(const wchar_t* name) {
    const DWORD len = GetEnvironmentVariableW(name, nullptr, 0);
    if (len == 0) return std::wstring();

    std::wstring value(len, L'\0');
    if (GetEnvironmentVariableW(name, value.data(), len) == 0) return std::wstring();
    if (!value.empty() && value.back() == L'\0') value.pop_back();
    return value;
}

static bool RedirectStdStreams(const std::wstring& path) {
    int fd = -1;
    if (_wsopen_s(&fd, path.c_str(), _O_CREAT | _O_APPEND | _O_WRONLY | _O_TEXT,
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

    FILE* out = _fdopen(1, "a");
    FILE* err = _fdopen(2, "a");
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
    loadPackaged(L"jre\\bin\\msvcp140.dll", L"msvcp140.dll");
    loadPackaged(L"jre\\bin\\vcruntime140.dll", L"vcruntime140.dll");
    loadPackaged(L"jre\\bin\\vcruntime140_1.dll", L"vcruntime140_1.dll");
    loadPackaged(L"jre\\bin\\java.dll", L"java.dll");
    loadPackaged(L"jre\\bin\\jli.dll", L"jli.dll");

    *jvmModule = loadPackaged(L"jre\\bin\\server\\jvm.dll", L"jvm.dll");
    if (!*jvmModule) {
        return false;
    }

    WriteLog(L"JVM DLLs loaded");
    return true;
}

static bool CheckAndLogJavaException(JNIEnv* env, const wchar_t* stage) {
    if (!env->ExceptionCheck()) return false;
    WriteLogF(L"Java exception during %s", stage);
    env->ExceptionDescribe();
    return true;
}

static bool RunEmbeddedMinecraft(const std::wstring& exeDir,
    const std::wstring& gameDir,
    const std::wstring& assetsDir,
    const std::wstring& nativesDir,
    const std::wstring& clientJar,
    const std::wstring& javaLog,
    const std::wstring& argsPath,
    const std::wstring& classPath)
{
    const std::wstring jreDir = exeDir + L"\\jre";
    const std::wstring jnaTmpDir = nativesDir;
    const std::wstring lwjglTmpDir = exeDir;
    const std::wstring logConfigPath = gameDir + L"\\log_configs\\client-uwp.xml";

    EnsureDirectoryTree(gameDir + L"\\logs");
    EnsureDirectoryTree(gameDir + L"\\crash-reports");

    if (!RedirectStdStreams(javaLog)) {
        WriteLogF(L"Failed to redirect stdout/stderr errno=%d winerr=%u", errno, GetLastError());
    } else {
        WriteLog(L"stdout/stderr redirected to java_output.log");
    }

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
    vmOptionStorage.push_back("-Djava.home=" + w2a(fwd(jreDir)));
    vmOptionStorage.push_back("-Djava.security.properties==" + w2a(fwd(jreDir + L"\\conf\\security\\xbox.properties")));
    vmOptionStorage.push_back("-Djava.security.egd=file:/dev/./urandom");
    vmOptionStorage.push_back("-Djava.io.tmpdir=" + w2a(fwd(jnaTmpDir)));
    vmOptionStorage.push_back("-Djna.tmpdir=" + w2a(fwd(jnaTmpDir)));
    vmOptionStorage.push_back("-Djna.nosys=true");
    vmOptionStorage.push_back("-Djna.nounpack=true");
    vmOptionStorage.push_back("-Djna.boot.library.name=jnidispatch");
    vmOptionStorage.push_back("-Djna.boot.library.path=" + w2a(fwd(nativesDir)));
    vmOptionStorage.push_back("-Djava.library.path=" + w2a(fwd(nativesDir)));
    vmOptionStorage.push_back("-Dorg.lwjgl.system.SharedLibraryExtractDirectory=" + w2a(fwd(lwjglTmpDir)));
    vmOptionStorage.push_back("-Dorg.lwjgl.glfw.libname=" + w2a(fwd(nativesDir + L"\\glfw.dll")));
    vmOptionStorage.push_back("-Dfabric.gameJarPath=" + w2a(fwd(clientJar)));
    vmOptionStorage.push_back("-Djava.class.path=" + w2a(classPath));
    vmOptionStorage.push_back("-Duser.dir=" + w2a(fwd(gameDir)));
    vmOptionStorage.push_back("-Dlog4j.configurationFile=" + w2a(fwd(logConfigPath)));

    const std::vector<std::string> appArgs = {
        "--username", "DevPlayer",
        "--version", "fabric-loader-0.19.2-1.21.11",
        "--gameDir", w2a(fwd(gameDir)),
        "--assetsDir", w2a(fwd(assetsDir)),
        "--assetIndex", "29",
        "--uuid", "00000000-0000-0000-0000-000000000000",
        "--accessToken", "0",
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
    env->CallStaticVoidMethod(mainClass, mainMethod, argv);
    if (CheckAndLogJavaException(env, L"CallStaticVoidMethod(main)")) {
        return false;
    }

    WriteLog(L"KnotClient.main returned");
    const jint destroyResult = vm->DestroyJavaVM();
    WriteLogF(L"DestroyJavaVM => %d", destroyResult);
    return true;
}

class App : public RuntimeClass<RuntimeClassFlags<WinRtClassicComMix>, IFrameworkView>
{
public:
    HRESULT STDMETHODCALLTYPE Initialize(ICoreApplicationView*) override { return S_OK; }

    HRESULT STDMETHODCALLTYPE SetWindow(ICoreWindow* window) override {
        g_setWindowCalled = true;
        if (g_logDir.empty()) {
            g_logDir = GetExecutableDir();
        }
        EnsureDirectoryTree(g_logDir);

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
        PublishCoreWindowProperty(window);
        window->Activate();
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Load(HSTRING) override { return S_OK; }

    HRESULT STDMETHODCALLTYPE Run() override
    {
        const std::wstring exeDir = GetExecutableDir();
        const std::wstring gameDir = exeDir + L"\\game";
        const std::wstring javaExe = exeDir + L"\\jre\\bin\\java.exe";
        const std::wstring assetsDir = exeDir + L"\\assets";
        const std::wstring nativesDir = exeDir + L"\\natives";
        const std::wstring clientJar = gameDir + L"\\versions\\1.21.11\\1.21.11.jar";
        const std::wstring argsPath = exeDir + L"\\java_args.txt";
        const std::wstring javaLog = exeDir + L"\\java_output.log";

        g_logDir = exeDir;
        SetCurrentDirectoryW(exeDir.c_str());
        SetEnvironmentVariableW(L"MC_RUNTIME_DIR", exeDir.c_str());

        wchar_t lp[MAX_PATH];
        swprintf_s(lp, L"%s\\mc_launch.log", exeDir.c_str());
        FILE* clf = nullptr;
        _wfopen_s(&clf, lp, L"w");
        if (clf) fclose(clf);

        WriteLog(L"=== MC.App Run() started ===");
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
        WriteLogF(L"exeDir: %s", exeDir.c_str());
        WriteLogF(L"java.exe  exists=%d", GetFileAttributesW(javaExe.c_str()) != INVALID_FILE_ATTRIBUTES);
        WriteLogF(L"gameDir   exists=%d", GetFileAttributesW(gameDir.c_str()) != INVALID_FILE_ATTRIBUTES);
        WriteLogF(L"clientJar exists=%d", GetFileAttributesW(clientJar.c_str()) != INVALID_FILE_ATTRIBUTES);

        std::vector<std::wstring> jars;
        CollectJars(gameDir + L"\\libraries", jars);
        jars.push_back(clientJar);
        WriteLogF(L"JAR count: %zu", jars.size());

        std::wstring cp;
        for (size_t i = 0; i < jars.size(); i++) {
            if (i > 0) cp += L";";
            cp += fwd(jars[i]);
        }
        WriteLog(L"Launching embedded JVM");
        if (!RunEmbeddedMinecraft(exeDir, gameDir, assetsDir, nativesDir, clientJar, javaLog, argsPath, cp)) {
            WriteLog(L"Embedded JVM launch failed");
            return E_FAIL;
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
    ComPtr<ICoreApplication> coreApp;
    GetActivationFactory(
        HStringReference(RuntimeClass_Windows_ApplicationModel_Core_CoreApplication).Get(),
        &coreApp);
    coreApp->Run(Make<AppSource>().Get());
    RoUninitialize();
    return 0;
}
