#include "profiles.h"
#include <mutex>

static std::recursive_mutex g_profilesMutex;

#include "launcher_common.h"
#include "runtime_config.h"

#include <algorithm>
#include <fstream>
#include <sstream>

#include <winrt/base.h>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>

const wchar_t kVanillaProfileId[] = L"vanilla";

// GetNamedValue throws on a type mismatch, so each of these returns its default
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

static bool JsonBoolOrDefault(const winrt::Windows::Data::Json::JsonObject& obj, const wchar_t* key, bool fallback) {
    using namespace winrt::Windows::Data::Json;
    if (!key || !obj.HasKey(key)) return fallback;
    try {
        const auto value = obj.GetNamedValue(key);
        if (value.ValueType() == JsonValueType::Boolean) {
            return obj.GetNamedBoolean(key);
        }
    } catch (...) {
    }
    return fallback;
}

std::wstring ProfilesRoot(const std::wstring& runtimeRoot) { return runtimeRoot + L"\\profiles"; }
std::wstring ProfileDir(const std::wstring& runtimeRoot, const std::wstring& id) { return ProfilesRoot(runtimeRoot) + L"\\" + id; }
std::wstring ProfileGameDir(const std::wstring& runtimeRoot, const std::wstring& id) { return ProfileDir(runtimeRoot, id) + L"\\game"; }
std::wstring LegacyProfileModsDir(const std::wstring& runtimeRoot, const std::wstring& id) { return ProfileDir(runtimeRoot, id) + L"\\mods"; }
std::wstring ProfileModsDir(const std::wstring& runtimeRoot, const std::wstring& id) { return ProfileGameDir(runtimeRoot, id) + L"\\mods"; }
std::wstring ProfilesJsonPath(const std::wstring& runtimeRoot) { return ProfilesRoot(runtimeRoot) + L"\\profiles.json"; }
std::wstring LegacyProfilesManifestPath(const std::wstring& runtimeRoot) { return ProfilesRoot(runtimeRoot) + L"\\profiles.txt"; }
std::wstring ActiveProfilePath(const std::wstring& runtimeRoot) { return ProfilesRoot(runtimeRoot) + L"\\active.txt"; }
std::wstring LegacyGameDataMigrationMarkerPath(const std::wstring& runtimeRoot) { return ProfilesRoot(runtimeRoot) + L"\\game_data_migrated.txt"; }

std::wstring MakeTargetId(const std::wstring& minecraftVersion, const std::wstring& loader, const std::wstring& loaderVersion) {
    return minecraftVersion + L"-" + loader + L"-" + (loaderVersion.empty() ? L"none" : loaderVersion);
}

LaunchTarget DefaultLaunchTarget() {
    LaunchTarget t;
    t.minecraftVersion = kDefaultMinecraftVersionW;
    t.loader = L"fabric";
    t.controllerProvider = L"fabric";
    t.loaderVersion = a2w(kDefaultFabricLoaderVersion);
    t.targetId = MakeTargetId(t.minecraftVersion, t.loader, t.loaderVersion);
    t.displayName = t.minecraftVersion + L" Fabric";
    t.javaRuntime = L"current";
    t.supportLevel = L"supported";
    t.notes = L"Current launcher default";
    return t;
}

std::vector<std::wstring> SplitTabs(const std::wstring& line) {
    std::vector<std::wstring> parts;
    size_t start = 0;
    while (start <= line.size()) {
        const size_t tab = line.find(L'\t', start);
        parts.push_back(line.substr(start, tab == std::wstring::npos ? std::wstring::npos : tab - start));
        if (tab == std::wstring::npos) break;
        start = tab + 1;
    }
    return parts;
}

std::wstring Capitalize(std::wstring s) {
    if (!s.empty()) s[0] = static_cast<wchar_t>(towupper(s[0]));
    return s;
}

std::wstring TargetShortText(const LaunchTarget& target) {
    return target.minecraftVersion + L" / " + Capitalize(target.loader);
}

std::wstring TargetProfileText(const LaunchTarget& target) {
    std::wstring text = TargetShortText(target);
    if (!target.loaderVersion.empty() && target.loaderVersion != L"none") {
        text += L" " + target.loaderVersion;
    }
    if (target.notes.find(L"Bundled controller unavailable") != std::wstring::npos) text += L" (no bundled controller)";
    return text;
}
LaunchTarget TargetFromProfile(const Profile& p) {
    LaunchTarget t = DefaultLaunchTarget();
    if (!p.minecraftVersion.empty()) t.minecraftVersion = p.minecraftVersion;
    if (!p.loader.empty()) t.loader = p.loader;
    t.controllerProvider = t.loader;
    if (!p.loaderVersion.empty()) t.loaderVersion = p.loaderVersion;
    t.targetId = !p.targetId.empty() ? p.targetId : MakeTargetId(t.minecraftVersion, t.loader, t.loaderVersion);
    t.displayName = t.minecraftVersion + L" " + Capitalize(t.loader);
    return t;
}

