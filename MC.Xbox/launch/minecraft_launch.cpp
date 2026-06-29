#include "minecraft_launch.h"

#include "app_globals.h"
#include "crash_report.h"
#include "launch_internal.h"
#include "loader.h"
#include "loader_common.h"
#include "neoforge.h"
#include "launcher_common.h"
#include "mod_defaults.h"
#include "mods_browser.h"
#include "profiles.h"
#include "runtime_manager.h"

#include <jni.h>
#include <io.h>
#include <fcntl.h>
#include <share.h>
#include <errno.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <fstream>
#include <functional>
#include <map>
#include <mutex>
#include <sstream>
#include <thread>
#include <vector>

#include <roapi.h>
#include <wrl.h>
#include <wrl/wrappers/corewrappers.h>
#include <windows.applicationmodel.core.h>
#include <windows.foundation.h>
#include <windows.foundation.collections.h>
#include <windows.ui.core.h>

#include "third_party/miniz/miniz.h"

using namespace Microsoft::WRL;
using Microsoft::WRL::Wrappers::HStringReference;
using namespace ABI::Windows::ApplicationModel::Core;
using namespace ABI::Windows::Foundation;
using namespace ABI::Windows::Foundation::Collections;
using namespace ABI::Windows::UI::Core;

typedef jint(JNICALL* JNI_CreateJavaVM_t)(JavaVM**, void**, void*);

static void DestroyEmbeddedJvm(JavaVM*& vm, JNIEnv*& env) {
    if (!vm) return;
    vm->DestroyJavaVM();
    vm = nullptr;
    env = nullptr;
    WriteLog(L"Embedded JVM destroyed after launch preparation failure");
}

static constexpr wchar_t kEGLNativeWindowTypeProperty[] = L"EGLNativeWindowTypeProperty";

static volatile LONG g_logTailerRunning = 0;
static HANDLE g_logTailerThreads[8] = {};
static int g_logTailerThreadCount = 0;
static HANDLE g_redirectedStdoutHandle = INVALID_HANDLE_VALUE;
static HANDLE g_redirectedStderrHandle = INVALID_HANDLE_VALUE;

struct LogTailerConfig {
    std::wstring path;
    std::wstring label;
};
static std::wstring TailTextForUi(const std::wstring& text, size_t maxLines = 14, size_t maxChars = 4200) {
    if (text.empty()) return L"Waiting for pre-launch log output...";

    size_t end = text.size();
    while (end > 0 && (text[end - 1] == L'\r' || text[end - 1] == L'\n')) {
        --end;
    }
    size_t start = end;
    size_t lines = 0;
    while (start > 0 && lines < maxLines) {
        --start;
        if (text[start] == L'\n') {
            ++lines;
        }
    }
    if (lines >= maxLines && start < end) {
        ++start;
    }
    if (end > start && end - start > maxChars) {
        start = end - maxChars;
        while (start < end && text[start] != L'\n') {
            ++start;
        }
        if (start < end) {
            ++start;
        }
    }

    std::wstring tail = text.substr(start, end - start);
    tail.erase(std::remove(tail.begin(), tail.end(), L'\r'), tail.end());
    for (wchar_t& ch : tail) {
        if (ch == L'\t') ch = L' ';
    }
    if (tail.empty()) return L"Waiting for pre-launch log output...";

    // The launch panel is not interactive, so keep each logical log line to one
    // visual row. Very long paths otherwise wrap and hide the newest entries.
    constexpr size_t maxLineChars = 150;
    std::wstring compact;
    size_t lineStart = 0;
    while (lineStart <= tail.size()) {
        size_t lineEnd = tail.find(L'\n', lineStart);
        if (lineEnd == std::wstring::npos) lineEnd = tail.size();
        std::wstring line = tail.substr(lineStart, lineEnd - lineStart);
        if (line.size() > maxLineChars) {
            constexpr size_t headChars = 96;
            constexpr size_t tailChars = maxLineChars - headChars - 5;
            line = line.substr(0, headChars) + L" ... " + line.substr(line.size() - tailChars);
        }
        if (!compact.empty()) compact += L'\n';
        compact += line;
        if (lineEnd >= tail.size()) break;
        lineStart = lineEnd + 1;
    }
    return compact.empty() ? L"Waiting for pre-launch log output..." : compact;
}

std::wstring ReadLaunchLogTailForUi(const std::vector<std::wstring>& paths, size_t maxLines) {
    for (const std::wstring& path : paths) {
        std::wstring text;
        if (ReadTextFile(path, text) && !text.empty()) {
            return TailTextForUi(text, maxLines);
        }
    }
    return L"Waiting for pre-launch log output...";
}

static bool FileContains(const std::wstring& path, const wchar_t* needle) {
    if (!needle || !*needle) return false;
    std::wstring text;
    if (!ReadTextFile(path, text) || text.empty()) return false;
    return text.find(needle) != std::wstring::npos;
}

