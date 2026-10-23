#include "compat_feed.h"

#include "http_client.h"
#include "launcher_common.h"
#include "telemetry.h"

#include <roapi.h>

#include <winrt/base.h>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>

#include <atomic>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

namespace compat {
namespace {

constexpr unsigned kFetchTimeoutMs = 4000;
constexpr unsigned long long kRefreshSeconds = 24ull * 60ull * 60ull;
constexpr uint32_t kMaxFlags = 1000;
constexpr uint32_t kMaxProjectIds = 32;
constexpr size_t kMaxKeyChars = 128;
constexpr size_t kMaxNoteChars = 240;
constexpr wchar_t kAny[] = L"*";

struct Entry {
    std::wstring slug;
    std::wstring modVersion;
    std::wstring mcVersion;
    std::wstring loader;
    std::wstring severity;
    std::wstring note;
    std::vector<std::wstring> projectIds;
};

std::mutex g_mutex;
std::vector<Entry> g_entries;
bool g_loaded = false;
std::atomic<bool> g_fetching{ false };

std::wstring FeedDir() {
    const std::wstring localDir = GetLocalStateDir();
    if (localDir.empty()) return std::wstring();
    return localDir + L"\\telemetry";
}

std::wstring FeedPath() {
    const std::wstring dir = FeedDir();
    return dir.empty() ? dir : dir + L"\\compat.json";
}

std::wstring MetaPath() {
    const std::wstring dir = FeedDir();
    return dir.empty() ? dir : dir + L"\\compat.meta";
}

unsigned long long UnixNow() {
    FILETIME now = {};
    GetSystemTimeAsFileTime(&now);
    ULARGE_INTEGER value;
    value.LowPart = now.dwLowDateTime;
    value.HighPart = now.dwHighDateTime;
    return value.QuadPart / 10000000ull - 11644473600ull;
}

std::wstring MetaValue(const std::wstring& body, const wchar_t* key) {
    const std::wstring needle = std::wstring(key) + L"\t";
    size_t at = body.find(needle);
    if (at == std::wstring::npos) return std::wstring();
    const size_t start = at + needle.size();
    const size_t end = body.find_first_of(L"\r\n", start);
    return TrimWhitespace(body.substr(start, end == std::wstring::npos ? std::wstring::npos : end - start));
}

bool WriteMeta(const std::string& etag, unsigned long long fetched) {
    const std::wstring path = MetaPath();
    if (path.empty()) return false;
    const std::wstring body =
        L"etag\t" + a2w(etag.c_str()) + L"\n" +
        L"fetched\t" + std::to_wstring(fetched) + L"\n";
    return WriteTextFile(path, body);
}

bool ReadMeta(std::string& etag, unsigned long long& fetched) {
    etag.clear();
    fetched = 0;

    const std::wstring path = MetaPath();
    if (path.empty()) return false;

    std::wstring body;
    if (!ReadTextFile(path, body)) return false;

    etag = w2a(MetaValue(body, L"etag"));
    const std::wstring stamp = MetaValue(body, L"fetched");
    if (!stamp.empty()) fetched = _wcstoui64(stamp.c_str(), nullptr, 10);
    return true;
}

std::wstring JsonString(
    const winrt::Windows::Data::Json::JsonObject& object,
    const wchar_t* key,
    const wchar_t* fallback) {
    using namespace winrt::Windows::Data::Json;
    const winrt::hstring name(key);
    if (!object.HasKey(name)) return std::wstring(fallback);
    const auto value = object.GetNamedValue(name);
    if (value.ValueType() != JsonValueType::String) return std::wstring(fallback);
    const std::wstring text = std::wstring(value.GetString());
    return text.empty() ? std::wstring(fallback) : text;
}

bool Parse(const std::wstring& text, std::vector<Entry>& out) {
    using namespace winrt::Windows::Data::Json;
    out.clear();

    try {
        const JsonObject root = JsonObject::Parse(winrt::hstring(text));
        if (!root.HasKey(L"flags")) return false;

        const auto flagsValue = root.GetNamedValue(L"flags");
        if (flagsValue.ValueType() != JsonValueType::Array) return false;

        const JsonArray flags = flagsValue.GetArray();
        if (flags.Size() > kMaxFlags) return false;
        for (uint32_t i = 0; i < flags.Size(); ++i) {
            const auto value = flags.GetAt(i);
            if (value.ValueType() != JsonValueType::Object) continue;
            const JsonObject flag = value.GetObject();

            Entry entry;
            entry.slug = ToLowerW(JsonString(flag, L"slug", L""));
            if (entry.slug.empty() || entry.slug.size() > kMaxKeyChars) continue;

            entry.modVersion = ToLowerW(JsonString(flag, L"modVersion", kAny));
            entry.mcVersion = ToLowerW(JsonString(flag, L"mcVersion", kAny));
            entry.loader = ToLowerW(JsonString(flag, L"loader", kAny));
            entry.severity = ToLowerW(JsonString(flag, L"severity", L"warn"));
            entry.note = JsonString(flag, L"note", L"");
            if (entry.modVersion.size() > kMaxKeyChars ||
                entry.mcVersion.size() > kMaxKeyChars ||
                entry.loader.size() > kMaxKeyChars ||
                entry.severity.size() > kMaxKeyChars) {
                continue;
            }
            if (entry.note.size() > kMaxNoteChars) entry.note.resize(kMaxNoteChars);

            if (flag.HasKey(L"projectIds") &&
                flag.GetNamedValue(L"projectIds").ValueType() == JsonValueType::Array) {
                const JsonArray ids = flag.GetNamedArray(L"projectIds");
                for (uint32_t j = 0; j < ids.Size() && j < kMaxProjectIds; ++j) {
                    if (ids.GetAt(j).ValueType() != JsonValueType::String) continue;
                    const std::wstring id = std::wstring(ids.GetAt(j).GetString());
                    if (!id.empty() && id.size() <= kMaxKeyChars) entry.projectIds.push_back(id);
                }
            }

            out.push_back(entry);
        }
        return true;
    } catch (const winrt::hresult_error&) {
        return false;
    }
}

void LoadLocked() {
    g_loaded = true;
    g_entries.clear();

    const std::wstring path = FeedPath();
    if (path.empty()) return;

    std::wstring text;
    if (!ReadTextFile(path, text)) return;
    if (!Parse(text, g_entries)) {
        WriteLog(L"compat feed cache would not parse, treating it as empty");
    }
}

bool Matches(const std::wstring& field, const std::wstring& actual) {
    if (field == kAny) return true;
    return field == ToLowerW(actual);
}

}

void RefreshAsync() {
    const std::wstring endpoint = telemetry::EndpointUrl();
    if (endpoint.empty()) return;
    if (g_fetching.exchange(true)) return;

    std::thread([endpoint]() {
        const HRESULT hr = RoInitialize(RO_INIT_MULTITHREADED);

        std::string etag;
        unsigned long long fetched = 0;
        const bool haveMeta = ReadMeta(etag, fetched);

        std::wstring existing;
        const bool haveCache = haveMeta && ReadTextFile(FeedPath(), existing);
        const unsigned long long now = UnixNow();

        if (haveCache && now > fetched && (now - fetched) < kRefreshSeconds) {
            g_fetching.store(false);
            if (SUCCEEDED(hr)) RoUninitialize();
            return;
        }

        // an etag without its cache could leave a 304 with nothing to read
        if (!haveCache) etag.clear();

        std::string received;
        const HttpResult result = HttpGetConditionalTimed(
            (endpoint + L"/v1/compat").c_str(), etag, kFetchTimeoutMs, received);

        if (result.status == 304) {
            if (WriteMeta(etag, now)) {
                WriteLog(L"compat feed unchanged");
            } else {
                WriteLog(L"compat feed timestamp could not be saved");
            }
        } else if (result.success() && !result.body.empty()) {
            std::vector<Entry> parsed;
            const std::wstring body = a2w(result.body.c_str());
            const std::wstring dir = FeedDir();
            if (!Parse(body, parsed)) {
                WriteLog(L"compat feed response would not parse");
            } else if (!dir.empty() && EnsureDirectoryTree(dir) && WriteTextFile(FeedPath(), body)) {
                if (!WriteMeta(received, now)) {
                    WriteLog(L"compat feed metadata could not be saved");
                }
                {
                    std::lock_guard<std::mutex> lock(g_mutex);
                    g_entries = std::move(parsed);
                    g_loaded = true;
                    WriteLogF(L"compat feed refreshed with %zu flags", g_entries.size());
                }
            } else {
                WriteLog(L"compat feed cache could not be saved");
            }
        } else {
            WriteLogF(L"compat feed fetch returned %d", result.status);
        }

        g_fetching.store(false);
        if (SUCCEEDED(hr)) RoUninitialize();
    }).detach();
}

Flag Lookup(
    const std::wstring& slug,
    const std::wstring& projectId,
    const std::wstring& mcVersion,
    const std::wstring& loader,
    const std::wstring& modVersion) {
    Flag flag;

    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_loaded) LoadLocked();
    if (g_entries.empty()) return flag;

    const std::wstring wantSlug = ToLowerW(slug);

    for (const Entry& entry : g_entries) {
        if (!Matches(entry.modVersion, modVersion)) continue;
        if (!Matches(entry.mcVersion, mcVersion)) continue;
        if (!Matches(entry.loader, loader)) continue;

        bool hit = !wantSlug.empty() && wantSlug == entry.slug;
        if (!hit && !projectId.empty()) {
            for (const std::wstring& id : entry.projectIds) {
                if (id == projectId) { hit = true; break; }
            }
        }
        if (!hit || (flag.found && flag.versionSpecific && entry.modVersion == kAny)) continue;

        flag.found = true;
        flag.versionSpecific = entry.modVersion != kAny;
        flag.slug = entry.slug;
        flag.severity = entry.severity;
        flag.note = entry.note;
        if (flag.versionSpecific) return flag;
    }

    return flag;
}

}