std::vector<LaunchTarget> LoadVersionCatalog(const std::wstring& runtimeRoot) {
    std::vector<LaunchTarget> out;
    const std::wstring packageDir = GetExecutableDir();
    const std::wstring paths[] = {
        packageDir + L"\\runtime\\version_catalog.tsv",
        runtimeRoot + L"\\runtime\\version_catalog.tsv"
    };

    std::wstring body;
    for (const std::wstring& path : paths) {
        if (ReadTextFile(path, body)) {
            WriteLogF(L"Loaded version catalog: %s", path.c_str());
            break;
        }
    }

    if (!body.empty()) {
        std::wstringstream ss(body);
        std::wstring line;
        while (std::getline(ss, line)) {
            if (!line.empty() && line.back() == L'\r') line.pop_back();
            if (line.empty() || line[0] == L'#') continue;
            std::vector<std::wstring> parts = SplitTabs(line);
            if (!parts.empty() && parts[0] == L"minecraftVersion") continue;
            if (parts.size() < 6) continue;

            LaunchTarget t;
            t.minecraftVersion = parts[0];
            t.displayName = parts.size() > 1 ? parts[1] : parts[0];
            t.loader = parts.size() > 2 ? ToLowerW(parts[2]) : L"fabric";
            t.loaderVersion = parts.size() > 3 ? parts[3] : L"none";
            t.javaRuntime = parts.size() > 4 ? parts[4] : L"current";
            t.supportLevel = parts.size() > 5 ? parts[5] : L"testing";
            t.notes = parts.size() > 7 ? parts[7] : (parts.size() > 6 ? parts[6] : L"");
            t.controllerProvider = parts.size() > 7 ? ToLowerW(parts[6]) : t.loader;
            if (t.minecraftVersion.empty() || t.loader.empty()) continue;
            t.targetId = MakeTargetId(t.minecraftVersion, t.loader, t.loaderVersion);
            out.push_back(t);
        }
    }

    const LaunchTarget def = DefaultLaunchTarget();
    auto hasDefault = std::find_if(out.begin(), out.end(), [&](const LaunchTarget& t) {
        return t.targetId == def.targetId;
    }) != out.end();
    if (!hasDefault) {
        out.insert(out.begin(), def);
    }
    if (out.empty()) out.push_back(def);
    return out;
}

LaunchTarget ResolveLaunchTarget(const std::wstring& runtimeRoot, const std::wstring& targetId) {
    const std::vector<LaunchTarget> targets = LoadVersionCatalog(runtimeRoot);
    for (const LaunchTarget& t : targets) {
        if (t.targetId == targetId) return t;
    }
    return targets.empty() ? DefaultLaunchTarget() : targets.front();
}

LaunchTarget ResolveProfileTarget(const std::wstring& runtimeRoot, const Profile& profile) {
    const std::vector<LaunchTarget> targets = LoadVersionCatalog(runtimeRoot);
    LaunchTarget profileTarget = TargetFromProfile(profile);
    const std::wstring id = profile.targetId.empty()
        ? profileTarget.targetId
        : profile.targetId;
    for (const LaunchTarget& t : targets) {
        if (t.targetId == id) return t;
    }
    for (const LaunchTarget& t : targets) {
        if (t.minecraftVersion == profileTarget.minecraftVersion &&
            _wcsicmp(t.loader.c_str(), profileTarget.loader.c_str()) == 0) {
            return t;
        }
    }
    return profileTarget;
}
Profile MakeBuiltinVanillaProfile() {
    const LaunchTarget def = DefaultLaunchTarget();
    return { kVanillaProfileId, L"Vanilla", def.minecraftVersion, def.loader, def.loaderVersion, def.targetId, true };
}

Profile ProfileWithTarget(const std::wstring& id, const std::wstring& name, const LaunchTarget& target, bool builtin) {
    return { id, name.empty() ? id : StripNewlines(name), target.minecraftVersion, target.loader, target.loaderVersion, target.targetId, builtin };
}

std::wstring ReadActiveProfileShim(const std::wstring& runtimeRoot) {
    std::ifstream f(ActiveProfilePath(runtimeRoot), std::ios::binary);
    std::string line;
    if (f && std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        return a2w(line.c_str());
    }
    return {};
}

