#include "telemetry.h"

#include "crash_parse.h"
#include "http_client.h"
#include "launcher_common.h"

#include <objbase.h>
#include <roapi.h>

#include <winrt/base.h>
#include <winrt/Windows.ApplicationModel.h>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <mutex>
#include <thread>
#include <vector>

namespace telemetry {
namespace {

constexpr unsigned kBeaconTimeoutMs = 3000;
constexpr unsigned kCrashTimeoutMs = 2000;
constexpr int kMaxQueuedFiles = 20;
constexpr size_t kMaxModsetEntries = 256;

constexpr int kPhaseLauncher = 0;
constexpr int kPhaseJvmInit = 1;
constexpr int kPhaseModLoad = 2;
constexpr int kPhaseIngame = 3;

std::mutex g_mutex;
std::mutex g_installIdMutex;
std::string g_installId;
LaunchContext g_context;
std::string g_launchId;
crashfp::JavaCrash g_javaCrash;
std::string g_javaCrashLaunchId;
std::wstring g_modsDir;
std::vector<std::wstring> g_modsetFiles;
std::atomic<bool> g_launchActive{ false };
std::atomic<bool> g_suspendQueued{ false };
std::atomic<bool> g_firstFrameSent{ false };
std::atomic<int> g_phase{ kPhaseLauncher };
std::atomic<unsigned long long> g_launchStartedAt{ 0 };

const char* PhaseName() {
    switch (g_phase.load()) {
        case kPhaseJvmInit: return "jvm_init";
        case kPhaseModLoad: return "mod_load";
        case kPhaseIngame: return "ingame";
        default: return "launcher";
    }
}

bool JsonFlag(const std::string& body, const char* key) {
    const std::string needle = std::string("\"") + key + "\"";
    size_t at = body.find(needle);
    if (at == std::string::npos) return false;
    at = body.find(':', at + needle.size());
    if (at == std::string::npos) return false;
    ++at;
    while (at < body.size() && (body[at] == ' ' || body[at] == '\t')) ++at;
    return body.compare(at, 4, "true") == 0;
}

int MinutesSoFar() {
    const unsigned long long started = g_launchStartedAt.load();
    if (started == 0) return 0;
    return static_cast<int>((GetTickCount64() - started) / 60000ull);
}

unsigned long long UnixSeconds() {
    FILETIME ft = {};
    GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER value;
    value.LowPart = ft.dwLowDateTime;
    value.HighPart = ft.dwHighDateTime;
    return (value.QuadPart / 10000000ull) - 11644473600ull;
}

std::wstring TelemetryDir() {
    const std::wstring localDir = GetLocalStateDir();
    if (localDir.empty()) return std::wstring();
    return localDir + L"\\telemetry";
}

void ClearCrashStreak() {
    const std::wstring dir = TelemetryDir();
    if (dir.empty()) return;
    DeleteFileW((dir + L"\\crash_streak.txt").c_str());
}

constexpr wchar_t kDefaultEndpoint[] = L"https://telemetry.banditvault.co.uk";

std::wstring Endpoint() {
    static const std::wstring endpoint = [] {
        const std::wstring fromEnv = GetEnvVarString(L"BANDIT_TELEMETRY_ENDPOINT");
        if (!fromEnv.empty()) return TrimWhitespace(fromEnv);

        const std::wstring dir = TelemetryDir();
        if (!dir.empty()) {
            std::wstring text;
            if (ReadTextFile(dir + L"\\endpoint.txt", text)) {
                const std::wstring override = TrimWhitespace(StripNewlines(text));
                if (!override.empty()) return override;
            }
        }

        return std::wstring(kDefaultEndpoint);
    }();
    return endpoint;
}

std::string NewGuidString() {
    GUID guid = {};
    if (FAILED(CoCreateGuid(&guid))) return std::string();

    wchar_t buffer[64] = {};
    if (StringFromGUID2(guid, buffer, 64) == 0) return std::string();

    std::wstring value(buffer);
    if (!value.empty() && value.front() == L'{') value.erase(value.begin());
    if (!value.empty() && value.back() == L'}') value.pop_back();
    return w2a(ToLowerW(value));
}

std::string InstallId() {
    std::lock_guard<std::mutex> lock(g_installIdMutex);
    if (!g_installId.empty()) return g_installId;

    const std::wstring dir = TelemetryDir();
    if (dir.empty()) return std::string();

    const std::wstring path = dir + L"\\install_id.txt";
    std::wstring existing;
    if (ReadTextFile(path, existing)) {
        const std::wstring trimmed = TrimWhitespace(StripNewlines(existing));
        if (!trimmed.empty()) {
            g_installId = w2a(trimmed);
            return g_installId;
        }
    }

    const std::string fresh = NewGuidString();
    if (fresh.empty() || !EnsureDirectoryTree(dir) ||
        !WriteTextFile(path, a2w(fresh.c_str()))) {
        WriteLog(L"telemetry install id could not be created");
        return std::string();
    }
    g_installId = fresh;
    return g_installId;
}

std::string BuildBeaconBody(const char* event, int minutes) {
    LaunchContext context;
    std::string launchId;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        context = g_context;
        launchId = g_launchId;
    }

