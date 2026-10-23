#include "update_check.h"

#include "http_client.h"
#include "launcher_common.h"
#include "telemetry.h"

#include "runtime_config.h"

#include <windows.h>
#include <roapi.h>

#include <atomic>
#include <mutex>
#include <thread>

namespace updates {
namespace {

constexpr unsigned kReleaseTimeoutMs = 6000;
constexpr unsigned kChangelogTimeoutMs = 8000;
constexpr unsigned long long kRefreshSeconds = 6ull * 60ull * 60ull;
constexpr size_t kMaxChangelogBytes = 512u * 1024u;
constexpr wchar_t kRepo[] = L"veroxsity/JavaUWP";
constexpr wchar_t kReleasesPage[] = L"https://github.com/veroxsity/JavaUWP/releases";

struct Channel {
    const wchar_t* name;
    const wchar_t* branch;
};

const Channel* ActiveChannel() {
    static const Channel channels[] = {
        { L"nightly", L"main" },
        { L"stable", L"stable" },
        { L"nightly-native", L"main-native" },
    };
    for (const Channel& channel : channels) {
        if (wcscmp(kReleaseChannelW, channel.name) == 0) return &channel;
    }
    return nullptr;
}

std::mutex g_mutex;
bool g_loaded = false;
std::wstring g_latest;
unsigned long long g_checkedAt = 0;
bool g_enabledLoaded = false;
bool g_enabled = true;
bool g_offerTaken = false;
std::atomic<bool> g_fetching{ false };

std::wstring Dir() {
    const std::wstring localDir = GetLocalStateDir();
    return localDir.empty() ? localDir : localDir + L"\\updates";
}

std::wstring FilePath(const wchar_t* name) {
    const std::wstring dir = Dir();
    return dir.empty() ? dir : dir + L"\\" + name;
}

unsigned long long NowSeconds() {
    FILETIME ft = {};
    GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER value;
    value.LowPart = ft.dwLowDateTime;
    value.HighPart = ft.dwHighDateTime;
    return value.QuadPart / 10000000ull;
}

bool ValidVersion(const std::wstring& version) {
    int dots = 0;
    size_t digits = 0;
    for (const wchar_t ch : version) {
        if (ch >= L'0' && ch <= L'9') {
            if (++digits > 5) return false;
        } else if (ch == L'.' && digits > 0) {
            ++dots;
            digits = 0;
        } else {
            return false;
        }
    }
    return digits > 0 && dots == 3;
}

// the release workflow writes this exact line into every release body
std::wstring ParsePackageVersion(const std::string& body) {
    static const char key[] = "Package version:";
    const size_t at = body.find(key);
    if (at == std::string::npos) return std::wstring();

    size_t i = at + sizeof(key) - 1;
    while (i < body.size() && (body[i] == ' ' || body[i] == '\t')) ++i;

    std::wstring version;
    while (i < body.size() && ((body[i] >= '0' && body[i] <= '9') || body[i] == '.')) {
        version += static_cast<wchar_t>(body[i++]);
    }
    while (!version.empty() && version.back() == L'.') version.pop_back();
    return ValidVersion(version) ? version : std::wstring();
}

void LoadLocked() {
    if (g_loaded) return;
    g_loaded = true;

    std::wstring text;
    const std::wstring path = FilePath(L"latest.txt");
    if (path.empty() || !ReadTextFile(path, text)) return;

    const size_t newline = text.find(L'\n');
    const std::wstring version = TrimWhitespace(text.substr(0, newline));
    if (!ValidVersion(version)) return;

    g_latest = version;
    if (newline != std::wstring::npos) {
        g_checkedAt = _wcstoui64(TrimWhitespace(text.substr(newline + 1)).c_str(), nullptr, 10);
    }
}

bool FetchLatest(const Channel& channel, std::wstring& version) {
    const std::wstring url = std::wstring(L"https://api.github.com/repos/") + kRepo + L"/releases/tags/" + channel.name;
    std::string etag;
    const HttpResult result = HttpGetConditionalTimed(url.c_str(), "", kReleaseTimeoutMs, etag);
    if (!result.success()) {
        WriteLogF(L"update check: release lookup returned %d", result.status);
        return false;
    }
    version = ParsePackageVersion(result.body);
    if (version.empty()) {
        WriteLog(L"update check: the release notes carried no package version");
        return false;
    }
    return true;
}

bool FetchChangelog(const Channel& channel) {
    const std::wstring url =
        std::wstring(L"https://raw.githubusercontent.com/") + kRepo + L"/" + channel.branch + L"/CHANGELOG.md";
    std::string etag;
    const HttpResult result = HttpGetConditionalTimed(url.c_str(), "", kChangelogTimeoutMs, etag);
    if (!result.success() || result.body.empty() || result.body.size() > kMaxChangelogBytes) {
        WriteLogF(L"update check: changelog fetch returned %d", result.status);
        return false;
    }
    // an error page would otherwise end up on screen as the changelog
    static const char heading[] = "# Changelog";
    if (result.body.compare(0, sizeof(heading) - 1, heading) != 0) {
        WriteLog(L"update check: the changelog response was not a changelog");
        return false;
    }

    const std::wstring path = FilePath(L"changelog.md");
    const std::wstring buildPath = FilePath(L"changelog.build");
    if (path.empty() || !EnsureDirectoryTree(Dir()) ||
        !WriteAllBytes(path, result.body.data(), result.body.size()) ||
        !WriteTextFile(buildPath, telemetry::LauncherBuild() + L"\n")) {
        WriteLog(L"update check: the changelog could not be saved");
        return false;
    }
    return true;
}

// a copy fetched by an older build would hide the notes packaged with this one
bool FetchedChangelogIsCurrent() {
    std::wstring saved;
    const std::wstring buildPath = FilePath(L"changelog.build");
    return !buildPath.empty() && ReadTextFile(buildPath, saved) &&
        TrimWhitespace(saved) == telemetry::LauncherBuild();
}

bool NewerThanRunning(const std::wstring& latest) {
    return !latest.empty() &&
        CompareVersionNumbers(w2a(latest), w2a(telemetry::LauncherBuild())) > 0;
}

void AppendInline(const std::wstring& line, std::wstring& out, TextRanges& bold) {
    bool inBold = false;
    unsigned boldStart = 0;
    for (size_t i = 0; i < line.size(); ++i) {
        const wchar_t ch = line[i];
        if (ch == L'*' && i + 1 < line.size() && line[i + 1] == L'*') {
            if (inBold) {
                bold.emplace_back(boldStart, static_cast<unsigned>(out.size()) - boldStart);
                inBold = false;
            } else {
                inBold = true;
                boldStart = static_cast<unsigned>(out.size());
            }
            ++i;
            continue;
        }
        if (ch == L'`') continue;
        if (ch == L'[') {
            const size_t close = line.find(L']', i + 1);
            if (close != std::wstring::npos && close + 1 < line.size() && line[close + 1] == L'(') {
                const size_t end = line.find(L')', close + 2);
                if (end != std::wstring::npos) {
                    out.append(line, i + 1, close - i - 1);
                    i = end;
                    continue;
                }
            }
        }
        out += ch;
    }
    if (inBold && out.size() > boldStart) {
        bold.emplace_back(boldStart, static_cast<unsigned>(out.size()) - boldStart);
    }
}

}

bool HasReleaseChannel() {
    return ActiveChannel() != nullptr;
}

bool Enabled() {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_enabledLoaded) {
        g_enabledLoaded = true;
        std::wstring text;
        const std::wstring path = FilePath(L"enabled.txt");
        if (!path.empty() && ReadTextFile(path, text)) {
            g_enabled = ToLowerW(TrimWhitespace(text)) != L"off";
        }
    }
    return g_enabled;
}