std::wstring ReadActiveProfileFromJson(const std::wstring& runtimeRoot) {
    using namespace winrt::Windows::Data::Json;
    std::wstring body;
    if (!ReadTextFile(ProfilesJsonPath(runtimeRoot), body)) return {};
    try {
        JsonObject root = JsonObject::Parse(winrt::hstring(body.c_str()));
        return JsonStringOrEmpty(root, L"activeProfileId");
    } catch (...) {
        return {};
    }
}

static bool SaveProfilesJson(const std::wstring& runtimeRoot, const std::vector<Profile>& profiles, const std::wstring& activeProfileId) {
    using namespace winrt::Windows::Data::Json;
    if (!EnsureDirectoryTree(ProfilesRoot(runtimeRoot))) return false;
    std::wstring existing;
    if (GetFileAttributesW(ProfilesJsonPath(runtimeRoot).c_str()) != INVALID_FILE_ATTRIBUTES) {
        if (!ReadTextFile(ProfilesJsonPath(runtimeRoot), existing)) return false;
        try {
            JsonObject registry = JsonObject::Parse(winrt::hstring(existing));
            if (!registry.HasKey(L"profiles") || registry.GetNamedValue(L"profiles").ValueType() != JsonValueType::Array) {
                WriteLog(L"Profile registry missing its profiles array. Keeping the existing file");
                return false;
            }
            for (const auto& value : registry.GetNamedArray(L"profiles")) {
                if (value.ValueType() != JsonValueType::Object || JsonStringOrEmpty(value.GetObject(), L"id").empty()) {
                    WriteLog(L"Profile registry contains an invalid profile. Keeping the existing file");
                    return false;
                }
            }
        }
        catch (...) { WriteLog(L"Profile registry damaged. Keeping the existing file"); return false; }
    }


    auto jsonString = [](const std::wstring& value) {
        return JsonValue::CreateStringValue(winrt::hstring(value.c_str()));
    };

    JsonObject root;
    root.SetNamedValue(L"schemaVersion", JsonValue::CreateNumberValue(2));
    const std::wstring activeId = activeProfileId.empty() ? std::wstring(kVanillaProfileId) : activeProfileId;
    root.SetNamedValue(L"activeProfileId", jsonString(activeId));

    JsonArray arr;
    for (const Profile& p : profiles) {
        JsonObject obj;
        obj.SetNamedValue(L"id", jsonString(p.id));
        obj.SetNamedValue(L"name", jsonString(StripNewlines(p.name)));
        obj.SetNamedValue(L"minecraftVersion", jsonString(p.minecraftVersion));
        obj.SetNamedValue(L"loader", jsonString(p.loader));
        obj.SetNamedValue(L"loaderVersion", jsonString(p.loaderVersion));
        obj.SetNamedValue(L"targetId", jsonString(p.targetId));
        obj.SetNamedValue(L"builtin", JsonValue::CreateBooleanValue(p.builtin));
        obj.SetNamedValue(L"controllerModEnabled", JsonValue::CreateBooleanValue(p.controllerModEnabled));
        arr.Append(obj);
    }
    root.SetNamedValue(L"profiles", arr);

    const std::wstring text = std::wstring(root.Stringify().c_str());
    if (!WriteTextFile(ProfilesJsonPath(runtimeRoot), text)) {
        WriteLogF(L"Failed to write profiles.json err=%u", GetLastError());
        return false;
    }
    return true;
}