    std::string body = "{";
    body += "\"installId\":\"" + JsonEscape(InstallId()) + "\",";
    body += "\"launchId\":\"" + JsonEscape(launchId) + "\",";
    body += "\"event\":\"" + std::string(event) + "\",";
    body += "\"launcherBuild\":\"" + JsonEscape(w2a(context.launcherBuild)) + "\",";
    body += "\"mcVersion\":\"" + JsonEscape(w2a(context.mcVersion)) + "\",";
    body += "\"loader\":\"" + JsonEscape(w2a(context.loader)) + "\",";
    body += "\"loaderVersion\":\"" + JsonEscape(w2a(context.loaderVersion)) + "\",";
    body += "\"modsetHash\":\"" + JsonEscape(w2a(context.modsetHash)) + "\",";
    body += "\"minutes\":" + std::to_string(minutes);
    body += "}";
    return body;
}

bool QueueBody(const char* kind, const std::string& body) {
    const std::wstring dir = TelemetryDir();
    if (dir.empty()) return false;

    const std::wstring queueDir = dir + L"\\queue";
    if (!EnsureDirectoryTree(queueDir)) return false;

    int existing = 0;
    WIN32_FIND_DATAW find = {};
    HANDLE handle = FindFirstFileExW((queueDir + L"\\*.json").c_str(),
        FindExInfoBasic, &find, FindExSearchNameMatch, nullptr, 0);
    if (handle != INVALID_HANDLE_VALUE) {
        do {
            if (!(find.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) ++existing;
        } while (FindNextFileW(handle, &find));
        FindClose(handle);
    }

    // cap the queue so a crash loop cannot fill localstate
    if (existing >= kMaxQueuedFiles) return false;

    const std::wstring path = queueDir + L"\\" + a2w(kind) + L"-" + std::to_wstring(GetTickCount64()) +
        L"-" + a2w(NewGuidString().c_str()) + L".json";
    return WriteTextFile(path, a2w(body.c_str()));
}

bool PostSession(const std::string& body, HttpResult& result) {
    const std::wstring endpoint = Endpoint();
    if (endpoint.empty()) return false;

    const std::wstring url = endpoint + L"/v1/session";
    result = HttpPostStringTimed(url.c_str(), body, L"application/json", kBeaconTimeoutMs);
    return result.success();
}

std::string ProjectIdFor(const std::wstring& modsDir, const std::wstring& file) {
    std::wstring body;
    const std::wstring path = GetParentDir(modsDir) + L"\\.bandit\\mod-installs\\" + file + L".meta";
    if (!ReadTextFile(path, body)) return std::string();

    const std::wstring needle = L"projectId\t";
    const size_t at = body.find(needle);
    if (at == std::wstring::npos) return std::string();

    const size_t start = at + needle.size();
    const size_t end = body.find_first_of(L"\r\n", start);
    return w2a(TrimWhitespace(body.substr(start, end == std::wstring::npos ? std::wstring::npos : end - start)));
}

std::string BuildModsetBody(
    const std::wstring& hash,
    const std::wstring& modsDir,
    const std::vector<std::wstring>& files) {
    std::string body = "{";
    body += "\"installId\":\"" + JsonEscape(InstallId()) + "\",";
    body += "\"hash\":\"" + JsonEscape(w2a(hash)) + "\",";
    body += "\"mods\":[";
    for (size_t i = 0; i < files.size(); ++i) {
        if (i != 0) body += ",";
        body += "{\"file\":\"" + JsonEscape(w2a(files[i])) + "\"";
        const std::string projectId = ProjectIdFor(modsDir, files[i]);
        if (!projectId.empty()) body += ",\"projectId\":\"" + JsonEscape(projectId) + "\"";
        body += "}";
    }
    body += "]}";
    return body;
}

void SendModset() {
    const std::wstring endpoint = Endpoint();
    if (endpoint.empty()) return;

    std::wstring hash;
    std::wstring modsDir;
    std::vector<std::wstring> files;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        hash = g_context.modsetHash;
        modsDir = g_modsDir;
        files = g_modsetFiles;
    }

    if (hash.empty() || hash == L"none" || files.empty()) return;
    if (files.size() > kMaxModsetEntries) {
        WriteLogF(L"telemetry modset not sent, %zu mods is over the cap", files.size());
        return;
    }

    const HttpResult result = HttpPostStringTimed((endpoint + L"/v1/modset").c_str(),
        BuildModsetBody(hash, modsDir, files), L"application/json", kBeaconTimeoutMs);
    WriteLogF(L"telemetry modset %s sent with %zu mods, http %d",
        hash.c_str(), files.size(), result.status);
}

void SendAsync(const char* event, int minutes) {
    if (!Enabled() || !g_launchActive.load()) return;

    const std::string body = BuildBeaconBody(event, minutes);
    const bool isLaunch = std::string(event) == "launch";
    std::thread([body, isLaunch]() {
        const HRESULT hr = RoInitialize(RO_INIT_MULTITHREADED);
        HttpResult result;
        if (!PostSession(body, result)) {
            if (!QueueBody("session", body)) {
                WriteLog(L"telemetry session could not be sent or queued");
            }
        } else if (isLaunch && JsonFlag(result.body, "needModset")) {
            SendModset();
        }
        if (SUCCEEDED(hr)) RoUninitialize();
    }).detach();
}

std::string BuildReportBody(
    const LaunchContext& context,
    const char* phase,
    const crashfp::JavaCrash& crash) {
    std::string body = "{";
    body += "\"installId\":\"" + JsonEscape(InstallId()) + "\",";
    body += "\"fingerprint\":\"" + JsonEscape(crash.fingerprint) + "\",";
    body += "\"launcherBuild\":\"" + JsonEscape(w2a(context.launcherBuild)) + "\",";
    body += "\"mcVersion\":\"" + JsonEscape(w2a(context.mcVersion)) + "\",";
    body += "\"loader\":\"" + JsonEscape(w2a(context.loader)) + "\",";
    body += "\"loaderVersion\":\"" + JsonEscape(w2a(context.loaderVersion)) + "\",";
    body += "\"modsetHash\":\"" + JsonEscape(w2a(context.modsetHash)) + "\",";
    body += "\"phase\":\"" + JsonEscape(phase) + "\",";
    body += "\"exitCode\":1,";
    body += "\"heapMaxMb\":" + std::to_string(crash.heapMaxMb) + ",";
    body += "\"heapAtCrashMb\":" + std::to_string(crash.heapAtCrashMb);
    body += "}";
    return body;
}

LaunchContext CurrentContext() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_context;
}

