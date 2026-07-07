#include "mods_browser.h"

#include "modpack_io.h"
#include "auth_screen.h"
#include "runtime_manager.h"
#include "http_client.h"
#include "launcher_common.h"
#include "launcher_mouse.h"
#include "launcher_ui.h"
#include "mod_types.h"
#include "mods_ui_globals.h"
#include "profiles.h"

#include "third_party/miniz/miniz.h"

#include <winrt/base.h>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.UI.Core.h>
#include <winrt/Windows.UI.Text.Core.h>
#include <winrt/Windows.UI.ViewManagement.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <fstream>
#include <functional>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include <thread>
#include <vector>

using namespace ABI::Windows::UI::Core;

static std::string ModrinthLoaderId(const std::wstring& loader) {
    std::wstring l = loader;
    for (auto& c : l) c = static_cast<wchar_t>(towlower(c));
    if (l == L"neoforge") return "neoforge";
    if (l == L"forge") return "forge";
    if (l == L"quilt") return "quilt";
    if (l == L"vanilla") return "";
    return "fabric";
}

static std::mutex g_modsSearchMutex;
static std::wstring g_modsSearchBuffer;
static int g_modsCaret = 0;
static bool g_modsSearchDirty = false;
static std::atomic<bool> g_modsSearchCapturing{false};
static std::atomic<bool> g_modsEditFocusRemoved{false};
static std::atomic<bool> g_modsSearchSubmit{false};

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