std::vector<Profile> LoadProfiles(const std::wstring& runtimeRoot) {
    std::lock_guard<std::recursive_mutex> guard(g_profilesMutex);
    using namespace winrt::Windows::Data::Json;
    std::vector<Profile> out;
    const LaunchTarget def = DefaultLaunchTarget();

    std::wstring body;
    if (ReadTextFile(ProfilesJsonPath(runtimeRoot), body)) {
        try {
            JsonObject root = JsonObject::Parse(winrt::hstring(body.c_str()));
            if (root.HasKey(L"profiles") && root.GetNamedValue(L"profiles").ValueType() == JsonValueType::Array) {
                JsonArray arr = root.GetNamedArray(L"profiles");
                for (uint32_t i = 0; i < arr.Size(); ++i) {
                    auto value = arr.GetAt(i);
                    if (value.ValueType() != JsonValueType::Object) continue;
                    JsonObject obj = value.GetObject();
                    Profile p;
                    p.id = JsonStringOrEmpty(obj, L"id");
                    p.name = JsonStringOrEmpty(obj, L"name");
                    p.minecraftVersion = JsonStringOrEmpty(obj, L"minecraftVersion");
                    p.loader = ToLowerW(JsonStringOrEmpty(obj, L"loader"));
                    p.loaderVersion = JsonStringOrEmpty(obj, L"loaderVersion");
                    p.targetId = JsonStringOrEmpty(obj, L"targetId");
                    p.builtin = JsonBoolOrFalse(obj, L"builtin");
                    p.controllerModEnabled = JsonBoolOrDefault(obj, L"controllerModEnabled", true);
                    if (p.id.empty()) continue;
                    if (p.name.empty()) p.name = p.id;
                    if (p.minecraftVersion.empty()) p.minecraftVersion = def.minecraftVersion;
                    if (p.loader.empty()) p.loader = def.loader;
                    if (p.loaderVersion.empty()) p.loaderVersion = def.loaderVersion;
                    if (p.targetId.empty()) p.targetId = MakeTargetId(p.minecraftVersion, p.loader, p.loaderVersion);
                    out.push_back(p);
                }
            }
        } catch (const winrt::hresult_error& ex) {
            WriteLogF(L"profiles.json parse failed hr=0x%08X msg=%s",
                static_cast<unsigned int>(ex.code()), ex.message().c_str());
        }
        if (std::find_if(out.begin(), out.end(), [](const Profile& p) { return p.id == kVanillaProfileId; }) == out.end()) {
            out.insert(out.begin(), MakeBuiltinVanillaProfile());
        }
        if (out.empty()) out.push_back(MakeBuiltinVanillaProfile());
        return out;
    }

    out.push_back(MakeBuiltinVanillaProfile());
    std::ifstream f(LegacyProfilesManifestPath(runtimeRoot), std::ios::binary);
    std::string line;
    while (f && std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        const size_t tab = line.find('\t');
        const std::wstring id = a2w((tab == std::string::npos ? line : line.substr(0, tab)).c_str());
        const std::wstring name = tab == std::string::npos ? id : a2w(line.substr(tab + 1).c_str());
        if (id.empty() || id == kVanillaProfileId) continue;
        out.push_back(ProfileWithTarget(id, name.empty() ? id : name, def, false));
    }
    const std::wstring active = ReadActiveProfileShim(runtimeRoot);
    SaveProfilesJson(runtimeRoot, out, active.empty() ? std::wstring(kVanillaProfileId) : active);
    return out;
}

static bool SaveProfiles(const std::wstring& runtimeRoot, const std::vector<Profile>& profiles) {
    std::wstring active = ReadActiveProfileFromJson(runtimeRoot);
    if (active.empty()) active = ReadActiveProfileShim(runtimeRoot);
    if (active.empty()) active = kVanillaProfileId;
    return SaveProfilesJson(runtimeRoot, profiles, active);
}

std::wstring GetActiveProfileId(const std::wstring& runtimeRoot) {
    const std::wstring jsonActive = ReadActiveProfileFromJson(runtimeRoot);
    const std::wstring shimActive = ReadActiveProfileShim(runtimeRoot);
    const std::wstring candidates[] = { jsonActive, shimActive };
    const std::vector<Profile> profiles = LoadProfiles(runtimeRoot);
    for (const std::wstring& id : candidates) {
        if (id.empty()) continue;
        for (const Profile& p : profiles) {
            if (p.id == id) return id;
        }
    }
    return kVanillaProfileId;
}

bool SetActiveProfileId(const std::wstring& runtimeRoot, const std::wstring& id) {
    std::lock_guard<std::recursive_mutex> guard(g_profilesMutex);
    const std::wstring active = id.empty() ? std::wstring(kVanillaProfileId) : id;
    if (!SaveProfilesJson(runtimeRoot, LoadProfiles(runtimeRoot), active)) return false;
    if (!WriteTextFile(ActiveProfilePath(runtimeRoot), active)) WriteLog(L"Active profile shim write failed");
    return true;
}

int ProfileModCount(const std::wstring& runtimeRoot, const std::wstring& id) {
    int n = 0;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((ProfileModsDir(runtimeRoot, id) + L"\\*.jar").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do { if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) ++n; } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    return n;
}

std::wstring MakeProfileId(const std::wstring& runtimeRoot, const std::wstring& name) {
    std::wstring base = SafeFileName(name);
    if (base.empty()) base = L"profile";
    std::wstring id = base;
    int n = 2;
    while (id == kVanillaProfileId || GetFileAttributesW(ProfileDir(runtimeRoot, id).c_str()) != INVALID_FILE_ATTRIBUTES) {
        id = base + L"-" + std::to_wstring(n++);
    }
    return id;
}

Profile GetProfileById(const std::wstring& runtimeRoot, const std::wstring& id) {
    for (const Profile& p : LoadProfiles(runtimeRoot)) {
        if (p.id == id) return p;
    }
    return MakeBuiltinVanillaProfile();
}