std::string BuildTraceBody(const crashfp::JavaCrash& crash) {
    std::string body = "{";
    body += "\"installId\":\"" + JsonEscape(InstallId()) + "\",";
    body += "\"fingerprint\":\"" + JsonEscape(crash.fingerprint) + "\",";
    body += "\"exception\":\"" + JsonEscape(crash.exception) + "\",";
    body += "\"message\":\"" + JsonEscape(crash.message) + "\",";
    body += "\"frames\":[";
    for (size_t i = 0; i < crash.frames.size(); ++i) {
        if (i != 0) body += ",";
        body += "\"" + JsonEscape(crash.frames[i]) + "\"";
    }
    body += "]";

    if (!crash.detail.empty()) {
        body += ",\"detail\":{";
        body += "\"kind\":\"" + JsonEscape(crash.detail.kind) + "\",";
        body += "\"targetClass\":\"" + JsonEscape(crash.detail.targetClass) + "\",";
        body += "\"targetMethod\":\"" + JsonEscape(crash.detail.targetMethod) + "\",";
        body += "\"descriptor\":\"" + JsonEscape(crash.detail.descriptor) + "\",";
        body += "\"owningMod\":\"" + JsonEscape(crash.detail.owningMod) + "\",";
        body += "\"symbol\":\"" + JsonEscape(crash.detail.symbol) + "\"";
        body += "}";
    }

    body += "}";
    return body;
}

void WriteCrashRecord(
    const std::wstring& runtimeRoot,
    const LaunchContext& context,
    const char* phase,
    const crashfp::JavaCrash& crash) {
    const std::wstring dir = TelemetryDir();
    if (dir.empty()) return;

    std::wstring zipPath;
    if (ReadTextFile(runtimeRoot + L"\\last_crash_report.txt", zipPath)) {
        zipPath = TrimWhitespace(StripNewlines(zipPath));
    }

    std::string body = "{";
    body += "\"ts\":" + std::to_string(UnixSeconds()) + ",";
    body += "\"fingerprint\":\"" + JsonEscape(crash.fingerprint) + "\",";
    body += "\"exception\":\"" + JsonEscape(crash.exception) + "\",";
    body += "\"message\":\"" + JsonEscape(crash.message) + "\",";
    body += "\"phase\":\"" + JsonEscape(phase) + "\",";
    body += "\"suspectedMod\":\"" + JsonEscape(crash.detail.owningMod) + "\",";
    body += "\"launcherBuild\":\"" + JsonEscape(w2a(context.launcherBuild)) + "\",";
    body += "\"mcVersion\":\"" + JsonEscape(w2a(context.mcVersion)) + "\",";
    body += "\"loader\":\"" + JsonEscape(w2a(context.loader)) + "\",";
    body += "\"loaderVersion\":\"" + JsonEscape(w2a(context.loaderVersion)) + "\",";
    body += "\"modsetHash\":\"" + JsonEscape(w2a(context.modsetHash)) + "\",";
    body += "\"heapMaxMb\":" + std::to_string(crash.heapMaxMb) + ",";
    body += "\"heapAtCrashMb\":" + std::to_string(crash.heapAtCrashMb) + ",";

    body += "\"frames\":[";
    for (size_t i = 0; i < crash.frames.size(); ++i) {
        if (i != 0) body += ",";
        body += "\"" + JsonEscape(crash.frames[i]) + "\"";
    }
    body += "],";

    body += "\"detail\":{";
    body += "\"kind\":\"" + JsonEscape(crash.detail.kind) + "\",";
    body += "\"targetClass\":\"" + JsonEscape(crash.detail.targetClass) + "\",";
    body += "\"targetMethod\":\"" + JsonEscape(crash.detail.targetMethod) + "\",";
    body += "\"descriptor\":\"" + JsonEscape(crash.detail.descriptor) + "\",";
    body += "\"owningMod\":\"" + JsonEscape(crash.detail.owningMod) + "\",";
    body += "\"symbol\":\"" + JsonEscape(crash.detail.symbol) + "\"";
    body += "},";

    body += "\"zip\":\"" + JsonEscape(w2a(zipPath)) + "\"";
    body += "}";

    if (!EnsureDirectoryTree(dir) ||
        !WriteTextFile(dir + L"\\last_crash.json", a2w(body.c_str()))) {
        WriteLog(L"telemetry crash record could not be written");
    }
}