LaunchUiSnapshot BuildLaunchUiSnapshot(
    const std::vector<std::wstring>& launchLogPaths,
    const std::wstring& glfwLogPath,
    const std::wstring& latestLogPath,
    const std::wstring& loaderLabel) {
    LaunchUiSnapshot snap;
    snap.logTail = ReadLaunchLogTailForUi(launchLogPaths);
    snap.status = L"Starting Minecraft";
    snap.detail = loaderLabel.empty()
        ? L"Preparing the Java runtime and launch files."
        : (L"Preparing " + loaderLabel + L" and the Java runtime.");
    snap.progress = 0.12f;

    std::wstring mcLaunch;
    for (const std::wstring& path : launchLogPaths) {
        std::wstring text;
        if (ReadTextFile(path, text) && !text.empty()) {
            mcLaunch = std::move(text);
            break;
        }
    }

    if (!mcLaunch.empty()) {
        if (mcLaunch.find(L"Launching embedded JVM") != std::wstring::npos) {
            snap.progress = 0.22f;
            snap.status = L"Starting Java runtime";
            snap.detail = L"The embedded JVM is loading.";
        }
        if (mcLaunch.find(L"JNI_CreateJavaVM") != std::wstring::npos) {
            snap.progress = (std::max)(snap.progress, 0.34f);
            snap.status = L"Starting Java runtime";
            snap.detail = L"Java is online. Loader startup comes next.";
        }
        if (mcLaunch.find(L"Invoking ") != std::wstring::npos) {
            snap.progress = (std::max)(snap.progress, 0.50f);
            snap.status = loaderLabel.empty() ? L"Loading Minecraft" : (L"Loading " + loaderLabel);
            snap.detail = L"Mods and libraries are being prepared. First launch can take a while.";
        }
        if (mcLaunch.find(L"NeoForge") != std::wstring::npos &&
            mcLaunch.find(L"deploy") != std::wstring::npos) {
            snap.progress = (std::max)(snap.progress, 0.42f);
            snap.status = L"Preparing NeoForge";
            snap.detail = L"NeoForge client files are being prepared.";
        }
    }

    std::wstring latest;
    if (ReadTextFile(latestLogPath, latest) && latest.size() > 180) {
        snap.progress = (std::max)(snap.progress, 0.58f);
        if (!snap.graphicsReady) {
            snap.status = L"Bootstrapping Minecraft";
            snap.detail = L"Minecraft is initializing in the background.";
        }
    }

    if (FileContains(glfwLogPath, L"glfwInit OK")) {
        snap.graphicsReady = true;
        snap.progress = (std::max)(snap.progress, 0.86f);
        snap.status = L"Opening Minecraft";
        snap.detail = L"Graphics are starting. The Mojang loading screen should appear shortly.";
    }
    if (FileContains(glfwLogPath, L"glfwCreateWindow")) {
        snap.graphicsReady = true;
        snap.progress = (std::max)(snap.progress, 0.93f);
        snap.status = L"Opening Minecraft";
        snap.detail = L"The game window is ready. Minecraft should take over any moment.";
    }

    return snap;
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
void CollectJars(const std::wstring& dir, std::vector<std::wstring>& jars) {
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

static std::wstring FileUriFromPath(const std::wstring& path) {
    const std::wstring normalized = fwd(path);
    if (normalized.empty()) return L"file:///";

    std::wstring uri = L"file:///";
    for (wchar_t c : normalized) {
        switch (c) {
        case L' ':
            uri += L"%20";
            break;
        case L'#':
            uri += L"%23";
            break;
        case L'%':
            uri += L"%25";
            break;
        case L'?':
            uri += L"%3F";
            break;
        default:
            uri += c;
            break;
        }
    }
    return uri;
}

static bool IsJvmMemoryOption(const std::wstring& arg) {
    const std::wstring trimmed = TrimWhitespace(arg);
    if (trimmed.empty()) return false;
    const std::wstring lower = ToLowerW(trimmed);
    return lower.rfind(L"-xmx", 0) == 0 ||
        lower.rfind(L"-xms", 0) == 0 ||
        lower.rfind(L"-xx:maxram", 0) == 0 ||
        lower.rfind(L"-xx:initialram", 0) == 0 ||
        lower.rfind(L"-xx:heap", 0) == 0;
}

void CollectManifestLibraryJars(
    const std::wstring& manifestPath,
    const std::wstring& runtimeRoot,
    const std::wstring& packageDir,
    std::vector<std::wstring>& jars) {
    std::vector<DownloadManifestEntry> entries;
    if (!ReadDownloadManifest(manifestPath, entries)) return;
    for (const auto& e : entries) {
        std::wstring rel = e.relativePath;
        std::replace(rel.begin(), rel.end(), L'\\', L'/');
        for (wchar_t& c : rel) c = static_cast<wchar_t>(towlower(c));
        if (rel.rfind(L"game/libraries/", 0) != 0) continue;
        if (rel.size() < 4 || rel.compare(rel.size() - 4, 4, L".jar") != 0) continue;
        if (rel.find(L"-natives-") != std::wstring::npos) continue;
        if (rel.find(L"-installer.jar") != std::wstring::npos) continue;
        const std::wstring libraryRelative = e.relativePath.substr(wcslen(L"game\\libraries\\"));
        const std::wstring packagedOverride = packageDir + L"\\runtime\\libraries\\" + libraryRelative;
        const std::wstring abs =
            GetFileAttributesW(packagedOverride.c_str()) != INVALID_FILE_ATTRIBUTES
                ? packagedOverride
                : JoinRuntimeRelativePath(runtimeRoot, e.relativePath);
        if (!abs.empty()) jars.push_back(abs);
    }
}

static HANDLE OpenRedirectOutputFile(const std::wstring& path) {
    EnsureDirectoryTree(GetParentDir(path));
    return CreateFile2(
        path.c_str(),
        GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        CREATE_ALWAYS,
        nullptr);
}

static bool BindCrtFdToHandle(int targetFd, HANDLE fileHandle) {
    if (fileHandle == INVALID_HANDLE_VALUE) return false;

    HANDLE crtHandle = INVALID_HANDLE_VALUE;
    if (!DuplicateHandle(
            GetCurrentProcess(),
            fileHandle,
            GetCurrentProcess(),
            &crtHandle,
            0,
            TRUE,
            DUPLICATE_SAME_ACCESS)) {
        return false;
    }

    const int fd = _open_osfhandle(reinterpret_cast<intptr_t>(crtHandle), _O_TEXT);
    if (fd < 0) {
        CloseHandle(crtHandle);
        return false;
    }

    if (_dup2(fd, targetFd) != 0) {
        _close(fd);
        return false;
    }
    _close(fd);
    return true;
}

static bool RedirectStdStreams(const std::wstring& stdoutPath, const std::wstring& stderrPath) {
    if (g_redirectedStdoutHandle != INVALID_HANDLE_VALUE) {
        CloseHandle(g_redirectedStdoutHandle);
        g_redirectedStdoutHandle = INVALID_HANDLE_VALUE;
    }
    if (g_redirectedStderrHandle != INVALID_HANDLE_VALUE) {
        CloseHandle(g_redirectedStderrHandle);
        g_redirectedStderrHandle = INVALID_HANDLE_VALUE;
    }

    g_redirectedStdoutHandle = OpenRedirectOutputFile(stdoutPath);
    if (g_redirectedStdoutHandle == INVALID_HANDLE_VALUE) {
        return false;
    }

    g_redirectedStderrHandle = OpenRedirectOutputFile(stderrPath.empty() ? stdoutPath : stderrPath);
    if (g_redirectedStderrHandle == INVALID_HANDLE_VALUE) {
        CloseHandle(g_redirectedStdoutHandle);
        g_redirectedStdoutHandle = INVALID_HANDLE_VALUE;
        return false;
    }

    if (!SetStdHandle(STD_OUTPUT_HANDLE, g_redirectedStdoutHandle) ||
        !SetStdHandle(STD_ERROR_HANDLE, g_redirectedStderrHandle)) {
        return false;
    }

    if (!BindCrtFdToHandle(1, g_redirectedStdoutHandle) ||
        !BindCrtFdToHandle(2, g_redirectedStderrHandle)) {
        return false;
    }

    FILE* out = _fdopen(_dup(1), "w");
    FILE* err = _fdopen(_dup(2), "w");
    if (out) *stdout = *out;
    if (err) *stderr = *err;
    setvbuf(stdout, nullptr, _IONBF, 0);
    setvbuf(stderr, nullptr, _IONBF, 0);

    printf("[launcher] stdout redirect probe\n");
    fprintf(stderr, "[launcher] stderr redirect probe\n");
    fflush(stdout);
    fflush(stderr);

    DWORD written = 0;
    static const char kStdoutProbe[] = "[launcher] Win32 stdout redirect probe\r\n";
    static const char kStderrProbe[] = "[launcher] Win32 stderr redirect probe\r\n";
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), kStdoutProbe, sizeof(kStdoutProbe) - 1, &written, nullptr);
    WriteFile(GetStdHandle(STD_ERROR_HANDLE), kStderrProbe, sizeof(kStderrProbe) - 1, &written, nullptr);
    return true;
}