std::wstring CreateProfile(const std::wstring& runtimeRoot, const std::wstring& name, const LaunchTarget& target) {
    std::lock_guard<std::recursive_mutex> guard(g_profilesMutex);
    std::vector<Profile> profiles = LoadProfiles(runtimeRoot);
    const std::wstring id = MakeProfileId(runtimeRoot, name);
    EnsureDirectoryTree(ProfileModsDir(runtimeRoot, id));
    EnsureDirectoryTree(ProfileGameDir(runtimeRoot, id));
    profiles.push_back(ProfileWithTarget(id, name.empty() ? id : StripNewlines(name), target, false));
    if (!SaveProfiles(runtimeRoot, profiles)) {
        DeleteDirectoryTree(ProfileDir(runtimeRoot, id));
        return {};
    }
    return id;
}

std::wstring CreateProfile(const std::wstring& runtimeRoot, const std::wstring& name) {
    std::lock_guard<std::recursive_mutex> guard(g_profilesMutex);
    return CreateProfile(runtimeRoot, name, DefaultLaunchTarget());
}

std::wstring CreateAutoProfile(const std::wstring& runtimeRoot, const LaunchTarget& target) {
    int n = 1;
    for (const Profile& p : LoadProfiles(runtimeRoot)) if (!p.builtin) ++n;
    return CreateProfile(runtimeRoot, L"Profile " + std::to_wstring(n) + L" - " + TargetShortText(target), target);
}

std::wstring ProfileBackupsRoot(const std::wstring& runtimeRoot) {
    return runtimeRoot + L"\\profile-backups";
}

std::wstring ProfileBackupDir(const std::wstring& runtimeRoot, const std::wstring& kind, const std::wstring& profileId) {
    return ProfileBackupsRoot(runtimeRoot) + L"\\" + kind + L"-" + CrashTimestampForFileName() + L"-" + SafeFileName(profileId);
}

static bool WriteProfileBackupMeta(const std::wstring& backupDir, const Profile& profile, const std::wstring& kind) {
    using namespace winrt::Windows::Data::Json;
    auto jsonString = [](const std::wstring& value) {
        return JsonValue::CreateStringValue(winrt::hstring(value.c_str()));
    };

    JsonObject obj;
    obj.SetNamedValue(L"schemaVersion", JsonValue::CreateNumberValue(1));
    obj.SetNamedValue(L"kind", jsonString(kind));
    obj.SetNamedValue(L"id", jsonString(profile.id));
    obj.SetNamedValue(L"name", jsonString(profile.name));
    obj.SetNamedValue(L"minecraftVersion", jsonString(profile.minecraftVersion));
    obj.SetNamedValue(L"loader", jsonString(profile.loader));
    obj.SetNamedValue(L"loaderVersion", jsonString(profile.loaderVersion));
    obj.SetNamedValue(L"targetId", jsonString(profile.targetId));
    obj.SetNamedValue(L"controllerModEnabled", JsonValue::CreateBooleanValue(profile.controllerModEnabled));
    obj.SetNamedValue(L"created", jsonString(CrashTimestampForFileName()));
    return WriteTextFile(backupDir + L"\\profile_backup.json", std::wstring(obj.Stringify().c_str()));
}

static bool ReadProfileBackupMeta(const std::wstring& backupDir, Profile& profile, std::wstring& kind) {
    using namespace winrt::Windows::Data::Json;
    if (backupDir.size() >= 8 && backupDir.compare(backupDir.size() - 8, 8, L".partial") == 0) return false;
    std::wstring body;
    if (!ReadTextFile(backupDir + L"\\profile_backup.json", body)) return false;
    try {
        JsonObject obj = JsonObject::Parse(winrt::hstring(body.c_str()));
        kind = JsonStringOrEmpty(obj, L"kind");
        profile.id = JsonStringOrEmpty(obj, L"id");
        profile.name = JsonStringOrEmpty(obj, L"name");
        profile.minecraftVersion = JsonStringOrEmpty(obj, L"minecraftVersion");
        profile.loader = ToLowerW(JsonStringOrEmpty(obj, L"loader"));
        profile.loaderVersion = JsonStringOrEmpty(obj, L"loaderVersion");
        profile.targetId = JsonStringOrEmpty(obj, L"targetId");
        profile.controllerModEnabled = JsonBoolOrDefault(obj, L"controllerModEnabled", true);
        profile.builtin = false;
        if (profile.id.empty()) return false;
        if (profile.name.empty()) profile.name = profile.id;
        const LaunchTarget def = DefaultLaunchTarget();
        if (profile.minecraftVersion.empty()) profile.minecraftVersion = def.minecraftVersion;
        if (profile.loader.empty()) profile.loader = def.loader;
        if (profile.loaderVersion.empty()) profile.loaderVersion = def.loaderVersion;
        if (profile.targetId.empty()) profile.targetId = MakeTargetId(profile.minecraftVersion, profile.loader, profile.loaderVersion);
        return true;
    } catch (...) {
        return false;
    }
}