std::string ReadFileUtf8(const std::wstring& path) {
    std::wstring text;
    if (!ReadTextFile(path, text)) return std::string();
    return w2a(text);
}

unsigned long long FileWrittenAt(const std::wstring& path) {
    WIN32_FILE_ATTRIBUTE_DATA data = {};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data)) return 0;
    ULARGE_INTEGER value;
    value.LowPart = data.ftLastWriteTime.dwLowDateTime;
    value.HighPart = data.ftLastWriteTime.dwHighDateTime;
    return value.QuadPart;
}

std::wstring NewestFileAfter(const std::wstring& dir, const std::wstring& pattern, unsigned long long after) {
    std::wstring best;
    unsigned long long bestAt = after;

    WIN32_FIND_DATAW find = {};
    HANDLE handle = FindFirstFileExW((dir + L"\\" + pattern).c_str(),
        FindExInfoBasic, &find, FindExSearchNameMatch, nullptr, 0);
    if (handle == INVALID_HANDLE_VALUE) return best;
    do {
        if (find.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        ULARGE_INTEGER value;
        value.LowPart = find.ftLastWriteTime.dwLowDateTime;
        value.HighPart = find.ftLastWriteTime.dwHighDateTime;
        if (value.QuadPart <= bestAt) continue;
        bestAt = value.QuadPart;
        best = dir + L"\\" + find.cFileName;
    } while (FindNextFileW(handle, &find));
    FindClose(handle);
    return best;
}

struct HardCrashSources {
    bool found = false;
    std::string markerText;
    std::string crashReport;
    std::string hsErr;
    std::string latestLog;
    std::string mcLaunchLog;
};

HardCrashSources g_hardCrash;

}

bool Enabled() {
    return !Endpoint().empty() && ConsentGranted() && !InstallId().empty();
}

bool Configured() {
    return !Endpoint().empty();
}

static std::wstring ConsentPath() {
    const std::wstring dir = TelemetryDir();
    if (dir.empty()) return std::wstring();
    return dir + L"\\consent.txt";
}

ConsentState Consent() {
    const std::wstring path = ConsentPath();
    if (path.empty()) return ConsentState::Unanswered;

    std::wstring text;
    if (!ReadTextFile(path, text)) return ConsentState::Unanswered;

    const std::wstring value = ToLowerW(TrimWhitespace(StripNewlines(text)));
    if (value == L"always") return ConsentState::Always;
    if (value == L"never") return ConsentState::Never;
    return ConsentState::Unanswered;
}

bool SetConsent(ConsentState state) {
    const std::wstring path = ConsentPath();
    if (path.empty()) return false;

    if (state == ConsentState::Always && InstallId().empty()) {
        WriteLog(L"telemetry consent could not be enabled without an install id");
        return false;
    }

    if (state == ConsentState::Unanswered) {
        const bool removed = DeleteFileW(path.c_str()) || GetLastError() == ERROR_FILE_NOT_FOUND;
        if (!removed) {
            WriteLogF(L"telemetry consent could not be cleared error=%lu", GetLastError());
            return false;
        }
        WriteLog(L"telemetry consent cleared");
        return true;
    }

    const wchar_t* value = (state == ConsentState::Always) ? L"always" : L"never";
    if (!EnsureDirectoryTree(TelemetryDir()) ||
        !WriteTextFile(path, std::wstring(value) + L"\n")) {
        WriteLogF(L"telemetry consent could not be saved as %s", value);
        return false;
    }
    WriteLogF(L"telemetry consent set to %s", value);
    return true;
}

bool ConsentGranted() {
    return Consent() == ConsentState::Always;
}

std::wstring InstallIdText() {
    return a2w(InstallId().c_str());
}

bool ResetInstallId() {
    const std::wstring dir = TelemetryDir();
    if (dir.empty()) return false;

    const std::wstring path = dir + L"\\install_id.txt";
    if (!DeleteFileW(path.c_str()) && GetLastError() != ERROR_FILE_NOT_FOUND) {
        WriteLogF(L"telemetry install id could not be cleared error=%lu", GetLastError());
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(g_installIdMutex);
        g_installId.clear();
    }
    ClearQueue();
    const bool created = !InstallId().empty();
    WriteLog(created ? L"telemetry install id reset" : L"telemetry install id reset failed");
    return created;
}

std::wstring EndpointUrl() {
    return Endpoint();
}

std::wstring LauncherBuild() {
    static const std::wstring build = [] {
        try {
            const auto version = winrt::Windows::ApplicationModel::Package::Current().Id().Version();
            wchar_t buffer[64] = {};
            swprintf_s(buffer, L"%u.%u.%u.%u",
                version.Major, version.Minor, version.Build, version.Revision);
            return std::wstring(buffer);
        } catch (...) {
            WriteLog(L"telemetry launcher version could not be read");
            return std::wstring(L"0.0.0.0");
        }
    }();
    return build;
}