bool SetEnabled(bool on) {
    const std::wstring path = FilePath(L"enabled.txt");
    if (path.empty() || !EnsureDirectoryTree(Dir()) ||
        !WriteTextFile(path, on ? L"on\n" : L"off\n")) {
        WriteLog(L"update check setting could not be saved");
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_enabledLoaded = true;
        g_enabled = on;
    }
    WriteLogF(L"update check turned %s", on ? L"on" : L"off");
    return true;
}

void StartCheckAsync() {
    const Channel* channel = ActiveChannel();
    if (!channel) {
        WriteLog(L"update check: this build has no release channel");
        return;
    }
    if (!Enabled()) {
        WriteLog(L"update check: turned off in settings");
        return;
    }

    {
        std::lock_guard<std::mutex> lock(g_mutex);
        LoadLocked();
        const unsigned long long now = NowSeconds();
        if (g_checkedAt != 0 && now >= g_checkedAt && now - g_checkedAt < kRefreshSeconds &&
            FetchedChangelogIsCurrent()) {
            return;
        }
    }
    if (g_fetching.exchange(true)) return;

    std::thread([channel]() {
        const HRESULT hr = RoInitialize(RO_INIT_MULTITHREADED);

        std::wstring version;
        if (FetchLatest(*channel, version)) {
            const unsigned long long now = NowSeconds();
            const std::wstring path = FilePath(L"latest.txt");
            if (path.empty() || !EnsureDirectoryTree(Dir()) ||
                !WriteTextFile(path, version + L"\n" + std::to_wstring(now) + L"\n")) {
                WriteLog(L"update check: the answer could not be saved");
            }
            {
                std::lock_guard<std::mutex> lock(g_mutex);
                g_loaded = true;
                g_latest = version;
                g_checkedAt = now;
            }
            WriteLogF(L"update check: newest %s build is %s, running %s",
                channel->name, version.c_str(), telemetry::LauncherBuild().c_str());
            FetchChangelog(*channel);
        }

        g_fetching.store(false);
        if (SUCCEEDED(hr)) RoUninitialize();
    }).detach();
}