std::wstring LatestProfileBackup(const std::wstring& runtimeRoot, const std::wstring& kind) {
    const std::wstring root = ProfileBackupsRoot(runtimeRoot);
    std::wstring latest;
    WIN32_FIND_DATAW fd = {};
    HANDLE h = FindFirstFileW((root + L"\\" + kind + L"-*").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) continue;
            if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
            const std::wstring name = fd.cFileName;
            Profile profile;
            std::wstring backupKind;
            if (!ReadProfileBackupMeta(root + L"\\" + name, profile, backupKind) || backupKind != kind ||
                !DirectoryExists(root + L"\\" + name + L"\\profile")) continue;
            if (latest.empty() || _wcsicmp(name.c_str(), latest.c_str()) > 0) latest = name;
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    return latest.empty() ? std::wstring() : root + L"\\" + latest;
}

std::wstring ProfileBackupDisplayName(const std::wstring& backupDir) {
    Profile p;
    std::wstring kind;
    if (ReadProfileBackupMeta(backupDir, p, kind)) return p.name;
    const size_t slash = backupDir.find_last_of(L"\\/");
    return slash == std::wstring::npos ? backupDir : backupDir.substr(slash + 1);
}

bool BackupProfile(const std::wstring& runtimeRoot, const std::wstring& id, std::wstring& backupDir) {
    std::lock_guard<std::recursive_mutex> guard(g_profilesMutex);
    if (id.empty() || id == kVanillaProfileId) return false;
    const Profile profile = GetProfileById(runtimeRoot, id);
    backupDir.clear();
    const std::wstring base = ProfileBackupDir(runtimeRoot, L"manual", id);
    std::wstring complete = base;
    unsigned suffix = 0;
    while (DirectoryExists(complete) || DirectoryExists(complete + L".partial")) {
        const std::wstring number = std::to_wstring(++suffix);
        complete = base + L"-" + std::wstring(10 - number.size(), L'0') + number;
    }
    const std::wstring staging = complete + L".partial";
    if (!CopyDirectoryTree(ProfileDir(runtimeRoot, id), staging + L"\\profile") ||
        !WriteProfileBackupMeta(staging, profile, L"manual") ||
        !MoveFileExW(staging.c_str(), complete.c_str(), MOVEFILE_WRITE_THROUGH)) {
        DeleteDirectoryTree(staging);
        return false;
    }
    backupDir = complete;
    return true;
}

std::wstring UniqueRestoredProfileId(const std::wstring& runtimeRoot, const std::wstring& desiredId, const std::vector<Profile>& profiles) {
    std::wstring base = SafeFileName(desiredId.empty() ? std::wstring(L"profile") : desiredId);
    if (base == kVanillaProfileId) base += L"-restored";
    std::wstring id = base;
    int n = 2;
    auto existsInProfiles = [&](const std::wstring& probe) {
        return std::find_if(profiles.begin(), profiles.end(), [&](const Profile& p) { return p.id == probe; }) != profiles.end();
    };
    while (existsInProfiles(id) || GetFileAttributesW(ProfileDir(runtimeRoot, id).c_str()) != INVALID_FILE_ATTRIBUTES) {
        id = base + L"-restored-" + std::to_wstring(n++);
    }
    return id;
}

bool RestoreProfileBackup(const std::wstring& runtimeRoot, const std::wstring& backupDir, bool removeBackup, std::wstring& restoredName) {
    std::lock_guard<std::recursive_mutex> guard(g_profilesMutex);
    Profile profile;
    std::wstring kind;
    if (!ReadProfileBackupMeta(backupDir, profile, kind)) return false;

    std::vector<Profile> profiles = LoadProfiles(runtimeRoot);
    profile.id = UniqueRestoredProfileId(runtimeRoot, profile.id, profiles);
    profile.builtin = false;

    const std::wstring source = backupDir + L"\\profile";
    const std::wstring dest = ProfileDir(runtimeRoot, profile.id);
    if (!CopyDirectoryTree(source, dest)) return false;
    profiles.push_back(profile);
    if (!SaveProfilesJson(runtimeRoot, profiles, profile.id)) {
        DeleteDirectoryTree(dest);
        return false;
    }
    restoredName = profile.name;
    if (removeBackup) DeleteDirectoryTree(backupDir);
    return true;
}