std::wstring ComputeModsetHash(const std::wstring& modsDir) {
    std::vector<std::wstring> mods;
    WIN32_FIND_DATAW find = {};
    HANDLE handle = FindFirstFileExW((modsDir + L"\\*.jar").c_str(),
        FindExInfoBasic, &find, FindExSearchNameMatch, nullptr, 0);
    if (handle != INVALID_HANDLE_VALUE) {
        do {
            if (!(find.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                mods.push_back(ToLowerW(find.cFileName));
            }
        } while (FindNextFileW(handle, &find));
        FindClose(handle);
    }
    if (mods.empty()) return std::wstring(L"none");

    std::sort(mods.begin(), mods.end());

    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_modsDir = modsDir;
        g_modsetFiles = mods;
    }

    std::string joined;
    for (const auto& mod : mods) {
        joined += w2a(mod);
        joined.push_back('\n');
    }

    return a2w(crashfp::Sha256Hex16(joined).c_str());
}

void BeginLaunch(const LaunchContext& context) {
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_context = context;
        g_launchId = NewGuidString();
        g_javaCrash = crashfp::JavaCrash();
        g_javaCrashLaunchId.clear();
    }
    g_launchStartedAt.store(GetTickCount64());
    g_suspendQueued.store(false);
    g_firstFrameSent.store(false);
    g_phase.store(kPhaseJvmInit);
    g_launchActive.store(true);
}

void EndLaunch() {
    g_launchActive.store(false);
    g_launchStartedAt.store(0);
    g_phase.store(kPhaseLauncher);
}

void SendLaunch() {
    g_phase.store(kPhaseModLoad);
    SendAsync("launch", 0);
}

void SendFirstFrame() {
    if (g_firstFrameSent.exchange(true)) return;
    SendAsync("first_frame", 0);
}

void SendPlayable() {
    ClearCrashStreak();
    SendFirstFrame();
    g_phase.store(kPhaseIngame);
    SendAsync("playable", 0);
}
void SendExit() {
    ClearCrashStreak();
    SendAsync("exit", MinutesSoFar());
}

void QueueSuspend() {
    if (!Enabled() || !g_launchActive.load()) return;
    if (g_suspendQueued.exchange(true)) return;

    const bool queued = QueueBody("session", BuildBeaconBody("suspend", MinutesSoFar()));
    WriteLog(queued ? L"telemetry suspend beacon queued" : L"telemetry suspend beacon dropped");
}

void RecordJavaCrash(const crashfp::JavaCrash& crash) {
    if (!crash.valid()) return;
    std::lock_guard<std::mutex> lock(g_mutex);
    g_javaCrash = crash;
    g_javaCrashLaunchId = g_launchId;
}

void ReportSoftCrash(const std::wstring& runtimeRoot) {
    crashfp::JavaCrash crash;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (!g_launchId.empty() && g_javaCrashLaunchId == g_launchId) crash = g_javaCrash;
    }
    if (!crash.valid()) {
        const std::string gcLog = ReadFileUtf8(LogsCurrentDir(runtimeRoot) + L"\\gc.log");
        int pinnedMb = 0, capMb = 0;
        if (crashparse::DetectHeapExhaustion(gcLog, pinnedMb, capMb)) {
            const std::string detail =
                "heap pinned at " + std::to_string(pinnedMb) + "M of " + std::to_string(capMb) + "M";
            crash = crashfp::Build("heap_exhausted", "heap_exhausted", detail, {});
            WriteLogF(L"telemetry soft crash was heap exhaustion, %s", a2w(detail.c_str()).c_str());
        } else {
            crash = crashfp::Build("unknown_soft_crash", "unknown_soft_crash", "", {});
            WriteLog(L"telemetry soft crash had no usable java exception, reporting as unknown");
        }
    }
    if (!crash.valid()) return;

    WriteCrashRecord(runtimeRoot, CurrentContext(), PhaseName(), crash);
    WriteLogF(L"telemetry soft crash %s phase=%s", a2w(crash.fingerprint.c_str()).c_str(),
        a2w(PhaseName()).c_str());

    if (!Enabled()) return;

    const std::wstring endpoint = Endpoint();

    const HRESULT hr = RoInitialize(RO_INIT_MULTITHREADED);

    const std::string reportBody = BuildReportBody(CurrentContext(), PhaseName(), crash);
    const HttpResult result = HttpPostStringTimed(
        (endpoint + L"/v1/report").c_str(), reportBody, L"application/json", kCrashTimeoutMs);

    if (result.success()) {
        if (JsonFlag(result.body, "needTrace")) {
            const std::string traceBody = BuildTraceBody(crash);
            const HttpResult traceResult = HttpPostStringTimed(
                (endpoint + L"/v1/trace").c_str(), traceBody,
                L"application/json", kCrashTimeoutMs);
            if (!traceResult.success() && !QueueBody("trace", traceBody)) {
                WriteLog(L"telemetry crash trace could not be sent or queued");
            }
        }
    } else {
        // queue the trace when the server could not answer whether it needs one
        const bool reportQueued = QueueBody("report", reportBody);
        const bool traceQueued = QueueBody("trace", BuildTraceBody(crash));
        if (!reportQueued || !traceQueued) {
            WriteLog(L"telemetry soft crash queue was incomplete");
        }
        WriteLogF(L"telemetry soft crash queued, post returned %d", result.status);
    }

    if (SUCCEEDED(hr)) RoUninitialize();
}