bool NewerAvailable(std::wstring& latest) {
    if (!HasReleaseChannel() || !Enabled()) return false;
    std::lock_guard<std::mutex> lock(g_mutex);
    LoadLocked();
    if (!NewerThanRunning(g_latest)) return false;
    latest = g_latest;
    return true;
}

bool TakeOffer(Offer& out) {
    if (!HasReleaseChannel() || !Enabled()) return false;

    // the menu asks every frame, so the disk is only touched once there is something to offer
    std::lock_guard<std::mutex> lock(g_mutex);
    LoadLocked();
    if (g_offerTaken || !NewerThanRunning(g_latest)) return false;

    std::wstring skipped;
    const std::wstring skippedPath = FilePath(L"skipped.txt");
    if (!skippedPath.empty()) ReadTextFile(skippedPath, skipped);
    if (g_latest == TrimWhitespace(skipped)) {
        g_offerTaken = true;
        return false;
    }

    g_offerTaken = true;
    out.current = telemetry::LauncherBuild();
    out.latest = g_latest;
    return true;
}

void SkipVersion(const std::wstring& version) {
    const std::wstring path = FilePath(L"skipped.txt");
    if (path.empty() || !EnsureDirectoryTree(Dir()) ||
        !WriteTextFile(path, version + L"\n")) {
        WriteLog(L"update check: the skipped version could not be saved");
        return;
    }
    WriteLogF(L"update check: skipping %s", version.c_str());
}

std::wstring ReleasesUrl() {
    return kReleasesPage;
}

std::wstring ChangelogMarkdown(bool* fromGitHub) {
    if (fromGitHub) *fromGitHub = false;

    std::wstring text;
    const std::wstring fetched = FilePath(L"changelog.md");
    if (!fetched.empty() && FetchedChangelogIsCurrent() && ReadTextFile(fetched, text) && !text.empty()) {
        if (fromGitHub) *fromGitHub = true;
        return text;
    }

    const std::wstring dir = GetExecutableDir();
    text.clear();
    if (!dir.empty() && ReadTextFile(dir + L"\\CHANGELOG.md", text) && !text.empty()) return text;
    return std::wstring();
}

void FormatChangelog(const std::wstring& markdown, std::wstring& text, TextRanges& bold, TextRanges& head) {
    text.clear();
    bold.clear();
    head.clear();

    // skip the preamble above the first dated section
    const bool hasSections = markdown.rfind(L"## ", 0) == 0 || markdown.find(L"\n## ") != std::wstring::npos;
    bool started = !hasSections;
    bool lastBlank = true;

    size_t pos = 0;
    while (pos < markdown.size()) {
        size_t eol = markdown.find(L'\n', pos);
        if (eol == std::wstring::npos) eol = markdown.size();
        const std::wstring line = TrimWhitespace(markdown.substr(pos, eol - pos));
        pos = eol + 1;

        if (!started) {
            if (line.rfind(L"## ", 0) == 0) started = true;
            else continue;
        }
        if (line.empty()) {
            if (!lastBlank) {
                text += L'\n';
                lastBlank = true;
            }
            continue;
        }
        if (line.rfind(L"# ", 0) == 0 || line == L"---" || line == L"***") continue;

        const bool major = line.rfind(L"## ", 0) == 0;
        if (major || line.rfind(L"### ", 0) == 0) {
            if (!text.empty() && !lastBlank) text += L'\n';
            const unsigned start = static_cast<unsigned>(text.size());
            AppendInline(line.substr(major ? 3 : 4), text, bold);
            const unsigned length = static_cast<unsigned>(text.size()) - start;
            (major ? head : bold).emplace_back(start, length);
        } else if (line.rfind(L"- ", 0) == 0 || line.rfind(L"* ", 0) == 0) {
            text += L"\u2022 ";
            AppendInline(line.substr(2), text, bold);
        } else {
            AppendInline(line, text, bold);
        }
        text += L'\n';
        lastBlank = false;
    }

    while (!text.empty() && text.back() == L'\n') text.pop_back();
}

}