bool DeleteProfilePermanent(const std::wstring& runtimeRoot, const std::wstring& id) {
    return DeleteProfile(runtimeRoot, id);
}

bool DeleteProfile(const std::wstring& runtimeRoot, const std::wstring& id) {
    std::lock_guard<std::recursive_mutex> guard(g_profilesMutex);
    if (id.empty() || id == kVanillaProfileId) return false;
    const Profile profile = GetProfileById(runtimeRoot, id);
    const std::wstring backupDir = ProfileBackupDir(runtimeRoot, L"deleted", id);
    if (!WriteProfileBackupMeta(backupDir, profile, L"deleted")) return false;
    const std::wstring source = ProfileDir(runtimeRoot, id);
    const std::wstring archived = backupDir + L"\\profile";
    if (!MoveFileExW(source.c_str(), archived.c_str(), MOVEFILE_WRITE_THROUGH)) return false;
    const std::wstring active = GetActiveProfileId(runtimeRoot);
    std::vector<Profile> kept;
    for (const Profile& p : LoadProfiles(runtimeRoot)) if (p.id != id) kept.push_back(p);
    if (!SaveProfilesJson(runtimeRoot, kept, active == id ? std::wstring(kVanillaProfileId) : active)) {
        if (!MoveFileExW(archived.c_str(), source.c_str(), MOVEFILE_WRITE_THROUGH)) {
            WriteLogF(L"Profile registry unchanged. Original retained at %s", archived.c_str());
        }
        return false;
    }
    return true;
}

bool RenameProfile(const std::wstring& runtimeRoot, const std::wstring& id, const std::wstring& newName) {
    std::lock_guard<std::recursive_mutex> guard(g_profilesMutex);
    if (id.empty() || id == kVanillaProfileId) return false;
    std::vector<Profile> profiles = LoadProfiles(runtimeRoot);
    for (auto& p : profiles) if (p.id == id && !p.builtin) p.name = StripNewlines(newName);
    return SaveProfiles(runtimeRoot, profiles);
}

bool SetProfileControllerModEnabled(const std::wstring& runtimeRoot, const std::wstring& id, bool enabled) {
    std::lock_guard<std::recursive_mutex> guard(g_profilesMutex);
    if (id.empty() || id == kVanillaProfileId) return false;
    std::vector<Profile> profiles = LoadProfiles(runtimeRoot);
    for (auto& p : profiles) {
        if (p.id == id && !p.builtin) {
            p.controllerModEnabled = enabled;
            break;
        }
    }
    return SaveProfiles(runtimeRoot, profiles);
}

void EnsureProfilesInitialized(const std::wstring& runtimeRoot) {
    std::lock_guard<std::recursive_mutex> guard(g_profilesMutex);
    EnsureDirectoryTree(ProfileModsDir(runtimeRoot, kVanillaProfileId));
    EnsureDirectoryTree(ProfileGameDir(runtimeRoot, kVanillaProfileId));
    if (GetFileAttributesW(ProfilesJsonPath(runtimeRoot).c_str()) != INVALID_FILE_ATTRIBUTES) return;
    if (GetFileAttributesW(LegacyProfilesManifestPath(runtimeRoot).c_str()) != INVALID_FILE_ATTRIBUTES) {
        LoadProfiles(runtimeRoot);
        return;
    }

    const std::wstring legacy = runtimeRoot + L"\\game\\user-mods";
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((legacy + L"\\*.jar").c_str(), &fd);
    const bool hasLegacy = h != INVALID_HANDLE_VALUE;

    std::vector<Profile> profiles;
    const LaunchTarget def = DefaultLaunchTarget();
    profiles.push_back(MakeBuiltinVanillaProfile());
    if (hasLegacy) {
        const std::wstring id = L"default";
        EnsureDirectoryTree(ProfileModsDir(runtimeRoot, id));
        EnsureDirectoryTree(ProfileGameDir(runtimeRoot, id));
        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            MoveFileExW((legacy + L"\\" + fd.cFileName).c_str(),
                (ProfileModsDir(runtimeRoot, id) + L"\\" + fd.cFileName).c_str(), MOVEFILE_REPLACE_EXISTING);
        } while (FindNextFileW(h, &fd));
        FindClose(h);
        profiles.push_back(ProfileWithTarget(id, L"Default", def, false));
        SaveProfilesJson(runtimeRoot, profiles, id);
        SetActiveProfileId(runtimeRoot, id);
    } else {
        SaveProfilesJson(runtimeRoot, profiles, kVanillaProfileId);
        SetActiveProfileId(runtimeRoot, kVanillaProfileId);
    }
}