void PrepareHardCrash(const std::wstring& runtimeRoot) {
    if (GetFileAttributesW(LaunchSuspendedMarkerPath(runtimeRoot).c_str()) != INVALID_FILE_ATTRIBUTES) {
        return;
    }

    std::wstring markerPath = CrashLaunchMarkerPath(runtimeRoot);
    unsigned long long markerAt = FileWrittenAt(markerPath);
    if (markerAt == 0) {
        markerPath = runtimeRoot + L"\\.minecraft_launch_active";
        markerAt = FileWrittenAt(markerPath);
    }
    if (markerAt == 0) return;

    g_hardCrash = HardCrashSources();
    g_hardCrash.found = true;
    g_hardCrash.markerText = ReadFileUtf8(markerPath);

    g_hardCrash.mcLaunchLog = ReadFileUtf8(LogsPreviousDir(runtimeRoot) + L"\\mc_launch.log");
    if (g_hardCrash.mcLaunchLog.empty()) {
        g_hardCrash.mcLaunchLog = ReadFileUtf8(LogsCurrentDir(runtimeRoot) + L"\\mc_launch.log");
    }

    const std::wstring gameDir = a2w(
        crashparse::MarkerValue(g_hardCrash.markerText, "gameDir").c_str());
    if (gameDir.empty()) return;

    const std::wstring report = NewestFileAfter(gameDir + L"\\crash-reports", L"*.txt", markerAt);
    if (!report.empty()) g_hardCrash.crashReport = ReadFileUtf8(report);

    const std::wstring hsErr = NewestFileAfter(gameDir, L"hs_err_pid*.log", markerAt);
    if (!hsErr.empty()) g_hardCrash.hsErr = ReadFileUtf8(hsErr);

    g_hardCrash.latestLog = ReadFileUtf8(gameDir + L"\\logs\\latest.log");
}

void ReportHardCrash(const std::wstring& runtimeRoot) {
    if (!g_hardCrash.found) return;

    crashparse::ParsedCrash parsed = crashparse::ParseMinecraftCrashReport(g_hardCrash.crashReport);
    if (!parsed.valid()) parsed = crashparse::ParseHsErr(g_hardCrash.hsErr);
    if (!parsed.valid()) parsed = crashparse::ParseLatestLog(g_hardCrash.latestLog);
    if (!parsed.valid()) {
        parsed = crashparse::ParsedCrash();
        parsed.outerClass = "unknown_hard_crash";
        parsed.rootClass = parsed.outerClass;
        parsed.source = "none";
    }

    crashfp::JavaCrash crash = crashfp::Build(
        parsed.outerClass, parsed.rootClass, parsed.message, parsed.frames);
    if (!crash.valid()) return;

    LaunchContext context;
    context.launcherBuild = a2w(crashparse::MarkerValue(g_hardCrash.markerText, "launcherBuild").c_str());
    context.mcVersion = a2w(crashparse::MarkerValue(g_hardCrash.markerText, "minecraftVersion").c_str());
    context.loader = a2w(crashparse::MarkerValue(g_hardCrash.markerText, "loader").c_str());
    context.loaderVersion = a2w(crashparse::MarkerValue(g_hardCrash.markerText, "loaderVersion").c_str());
    context.modsetHash = a2w(crashparse::MarkerValue(g_hardCrash.markerText, "modsetHash").c_str());

    const std::string phase = crashparse::DetectPhase(g_hardCrash.mcLaunchLog);

    WriteCrashRecord(runtimeRoot, context, phase.c_str(), crash);
    WriteLogF(L"telemetry hard crash %s phase=%s source=%s",
        a2w(crash.fingerprint.c_str()).c_str(),
        a2w(phase.c_str()).c_str(),
        a2w(parsed.source.c_str()).c_str());

    if (!Enabled()) return;

    const bool reportQueued = QueueBody("report", BuildReportBody(context, phase.c_str(), crash));
    const bool traceQueued = QueueBody("trace", BuildTraceBody(crash));
    if (!reportQueued || !traceQueued) {
        WriteLog(L"telemetry hard crash queue was incomplete");
    }
}