bool PublishCoreWindowProperty(ICoreWindow* window) {
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

static bool PreloadJvm(
    const std::wstring& exeDir,
    const std::wstring& jreDir,
    const std::wstring& packagedJreRelativeDir,
    const std::wstring& nativesDir,
    HMODULE* jvmModule) {
    const std::wstring jreBin = jreDir + L"\\bin";
    const std::wstring jreServer = jreBin + L"\\server";
    const std::wstring path = jreBin + L";" + jreServer + L";" + exeDir + L";" + nativesDir + L";" + GetEnvVarString(L"PATH");
    SetEnvironmentVariableW(L"PATH", path.c_str());
    SetEnvironmentVariableW(L"JAVA_HOME", jreDir.c_str());

    const std::wstring packagedPrefix = packagedJreRelativeDir.empty() ? L"jre" : packagedJreRelativeDir;
    auto loadPackaged = [&](const std::wstring& relativePath, const wchar_t* label) -> HMODULE {
        HMODULE module = LoadPackagedLibrary(relativePath.c_str(), 0);
        if (!module) {
            WriteLogF(L"LoadPackagedLibrary(%s) failed err=%u", label, GetLastError());
        }
        return module;
    };

    // Preload the JRE CRT/runtime DLLs from jre\bin so jvm.dll can resolve
    // its non-system imports while running inside the app package.
	loadPackaged(packagedPrefix + L"\\bin\\vcruntime140.dll", L"vcruntime140.dll");
    loadPackaged(packagedPrefix + L"\\bin\\vcruntime140_1.dll", L"vcruntime140_1.dll");
    loadPackaged(packagedPrefix + L"\\bin\\msvcp140.dll", L"msvcp140.dll");
    loadPackaged(packagedPrefix + L"\\bin\\jli.dll", L"jli.dll");

    *jvmModule = loadPackaged(packagedPrefix + L"\\bin\\server\\jvm.dll", L"jvm.dll");
    if (!*jvmModule) {
        return false;
    }
	
	loadPackaged(packagedPrefix + L"\\bin\\java.dll", L"java.dll");

    WriteLogF(L"JVM DLLs loaded from package runtime %s", packagedPrefix.c_str());
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

static bool CheckAndClearReturnToLauncherSignal(JNIEnv* env, const wchar_t* stage) {
    if (!env || !env->ExceptionCheck()) return false;

    jthrowable throwable = env->ExceptionOccurred();
    env->ExceptionClear();
    if (!throwable) {
        return false;
    }

    const std::wstring exceptionText = JavaObjectToWideString(env, throwable);
    const bool isReturnSignal =
        exceptionText.find(L"BanditVaultReturnToLauncher") != std::wstring::npos ||
        exceptionText.find(L"ReturnToLauncherSignal") != std::wstring::npos;

    if (isReturnSignal) {
        WriteLogF(L"Java return-to-launcher signal during %s: %s", stage, exceptionText.c_str());
        env->DeleteLocalRef(throwable);
        return true;
    }

    env->Throw(throwable);
    env->DeleteLocalRef(throwable);
    return false;
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

bool RunEmbeddedMinecraft(const std::wstring& exeDir,
    const std::wstring& packageDir,
    const std::wstring& jreDir,
    const std::wstring& packagedJreRelativeDir,
    const std::wstring& javaBasePatchName,
    const std::wstring& javaZipfsPatchName,
    const std::wstring& gameDir,
    const std::wstring& assetsDir,
    const std::wstring& nativesDir,
    const std::wstring& bundledModsDir,
    const std::wstring& userModsDir,
    const std::wstring& clientJar,
    const std::wstring& javaLog,
    const std::wstring& argsPath,
    const std::wstring& classPath,
    const std::wstring& launchVersion,
    const std::wstring& assetIndex,
    const std::wstring& minecraftVersion,
    const std::wstring& loader,
    const std::wstring& loaderVersion,
    const std::wstring& mainClassName,
    const std::vector<std::wstring>& extraJvmArgs,
    const std::vector<std::wstring>& extraGameArgs,
    const std::wstring& neoFormVersion,
    const std::wstring& neoForgeInstallToolsVersion,
    const std::wstring& neoForgeJarSplitterVersion,
    const std::wstring& neoForgeBinaryPatcherVersion,
    const std::wstring& neoForgeAutoRenamingToolVersion,
    const LaunchAuthConfig& authConfig,
    LaunchProgressCallback progress)
{
    const auto reportProgress = [&](const wchar_t* status, const wchar_t* detail, float value) {
        if (progress) {
            progress(status, detail, value);
        }
    };

    const LoaderId loaderId = ParseLoaderId(loader);
    std::wstring loaderLabel = L"Minecraft";
    if (loaderId == LoaderId::Fabric) {
        loaderLabel = L"Fabric";
    } else if (loaderId == LoaderId::NeoForge) {
        loaderLabel = L"NeoForge";
    } else if (loaderId == LoaderId::Forge) {
        loaderLabel = L"Forge";
    }
    reportProgress(
        L"Starting Minecraft",
        (L"Preparing " + loaderLabel + L" and launch files.").c_str(),
        0.12f);
    const std::wstring effectiveMainClass = LoaderDefaultMainClass(loaderId, mainClassName);
    std::wstring mainClassPath = effectiveMainClass;
    std::replace(mainClassPath.begin(), mainClassPath.end(), L'.', L'/');
    const std::wstring libraryDir = exeDir + L"\\game\\libraries";
    const std::wstring neoForgeVersionForLaunch = NeoForgeVersionFromLaunchVersion(launchVersion);
    std::wstring neoFormVersionForLaunch = !neoFormVersion.empty()
        ? neoFormVersion
        : FirstArgValue(extraGameArgs, L"--fml.neoFormVersion");
    if (neoFormVersionForLaunch.empty() && loaderId == LoaderId::Forge) {
        neoFormVersionForLaunch = FirstArgValue(extraGameArgs, L"--fml.mcpVersion");
    }
    const std::wstring neoForgeUniversalJar =
        loaderId == LoaderId::NeoForge && !neoForgeVersionForLaunch.empty()
            ? libraryDir + L"\\" + MavenPath(L"net.neoforged", L"neoforge", neoForgeVersionForLaunch, L"universal")
            : L"";
    std::wstring neoForgeMinecraftSrgJar;
    if (!minecraftVersion.empty() && !neoFormVersionForLaunch.empty()) {
        const std::wstring mcAndNeoForm = minecraftVersion + L"-" + neoFormVersionForLaunch;
        neoForgeMinecraftSrgJar = libraryDir + L"\\" + MavenPath(L"net.minecraft", L"client", mcAndNeoForm, L"srg");
    }
    auto expandLaunchArg = [&](std::wstring arg) {
        auto replaceAll = [&](const std::wstring& from, const std::wstring& to) {
            size_t pos = 0;
            while ((pos = arg.find(from, pos)) != std::wstring::npos) {
                arg.replace(pos, from.size(), to);
                pos += to.size();
            }
        };
        replaceAll(L"${library_directory}", fwd(libraryDir));
        replaceAll(L"${classpath_separator}", L";");
        replaceAll(L"${version_name}", launchVersion);
        replaceAll(L"${natives_directory}", fwd(nativesDir));
        replaceAll(L"${launcher_name}", L"BanditLauncher");
        replaceAll(L"${launcher_version}", L"1");
        return arg;
    };
    const std::wstring jnaTmpDir = gameDir + L"\\tmp";
    const std::wstring lwjglTmpDir = exeDir + L"\\tmp";
    const std::wstring launcherOverrideDir = gameDir + L"\\launcher-overrides";
    const std::wstring packagedNativesDir = packageDir + L"\\natives";
    const bool suppliedNativesReady =
        GetFileAttributesW((nativesDir + L"\\lwjgl.dll").c_str()) != INVALID_FILE_ATTRIBUTES &&
        GetFileAttributesW((nativesDir + L"\\glfw.dll").c_str()) != INVALID_FILE_ATTRIBUTES;
    const bool packagedNativesReady =
        GetFileAttributesW((packagedNativesDir + L"\\lwjgl.dll").c_str()) != INVALID_FILE_ATTRIBUTES &&
        GetFileAttributesW((packagedNativesDir + L"\\glfw.dll").c_str()) != INVALID_FILE_ATTRIBUTES;
    const std::wstring lwjglNativeDir =
        suppliedNativesReady ? nativesDir :
        (packagedNativesReady ? packagedNativesDir : nativesDir);
    const std::wstring lwjglGlfwDll = lwjglNativeDir + L"\\glfw.dll";
    const std::wstring logConfigPath = exeDir + L"\\game\\log_configs\\client-uwp.xml";
    const std::wstring fabricLogPath = gameDir + L"\\logs\\fabric-loader.log";
    const std::wstring forgeLogPath = gameDir + L"\\logs\\forge-loader.log";
    const std::wstring latestLogPath = gameDir + L"\\logs\\latest.log";
    const std::wstring xboxCompatLogPath = gameDir + L"\\xbox_compat.log";
    const std::wstring launcherLogDir = LogsCurrentDir(exeDir);
    EnsureDirectoryTree(launcherLogDir);
    const std::wstring stderrLogPath = launcherLogDir + L"\\stderr_stream.log";

    EnsureDirectoryTree(gameDir + L"\\logs");
    EnsureDirectoryTree(gameDir + L"\\crash-reports");
    EnsureDirectoryTree(gameDir + L"\\saves");
    EnsureDirectoryTree(gameDir + L"\\resourcepacks");
    EnsureDirectoryTree(gameDir + L"\\screenshots");
    EnsureDirectoryTree(gameDir + L"\\config");
    EnsureDirectoryTree(jnaTmpDir);
    EnsureDirectoryTree(launcherOverrideDir);
    DeleteDirectoryTree(gameDir + L"\\showdown");
    DeleteDirectoryTree(exeDir + L"\\showdown");
    EnsureDirectoryTree(gameDir + L"\\showdown");
    EnsureDirectoryTree(exeDir + L"\\showdown");
    EnsureDirectoryTree(userModsDir);
    if ((loaderId == LoaderId::Forge || loaderId == LoaderId::NeoForge) &&
        !bundledModsDir.empty() &&
        DirectoryExists(bundledModsDir)) {
        CopyDirectoryContentsAlways(bundledModsDir, userModsDir);
        WriteLogF(L"Synced bundled loader mods from %s to %s", bundledModsDir.c_str(), userModsDir.c_str());
    }
    const int earlyBlockedRemoved = PurgeBlockedModsFromDir(exeDir, userModsDir);
    if (earlyBlockedRemoved > 0) {
        WriteLogF(L"Removed %d blocked mod(s) from active profile before configuring launch", earlyBlockedRemoved);
    }
    ConfigureKnownModDefaults(gameDir, userModsDir, minecraftVersion, bundledModsDir);
    if (SetCurrentDirectoryW(gameDir.c_str())) {
        WriteLogF(L"Process current directory set to gameDir: %s", gameDir.c_str());
    } else {
        WriteLogF(L"Failed to set process current directory to gameDir err=%u", GetLastError());
    }
    EnsureDirectoryTree(lwjglTmpDir);
    DeleteFileW(javaLog.c_str());
    DeleteFileW(stderrLogPath.c_str());
    DeleteFileW(fabricLogPath.c_str());
    DeleteFileW(forgeLogPath.c_str());
    DeleteFileW(latestLogPath.c_str());
    DeleteFileW(xboxCompatLogPath.c_str());

    if (!RedirectStdStreams(javaLog, stderrLogPath)) {
        WriteLogF(L"Failed to redirect stdout/stderr errno=%d winerr=%u", errno, GetLastError());
    } else {
        WriteLog(L"stdout redirected to logs/current/java_output.log; stderr redirected to logs/current/stderr_stream.log");
    }

    const std::vector<LogTailerConfig> tailerConfigs = {
        LogTailerConfig{ javaLog, L"java_output.log" },
        LogTailerConfig{ stderrLogPath, L"stderr_stream.log" },
        LogTailerConfig{ loaderId == LoaderId::Fabric ? fabricLogPath : forgeLogPath, LoaderTailLogLabel(loaderId) },
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

    LoaderJvmContext loaderCtx;
    loaderCtx.loader = loaderId;
    loaderCtx.exeDir = exeDir;
    loaderCtx.packageDir = packageDir;
    loaderCtx.gameDir = gameDir;
    loaderCtx.clientJar = clientJar;
    loaderCtx.bundledModsDir = bundledModsDir;
    loaderCtx.userModsDir = userModsDir;
    loaderCtx.launchVersion = launchVersion;
    loaderCtx.loaderVersion = loaderVersion;
    loaderCtx.minecraftVersion = minecraftVersion;
    loaderCtx.neoFormVersion = neoFormVersion;
    loaderCtx.neoForgeInstallToolsVersion = neoForgeInstallToolsVersion;
    loaderCtx.neoForgeJarSplitterVersion = neoForgeJarSplitterVersion;
    loaderCtx.neoForgeBinaryPatcherVersion = neoForgeBinaryPatcherVersion;
    loaderCtx.neoForgeAutoRenamingToolVersion = neoForgeAutoRenamingToolVersion;
    loaderCtx.extraGameArgs = extraGameArgs;
    loaderCtx.launcherOverrideDir = launcherOverrideDir;
    loaderCtx.launcherLogDir = launcherLogDir;
    loaderCtx.fabricLogPath = fabricLogPath;
    loaderCtx.libraryDir = libraryDir;
    loaderCtx.neoForgeMinecraftSrgJar = neoForgeMinecraftSrgJar;
    loaderCtx.expandLaunchArg = expandLaunchArg;

    std::wstring effectiveClassPath = classPath;
    LoaderJvmSetupResult loaderSetup;
    LoaderAdjustClasspath(loaderCtx, effectiveClassPath, loaderSetup);
    effectiveClassPath = loaderSetup.effectiveClassPath;
    const bool neoForgeStartedWithGameClassPath = loaderSetup.neoForgeStartedWithGameClassPath;

    std::vector<std::string> vmOptionStorage;
    vmOptionStorage.reserve(16);
    vmOptionStorage.push_back("-Xmx3G");
    vmOptionStorage.push_back("-Xms512M");
    vmOptionStorage.push_back("-XX:MaxDirectMemorySize=512M");
    WriteLog(L"JVM memory defaults: -Xmx3G -Xms512M -XX:MaxDirectMemorySize=512M");
    vmOptionStorage.push_back("--enable-native-access=ALL-UNNAMED");
    vmOptionStorage.push_back("--add-opens=jdk.zipfs/jdk.nio.zipfs=ALL-UNNAMED");
    const std::wstring selectedJavaBasePatchName =
        javaBasePatchName.empty() ? L"java-base-uwp-filesystem.jar" : javaBasePatchName;
    const std::wstring localJavaBasePatch = exeDir + L"\\" + selectedJavaBasePatchName;
    const std::wstring packagedJavaBasePatch = packageDir + L"\\" + selectedJavaBasePatchName;
    const std::wstring javaBasePatch =
        GetFileAttributesW(localJavaBasePatch.c_str()) != INVALID_FILE_ATTRIBUTES
            ? localJavaBasePatch
            : packagedJavaBasePatch;
    if (GetFileAttributesW(javaBasePatch.c_str()) != INVALID_FILE_ATTRIBUTES) {
        vmOptionStorage.push_back("--patch-module=java.base=" + w2a(fwd(javaBasePatch)));
        WriteLogF(L"Java base UWP filesystem patch enabled: %s", javaBasePatch.c_str());
    } else {
        WriteLogF(L"Java base UWP filesystem patch missing: %s", javaBasePatch.c_str());
    }
    const std::wstring selectedJavaZipfsPatchName =
        javaZipfsPatchName.empty() ? L"java-zipfs-realpath.jar" : javaZipfsPatchName;
    const std::wstring localJavaZipfsPatch = exeDir + L"\\" + selectedJavaZipfsPatchName;
    const std::wstring packagedJavaZipfsPatch = packageDir + L"\\" + selectedJavaZipfsPatchName;
    const std::wstring javaZipfsPatch =
        GetFileAttributesW(localJavaZipfsPatch.c_str()) != INVALID_FILE_ATTRIBUTES
            ? localJavaZipfsPatch
            : packagedJavaZipfsPatch;
    if (GetFileAttributesW(javaZipfsPatch.c_str()) != INVALID_FILE_ATTRIBUTES) {
        vmOptionStorage.push_back("--patch-module=jdk.zipfs=" + w2a(fwd(javaZipfsPatch)));
        WriteLogF(L"Java ZipFS realpath patch enabled: %s", javaZipfsPatch.c_str());
    } else {
        WriteLogF(L"Java ZipFS realpath patch missing: %s", javaZipfsPatch.c_str());
    }
    const bool useJava21DesktopPatch = selectedJavaBasePatchName.find(L"-21.jar") != std::wstring::npos;
    const std::wstring javaDesktopPatchName = useJava21DesktopPatch ? L"java-desktop-uwp-awt-21.jar" : L"java-desktop-uwp-awt.jar";
    const std::wstring localJavaDesktopPatch = exeDir + L"\\" + javaDesktopPatchName;
    const std::wstring packagedJavaDesktopPatch = packageDir + L"\\" + javaDesktopPatchName;
    const std::wstring javaDesktopPatch =
        GetFileAttributesW(localJavaDesktopPatch.c_str()) != INVALID_FILE_ATTRIBUTES
            ? localJavaDesktopPatch
            : packagedJavaDesktopPatch;
    if (GetFileAttributesW(javaDesktopPatch.c_str()) != INVALID_FILE_ATTRIBUTES) {
        vmOptionStorage.push_back("--patch-module=java.desktop=" + w2a(fwd(javaDesktopPatch)));
        WriteLogF(L"Java desktop UWP AWT patch enabled: %s", javaDesktopPatch.c_str());
    } else {
        WriteLogF(L"Java desktop UWP AWT patch missing: %s", javaDesktopPatch.c_str());
    }
    vmOptionStorage.push_back("-Djava.home=" + w2a(fwd(jreDir)));
    vmOptionStorage.push_back("-Djava.security.properties==" + w2a(fwd(jreDir + L"\\conf\\security\\xbox.properties")));
    vmOptionStorage.push_back("-Djava.security.egd=file:/dev/urandom");
    vmOptionStorage.push_back("-Djava.awt.headless=true");
    vmOptionStorage.push_back("-Dbanditvault.awt.skipDesktopProperties=true");
    vmOptionStorage.push_back("-Dswing.defaultlaf=javax.swing.plaf.metal.MetalLookAndFeel");
    vmOptionStorage.push_back("-Dfml.readTimeout=300");
    vmOptionStorage.push_back("-Dfml.loginTimeout=300");
    LoaderAddJvmOptions(loaderCtx, vmOptionStorage);
    for (const std::wstring& arg : extraJvmArgs) {
        const std::wstring expanded = expandLaunchArg(arg);
        if (IsJvmMemoryOption(expanded)) {
            WriteLogF(L"Ignoring modpack JVM memory override on Xbox: %s", expanded.c_str());
        } else if (!expanded.empty()) {
            vmOptionStorage.push_back(w2a(expanded));
        }
    }
    if (VerboseLoggingEnabled()) {
        vmOptionStorage.push_back("-Dmixin.debug.verbose=true");
    }
    vmOptionStorage.push_back("-Djava.io.tmpdir=" + w2a(fwd(jnaTmpDir)));
    vmOptionStorage.push_back("-Djna.tmpdir=" + w2a(fwd(jnaTmpDir)));
    vmOptionStorage.push_back("-Djna.nosys=true");
    vmOptionStorage.push_back("-Djna.nounpack=true");
    vmOptionStorage.push_back("-Djna.boot.library.name=jnidispatch");
    vmOptionStorage.push_back("-Djna.boot.library.path=" + w2a(fwd(nativesDir)));
    vmOptionStorage.push_back("-Djava.library.path=" + w2a(fwd(lwjglNativeDir)));
    vmOptionStorage.push_back("-Dorg.lwjgl.librarypath=" + w2a(fwd(lwjglNativeDir)));
    if (VerboseLoggingEnabled()) {
        vmOptionStorage.push_back("-Dorg.lwjgl.util.Debug=true");
        vmOptionStorage.push_back("-Dorg.lwjgl.util.DebugLoader=true");
    }
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
    WriteLogF(L"Log4j configuration: %s", FileUriFromPath(logConfigPath).c_str());
    vmOptionStorage.push_back("-Djava.class.path=" + w2a(effectiveClassPath));
    if (loaderId == LoaderId::Forge) {
        vmOptionStorage.push_back("-DlegacyClassPath=" + w2a(effectiveClassPath));
        WriteLog(L"Forge legacyClassPath aligned with game classpath");
    }
    vmOptionStorage.push_back("-Duser.dir=" + w2a(fwd(gameDir)));
    vmOptionStorage.push_back("-Dlog4j.configurationFile=" + w2a(FileUriFromPath(logConfigPath)));
    vmOptionStorage.push_back("-XX:ErrorFile=" + w2a(fwd(gameDir + L"\\hs_err_pid%p.log")));

    std::vector<std::string> appArgs = {
        "--username", authConfig.username,
        "--version", w2a(launchVersion),
        "--gameDir", w2a(fwd(gameDir)),
        "--assetsDir", w2a(fwd(assetsDir)),
        "--assetIndex", w2a(assetIndex),
        "--uuid", authConfig.uuid,
        "--accessToken", authConfig.accessToken,
        "--versionType", "release"
    };
    for (const std::wstring& arg : extraGameArgs) {
        appArgs.push_back(w2a(expandLaunchArg(arg)));
    }
    if (loaderId == LoaderId::NeoForge && !neoForgeUniversalJar.empty()) {
        if (GetFileAttributesW(neoForgeUniversalJar.c_str()) != INVALID_FILE_ATTRIBUTES) {
            WriteLogF(L"NeoForge universal jar available for launch-handler discovery: %s", neoForgeUniversalJar.c_str());
        } else {
            WriteLogF(L"NeoForge universal jar missing for launch-handler discovery: %s", neoForgeUniversalJar.c_str());
        }
    }

    fprintf(af, "# JVM options\n");
    for (const auto& opt : vmOptionStorage) {
        fprintf(af, "%s\n", opt.c_str());
    }
    fprintf(af, "# Main class\n%s\n", w2a(effectiveMainClass).c_str());
    fprintf(af, "# App args\n");
    for (const auto& arg : appArgs) {
        fprintf(af, "%s\n", arg.c_str());
    }
    fclose(af);
    WriteLog(L"Embedded JVM options written");
    reportProgress(
        L"Starting Java runtime",
        L"Loading the embedded JVM.",
        0.24f);

    HMODULE jvmModule = nullptr;
    if (!PreloadJvm(exeDir, jreDir, packagedJreRelativeDir, nativesDir, &jvmModule)) {
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
    reportProgress(
        L"Starting Java runtime",
        L"Java is online. Preparing loader startup next.",
        0.38f);

    if (!LoaderPrepareArtifactsAfterJvm(env, loaderCtx, effectiveClassPath, neoForgeStartedWithGameClassPath)) {
        DestroyEmbeddedJvm(vm, env);
        return false;
    }
    reportProgress(
        (L"Loading " + loaderLabel).c_str(),
        L"Loader artifacts are ready. Minecraft main is starting next.",
        0.52f);

    jclass mainClass = env->FindClass(w2a(mainClassPath).c_str());
    if (!mainClass || CheckAndLogJavaException(env, (L"FindClass(" + effectiveMainClass + L")").c_str())) {
        DestroyEmbeddedJvm(vm, env);
        return false;
    }

    jmethodID mainMethod = env->GetStaticMethodID(mainClass, "main", "([Ljava/lang/String;)V");
    if (!mainMethod || CheckAndLogJavaException(env, L"GetStaticMethodID(main)")) {
        DestroyEmbeddedJvm(vm, env);
        return false;
    }

    jclass stringClass = env->FindClass("java/lang/String");
    if (!stringClass || CheckAndLogJavaException(env, L"FindClass(String)")) {
        DestroyEmbeddedJvm(vm, env);
        return false;
    }

    jobjectArray argv = env->NewObjectArray(static_cast<jsize>(appArgs.size()), stringClass, nullptr);
    if (!argv || CheckAndLogJavaException(env, L"NewObjectArray")) {
        DestroyEmbeddedJvm(vm, env);
        return false;
    }

    for (jsize i = 0; i < static_cast<jsize>(appArgs.size()); ++i) {
        jstring value = env->NewStringUTF(appArgs[i].c_str());
        if (!value || CheckAndLogJavaException(env, L"NewStringUTF")) {
            DestroyEmbeddedJvm(vm, env);
            return false;
        }
        env->SetObjectArrayElement(argv, i, value);
        env->DeleteLocalRef(value);
        if (CheckAndLogJavaException(env, L"SetObjectArrayElement")) {
            DestroyEmbeddedJvm(vm, env);
            return false;
        }
    }

    WriteLogF(L"Invoking %s.main via embedded JVM", effectiveMainClass.c_str());
    reportProgress(
        (L"Loading " + loaderLabel).c_str(),
        L"Mods and libraries are starting. The Mojang loading screen appears once graphics initialize.",
        0.64f);
    WriteTextFile(CrashLaunchMarkerPath(exeDir),
        std::wstring(L"minecraftVersion=") + minecraftVersion + L"\n" +
        L"launchVersion=" + launchVersion + L"\n" +
        L"jreDir=" + jreDir + L"\n" +
        L"gameDir=" + gameDir + L"\n" +
        L"nativesDir=" + nativesDir + L"\n");
    std::atomic<bool> javaMainRunning{ true };
    std::thread javaMainWatchdog([&javaMainRunning, vm, effectiveMainClass]() {
        unsigned seconds = 0;
        while (javaMainRunning.load()) {
            std::this_thread::sleep_for(std::chrono::seconds(5));
            seconds += 5;
            if (javaMainRunning.load()) {
                WriteLogF(L"%s.main still running after %u seconds", effectiveMainClass.c_str(), seconds);
                if (seconds == 15 || (seconds >= 30 && (seconds % 30) == 0)) {
                    DumpJavaThreadStacks(vm, (effectiveMainClass + L".main watchdog").c_str());
                }
            }
        }
    });

    env->CallStaticVoidMethod(mainClass, mainMethod, argv);
    javaMainRunning.store(false);
    if (javaMainWatchdog.joinable()) {
        javaMainWatchdog.join();
    }

    if (CheckAndClearReturnToLauncherSignal(env, L"CallStaticVoidMethod(main)")) {
        LogTextFileTail(javaLog, L"java_output.log");
        LogTextFileTail(stderrLogPath, L"stderr_stream.log");
        WriteLog(L"Minecraft requested return to launcher");
        StopLogTailers();
        DeleteFileW(CrashLaunchMarkerPath(exeDir).c_str());
        return true;
    }

    if (CheckAndLogJavaException(env, L"CallStaticVoidMethod(main)")) {
        LogTextFileTail(javaLog, L"java_output.log");
        LogTextFileTail(stderrLogPath, L"stderr_stream.log");
        WriteLog(L"Embedded JVM failed after startup; terminating host process to avoid JVM/native reuse");
        StopLogTailers();
        CreateCrashReportZip(exeDir, L"Java exception after Minecraft startup");
        DeleteFileW(CrashLaunchMarkerPath(exeDir).c_str());
        ExitProcess(1);
        return false;
    }

    WriteLogF(L"%s.main returned", effectiveMainClass.c_str());
    g_minecraftRunning.store(false);
    WriteLog(L"Minecraft exited normally; returning to launcher menu");
    StopLogTailers();
    DeleteFileW(CrashLaunchMarkerPath(exeDir).c_str());
    return true;
}

