#include "mod_defaults.h"

#include "crash_report.h"
#include "launcher_common.h"

#include <cmath>
#include <sstream>
#include <vector>

#include <winrt/base.h>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>

static bool ModJarHasFabricId(const std::wstring& jarPath, const wchar_t* modId) {
    using namespace winrt::Windows::Data::Json;
    if (!modId || !*modId) return false;

    std::wstring fabricModJson;
    if (!ReadZipTextFile(jarPath, "fabric.mod.json", fabricModJson)) {
        return false;
    }

    try {
        JsonObject root = JsonObject::Parse(winrt::hstring(fabricModJson.c_str()));
        if (!root.HasKey(L"id") || root.GetNamedValue(L"id").ValueType() != JsonValueType::String) {
            return false;
        }
        return root.GetNamedString(L"id") == modId;
    } catch (...) {
        return false;
    }
}

static bool FindModJarByFabricIdOrName(
    const std::wstring& modsDir,
    const wchar_t* modId,
    const wchar_t* fileNameToken,
    std::wstring& jarPath) {
    jarPath.clear();

    WIN32_FIND_DATAW fd = {};
    HANDLE h = FindFirstFileW((modsDir + L"\\*.jar").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return false;

    std::wstring fallback;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        const std::wstring path = modsDir + L"\\" + fd.cFileName;
        if (ModJarHasFabricId(path, modId)) {
            jarPath = path;
            FindClose(h);
            return true;
        }

        if (fallback.empty() && fileNameToken && *fileNameToken) {
            const std::wstring lowerName = ToLowerW(fd.cFileName);
            if (lowerName.find(fileNameToken) != std::wstring::npos) {
                fallback = path;
            }
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);

    if (!fallback.empty()) {
        jarPath = fallback;
        return true;
    }
    return false;
}

static bool JsonArrayRemoveString(winrt::Windows::Data::Json::JsonObject& root, const wchar_t* key, const std::wstring& item) {
    using namespace winrt::Windows::Data::Json;
    if (!root.HasKey(key) || root.GetNamedValue(key).ValueType() != JsonValueType::Array) return false;

    JsonArray oldArray = root.GetNamedArray(key);
    JsonArray newArray;
    bool changed = false;
    for (uint32_t i = 0; i < oldArray.Size(); ++i) {
        IJsonValue value = oldArray.GetAt(i);
        if (value.ValueType() == JsonValueType::String && std::wstring(value.GetString().c_str()) == item) {
            changed = true;
            continue;
        }
        newArray.Append(value);
    }
    if (changed) root.SetNamedValue(key, newArray);
    return changed;
}

static bool PatchMoonlightPoiMixin(const std::wstring& gameDir, const std::wstring& userModsDir, const std::wstring& minecraftVersion) {
    if (CompareVersionNumbers(w2a(minecraftVersion), "1.21.1") != 0) return false;

    std::wstring moonlightJar;
    if (!FindModJarByFabricIdOrName(userModsDir, L"moonlight", L"moonlight", moonlightJar)) {
        WriteLogF(L"Moonlight PoiMixin patch skipped; Moonlight jar not found in %s", userModsDir.c_str());
        return false;
    }

    const char* mixinEntry = "moonlight-common.mixins.json";
    std::wstring mixinText;
    if (!ReadZipTextFile(moonlightJar, mixinEntry, mixinText)) {
        WriteLogF(L"Moonlight PoiMixin patch skipped; %s not found in %s", a2w(mixinEntry).c_str(), moonlightJar.c_str());
        return false;
    }
    if (mixinText.find(L"PoiMixin") == std::wstring::npos) {
        WriteLogF(L"Moonlight PoiMixin patch skipped; PoiMixin already absent in %s", moonlightJar.c_str());
        return false;
    }

    using namespace winrt::Windows::Data::Json;
    JsonObject root;
    try {
        root = JsonObject::Parse(winrt::hstring(mixinText.c_str()));
    } catch (...) {
        WriteLogF(L"Moonlight mixin patch skipped; could not parse %s in %s", a2w(mixinEntry).c_str(), moonlightJar.c_str());
        return false;
    }

    bool changed = false;
    changed |= JsonArrayRemoveString(root, L"mixins", L"PoiMixin");
    changed |= JsonArrayRemoveString(root, L"client", L"PoiMixin");
    changed |= JsonArrayRemoveString(root, L"server", L"PoiMixin");
    if (!changed) return false;

    const std::wstring overridePath = gameDir + L"\\launcher-overrides\\" + a2w(mixinEntry);
    if (WriteTextFile(overridePath, std::wstring(root.Stringify().c_str()))) {
        WriteLogF(L"Moonlight PoiMixin patched via launcher override: %s source=%s", overridePath.c_str(), moonlightJar.c_str());
        return true;
    }

    WriteLogF(L"Moonlight PoiMixin launcher override write failed: %s err=%u", overridePath.c_str(), GetLastError());

    const std::wstring backupPath = gameDir + L"\\.bandit\\mod-originals\\" + GetFileName(moonlightJar);
    if (RewriteZipTextEntry(moonlightJar, mixinEntry, std::wstring(root.Stringify().c_str()), backupPath)) {
        WriteLogF(L"Patched Moonlight PoiMixin for NeoForge 1.21.1: %s backup=%s", moonlightJar.c_str(), backupPath.c_str());
        return true;
    }

    WriteLogF(L"Moonlight PoiMixin patch failed: %s", moonlightJar.c_str());
    return false;
}

static bool TomlLineHasKey(const std::wstring& line, const std::wstring& key) {
    size_t pos = 0;
    while (pos < line.size() && iswspace(line[pos])) ++pos;
    if (pos + key.size() > line.size() || line.compare(pos, key.size(), key) != 0) return false;
    pos += key.size();
    while (pos < line.size() && iswspace(line[pos])) ++pos;
    return pos < line.size() && line[pos] == L'=';
}

static bool TomlLineStartsSection(const std::wstring& line, std::wstring* sectionName = nullptr) {
    size_t start = 0;
    while (start < line.size() && iswspace(line[start])) ++start;
    if (start >= line.size() || line[start] != L'[') return false;

    size_t end = line.find(L']', start + 1);
    if (end == std::wstring::npos) return false;
    if (sectionName) {
        *sectionName = line.substr(start + 1, end - start - 1);
    }
    return true;
}

static std::wstring TrimWhitespace(std::wstring value) {
    while (!value.empty() && iswspace(value.front())) value.erase(value.begin());
    while (!value.empty() && iswspace(value.back())) value.pop_back();
    return value;
}

static std::wstring TomlValueForLine(const std::wstring& line) {
    const size_t eq = line.find(L'=');
    if (eq == std::wstring::npos) return std::wstring();

    std::wstring value = line.substr(eq + 1);
    const size_t comment = value.find(L'#');
    if (comment != std::wstring::npos) value = value.substr(0, comment);
    return ToLowerW(TrimWhitespace(value));
}

static bool TomlValueMatchesAny(const std::wstring& line, const std::vector<std::wstring>& values) {
    if (values.empty()) return true;
    const std::wstring actual = TomlValueForLine(line);
    for (std::wstring expected : values) {
        expected = ToLowerW(TrimWhitespace(expected));
        if (actual == expected) return true;
    }
    return false;
}

static bool UpsertTomlTopLevelSetting(
    std::wstring& text,
    const std::wstring& key,
    const std::wstring& value,
    bool onlyWhenDefault) {
    std::wstringstream in(text);
    std::wstring out;
    std::wstring line;
    bool found = false;
    bool changed = false;
    bool inTopLevel = true;

    while (std::getline(in, line)) {
        bool hadCr = !line.empty() && line.back() == L'\r';
        std::wstring normalized = hadCr ? line.substr(0, line.size() - 1) : line;

        if (TomlLineStartsSection(normalized)) {
            inTopLevel = false;
        }

        if (inTopLevel && TomlLineHasKey(normalized, key)) {
            found = true;
            const std::wstring lower = ToLowerW(normalized);
            if (!onlyWhenDefault || lower.find(L"\"default\"") != std::wstring::npos) {
                normalized = key + L" = " + value;
                changed = true;
            }
        }

        out += normalized;
        if (hadCr) out += L"\r";
        out += L"\n";
    }

    if (!found) {
        text = key + L" = " + value + L"\n" + out;
        return true;
    }

    if (changed) text = out;
    return changed;
}

static bool UpsertTomlTopLevelSettingWhenMissingOrValues(
    std::wstring& text,
    const std::wstring& key,
    const std::wstring& value,
    const std::vector<std::wstring>& oldValues) {
    std::wstringstream in(text);
    std::wstring out;
    std::wstring line;
    bool found = false;
    bool changed = false;
    bool inTopLevel = true;

    while (std::getline(in, line)) {
        bool hadCr = !line.empty() && line.back() == L'\r';
        std::wstring normalized = hadCr ? line.substr(0, line.size() - 1) : line;

        if (TomlLineStartsSection(normalized)) {
            inTopLevel = false;
        }

        if (inTopLevel && TomlLineHasKey(normalized, key)) {
            found = true;
            if (TomlValueMatchesAny(normalized, oldValues)) {
                normalized = key + L" = " + value;
                changed = true;
            }
        }

        out += normalized;
        if (hadCr) out += L"\r";
        out += L"\n";
    }

    if (!found) {
        text = key + L" = " + value + L"\n" + out;
        return true;
    }

    if (changed) text = out;
    return changed;
}

static bool UpsertTomlSectionSetting(
    std::wstring& text,
    const std::wstring& section,
    const std::wstring& key,
    const std::wstring& value,
    bool onlyWhenDefault) {
    std::wstringstream in(text);
    std::wstring out;
    std::wstring line;
    bool inSection = false;
    bool sawSection = false;
    bool found = false;
    bool changed = false;

    while (std::getline(in, line)) {
        bool hadCr = !line.empty() && line.back() == L'\r';
        std::wstring normalized = hadCr ? line.substr(0, line.size() - 1) : line;

        std::wstring nextSection;
        if (TomlLineStartsSection(normalized, &nextSection)) {
            if (inSection && !found) {
                out += key + L" = " + value + L"\n";
                found = true;
                changed = true;
            }
            inSection = nextSection == section;
            sawSection = sawSection || inSection;
        } else if (inSection && TomlLineHasKey(normalized, key)) {
            found = true;
            const std::wstring lower = ToLowerW(normalized);
            if (!onlyWhenDefault || lower.find(L"\"default\"") != std::wstring::npos) {
                normalized = key + L" = " + value;
                changed = true;
            }
        }

        out += normalized;
        if (hadCr) out += L"\r";
        out += L"\n";
    }

    if (sawSection && inSection && !found) {
        out += key + L" = " + value + L"\n";
        found = true;
        changed = true;
    } else if (!sawSection) {
        out += L"\n[" + section + L"]\n" + key + L" = " + value + L"\n";
        found = true;
        changed = true;
    }

    if (changed) text = out;
    return changed;
}

static bool UpsertTomlSectionSettingWhenMissingOrValues(
    std::wstring& text,
    const std::wstring& section,
    const std::wstring& key,
    const std::wstring& value,
    const std::vector<std::wstring>& oldValues) {
    std::wstringstream in(text);
    std::wstring out;
    std::wstring line;
    bool inSection = false;
    bool sawSection = false;
    bool found = false;
    bool changed = false;

    while (std::getline(in, line)) {
        bool hadCr = !line.empty() && line.back() == L'\r';
        std::wstring normalized = hadCr ? line.substr(0, line.size() - 1) : line;

        std::wstring nextSection;
        if (TomlLineStartsSection(normalized, &nextSection)) {
            if (inSection && !found) {
                out += key + L" = " + value + L"\n";
                found = true;
                changed = true;
            }
            inSection = nextSection == section;
            sawSection = sawSection || inSection;
        } else if (inSection && TomlLineHasKey(normalized, key)) {
            found = true;
            if (TomlValueMatchesAny(normalized, oldValues)) {
                normalized = key + L" = " + value;
                changed = true;
            }
        }

        out += normalized;
        if (hadCr) out += L"\r";
        out += L"\n";
    }

    if (sawSection && inSection && !found) {
        out += key + L" = " + value + L"\n";
        found = true;
        changed = true;
    } else if (!sawSection) {
        out += L"\n[" + section + L"]\n" + key + L" = " + value + L"\n";
        found = true;
        changed = true;
    }

    if (changed) text = out;
    return changed;
}

static bool JsonNumberMissingOrDefault(const winrt::Windows::Data::Json::JsonObject& obj, const wchar_t* key, double defaultValue) {
    using namespace winrt::Windows::Data::Json;
    if (!obj.HasKey(key)) return true;
    try {
        const IJsonValue value = obj.GetNamedValue(key);
        return value.ValueType() == JsonValueType::Number &&
            std::fabs(value.GetNumber() - defaultValue) < 0.0001;
    } catch (...) {
        return true;
    }
}

static bool JsonStringMissingOrDefault(const winrt::Windows::Data::Json::JsonObject& obj, const wchar_t* key, const wchar_t* defaultValue) {
    using namespace winrt::Windows::Data::Json;
    if (!obj.HasKey(key)) return true;
    try {
        const IJsonValue value = obj.GetNamedValue(key);
        if (value.ValueType() != JsonValueType::String) return true;
        return obj.GetNamedString(key) == defaultValue;
    } catch (...) {
        return true;
    }
}

static bool SetJsonNumberIfDefault(winrt::Windows::Data::Json::JsonObject& obj, const wchar_t* key, double value, double defaultValue) {
    using namespace winrt::Windows::Data::Json;
    if (!JsonNumberMissingOrDefault(obj, key, defaultValue)) return false;
    obj.Insert(key, JsonValue::CreateNumberValue(value));
    return true;
}

static bool SetJsonStringIfDefault(winrt::Windows::Data::Json::JsonObject& obj, const wchar_t* key, const wchar_t* value, const wchar_t* defaultValue) {
    using namespace winrt::Windows::Data::Json;
    if (!JsonStringMissingOrDefault(obj, key, defaultValue)) return false;
    obj.Insert(key, JsonValue::CreateStringValue(value));
    return true;
}

static winrt::Windows::Data::Json::JsonObject EnsureJsonObject(winrt::Windows::Data::Json::JsonObject& parent, const wchar_t* key, bool& changed) {
    using namespace winrt::Windows::Data::Json;
    if (parent.HasKey(key)) {
        try {
            IJsonValue value = parent.GetNamedValue(key);
            if (value.ValueType() == JsonValueType::Object) {
                return value.GetObject();
            }
        } catch (...) {
        }
    }

    JsonObject obj = JsonObject();
    parent.Insert(key, obj);
    changed = true;
    return obj;
}

static bool JsonBoolMissingOrDefault(const winrt::Windows::Data::Json::JsonObject& obj, const wchar_t* key, bool defaultValue) {
    using namespace winrt::Windows::Data::Json;
    if (!obj.HasKey(key)) return true;
    try {
        const IJsonValue value = obj.GetNamedValue(key);
        return value.ValueType() == JsonValueType::Boolean &&
            value.GetBoolean() == defaultValue;
    } catch (...) {
        return true;
    }
}

static bool SetJsonBoolIfDefault(winrt::Windows::Data::Json::JsonObject& obj, const wchar_t* key, bool value, bool defaultValue) {
    using namespace winrt::Windows::Data::Json;
    if (!JsonBoolMissingOrDefault(obj, key, defaultValue)) return false;
    obj.Insert(key, JsonValue::CreateBooleanValue(value));
    return true;
}

static bool ConfigureSodiumDefaults(const std::wstring& gameDir, const std::wstring& userModsDir, const std::wstring& minecraftVersion) {
    std::wstring sodiumJar;
    if (!FindModJarByFabricIdOrName(userModsDir, L"sodium", L"sodium", sodiumJar)) {
        return false;
    }

    using namespace winrt::Windows::Data::Json;
    const std::wstring configPath = gameDir + L"\\config\\sodium-options.json";
    JsonObject obj = JsonObject();

    std::wstring configText;
    if (ReadTextFile(configPath, configText)) {
        try {
            obj = JsonObject::Parse(winrt::hstring(configText.c_str()));
        } catch (...) {
            obj = JsonObject();
        }
    }

    bool changed = false;
    JsonObject advanced = EnsureJsonObject(obj, L"advanced", changed);
    JsonObject performance = EnsureJsonObject(obj, L"performance", changed);

    // Legacy Sodium's staging and render-ahead defaults can stall badly on the Mesa/UWP path.
    changed |= SetJsonStringIfDefault(advanced, L"arena_memory_allocator", L"SWAP", L"ASYNC");
    changed |= SetJsonBoolIfDefault(advanced, L"use_advanced_staging_buffers", false, true);
    changed |= SetJsonNumberIfDefault(advanced, L"cpu_render_ahead_limit", 1.0, 3.0);
    changed |= SetJsonBoolIfDefault(performance, L"always_defer_chunk_updates", true, false);
    if (CompareVersionNumbers(w2a(minecraftVersion), "1.17") < 0) {
        changed |= SetJsonBoolIfDefault(advanced, L"use_chunk_multidraw", false, true);
    }

    if (changed) {
        const std::wstring out = obj.Stringify().c_str();
        if (WriteTextFile(configPath, out)) {
            WriteLogF(L"Sodium config seeded for UWP graphics defaults: %s", configPath.c_str());
        } else {
            WriteLogF(L"Failed to write Sodium config: %s err=%u", configPath.c_str(), GetLastError());
        }
    } else {
        WriteLog(L"Sodium config already has non-default graphics settings; leaving it unchanged");
    }

    if (CompareVersionNumbers(w2a(minecraftVersion), "1.20.1") < 0) {
        const std::wstring mixinConfigPath = gameDir + L"\\config\\sodium-mixins.properties";
        std::wstring mixinConfigText;
        if (ReadTextFile(mixinConfigPath, mixinConfigText) &&
            mixinConfigText.find(L"# Generated by BanditLauncher for legacy Mesa/UWP stability.") != std::wstring::npos) {
            // ponytail: repair the old Bandit seed only; non-generated user configs stay owned by the user.
            bool mixinChanged = false;
            static const wchar_t* oldMixinDisables[] = {
                L"mixin.features.chunk_rendering=false\n",
                L"mixin.features.chunk_rendering=false\r\n",
                L"mixin.features.particle=false\n",
                L"mixin.features.particle=false\r\n",
                L"mixin.features.entity.smooth_lighting=false\n",
                L"mixin.features.entity.smooth_lighting=false\r\n",
            };
            for (const wchar_t* key : oldMixinDisables) {
                const std::wstring line(key);
                const size_t pos = mixinConfigText.find(line);
                if (pos != std::wstring::npos) {
                    mixinConfigText.erase(pos, line.size());
                    mixinChanged = true;
                }
            }
            if (mixinChanged) {
                if (WriteTextFile(mixinConfigPath, mixinConfigText)) {
                    WriteLogF(L"Sodium legacy mixin disables removed for Minecraft %s: %s", minecraftVersion.c_str(), mixinConfigPath.c_str());
                } else {
                    WriteLogF(L"Failed to repair Sodium legacy mixin config: %s err=%u", mixinConfigPath.c_str(), GetLastError());
                }
            }
        }
    }
    return true;
}

static bool ConfigureLambdaControlsDefaults(const std::wstring& gameDir, const std::wstring& userModsDir) {
    std::wstring lambdaControlsJar;
    if (!FindModJarByFabricIdOrName(userModsDir, L"lambdacontrols", L"lambdacontrols", lambdaControlsJar)) {
        return false;
    }

    const std::wstring configPath = gameDir + L"\\config\\lambdacontrols.toml";
    std::wstring configText;
    if (!ReadTextFile(configPath, configText) &&
        !ReadZipTextFile(lambdaControlsJar, "config.toml", configText)) {
        configText =
            L"controls = \"controller\"\n"
            L"auto_switch_mode = true\n"
            L"\n"
            L"[controller]\n"
            L"id = 0\n"
            L"type = \"xbox\"\n";
    }

    bool changed = false;
    changed |= UpsertTomlTopLevelSetting(configText, L"controls", L"\"controller\"", true);
    changed |= UpsertTomlTopLevelSetting(configText, L"auto_switch_mode", L"true", false);
    changed |= UpsertTomlSectionSettingWhenMissingOrValues(configText, L"gameplay", L"analog_movement", L"false", { L"true" });
    changed |= UpsertTomlSectionSetting(configText, L"controller", L"id", L"0", false);
    changed |= UpsertTomlSectionSetting(configText, L"controller", L"type", L"\"xbox\"", true);
    changed |= UpsertTomlSectionSettingWhenMissingOrValues(configText, L"controller", L"dead_zone", L"0.22", { L"0.20", L"0.2", L"0.30", L"0.3" });
    changed |= UpsertTomlSectionSettingWhenMissingOrValues(configText, L"controller", L"right_dead_zone", L"0.22", { L"0.25", L"0.30", L"0.3" });
    changed |= UpsertTomlSectionSettingWhenMissingOrValues(configText, L"controller", L"left_dead_zone", L"0.22", { L"0.25", L"0.30", L"0.3" });
    changed |= UpsertTomlSectionSettingWhenMissingOrValues(configText, L"controller", L"rotation_speed", L"24.0", { L"8.0", L"8", L"10.0", L"10", L"40.0", L"40" });
    changed |= UpsertTomlSectionSettingWhenMissingOrValues(configText, L"controller", L"mouse_speed", L"24.0", { L"18.0", L"18", L"25.0", L"25", L"30.0", L"30" });

    if (changed) {
        if (WriteTextFile(configPath, configText)) {
            WriteLogF(L"LambdaControls config seeded for UWP controller defaults: %s", configPath.c_str());
        } else {
            WriteLogF(L"Failed to write LambdaControls config: %s err=%u", configPath.c_str(), GetLastError());
        }
    } else {
        WriteLog(L"LambdaControls config already has non-default controller settings; leaving it unchanged");
    }
    return true;
}

static bool ConfigureMidnightControlsDefaults(const std::wstring& gameDir, const std::wstring& userModsDir) {
    std::wstring midnightControlsJar;
    if (!FindModJarByFabricIdOrName(userModsDir, L"midnightcontrols", L"midnightcontrols", midnightControlsJar)) {
        return false;
    }

    using namespace winrt::Windows::Data::Json;
    const std::wstring configPath = gameDir + L"\\config\\midnightcontrols.json";
    JsonObject obj = JsonObject();

    std::wstring configText;
    if (ReadTextFile(configPath, configText)) {
        try {
            obj = JsonObject::Parse(winrt::hstring(configText.c_str()));
        } catch (...) {
            obj = JsonObject();
        }
    }

    bool changed = false;
    changed |= SetJsonStringIfDefault(obj, L"controlsMode", L"CONTROLLER", L"DEFAULT");
    changed |= SetJsonStringIfDefault(obj, L"controllerType", L"XBOX", L"DEFAULT");
    changed |= SetJsonNumberIfDefault(obj, L"rightDeadZone", 0.22, 0.30);
    changed |= SetJsonNumberIfDefault(obj, L"rightDeadZone", 0.22, 0.25);
    changed |= SetJsonNumberIfDefault(obj, L"leftDeadZone", 0.22, 0.30);
    changed |= SetJsonNumberIfDefault(obj, L"leftDeadZone", 0.22, 0.25);
    changed |= SetJsonNumberIfDefault(obj, L"rotationSpeed", 20.0, 12.0);
    changed |= SetJsonNumberIfDefault(obj, L"rotationSpeed", 20.0, 35.0);
    changed |= SetJsonNumberIfDefault(obj, L"yAxisRotationSpeed", 20.0, 12.0);
    changed |= SetJsonNumberIfDefault(obj, L"yAxisRotationSpeed", 20.0, 35.0);
    changed |= SetJsonNumberIfDefault(obj, L"mouseSpeed", 24.0, 18.0);
    changed |= SetJsonNumberIfDefault(obj, L"mouseSpeed", 24.0, 25.0);

    if (changed) {
        const std::wstring out = obj.Stringify().c_str();
        if (WriteTextFile(configPath, out)) {
            WriteLogF(L"MidnightControls config seeded for UWP controller defaults: %s", configPath.c_str());
        } else {
            WriteLogF(L"Failed to write MidnightControls config: %s err=%u", configPath.c_str(), GetLastError());
        }
    } else {
        WriteLog(L"MidnightControls config already has non-default controller settings; leaving it unchanged");
    }
    return true;
}

static bool ConfigureDistantHorizonsDefaults(const std::wstring& gameDir, const std::wstring& userModsDir) {
    std::wstring distantHorizonsJar;
    if (!FindModJarByFabricIdOrName(userModsDir, L"distanthorizons", L"distanthorizons", distantHorizonsJar)) {
        return false;
    }

    const std::wstring configDir = gameDir + L"\\config";
    EnsureDirectoryTree(configDir);

    const std::wstring configPath = configDir + L"\\DistantHorizons.toml";
    std::wstring configText;
    if (!ReadTextFile(configPath, configText)) {
        configText =
            L"_version = 4\n"
            L"\n"
            L"[server]\n"
            L"maxSyncOnLoadRequestDistance = 512\n"
            L"maxGenerationRequestDistance = 512\n"
            L"enableServerGeneration = false\n"
            L"\n"
            L"[common.multiThreading]\n"
            L"numberOfThreads = 2\n"
            L"threadRunTimeRatio = \"0.35\"\n"
            L"\n"
            L"[client.advanced]\n"
            L"enableDistantGeneration = false\n"
            L"\n"
            L"[client.advanced.autoUpdater]\n"
            L"enableAutoUpdater = false\n"
            L"enableSilentUpdates = false\n"
            L"\n"
            L"[client.advanced.graphics.quality]\n"
            L"verticalQuality = \"LOW\"\n"
            L"lodChunkRenderDistanceRadius = 64\n"
            L"horizontalQuality = \"LOW\"\n";
    }

    bool changed = false;
    changed |= UpsertTomlSectionSetting(configText, L"client.advanced.autoUpdater", L"enableAutoUpdater", L"false", false);
    changed |= UpsertTomlSectionSetting(configText, L"client.advanced.autoUpdater", L"enableSilentUpdates", L"false", false);
    changed |= UpsertTomlSectionSettingWhenMissingOrValues(configText, L"client.advanced", L"enableDistantGeneration", L"false", { L"true" });
    changed |= UpsertTomlSectionSettingWhenMissingOrValues(configText, L"server", L"enableServerGeneration", L"false", { L"true" });
    changed |= UpsertTomlSectionSettingWhenMissingOrValues(configText, L"server", L"maxGenerationRequestDistance", L"512", { L"4096" });
    changed |= UpsertTomlSectionSettingWhenMissingOrValues(configText, L"server", L"maxSyncOnLoadRequestDistance", L"512", { L"4096" });
    changed |= UpsertTomlSectionSettingWhenMissingOrValues(configText, L"common.multiThreading", L"numberOfThreads", L"2", { L"16", L"8", L"4" });
    changed |= UpsertTomlSectionSettingWhenMissingOrValues(configText, L"common.multiThreading", L"threadRunTimeRatio", L"\"0.35\"", { L"\"1.0\"", L"\"1\"", L"1.0", L"1" });
    changed |= UpsertTomlSectionSettingWhenMissingOrValues(configText, L"client.advanced.graphics.quality", L"verticalQuality", L"\"LOW\"", { L"\"MEDIUM\"", L"\"HIGH\"", L"\"EXTREME\"" });
    changed |= UpsertTomlSectionSettingWhenMissingOrValues(configText, L"client.advanced.graphics.quality", L"horizontalQuality", L"\"LOW\"", { L"\"MEDIUM\"", L"\"HIGH\"", L"\"EXTREME\"" });
    changed |= UpsertTomlSectionSettingWhenMissingOrValues(configText, L"client.advanced.graphics.quality", L"lodChunkRenderDistanceRadius", L"64", { L"128", L"256", L"512", L"1024" });

    if (changed) {
        if (WriteTextFile(configPath, configText)) {
            WriteLogF(L"Distant Horizons config seeded for Xbox startup stability: %s", configPath.c_str());
        } else {
            WriteLogF(L"Failed to write Distant Horizons config: %s err=%u", configPath.c_str(), GetLastError());
        }
    } else {
        WriteLog(L"Distant Horizons config already has Xbox-safe startup settings; leaving it unchanged");
    }
    return true;
}

static bool DirectoryHasBanditController(const std::wstring& modsDir) {
    if (modsDir.empty()) return false;
    std::wstring controllerJar;
    return FindModJarByFabricIdOrName(
            modsDir,
            L"banditvault_fabric_controller",
            L"banditvault-fabric-controller",
            controllerJar) ||
        FindModJarByFabricIdOrName(
            modsDir,
            L"banditvault_forge_controller",
            L"banditvault-forge-controller",
            controllerJar) ||
        FindModJarByFabricIdOrName(
            modsDir,
            L"banditvault_neoforge_controller",
            L"banditvault-neoforge-controller",
            controllerJar);
}

void ConfigureKnownModDefaults(
    const std::wstring& gameDir,
    const std::wstring& userModsDir,
    const std::wstring& minecraftVersion,
    const std::wstring& bundledModsDir) {
    PatchMoonlightPoiMixin(gameDir, userModsDir, minecraftVersion);
    ConfigureSodiumDefaults(gameDir, userModsDir, minecraftVersion);
    ConfigureDistantHorizonsDefaults(gameDir, userModsDir);
    const bool hasLambdaControls = ConfigureLambdaControlsDefaults(gameDir, userModsDir);
    const bool hasMidnightControls = ConfigureMidnightControlsDefaults(gameDir, userModsDir);
    const bool legacyControllerMod =
        hasLambdaControls ||
        hasMidnightControls ||
        DirectoryHasBanditController(userModsDir) ||
        DirectoryHasBanditController(bundledModsDir);
    SetEnvironmentVariableW(L"MC_LEGACY_CONTROLLER_MOD", legacyControllerMod ? L"1" : L"0");
    WriteLogF(L"Legacy controller mod input mode: %s", legacyControllerMod ? L"enabled" : L"disabled");
}