namespace {

std::wstring JsonText(const winrt::Windows::Data::Json::JsonObject& object, const wchar_t* name) {
    using winrt::Windows::Data::Json::JsonValueType;
    if (!object.HasKey(name)) return std::wstring();
    const auto value = object.GetNamedValue(name);
    if (value.ValueType() != JsonValueType::String) return std::wstring();
    return std::wstring(value.GetString());
}

int JsonInt(const winrt::Windows::Data::Json::JsonObject& object, const wchar_t* name) {
    using winrt::Windows::Data::Json::JsonValueType;
    if (!object.HasKey(name)) return 0;
    const auto value = object.GetNamedValue(name);
    if (value.ValueType() != JsonValueType::Number) return 0;
    return static_cast<int>(value.GetNumber());
}

long long JsonInt64(const winrt::Windows::Data::Json::JsonObject& object, const wchar_t* name) {
    using winrt::Windows::Data::Json::JsonValueType;
    if (!object.HasKey(name)) return 0;
    const auto value = object.GetNamedValue(name);
    if (value.ValueType() != JsonValueType::Number) return 0;
    return static_cast<long long>(value.GetNumber());
}

std::wstring RepeatStatePath() {
    const std::wstring dir = TelemetryDir();
    if (dir.empty()) return std::wstring();
    return dir + L"\\crash_streak.txt";
}

LaunchContext ContextFromRecord(const CrashRecord& record) {
    LaunchContext context;
    context.launcherBuild = record.launcherBuild;
    context.mcVersion = record.mcVersion;
    context.loader = record.loader;
    context.loaderVersion = record.loaderVersion;
    context.modsetHash = record.modsetHash;
    return context;
}

crashfp::JavaCrash CrashFromRecord(const CrashRecord& record) {
    crashfp::JavaCrash crash;
    crash.fingerprint = w2a(record.fingerprint);
    crash.exception = w2a(record.exception);
    crash.message = w2a(record.message);
    crash.heapMaxMb = record.heapMaxMb;
    crash.heapAtCrashMb = record.heapAtCrashMb;
    for (const std::wstring& frame : record.frames) crash.frames.push_back(w2a(frame));
    crash.detail = record.detail;
    return crash;
}

}

CrashRecord ReadLastCrash() {
    CrashRecord record;

    const std::wstring dir = TelemetryDir();
    if (dir.empty()) return record;

    const std::wstring path = dir + L"\\last_crash.json";
    std::wstring text;
    if (!ReadTextFile(path, text)) return record;

    unsigned long long writtenAt = 0;

    try {
        using namespace winrt::Windows::Data::Json;
        const JsonObject root = JsonObject::Parse(winrt::hstring(text.c_str()));

        record.fingerprint = JsonText(root, L"fingerprint");
        if (record.fingerprint.empty()) return CrashRecord();

        writtenAt = static_cast<unsigned long long>(JsonInt64(root, L"ts"));

        record.exception = JsonText(root, L"exception");
        record.message = JsonText(root, L"message");
        record.phase = JsonText(root, L"phase");
        record.suspectedMod = JsonText(root, L"suspectedMod");
        record.launcherBuild = JsonText(root, L"launcherBuild");
        record.mcVersion = JsonText(root, L"mcVersion");
        record.loader = JsonText(root, L"loader");
        record.loaderVersion = JsonText(root, L"loaderVersion");
        record.modsetHash = JsonText(root, L"modsetHash");
        record.zip = JsonText(root, L"zip");
        record.heapMaxMb = JsonInt(root, L"heapMaxMb");
        record.heapAtCrashMb = JsonInt(root, L"heapAtCrashMb");

        if (root.HasKey(L"frames") &&
            root.GetNamedValue(L"frames").ValueType() == JsonValueType::Array) {
            const JsonArray frames = root.GetNamedArray(L"frames");
            for (uint32_t i = 0; i < frames.Size(); ++i) {
                if (frames.GetAt(i).ValueType() != JsonValueType::String) continue;
                record.frames.push_back(std::wstring(frames.GetAt(i).GetString()));
            }
        }

        if (root.HasKey(L"detail") &&
            root.GetNamedValue(L"detail").ValueType() == JsonValueType::Object) {
            const JsonObject detail = root.GetNamedObject(L"detail");
            record.detail.kind = w2a(JsonText(detail, L"kind"));
            record.detail.targetClass = w2a(JsonText(detail, L"targetClass"));
            record.detail.targetMethod = w2a(JsonText(detail, L"targetMethod"));
            record.detail.descriptor = w2a(JsonText(detail, L"descriptor"));
            record.detail.owningMod = w2a(JsonText(detail, L"owningMod"));
            record.detail.symbol = w2a(JsonText(detail, L"symbol"));
        }

        record.found = true;
    } catch (...) {
        WriteLog(L"telemetry last_crash.json could not be parsed, ignoring it");
        return CrashRecord();
    }

    constexpr unsigned long long kMaxAgeSeconds = 14ull * 24ull * 60ull * 60ull;
    const unsigned long long now = UnixSeconds();
    if (writtenAt == 0 || (now > writtenAt && now - writtenAt > kMaxAgeSeconds)) {
        WriteLogF(L"telemetry last_crash.json is stale (ts=%llu), discarding it", writtenAt);
        DeleteFileW(path.c_str());
        ClearCrashStreak();
        return CrashRecord();
    }

    const std::wstring streakPath = RepeatStatePath();
    if (!streakPath.empty()) {
        std::wstring previous;
        if (ReadTextFile(streakPath, previous)) {
            const std::wstring trimmed = TrimWhitespace(StripNewlines(previous));
            const size_t space = trimmed.find(L' ');
            if (space != std::wstring::npos &&
                trimmed.substr(0, space) == record.fingerprint) {
                record.repeatCount = _wtoi(trimmed.substr(space + 1).c_str()) + 1;
                if (record.repeatCount < 1) record.repeatCount = 1;
            }
        }
        EnsureDirectoryTree(dir);
        WriteTextFile(streakPath,
            record.fingerprint + L" " + std::to_wstring(record.repeatCount) + L"\n");
    }

    return record;
}