static bool MoveLegacyGameDataPathToProfile(const std::wstring& runtimeRoot, const std::wstring& profileId, const std::wstring& relativePath) {
    const std::wstring source = runtimeRoot + L"\\game\\" + relativePath;
    const DWORD attrs = GetFileAttributesW(source.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES) return GetLastError() == ERROR_FILE_NOT_FOUND || GetLastError() == ERROR_PATH_NOT_FOUND;

    const std::wstring dest = ProfileGameDir(runtimeRoot, profileId) + L"\\" + relativePath;
    if (GetFileAttributesW(dest.c_str()) != INVALID_FILE_ATTRIBUTES) {
        WriteLogF(L"Legacy game data migration skipped existing destination: %s", dest.c_str());
        return false;
    }

    EnsureDirectoryTree(GetParentDir(dest));
    if (MoveFileExW(source.c_str(), dest.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        WriteLogF(L"Legacy game data migrated: %s -> %s", source.c_str(), dest.c_str());
        return true;
    } else {
        WriteLogF(L"Legacy game data migration failed: %s -> %s err=%u", source.c_str(), dest.c_str(), GetLastError());
        return false;
    }
}

void MigrateLegacyProfileModsForProfile(const std::wstring& runtimeRoot, const std::wstring& profileId) {
    const std::wstring legacyMods = LegacyProfileModsDir(runtimeRoot, profileId);
    if (!DirectoryExists(legacyMods)) return;

    const std::wstring targetMods = ProfileModsDir(runtimeRoot, profileId);
    EnsureDirectoryTree(targetMods);

    WIN32_FIND_DATAW fd = {};
    HANDLE h = FindFirstFileW((legacyMods + L"\\*").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
            const std::wstring source = legacyMods + L"\\" + fd.cFileName;
            const std::wstring dest = targetMods + L"\\" + fd.cFileName;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                if (MovePathIfExists(source, dest, false)) {
                    WriteLogF(L"Legacy profile mod directory migrated: %s -> %s", source.c_str(), dest.c_str());
                }
            } else if (MovePathIfExists(source, dest)) {
                WriteLogF(L"Legacy profile mod migrated: %s -> %s", source.c_str(), dest.c_str());
            }
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }

    RemoveDirectoryW(legacyMods.c_str());
}

void EnsureProfileGameDataInitialized(const std::wstring& runtimeRoot, const std::wstring& profileId) {
    if (profileId.empty()) return;
    EnsureDirectoryTree(ProfileGameDir(runtimeRoot, profileId));
    EnsureDirectoryTree(ProfileModsDir(runtimeRoot, profileId));
    MigrateLegacyProfileModsForProfile(runtimeRoot, profileId);

    const std::wstring marker = LegacyGameDataMigrationMarkerPath(runtimeRoot);
    if (GetFileAttributesW(marker.c_str()) != INVALID_FILE_ATTRIBUTES) return;

    bool migrated = true;
    migrated = MoveLegacyGameDataPathToProfile(runtimeRoot, profileId, L"saves") && migrated;
    migrated = MoveLegacyGameDataPathToProfile(runtimeRoot, profileId, L"config") && migrated;
    migrated = MoveLegacyGameDataPathToProfile(runtimeRoot, profileId, L"resourcepacks") && migrated;
    migrated = MoveLegacyGameDataPathToProfile(runtimeRoot, profileId, L"screenshots") && migrated;
    migrated = MoveLegacyGameDataPathToProfile(runtimeRoot, profileId, L"shaderpacks") && migrated;
    migrated = MoveLegacyGameDataPathToProfile(runtimeRoot, profileId, L"server-resource-packs") && migrated;
    migrated = MoveLegacyGameDataPathToProfile(runtimeRoot, profileId, L"options.txt") && migrated;
    migrated = MoveLegacyGameDataPathToProfile(runtimeRoot, profileId, L"optionsof.txt") && migrated;
    migrated = MoveLegacyGameDataPathToProfile(runtimeRoot, profileId, L"servers.dat") && migrated;

    if (!migrated) return;

    EnsureDirectoryTree(GetParentDir(marker));
    WriteTextFile(marker, profileId + L"\n");
}

std::vector<std::wstring> ListProfileMods(const std::wstring& runtimeRoot, const std::wstring& id) {
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

std::wstring ProfileDisplayName(const std::wstring& runtimeRoot, const std::wstring& id) {
    for (const Profile& p : LoadProfiles(runtimeRoot)) if (p.id == id) return p.name;
    return id;
}

std::wstring ProfileDisplayTarget(const std::wstring& runtimeRoot, const std::wstring& id) {
    return TargetProfileText(ResolveProfileTarget(runtimeRoot, GetProfileById(runtimeRoot, id)));
}