static std::wstring BuildModrinthSearchUrl(const char* index, int limit, int offset, const std::wstring& query, const char* projectType, const std::string& gameVersion, const std::string& loaderId) {
    std::string facets = std::string("[[\"project_type:") + projectType + "\"]";
    if (!loaderId.empty()) facets += ",[\"categories:" + loaderId + "\"]";
    facets += ",[\"versions:" + gameVersion + "\"]";
    facets += ",[\"client_side:required\",\"client_side:optional\"]]";
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

struct ProfileModInstallMeta {
    std::wstring projectId;
    std::wstring title;
    bool dependency = false;
    std::set<std::wstring> requiredBy;
};

static std::wstring ProfileModInstallMetaDirFromModsDir(const std::wstring& userModsDir) {
    return GetParentDir(userModsDir) + L"\\.bandit\\mod-installs";
}

static std::wstring ProfileModInstallMetaPathFromModsDir(const std::wstring& userModsDir, const std::wstring& fileName) {
    return ProfileModInstallMetaDirFromModsDir(userModsDir) + L"\\" + SafeFileName(fileName) + L".meta";
}

static std::set<std::wstring> SplitCommaSet(const std::wstring& value) {
    std::set<std::wstring> out;
    size_t start = 0;
    while (start <= value.size()) {
        const size_t comma = value.find(L',', start);
        std::wstring part = value.substr(start, comma == std::wstring::npos ? std::wstring::npos : comma - start);
        if (!part.empty()) out.insert(part);
        if (comma == std::wstring::npos) break;
        start = comma + 1;
    }
    return out;
}

static std::wstring JoinCommaSet(const std::set<std::wstring>& values) {
    std::wstring out;
    for (const std::wstring& value : values) {
        if (!out.empty()) out += L",";
        out += value;
    }
    return out;
}

static bool ReadProfileModInstallMetaFromModsDir(
    const std::wstring& userModsDir,
    const std::wstring& fileName,
    ProfileModInstallMeta& meta) {
    std::wstring body;
    if (!ReadTextFile(ProfileModInstallMetaPathFromModsDir(userModsDir, fileName), body)) return false;

    meta = {};
    std::wstringstream ss(body);
    std::wstring line;
    bool any = false;
    while (std::getline(ss, line)) {
        if (!line.empty() && line.back() == L'\r') line.pop_back();
        const size_t tab = line.find(L'\t');
        if (tab == std::wstring::npos) continue;
        const std::wstring key = line.substr(0, tab);
        const std::wstring value = line.substr(tab + 1);
        if (key == L"projectId") { meta.projectId = value; any = true; }
        else if (key == L"title") { meta.title = value; any = true; }
        else if (key == L"dependency") { meta.dependency = value == L"1"; any = true; }
        else if (key == L"requiredBy") { meta.requiredBy = SplitCommaSet(value); any = true; }
    }
    return any;
}

static void WriteProfileModInstallMetaFromModsDir(
    const std::wstring& userModsDir,
    const std::wstring& fileName,
    const ProfileModInstallMeta& meta) {
    const std::wstring body =
        L"projectId\t" + StripNewlines(meta.projectId) + L"\n" +
        L"title\t" + StripNewlines(meta.title) + L"\n" +
        L"dependency\t" + std::wstring(meta.dependency ? L"1" : L"0") + L"\n" +
        L"requiredBy\t" + JoinCommaSet(meta.requiredBy) + L"\n";
    WriteTextFile(ProfileModInstallMetaPathFromModsDir(userModsDir, fileName), body);
}

static void RecordProfileModInstall(
    const std::wstring& userModsDir,
    const std::wstring& fileName,
    const std::wstring& projectId,
    const std::wstring& title,
    bool dependency,
    const std::wstring& requiredBy) {
    ProfileModInstallMeta meta;
    ReadProfileModInstallMetaFromModsDir(userModsDir, fileName, meta);
    const bool hadExistingMeta = !meta.projectId.empty() || !meta.title.empty() || meta.dependency || !meta.requiredBy.empty();
    if (!projectId.empty()) meta.projectId = projectId;
    if (!title.empty()) meta.title = title;
    if (!dependency) {
        meta.dependency = false;
        meta.requiredBy.clear();
    } else if (meta.dependency) {
        if (!requiredBy.empty()) meta.requiredBy.insert(requiredBy);
    } else if (!hadExistingMeta) {
        meta.dependency = true;
        if (!requiredBy.empty()) meta.requiredBy.insert(requiredBy);
    }
    if (meta.projectId.empty()) meta.projectId = projectId;
    WriteProfileModInstallMetaFromModsDir(userModsDir, fileName, meta);
}

static int RemoveProfileModAndUnusedDependencies(const std::wstring& runtimeRoot, const std::wstring& profileId, const std::wstring& jarName) {
    const std::wstring modsDir = ProfileModsDir(runtimeRoot, profileId);
    ProfileModInstallMeta removedMeta;
    const bool hasMeta = ReadProfileModInstallMetaFromModsDir(modsDir, jarName, removedMeta);

    int removed = 0;
    if (DeleteFileW((modsDir + L"\\" + jarName).c_str())) {
        ++removed;
    }
    DeleteFileW(ModMetaPath(runtimeRoot, jarName).c_str());
    DeleteFileW(ProfileModInstallMetaPathFromModsDir(modsDir, jarName).c_str());

    if (!hasMeta || removedMeta.projectId.empty()) {
        return removed;
    }

    const std::vector<std::wstring> remaining = ListProfileMods(runtimeRoot, profileId);
    for (const std::wstring& candidate : remaining) {
        ProfileModInstallMeta meta;
        if (!ReadProfileModInstallMetaFromModsDir(modsDir, candidate, meta)) continue;
        if (!meta.dependency) continue;
        const size_t erased = meta.requiredBy.erase(removedMeta.projectId);
        if (erased == 0) continue;

        if (meta.requiredBy.empty()) {
            if (DeleteFileW((modsDir + L"\\" + candidate).c_str())) {
                ++removed;
                WriteLogF(L"Removed unused dependency mod: %s", candidate.c_str());
            }
            DeleteFileW(ModMetaPath(runtimeRoot, candidate).c_str());
            DeleteFileW(ProfileModInstallMetaPathFromModsDir(modsDir, candidate).c_str());
        } else {
            WriteProfileModInstallMetaFromModsDir(modsDir, candidate, meta);
        }
    }

    return removed;
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

static bool FetchModrinthMods(const std::wstring& runtimeRoot, const char* index, const std::wstring& query, int offset, int limit, std::vector<ModCard>& out, int& totalHits, std::wstring& error, const char* projectType, const std::string& gameVersion, const std::string& loaderId) {
    using namespace winrt::Windows::Data::Json;
    error.clear();
    const bool modpack = projectType && std::strcmp(projectType, "modpack") == 0;

    const std::wstring url = BuildModrinthSearchUrl(index, limit, offset, query, projectType, gameVersion, loaderId);
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
                std::wstring loaderName = loaderId.empty() ? std::wstring(L"Vanilla") : a2w(loaderId.c_str());
                if (!loaderName.empty()) loaderName[0] = static_cast<wchar_t>(towupper(loaderName[0]));
                card.description = loaderName + (modpack ? L" modpack for Minecraft " : L" mod for Minecraft ") + a2w(gameVersion.c_str());
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

struct BlockedMod {
    const wchar_t* token;
    const wchar_t* reason;
};

// Mods known to break this UWP runtime. Match on filename token boundaries so
// blocked mods do not also remove dependency libraries with similar names.
static const BlockedMod kBlockedMods[] = {
    { L"crashassistant", L"Crash Assistant starts an external helper and hangs before GLFW startup" },
    { L"crash-assistant", L"Crash Assistant starts an external helper and hangs before GLFW startup" },
    { L"crash_assistant", L"Crash Assistant starts an external helper and hangs before GLFW startup" },
	{ L"flashback", L"Xbox does not have win32, which Flashback requires to run." },
	{ L"ixeris", L"Ixeris replaces GLFW event polling/threading, which conflicts with the UWP GLFW shim" },
	{ L"lambdacontrols", L"LambdaControls conflicts with the bundled Bandit controller compatibility layer" },
    { L"lambda-controls", L"LambdaControls conflicts with the bundled Bandit controller compatibility layer" },
    { L"midnightcontrols", L"MidnightControls conflicts with the bundled Bandit controller compatibility layer" },
    { L"midnight-controls", L"MidnightControls conflicts with the bundled Bandit controller compatibility layer" },
	{ L"puzzle", L"Puzzle applies splash/model mixins and currently hangs before the UWP GLFW shim loads" },
    { L"rrls", L"Remove Reloading Screen failed config file canonical-path checks under LocalState" },
    { L"remove-reloading-screen", L"Remove Reloading Screen failed config file canonical-path checks under LocalState" },
	{ L"xaeros-minimap", L"Exception Access Violation" },
    { L"xaeros-world-map", L"Exception Access Violation" },
};

static bool IsAsciiNameChar(wchar_t ch) {
    return (ch >= L'a' && ch <= L'z') ||
           (ch >= L'0' && ch <= L'9');
}

static bool FilenameHasBlockedToken(const std::wstring& lower, const std::wstring& token) {
    size_t pos = lower.find(token);
    while (pos != std::wstring::npos) {
        const size_t end = pos + token.size();
        const bool leftOk = pos == 0 || !IsAsciiNameChar(lower[pos - 1]);
        const bool rightOk = end >= lower.size() || !IsAsciiNameChar(lower[end]);
        if (leftOk && rightOk) return true;
        pos = lower.find(token, pos + 1);
    }
    return false;
}

static const BlockedMod* FindBlockedModFile(const std::wstring& fileName) {
    const std::wstring lower = ToLowerW(fileName);
    for (const BlockedMod& b : kBlockedMods) {
        if (FilenameHasBlockedToken(lower, b.token)) return &b;
    }
    return nullptr;
}

bool IsBlockedModFileName(const std::wstring& fileName) {
    return FindBlockedModFile(fileName) != nullptr;
}

static std::vector<std::string> SplitConstraintAlternatives(const std::string& constraint) {
    std::vector<std::string> out;
    size_t start = 0;
    for (;;) {
        const size_t pos = constraint.find("||", start);
        std::string part = constraint.substr(start, pos == std::string::npos ? std::string::npos : pos - start);
        while (!part.empty() && isspace(static_cast<unsigned char>(part.front()))) part.erase(part.begin());
        while (!part.empty() && isspace(static_cast<unsigned char>(part.back()))) part.pop_back();
        if (!part.empty()) out.push_back(part);
        if (pos == std::string::npos) break;
        start = pos + 2;
    }
    return out;
}

static bool SatisfiesVersionPredicatePart(const std::string& actual, std::string part) {
    while (!part.empty() && isspace(static_cast<unsigned char>(part.front()))) part.erase(part.begin());
    while (!part.empty() && isspace(static_cast<unsigned char>(part.back()))) part.pop_back();
    if (part.empty() || part == "*") return true;

    std::string op = "=";
    size_t versionStart = 0;
    if (part.rfind(">=", 0) == 0 || part.rfind("<=", 0) == 0 || part.rfind("==", 0) == 0) {
        op = part.substr(0, 2);
        versionStart = 2;
    } else if (part[0] == '>' || part[0] == '<' || part[0] == '=') {
        op = part.substr(0, 1);
        versionStart = 1;
    }

    std::string expected = part.substr(versionStart);
    while (!expected.empty() && isspace(static_cast<unsigned char>(expected.front()))) expected.erase(expected.begin());
    if (expected.empty() || expected == "*") return true;

    const int cmp = CompareVersionNumbers(actual, expected);
    if (op == ">=") return cmp >= 0;
    if (op == "<=") return cmp <= 0;
    if (op == ">") return cmp > 0;
    if (op == "<") return cmp < 0;
    if (op == "=" || op == "==") return cmp == 0;

    return true;
}

static bool SatisfiesVersionPredicate(const std::string& actual, const std::wstring& predicate) {
    std::string constraint = w2a(predicate);
    while (!constraint.empty() && isspace(static_cast<unsigned char>(constraint.front()))) constraint.erase(constraint.begin());
    while (!constraint.empty() && isspace(static_cast<unsigned char>(constraint.back()))) constraint.pop_back();
    if (constraint.empty() || constraint == "*") return true;

    std::vector<std::string> alternatives = SplitConstraintAlternatives(constraint);
    if (alternatives.empty()) alternatives.push_back(constraint);
    for (const std::string& alternative : alternatives) {
        std::stringstream ss(alternative);
        std::string part;
        bool ok = true;
        bool sawPart = false;
        while (ss >> part) {
            sawPart = true;
            if (!SatisfiesVersionPredicatePart(actual, part)) {
                ok = false;
                break;
            }
        }
        if (ok && sawPart) return true;
    }
    return false;
}

static bool ModJarMatchesFabricLoader(const std::wstring& jarPath, const std::string& loaderVersion, std::wstring& reason) {
    using namespace winrt::Windows::Data::Json;
    reason.clear();
    if (loaderVersion.empty()) return true;

    std::wstring fabricModJson;
    if (!ReadZipTextFile(jarPath, "fabric.mod.json", fabricModJson)) {
        return true;
    }

    try {
        JsonObject root = JsonObject::Parse(winrt::hstring(fabricModJson.c_str()));
        if (!root.HasKey(L"depends") || root.GetNamedValue(L"depends").ValueType() != JsonValueType::Object) {
            return true;
        }
        JsonObject depends = root.GetNamedObject(L"depends");
        if (!depends.HasKey(L"fabricloader")) return true;

        const IJsonValue value = depends.GetNamedValue(L"fabricloader");
        std::vector<std::wstring> predicates;
        if (value.ValueType() == JsonValueType::String) {
            predicates.push_back(depends.GetNamedString(L"fabricloader").c_str());
        } else if (value.ValueType() == JsonValueType::Array) {
            JsonArray arr = value.GetArray();
            for (uint32_t i = 0; i < arr.Size(); ++i) {
                if (arr.GetAt(i).ValueType() == JsonValueType::String) {
                    predicates.push_back(arr.GetAt(i).GetString().c_str());
                }
            }
        }

        if (predicates.empty()) return true;
        for (const std::wstring& predicate : predicates) {
            if (SatisfiesVersionPredicate(loaderVersion, predicate)) return true;
        }

        reason = L"requires Fabric Loader " + predicates.front() + L", target has " + a2w(loaderVersion.c_str());
        return false;
    } catch (...) {
        return true;
    }
}

static bool FabricModDependsOn(const std::wstring& jarPath, const wchar_t* modId) {
    using namespace winrt::Windows::Data::Json;
    if (!modId || !*modId) return false;

    std::wstring fabricModJson;
    if (!ReadZipTextFile(jarPath, "fabric.mod.json", fabricModJson)) {
        return false;
    }

    try {
        JsonObject root = JsonObject::Parse(winrt::hstring(fabricModJson.c_str()));
        if (!root.HasKey(L"depends") || root.GetNamedValue(L"depends").ValueType() != JsonValueType::Object) {
            return false;
        }

        JsonObject depends = root.GetNamedObject(L"depends");
        return depends.HasKey(modId);
    } catch (...) {
        return false;
    }
}

int PurgeBlockedModsFromDir(const std::wstring& runtimeRoot, const std::wstring& modsDir) {
    int removed = 0;
    WIN32_FIND_DATAW fd = {};
    HANDLE h = FindFirstFileW((modsDir + L"\\*.jar").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        const BlockedMod* blocked = FindBlockedModFile(fd.cFileName);
        if (!blocked) continue;
        const std::wstring path = modsDir + L"\\" + fd.cFileName;
        WriteLogF(L"Removing blocked mod before launch: %s (%s)", fd.cFileName, blocked->reason);
        if (DeleteFileW(path.c_str())) {
            DeleteFileW(ModMetaPath(runtimeRoot, fd.cFileName).c_str());
            ++removed;
        } else {
            WriteLogF(L"Failed to remove blocked mod: %s err=%u", path.c_str(), GetLastError());
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return removed;
}

static bool InstallModrinthProjectRecursive(
    const std::wstring& projectIdOrSlug,
    const std::wstring& runtimeRoot,
    const std::wstring& userModsDir,
    std::set<std::wstring>& visited,
    std::vector<std::wstring>& installed,
    const ModCard* topMeta,
    std::wstring& error,
    const std::string& gameVersion,
    const std::string& loaderId,
    const std::string& loaderVersion,
    const std::wstring& rootProjectId,
    bool installingDependency) {
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
    const std::string versions = FormUrlEncode("[\"" + gameVersion + "\"]");
    const std::wstring url =
        L"https://api.modrinth.com/v2/project/" + a2w(FormUrlEncode(project).c_str()) +
        L"/version?" +
        (loaderId.empty() ? std::wstring() : (L"loaders=" + a2w(FormUrlEncode("[\"" + loaderId + "\"]").c_str()) + L"&")) +
        L"game_versions=" + a2w(versions.c_str()) +
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
            error = L"No " + (loaderId.empty() ? std::wstring(L"compatible") : a2w(loaderId.c_str())) + L" " + a2w(gameVersion.c_str()) + L" version was found";
            return false;
        }

        std::vector<JsonObject> candidates;
        for (uint32_t i = 0; i < versionsArray.Size(); ++i) {
            auto value = versionsArray.GetAt(i);
            if (value.ValueType() != JsonValueType::Object) continue;
            JsonObject candidate = value.GetObject();
            if (JsonStringOrEmpty(candidate, L"version_type") == L"release") {
                candidates.push_back(candidate);
            }
        }
        for (uint32_t i = 0; i < versionsArray.Size(); ++i) {
            auto value = versionsArray.GetAt(i);
            if (value.ValueType() != JsonValueType::Object) continue;
            JsonObject candidate = value.GetObject();
            if (JsonStringOrEmpty(candidate, L"version_type") != L"release") {
                candidates.push_back(candidate);
            }
        }
        if (candidates.empty()) {
            error = L"No installable Modrinth version was found";
            return false;
        }

        EnsureDirectoryTree(userModsDir);
        std::wstring lastSkipReason;
        for (JsonObject version : candidates) {
            std::wstring downloadUrl;
            std::wstring filename;
            std::string sha1;
            unsigned long long fileSize = 0;
            if (!ExtractPrimaryModrinthFile(version, downloadUrl, filename, sha1, fileSize)) {
                lastSkipReason = L"version without downloadable file";
                continue;
            }
            if (const BlockedMod* blocked = FindBlockedModFile(filename)) {
                error = L"Blocked incompatible mod: " + filename;
                WriteLogF(L"%s (%s)", error.c_str(), blocked->reason);
                return false;
            }

            const std::wstring destination = userModsDir + L"\\" + SafeFileName(filename);
            const std::wstring installedFileName = SafeFileName(filename);
            const std::wstring actualProjectId = JsonStringOrEmpty(version, L"project_id").empty()
                ? projectIdOrSlug
                : JsonStringOrEmpty(version, L"project_id");

            if (FileMatchesSha1(destination, sha1)) {
                std::wstring reason;
                if (ModJarMatchesFabricLoader(destination, loaderVersion, reason)) {
                    WriteLogF(L"Mod already installed: %s", destination.c_str());
                    RecordProfileModInstall(userModsDir, installedFileName, actualProjectId,
                        topMeta ? topMeta->title : projectIdOrSlug,
                        installingDependency, rootProjectId);
                    return true;
                }
                WriteLogF(L"Installed Modrinth file is not compatible with target loader: %s (%s)",
                    destination.c_str(), reason.c_str());
            }

            const std::wstring tempPath = destination + L".download";
            DeleteFileW(tempPath.c_str());
            WriteLogF(L"Downloading Modrinth mod candidate %s", filename.c_str());
            SetInstallStatus(L"Checking " + filename);
            if (!DownloadUrlToFile(downloadUrl, tempPath, MakeInstallProgress(L"Checking " + filename, fileSize))) {
                DeleteFileW(tempPath.c_str());
                error = L"Mod download failed: " + filename;
                return false;
            }

            if (!FileMatchesSha1(tempPath, sha1)) {
                DeleteFileW(tempPath.c_str());
                error = L"Mod verification failed: " + filename;
                return false;
            }

            std::wstring incompatReason;
            if (!ModJarMatchesFabricLoader(tempPath, loaderVersion, incompatReason)) {
                DeleteFileW(tempPath.c_str());
                lastSkipReason = filename + L" " + incompatReason;
                WriteLogF(L"Skipping Modrinth file for target loader %s: %s",
                    a2w(loaderVersion.c_str()).c_str(), lastSkipReason.c_str());
                continue;
            }

            if (loaderId == "fabric" &&
                ToLowerW(projectIdOrSlug) != L"p7dr8msh" &&
                FabricModDependsOn(tempPath, L"fabric")) {
                WriteLogF(L"Mod %s requires Fabric API; ensuring Fabric API dependency", filename.c_str());
                if (!InstallModrinthProjectRecursive(L"P7dR8mSH", runtimeRoot, userModsDir, visited, installed, nullptr, error, gameVersion, loaderId, loaderVersion, rootProjectId, true)) {
                    DeleteFileW(tempPath.c_str());
                    return false;
                }
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
                        !InstallModrinthProjectRecursive(depProject, runtimeRoot, userModsDir, visited, installed, nullptr, error, gameVersion, loaderId, loaderVersion, rootProjectId, true)) {
                        DeleteFileW(tempPath.c_str());
                        return false;
                    }
                }
            }

            DeleteFileW(destination.c_str());
            if (!MoveFileExW(tempPath.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING)) {
                DeleteFileW(tempPath.c_str());
                error = L"Could not install mod: " + filename;
                WriteLogF(L"MoveFileEx failed for mod %s err=%u", destination.c_str(), GetLastError());
                return false;
            }

            installed.push_back(installedFileName);
            if (topMeta) {
                ModCard meta = *topMeta;
                WriteModMeta(runtimeRoot, installedFileName, meta);
            }
            RecordProfileModInstall(userModsDir, installedFileName, actualProjectId,
                topMeta ? topMeta->title : projectIdOrSlug,
                installingDependency, rootProjectId);
            WriteLogF(L"Installed Modrinth mod %s", destination.c_str());
            return true;
        }

        error = L"No Modrinth file matched " + a2w(gameVersion.c_str()) + L" / " +
            a2w(loaderId.c_str()) + L" " + a2w(loaderVersion.c_str());
        if (!lastSkipReason.empty()) error += L" (" + lastSkipReason + L")";
        return false;
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
    std::wstring& error,
    const std::string& gameVersion,
    const std::string& loaderId,
    const std::string& loaderVersion) {
    std::set<std::wstring> visited;
    const std::wstring id = !card.projectId.empty() ? card.projectId : card.slug;
    return InstallModrinthProjectRecursive(id, runtimeRoot, userModsDir, visited, installed, &card, error, gameVersion, loaderId, loaderVersion, id, false);
}


static bool ResolveModpackMrpack(const std::wstring& idOrSlug, std::wstring& url, std::wstring& filename, std::string& sha1, unsigned long long& size, std::wstring& error, const std::string& gameVersion, const std::string& loaderId) {
    using namespace winrt::Windows::Data::Json;
    const std::string project = w2a(idOrSlug);
    const std::string versions = FormUrlEncode("[\"" + gameVersion + "\"]");
    const std::wstring vurl =
        L"https://api.modrinth.com/v2/project/" + a2w(FormUrlEncode(project).c_str()) +
        L"/version?" +
        (loaderId.empty() ? std::wstring() : (L"loaders=" + a2w(FormUrlEncode("[\"" + loaderId + "\"]").c_str()) + L"&")) +
        L"game_versions=" + a2w(versions.c_str()) +
        L"&include_changelog=false";
    const HttpResult response = HttpGetString(vurl.c_str());
    if (!response.success()) {
        error = L"Modpack version lookup failed HTTP " + std::to_wstring(response.status);
        return false;
    }
    try {
        JsonArray arr = JsonArray::Parse(winrt::to_hstring(response.body));
        if (arr.Size() == 0) {
            error = L"No " + (loaderId.empty() ? std::wstring(L"compatible") : a2w(loaderId.c_str())) + L" " + a2w(gameVersion.c_str()) + L" build of this pack";
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

static bool InstallModpack(const ModCard& card, const std::wstring& runtimeRoot, const std::wstring& profileId, std::wstring& error, const std::string& gameVersion, const std::string& loaderId) {
    const std::wstring idOrSlug = !card.projectId.empty() ? card.projectId : card.slug;

    SetInstallStatus(L"Resolving " + card.title + L"...");
    std::wstring mrUrl, mrName;
    std::string mrSha1;
    unsigned long long mrSize = 0;
    if (!ResolveModpackMrpack(idOrSlug, mrUrl, mrName, mrSha1, mrSize, error, gameVersion, loaderId)) return false;

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

    SetInstallStatus(L"Installing " + card.title + L"...");
    const bool ok = InstallModpackFromFile(mrPath, runtimeRoot, profileId, error);
    DeleteFileW(mrPath.c_str());
    return ok;
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

static void StartInstallJob(const ModCard& card, const std::wstring& runtimeRoot, const LaunchTarget& target) {
    if (g_installRunning.load()) return;
    g_installRunning.store(true);
    g_installResultReady.store(false);
    g_installTotal.store(0);
    g_installDone.store(0);
    SetInstallStatus(L"Starting " + card.title + L"...");
    ModCard copy = card;
    std::wstring rootCopy = runtimeRoot;
    LaunchTarget targetCopy = target;
    std::thread([copy, rootCopy, targetCopy]() {
        const std::string gameVersion = w2a(targetCopy.minecraftVersion);
        const std::string loaderId = ModrinthLoaderId(targetCopy.loader);
        const std::string loaderVersion = w2a(targetCopy.loaderVersion);
        std::wstring err;
        bool ok;
        if (copy.isModpack) {
            const std::wstring pid = CreateProfile(rootCopy, copy.title, targetCopy);
            WriteLogF(L"Installing modpack '%s' target=%s into profile %s", copy.title.c_str(), targetCopy.targetId.c_str(), pid.c_str());
            ok = InstallModpack(copy, rootCopy, pid, err, gameVersion, loaderId);
            if (ok) {
                SetActiveProfileId(rootCopy, pid);
                SetInstallStatus(L"Installed profile " + copy.title);
            } else {
                DeleteProfilePermanent(rootCopy, pid);
                SetInstallStatus(err.empty() ? L"Modpack install failed" : err);
            }
        } else {
            const std::wstring active = GetActiveProfileId(rootCopy);
            if (active == kVanillaProfileId) {
                ok = false;
                SetInstallStatus(L"Vanilla is read-only. Pick or make a profile first.");
            } else {
                const LaunchTarget activeTarget = ResolveProfileTarget(rootCopy, GetProfileById(rootCopy, active));
                if (activeTarget.targetId != targetCopy.targetId) {
                    ok = false;
                    WriteLogF(L"Install blocked: browse target %s != active profile target %s",
                        targetCopy.targetId.c_str(), activeTarget.targetId.c_str());
                    SetInstallStatus(L"Active profile is " + TargetProfileText(activeTarget) +
                        L". Select or create a " + TargetShortText(targetCopy) + L" profile to install these.");
                } else {
                    std::vector<std::wstring> installed;
                    WriteLogF(L"Installing mod '%s' target=%s into profile %s", copy.title.c_str(), targetCopy.targetId.c_str(), active.c_str());
                    ok = InstallModrinthProject(copy, rootCopy, ProfileModsDir(rootCopy, active), installed, err, gameVersion, loaderId, loaderVersion);
                    SetInstallStatus(ok
                        ? (installed.empty() ? L"Already installed" : L"Installed " + std::to_wstring(installed.size()) + L" file(s)")
                        : (err.empty() ? L"Install failed" : err));
                }
            }
        }
        g_installResultOk.store(ok);
        g_installRunning.store(false);
        g_installResultReady.store(true);
    }).detach();
}

static std::vector<std::wstring> RecommendedSlugsForTarget(const LaunchTarget& target) {
    const std::string version = w2a(target.minecraftVersion);
    std::wstring loader = target.loader;
    std::transform(loader.begin(), loader.end(), loader.begin(),
        [](wchar_t c) { return static_cast<wchar_t>(towlower(c)); });
    if (loader == L"neoforge") {
        if (CompareVersionNumbers(version, "1.21.1") >= 0) {
            return {
                L"sodium",
                L"jei"
            };
        }
        return {};
    }

    if (loader != L"fabric") {
        return {};
    }

    if (CompareVersionNumbers(version, "1.20.1") >= 0) {
        std::vector<std::wstring> slugs = {
            L"sodium",
            L"modernfix",
            L"ferrite-core",
            L"c2me-fabric",
            L"scalablelux",
            L"asyncparticles",
            L"mcwifipnp",
            L"fpsdisplay",
            L"modmenu"
        };
        return slugs;
    }

    return {
        L"sodium",
        L"modernfix",
        L"ferrite-core",
        L"lithium",
        L"starlight",
        L"krypton",
        L"lambdynamiclights",
        L"fpsdisplay",
        L"modmenu"
    };
}

static bool ModrinthProjectHasTargetVersion(const std::wstring& projectIdOrSlug, const std::string& gameVersion, const std::string& loaderId) {
    using namespace winrt::Windows::Data::Json;
    if (projectIdOrSlug.empty() || gameVersion.empty()) return false;

    const std::string versions = FormUrlEncode("[\"" + gameVersion + "\"]");
    const std::wstring url =
        L"https://api.modrinth.com/v2/project/" + a2w(FormUrlEncode(w2a(projectIdOrSlug)).c_str()) +
        L"/version?" +
        (loaderId.empty() ? std::wstring() : (L"loaders=" + a2w(FormUrlEncode("[\"" + loaderId + "\"]").c_str()) + L"&")) +
        L"game_versions=" + a2w(versions.c_str());

    const HttpResult response = HttpGetString(url.c_str());
    if (!response.success()) {
        WriteLogF(L"Recommended version check failed HTTP %d project=%s url=%s",
            response.status,
            projectIdOrSlug.c_str(),
            url.c_str());
        return false;
    }

    try {
        return JsonArray::Parse(winrt::to_hstring(response.body)).Size() > 0;
    } catch (...) {
        WriteLogF(L"Recommended version check parse failed project=%s", projectIdOrSlug.c_str());
        return false;
    }
}

static bool FetchRecommendedMods(const std::wstring& runtimeRoot, const LaunchTarget& target, std::vector<ModCard>& out, std::wstring& error) {
    using namespace winrt::Windows::Data::Json;
    error.clear();

    const std::vector<std::wstring> recommendedSlugs = RecommendedSlugsForTarget(target);
    if (recommendedSlugs.empty()) {
        return true;
    }

    std::string ids = "[";
    bool first = true;
    for (const std::wstring& slug : recommendedSlugs) {
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

    const std::string gameVersion = w2a(target.minecraftVersion);
    const std::string loaderId = ModrinthLoaderId(target.loader);
    for (const std::wstring& slug : recommendedSlugs) {
        std::wstring key = slug;
        std::transform(key.begin(), key.end(), key.begin(),
            [](wchar_t c) { return static_cast<wchar_t>(towlower(c)); });
        auto it = bySlug.find(key);
        if (it == bySlug.end()) continue;

        const std::wstring projectKey = it->second.projectId.empty() ? it->second.slug : it->second.projectId;
        if (!ModrinthProjectHasTargetVersion(projectKey, gameVersion, loaderId)) {
            WriteLogF(L"Recommended mod hidden for target=%s project=%s slug=%s",
                target.targetId.c_str(),
                projectKey.c_str(),
                it->second.slug.c_str());
            continue;
        }
        out.push_back(it->second);
    }
    return true;
}

static int ModsTargetIndex(const AuthUiState& state) {
    for (size_t i = 0; i < state.modsTargets.size(); ++i) {
        if (state.modsTargets[i].targetId == state.modsBrowseTargetId) return static_cast<int>(i);
    }
    return -1;
}

LaunchTarget CurrentModsTarget(const AuthUiState& state) {
    const int idx = ModsTargetIndex(state);
    if (idx >= 0 && idx < static_cast<int>(state.modsTargets.size())) return state.modsTargets[static_cast<size_t>(idx)];
    return DefaultLaunchTarget();
}

static void EnsureModsTargetState(AuthUiState& state, const std::wstring& runtimeRoot) {
    if (state.modsTargets.empty()) {
        state.modsTargets = LoadVersionCatalog(runtimeRoot);
    }
    if (state.modsBrowseTargetId.empty()) {
        const Profile active = GetProfileById(runtimeRoot, GetActiveProfileId(runtimeRoot));
        state.modsBrowseTargetId = ResolveProfileTarget(runtimeRoot, active).targetId;
    }
    const int idx = ModsTargetIndex(state);
    if (idx < 0 || idx >= static_cast<int>(state.modsTargets.size())) {
        state.modsBrowseTargetId = state.modsTargets.empty() ? DefaultLaunchTarget().targetId : state.modsTargets.front().targetId;
    }
}

static void SetModsTargetFromProfile(AuthUiState& state, const std::wstring& runtimeRoot, const std::wstring& profileId) {
    EnsureModsTargetState(state, runtimeRoot);
    const Profile profile = GetProfileById(runtimeRoot, profileId);
    state.modsBrowseTargetId = ResolveProfileTarget(runtimeRoot, profile).targetId;
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
    EnsureModsTargetState(state, runtimeRoot);

    const std::wstring query = state.modsSearchQuery;

    if (state.selectedModsTab == 0) {
        EnsureProfilesInitialized(runtimeRoot);
        const std::wstring active = GetActiveProfileId(runtimeRoot);
        const std::wstring deletedBackup = LatestProfileBackup(runtimeRoot, L"deleted");
        if (!deletedBackup.empty()) {
            ModCard undo;
            undo.projectId = L"__restore_deleted__";
            undo.title = L"Undo deleted profile";
            undo.description = ProfileBackupDisplayName(deletedBackup) + L" - restore the last deleted profile";
            undo.status = L"A restore";
            state.modsCards.push_back(undo);
        }
        const std::wstring manualBackup = LatestProfileBackup(runtimeRoot, L"manual");
        if (!manualBackup.empty()) {
            ModCard restore;
            restore.projectId = L"__restore_backup__";
            restore.title = L"Restore profile backup";
            restore.description = ProfileBackupDisplayName(manualBackup) + L" - restore the latest manual backup";
            restore.status = L"A restore copy";
            state.modsCards.push_back(restore);
        }
        {
            ModCard add;
            add.projectId = L"__new__";
            add.title = L"+ New profile";
            add.description = L"Create " + TargetProfileText(CurrentModsTarget(state)) + L" profile";
            state.modsCards.push_back(add);
        }
        for (const Profile& p : LoadProfiles(runtimeRoot)) {
            MigrateLegacyProfileModsForProfile(runtimeRoot, p.id);
            ModCard c;
            c.projectId = L"__profile__";
            c.filePath = p.id;
            c.title = p.name;
            const bool isActive = (p.id == active);
            c.installed = isActive;
            const std::wstring targetText = TargetProfileText(ResolveProfileTarget(runtimeRoot, p));
            if (p.builtin) {
                c.description = targetText + L" - Pure vanilla, no mods";
            } else {
                const int n = ProfileModCount(runtimeRoot, p.id);
                c.description = targetText + L" - " + std::to_wstring(n) + (n == 1 ? L" mod" : L" mods");
            }
            c.status = isActive ? L"\u25CF Playing this" : L"";
            state.modsCards.push_back(c);
        }
        state.status = L"A open or restore  -  X delete profile  -  installs go to the active profile";
        return;
    }

    if (state.selectedModsTab == 3) {
        std::vector<ModCard> all;
        std::wstring error;
        const LaunchTarget modsTarget = CurrentModsTarget(state);
        if (!FetchRecommendedMods(runtimeRoot, modsTarget, all, error)) {
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
    const LaunchTarget modsTarget = CurrentModsTarget(state);
    const std::string gameVersion = w2a(modsTarget.minecraftVersion);
    const std::string loaderId = ModrinthLoaderId(modsTarget.loader);
    std::wstring error;
    int total = 0;
    if (!FetchModrinthMods(runtimeRoot, index, query, 0, kModPageSize, state.modsCards, total, error, projectType, gameVersion, loaderId)) {
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

void ShowModsPage(
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
    state.modsTargetOpen = false;
    state.modsSearchEditing = false;
    state.modsDetailOpen = false;
    state.modsDetailScroll = 0;
    state.modsSearchQuery.clear();
    state.status = L"Loading installed mods";
    state.modsTargets = LoadVersionCatalog(runtimeRoot);
    SetModsTargetFromProfile(state, runtimeRoot, GetActiveProfileId(runtimeRoot));
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
    float lastMouseX = -100000.0f;
    float lastMouseY = -100000.0f;

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

    auto applyTargetIndex = [&](int idx) {
        EnsureModsTargetState(state, runtimeRoot);
        if (state.modsTargets.empty()) return;
        const int total = static_cast<int>(state.modsTargets.size());
        idx = (idx % total + total) % total;
        state.modsBrowseTargetId = state.modsTargets[static_cast<size_t>(idx)].targetId;
        LoadModsTab(state, runtimeRoot, userModsDir);
        state.modsFocus = 3;
        state.selectedModIndex = 0;
        state.modsScrollRow = 0;
        loadedQuery = state.modsSearchQuery;
        state.status = L"New profiles will use " + TargetProfileText(CurrentModsTarget(state));
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

        const LaunchTarget moreTarget = CurrentModsTarget(state);
        const std::string moreGameVersion = w2a(moreTarget.minecraftVersion);
        const std::string moreLoaderId = ModrinthLoaderId(moreTarget.loader);
        int total = state.modsTotalHits;
        std::wstring error;
        if (!FetchModrinthMods(runtimeRoot, index, state.modsSearchQuery, before, kModPageSize, state.modsCards, total, error, projectType, moreGameVersion, moreLoaderId)) {
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
    int lastEnsuredSel = -1;
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

        if (backDown && !backWasDown && !state.modsDetailOpen && !state.modsProfileOpen && !state.modsTargetOpen && !g_installRunning.load()) {
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
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
            continue;
        }

        if (g_installRunning.load()) {
            const std::wstring s = GetInstallStatus();
            state.status = s.empty() ? L"Installing..." : s;
            state.isError = false;
            upWasDown = upDown; downWasDown = downDown; leftWasDown = leftDown;
            rightWasDown = rightDown; selectWasDown = selectDown; enterWasDown = enterDown;
            backWasDown = backDown; pageUpWasDown = pageUpDown; pageDownWasDown = pageDownDown; xWasDown = xDown; yWasDown = yDown;
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
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

        bool clickActivate = false;
        float wheelDelta = 0.0f;
        state.modsHoverTab = -1;
        {
            LauncherMouse& launcherMouse = LauncherMouseInstance();
            if (launcherMouse.Visible()) {
                const float mx = launcherMouse.X();
                const float my = launcherMouse.Y();
                const int hid = renderer->HitTest(mx, my);
                const bool firstObservation = lastMouseX < -90000.0f;
                const bool moved = !firstObservation &&
                    (mx - lastMouseX > 0.5f || lastMouseX - mx > 0.5f ||
                     my - lastMouseY > 0.5f || lastMouseY - my > 0.5f);
                const bool clicked = launcherMouse.TakeClick();
                wheelDelta = launcherMouse.TakeWheel();
                lastMouseX = mx;
                lastMouseY = my;
                const bool apply = moved || clicked;

                if (state.modsProfileOpen) {
                    int profileFocus = -1;
                    if (hid == launchhit::kProfilePlay) profileFocus = 0;
                    else if (hid == launchhit::kProfileDelete) profileFocus = 1;
                    else if (hid == launchhit::kProfileBackup) profileFocus = 3;
                    else if (hid == launchhit::kProfileExport) profileFocus = 4;
                    else if (hid == launchhit::kProfileController) profileFocus = 5;
                    if (profileFocus >= 0) {
                        if (apply) state.modsProfileFocus = profileFocus;
                        if (clicked) clickActivate = true;
                    } else if (hid >= launchhit::kProfileGridBase) {
                        const int gridIndex = hid - launchhit::kProfileGridBase;
                        if (apply && gridIndex >= 0 && gridIndex < static_cast<int>(state.modsProfileMods.size())) {
                            state.modsProfileFocus = 2;
                            state.modsProfileSel = gridIndex;
                        }
                    } else if (hid == launchhit::kBack && clicked) {
                        state.modsProfileOpen = false;
                        state.status.clear();
                    }
                } else if (state.modsDetailOpen) {
                    if (hid == launchhit::kDetailInstall && clicked) {
                        clickActivate = true;
                    } else if (hid == launchhit::kBack && clicked) {
                        state.modsDetailOpen = false;
                        state.status.clear();
                    }
                } else if (state.modsTargetOpen) {
                    if (hid >= launchhit::kTargetItemBase && hid < launchhit::kProfileGridBase) {
                        const int targetIndex = hid - launchhit::kTargetItemBase;
                        if (apply && targetIndex >= 0 && targetIndex < static_cast<int>(state.modsTargets.size())) {
                            state.modsTargetSel = targetIndex;
                        }
                        if (clicked) clickActivate = true;
                    } else if (hid == launchhit::kTarget && clicked) {
                        state.modsTargetOpen = false;
                    }
                } else {
                    if (hid == launchhit::kBack && clicked) {
                        WriteLog(L"Mods page closed");
                        EndModsSearchCapture();
                        StopIconWorker();
                        state.showModsPage = false;
                        state.showMainMenu = true;
                        state.modsCards.clear();
                        state.isError = false;
                        return;
                    }
                    if (hid >= launchhit::kTabBase && hid < launchhit::kTabBase + 5) {
                        const int tabIndex = hid - launchhit::kTabBase;
                        state.modsHoverTab = tabIndex;
                        if (apply) state.modsFocus = 0;
                        if (clicked) {
                            if (tabIndex != state.selectedModsTab) {
                                state.selectedModsTab = tabIndex;
                                state.modsTargetOpen = false;
                                LoadModsTab(state, runtimeRoot, userModsDir);
                                loadedQuery = state.modsSearchQuery;
                                state.selectedModIndex = 0;
                                state.modsScrollRow = 0;
                            }
                            state.modsFocus = 0;
                        }
                    } else if (hid == launchhit::kTarget) {
                        if (apply) state.modsFocus = 3;
                        if (clicked) clickActivate = true;
                    } else if (hid == launchhit::kSearch) {
                        if (apply) state.modsFocus = 1;
                        if (clicked) clickActivate = true;
                    } else if (hid >= launchhit::kCardBase && hid < launchhit::kTargetItemBase) {
                        const int cardIndex = hid - launchhit::kCardBase;
                        if (apply && cardIndex >= 0 && cardIndex < count) {
                            state.modsFocus = 2;
                            state.selectedModIndex = cardIndex;
                            ensureSelectionVisible();
                        }
                        if (clicked) clickActivate = true;
                    }
                }
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
                if (!state.modsProfileBuiltin) {
                    if (state.modsProfileFocus == 0) state.modsProfileFocus = 5;
                    else if (state.modsProfileFocus == 5) state.modsProfileFocus = 4;
                    else if (state.modsProfileFocus == 4) state.modsProfileFocus = 3;
                    else if (state.modsProfileFocus == 3) state.modsProfileFocus = 1;
                    else state.modsProfileFocus = 1;
                }
            } else if (!gridFocus && rightDown && !rightWasDown) {
                if (!state.modsProfileBuiltin) {
                    if (state.modsProfileFocus == 1) state.modsProfileFocus = 3;
                    else if (state.modsProfileFocus == 3) state.modsProfileFocus = 4;
                    else if (state.modsProfileFocus == 4) state.modsProfileFocus = 5;
                    else if (state.modsProfileFocus == 5) state.modsProfileFocus = 0;
                    else state.modsProfileFocus = 0;
                } else {
                    state.modsProfileFocus = 0;
                }
            } else if (!gridFocus && (downDown && !downWasDown)) {
                if (pmTotal > 0) {
                    state.modsProfileFocus = 2;
                    state.modsProfileSel = 0;
                    state.modsProfileScroll = 0;
                }
            } else if (gridFocus && (upDown && !upWasDown)) {
                if (state.modsProfileSel < 2) {
                    state.modsProfileFocus = state.modsProfileBuiltin ? 0 : 5;
                }
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
                    const int removed = RemoveProfileModAndUnusedDependencies(runtimeRoot, state.modsProfileId, jar);
                    state.modsProfileMods = ListProfileMods(runtimeRoot, state.modsProfileId);
                    const int newTotal = static_cast<int>(state.modsProfileMods.size());
                    if (state.modsProfileSel >= newTotal) state.modsProfileSel = (std::max)(0, newTotal - 1);
                    if (newTotal == 0) state.modsProfileFocus = 0;
                    pmEnsureVisible();
                    state.status = removed > 1
                        ? L"Removed " + jar + L" and " + std::to_wstring(removed - 1) + L" unused dependenc" + (removed == 2 ? L"y" : L"ies")
                        : L"Removed " + jar;
                } else if (!state.modsProfileBuiltin) {
                    const std::wstring gone = state.modsProfileName;
                    DeleteProfile(runtimeRoot, state.modsProfileId);
                    state.modsProfileOpen = false;
                    LoadModsTab(state, runtimeRoot, userModsDir);
                    state.status = L"Deleted " + gone + L". Use Undo deleted profile to restore it.";
                }
            } else if ((selectDown && !selectWasDown) || (enterDown && !enterWasDown) || clickActivate) {
                if (state.modsProfileFocus == 1 && !state.modsProfileBuiltin) {
                    const std::wstring gone = state.modsProfileName;
                    DeleteProfile(runtimeRoot, state.modsProfileId);
                    state.modsProfileOpen = false;
                    LoadModsTab(state, runtimeRoot, userModsDir);
                    state.status = L"Deleted " + gone + L". Use Undo deleted profile to restore it.";
                } else if (state.modsProfileFocus == 3 && !state.modsProfileBuiltin) {
                    std::wstring backupDir;
                    if (BackupProfile(runtimeRoot, state.modsProfileId, backupDir)) {
                        LoadModsTab(state, runtimeRoot, userModsDir);
                        state.status = L"Backed up " + state.modsProfileName;
                        state.isError = false;
                    } else {
                        state.status = L"Could not back up " + state.modsProfileName;
                        state.isError = true;
                    }
                } else if (state.modsProfileFocus == 4 && !state.modsProfileBuiltin) {
                    std::wstring exportError;
                    const std::wstring exportPath = DefaultProfileExportPath(runtimeRoot, state.modsProfileId);
                    if (ExportProfileMrpack(runtimeRoot, state.modsProfileId, exportPath, exportError)) {
                        state.status = L"Exported " + state.modsProfileName + L". Download the .mrpack from Remote Files on your PC.";
                        state.isError = false;
                    } else {
                        state.status = exportError.empty() ? L"Could not export profile pack" : exportError;
                        state.isError = true;
                    }
                } else if (state.modsProfileFocus == 5 && !state.modsProfileBuiltin) {
                    state.modsProfileControllerModEnabled = !state.modsProfileControllerModEnabled;
                    SetProfileControllerModEnabled(runtimeRoot, state.modsProfileId, state.modsProfileControllerModEnabled);
                    if (state.modsProfileControllerModEnabled) {
                        const int removed = DeleteControlifyModsFromDir(ProfileModsDir(runtimeRoot, state.modsProfileId));
                        state.modsProfileMods = ListProfileMods(runtimeRoot, state.modsProfileId);
                        state.status = removed > 0
                            ? L"Bandit controller mod enabled and Controlify removed"
                            : L"Bandit controller mod will be forced on launch";
                    } else {
                        const int removed = DeleteBanditControllerModsFromDir(ProfileModsDir(runtimeRoot, state.modsProfileId));
                        state.modsProfileMods = ListProfileMods(runtimeRoot, state.modsProfileId);
                        state.status = removed > 0
                            ? L"Bandit controller mod disabled and removed"
                            : L"Bandit controller mod disabled";
                    }
                    state.isError = false;
                } else if (state.modsProfileFocus == 0) {
                    SetActiveProfileId(runtimeRoot, state.modsProfileId);
                    state.activeProfileId = state.modsProfileId;
                    state.activeProfileName = state.modsProfileName;
                    SetModsTargetFromProfile(state, runtimeRoot, state.modsProfileId);
                    state.status = L"Play will use " + state.modsProfileName;
                }
            }
            if (wheelDelta != 0.0f && pmTotal > 0) {
                const int wheelNotches = static_cast<int>(wheelDelta > 0.0f ? (wheelDelta + 0.5f) : (wheelDelta - 0.5f));
                if (wheelNotches != 0) {
                    const int profileTotalRows = (pmTotal + 1) / 2;
                    const int maxProfileScroll = (std::max)(0, profileTotalRows - pmRows);
                    int nextScroll = state.modsProfileScroll - wheelNotches;
                    if (nextScroll < 0) nextScroll = 0;
                    if (nextScroll > maxProfileScroll) nextScroll = maxProfileScroll;
                    state.modsProfileScroll = nextScroll;
                }
            }
            upWasDown = upDown; downWasDown = downDown; leftWasDown = leftDown;
            rightWasDown = rightDown; selectWasDown = selectDown; enterWasDown = enterDown;
            backWasDown = backDown; pageUpWasDown = pageUpDown; pageDownWasDown = pageDownDown; xWasDown = xDown; yWasDown = yDown;
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
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
            } else if ((selectDown && !selectWasDown) || (enterDown && !enterWasDown) || clickActivate) {
                StartInstallJob(state.modsDetailCard, runtimeRoot, CurrentModsTarget(state));
            } else if (upDown && !upWasDown) {
                state.modsDetailScroll = (std::max)(0, state.modsDetailScroll - 2);
            } else if (downDown && !downWasDown) {
                state.modsDetailScroll = (std::min)(g_detailMaxScroll.load(), state.modsDetailScroll + 2);
            } else if (pageDownDown && !pageDownWasDown) {
                state.modsDetailScroll = (std::min)(g_detailMaxScroll.load(), state.modsDetailScroll + 8);
            } else if (pageUpDown && !pageUpWasDown) {
                state.modsDetailScroll = (std::max)(0, state.modsDetailScroll - 8);
            }
            if (wheelDelta != 0.0f) {
                const int wheelNotches = static_cast<int>(wheelDelta > 0.0f ? (wheelDelta + 0.5f) : (wheelDelta - 0.5f));
                if (wheelNotches > 0) {
                    state.modsDetailScroll = (std::max)(0, state.modsDetailScroll - wheelNotches * 2);
                } else if (wheelNotches < 0) {
                    state.modsDetailScroll = (std::min)(g_detailMaxScroll.load(), state.modsDetailScroll + (-wheelNotches) * 2);
                }
            }
            upWasDown = upDown; downWasDown = downDown; leftWasDown = leftDown;
            rightWasDown = rightDown; selectWasDown = selectDown; enterWasDown = enterDown;
            backWasDown = backDown; pageUpWasDown = pageUpDown; pageDownWasDown = pageDownDown; xWasDown = xDown; yWasDown = yDown;
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
            continue;
        }

        if (wheelDelta != 0.0f && count > 0) {
            const int wheelNotches = static_cast<int>(wheelDelta > 0.0f ? (wheelDelta + 0.5f) : (wheelDelta - 0.5f));
            if (wheelNotches != 0) {
                const int rowsVisible = (std::max)(1, g_modsRowsVisible.load());
                const int totalRows = (count + 1) / 2;
                const int maxScrollRow = (std::max)(0, totalRows - rowsVisible);
                int nextScroll = state.modsScrollRow - wheelNotches;
                if (nextScroll < 0) nextScroll = 0;
                if (nextScroll > maxScrollRow) nextScroll = maxScrollRow;
                state.modsScrollRow = nextScroll;
                if (wheelNotches < 0 && state.modsScrollRow >= maxScrollRow &&
                    !state.modsExhausted &&
                    (state.selectedModsTab == 1 || state.selectedModsTab == 2 || state.selectedModsTab == 4)) {
                    loadMore();
                }
            }
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
                state.modsFocus = 3;
            }
        } else if (state.modsFocus == 3) {
            if (state.modsTargetOpen) {
                const int total = static_cast<int>(state.modsTargets.size());
                if (upDown && !upWasDown) {
                    if (total > 0) state.modsTargetSel = (state.modsTargetSel - 1 + total) % total;
                } else if (downDown && !downWasDown) {
                    if (total > 0) state.modsTargetSel = (state.modsTargetSel + 1) % total;
                } else if (backDown && !backWasDown) {
                    state.modsTargetOpen = false;
                } else if ((selectDown && !selectWasDown) || (enterDown && !enterWasDown) || clickActivate) {
                    applyTargetIndex(state.modsTargetSel);
                    state.modsTargetOpen = false;
                }
            } else {
                if ((selectDown && !selectWasDown) || (enterDown && !enterWasDown) || clickActivate) {
                    EnsureModsTargetState(state, runtimeRoot);
                    state.modsTargetSel = (std::max)(0, ModsTargetIndex(state));
                    state.modsTargetOpen = true;
                } else if ((leftDown && !leftWasDown) || (upDown && !upWasDown)) {
                    state.modsFocus = 0;
                } else if (downDown && !downWasDown) {
                    enterSearch();
                }
            }
        } else if (state.modsFocus == 1) {
            if (!state.modsSearchEditing) {
                if ((selectDown && !selectWasDown) || (enterDown && !enterWasDown) || clickActivate) {
                    state.modsSearchEditing = true;
                    ModsSearchBeginInput();
                } else if ((upDown && !upWasDown) || (leftDown && !leftWasDown)) {
                    state.modsFocus = 3;
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
                    nextFocus = 3;
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
                    state.modsFocus = 1;
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

            if (state.modsFocus == 2 && state.selectedModIndex != lastEnsuredSel) {
                ensureSelectionVisible();
            }
            lastEnsuredSel = state.selectedModIndex;

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
                    state.status = L"Deleted " + deleted + L". Use Undo deleted profile to restore it.";
                }
            }

            if (((selectDown && !selectWasDown) || clickActivate) && state.selectedModIndex >= 0 && state.selectedModIndex < count) {
                ModCard selected = state.modsCards[static_cast<size_t>(state.selectedModIndex)];
                if (state.selectedModsTab == 0) {
                    if (selected.projectId == L"__restore_deleted__") {
                        std::wstring restored;
                        if (RestoreProfileBackup(runtimeRoot, LatestProfileBackup(runtimeRoot, L"deleted"), true, restored)) {
                            LoadModsTab(state, runtimeRoot, userModsDir);
                            ensureSelectionVisible();
                            state.status = L"Restored " + restored;
                            state.isError = false;
                        } else {
                            state.status = L"Could not restore deleted profile";
                            state.isError = true;
                        }
                    } else if (selected.projectId == L"__restore_backup__") {
                        std::wstring restored;
                        if (RestoreProfileBackup(runtimeRoot, LatestProfileBackup(runtimeRoot, L"manual"), false, restored)) {
                            LoadModsTab(state, runtimeRoot, userModsDir);
                            ensureSelectionVisible();
                            state.status = L"Restored backup " + restored;
                            state.isError = false;
                        } else {
                            state.status = L"Could not restore profile backup";
                            state.isError = true;
                        }
                    } else if (selected.projectId == L"__new__") {
                        const LaunchTarget target = CurrentModsTarget(state);
                        const std::wstring pid = CreateAutoProfile(runtimeRoot, target);
                        SetActiveProfileId(runtimeRoot, pid);
                        state.modsBrowseTargetId = target.targetId;
                        LoadModsTab(state, runtimeRoot, userModsDir);
                        ensureSelectionVisible();
                        state.status = L"New profile ready. Browse mods to fill it.";
                    } else if (selected.projectId == L"__profile__") {
                        SetModsTargetFromProfile(state, runtimeRoot, selected.filePath);
                        state.modsProfileOpen = true;
                        state.modsProfileId = selected.filePath;
                        state.modsProfileName = selected.title;
                        state.modsProfileTargetText = ProfileDisplayTarget(runtimeRoot, selected.filePath);
                        state.modsProfileBuiltin = (selected.filePath == kVanillaProfileId);
                        state.modsProfileControllerModEnabled = GetProfileById(runtimeRoot, selected.filePath).controllerModEnabled;
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
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
}