void ClearLastCrash() {
    const std::wstring dir = TelemetryDir();
    if (dir.empty()) return;
    DeleteFileW((dir + L"\\last_crash.json").c_str());
}

void SendLastCrashOnce(const CrashRecord& record) {
    if (!record.found) return;

    const std::wstring endpoint = Endpoint();
    if (endpoint.empty()) return;

    const LaunchContext context = ContextFromRecord(record);
    const crashfp::JavaCrash crash = CrashFromRecord(record);
    const std::string phase = record.phase.empty() ? std::string("launcher") : w2a(record.phase);

    std::thread([endpoint, context, crash, phase]() {
        const HRESULT hr = RoInitialize(RO_INIT_MULTITHREADED);

        const std::string reportBody = BuildReportBody(context, phase.c_str(), crash);
        const HttpResult result = HttpPostStringTimed(
            (endpoint + L"/v1/report").c_str(), reportBody, L"application/json", kBeaconTimeoutMs);
        if (result.success() && JsonFlag(result.body, "needTrace")) {
            const HttpResult traceResult = HttpPostStringTimed(
                (endpoint + L"/v1/trace").c_str(), BuildTraceBody(crash),
                L"application/json", kBeaconTimeoutMs);
            WriteLogF(L"telemetry one-off crash trace returned %d", traceResult.status);
        }
        WriteLogF(L"telemetry one-off crash report returned %d", result.status);

        if (SUCCEEDED(hr)) RoUninitialize();
    }).detach();
}

std::wstring ConsentPayloadPreview(const CrashRecord& record) {
    const std::string report = BuildReportBody(
        ContextFromRecord(record), w2a(record.phase).c_str(), CrashFromRecord(record));
    const std::string trace = BuildTraceBody(CrashFromRecord(record));

    const auto pretty = [](const std::string& body) {
        std::string value;
        for (const char ch : body) {
            if (ch == '{') { value += "{\n  "; continue; }
            if (ch == '}') { value += "\n}"; continue; }
            if (ch == ',') { value += ",\n  "; continue; }
            value += ch;
        }
        return a2w(value.c_str());
    };

    return L"crash summary\n" + pretty(report) + L"\n\ncrash trace\n" + pretty(trace);
}

void ClearQueue() {
    const std::wstring dir = TelemetryDir();
    if (dir.empty()) return;

    const std::wstring queueDir = dir + L"\\queue";
    WIN32_FIND_DATAW find = {};
    HANDLE handle = FindFirstFileExW((queueDir + L"\\*.json").c_str(),
        FindExInfoBasic, &find, FindExSearchNameMatch, nullptr, 0);
    if (handle == INVALID_HANDLE_VALUE) return;

    int removed = 0;
    do {
        if (find.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        if (DeleteFileW((queueDir + L"\\" + find.cFileName).c_str())) ++removed;
    } while (FindNextFileW(handle, &find));
    FindClose(handle);

    WriteLogF(L"telemetry queue cleared, %d files removed", removed);
}

void FlushQueueAsync() {
    if (!Enabled()) return;

    std::thread([]() {
        const HRESULT hr = RoInitialize(RO_INIT_MULTITHREADED);

        const std::wstring queueDir = TelemetryDir() + L"\\queue";
        std::vector<std::wstring> files;
        WIN32_FIND_DATAW find = {};
        HANDLE handle = FindFirstFileExW((queueDir + L"\\*.json").c_str(),
            FindExInfoBasic, &find, FindExSearchNameMatch, nullptr, 0);
        if (handle != INVALID_HANDLE_VALUE) {
            do {
                if (!(find.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                    files.push_back(queueDir + L"\\" + find.cFileName);
                }
            } while (FindNextFileW(handle, &find));
            FindClose(handle);
        }

        std::sort(files.begin(), files.end());

        int sent = 0;
        for (const auto& path : files) {
            std::wstring body;
            if (!ReadTextFile(path, body)) {
                WriteLogF(L"telemetry queued file could not be read %s", path.c_str());
                continue;
            }

            const std::wstring name = GetFileName(path);
            std::wstring route = L"/v1/session";
            if (name.compare(0, 7, L"report-") == 0) route = L"/v1/report";
            else if (name.compare(0, 6, L"trace-") == 0) route = L"/v1/trace";

            // retrying a rejected payload cannot make it valid
            const std::wstring endpoint = Endpoint();
            const HttpResult result = HttpPostStringTimed(
                (endpoint + route).c_str(), w2a(body), L"application/json", kBeaconTimeoutMs);
            if (result.success() || (result.status >= 400 && result.status < 500)) {
                DeleteFileW(path.c_str());
                ++sent;
            }
        }

        if (!files.empty()) {
            WriteLogF(L"telemetry queue flushed %d of %zu", sent, files.size());
        }
        if (SUCCEEDED(hr)) RoUninitialize();
    }).detach();
}

}
