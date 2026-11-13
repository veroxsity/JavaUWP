#include "themes.h"

#include "i18n.h"
#include "launcher_common.h"

#include <winrt/base.h>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>

#include <algorithm>
#include <mutex>

namespace themes {
namespace {

constexpr wchar_t kDefaultId[] = L"bandit";
constexpr wchar_t kCustomPrefix[] = L"custom:";
constexpr UINT32 kDarkInk = 0x121315;
constexpr UINT32 kLightInk = 0xF7F5F0;

std::mutex g_mutex;
std::vector<Theme> g_themes;
size_t g_current = 0;
bool g_loaded = false;

float Luminance(UINT32 rgb) {
    const float r = ((rgb >> 16) & 0xFF) / 255.0f;
    const float g = ((rgb >> 8) & 0xFF) / 255.0f;
    const float b = (rgb & 0xFF) / 255.0f;
    return 0.2126f * r + 0.7152f * g + 0.0722f * b;
}

bool IsLight(UINT32 rgb) {
    return Luminance(rgb) > 0.5f;
}

Theme BuiltIn(const wchar_t* id, const wchar_t* name, Palette palette) {
    palette.shade = IsLight(palette.bg) ? 0xFFFFFF : 0x000000;
    Theme theme;
    theme.id = id;
    theme.name = name;
    theme.palette = palette;
    return theme;
}

std::vector<Theme> BuiltIns() {
    return {
        // the tan of the crate behind the raccoon on the app icon, lifted so it reads on a dark screen
        BuiltIn(kDefaultId, TR_NOOP(L"Bandit"),
            { 0x0D0E10, 0x1B1D21, 0x2C2F35, 0xF4F2ED, 0xA5A29C, 0xE3A96B, 0x7CC48D, 0xEC7462, 0x121315 }),
        BuiltIn(L"midnight", TR_NOOP(L"Midnight"),
            { 0x0B0E14, 0x161B26, 0x252C3B, 0xEEF2F8, 0x9AA5B8, 0x78AEFF, 0x72C79A, 0xF07A6E, 0x0E1118 }),
        BuiltIn(L"moss", TR_NOOP(L"Moss"),
            { 0x0C100D, 0x171D18, 0x263027, 0xEFF3EC, 0xA1AC9F, 0x9BD46E, 0x5FC6A4, 0xEE7A66, 0x101410 }),
        BuiltIn(L"ocean", TR_NOOP(L"Ocean"),
            { 0x0A1012, 0x141D20, 0x223034, 0xECF4F4, 0x97A9AB, 0x52D0C4, 0x7CC48D, 0xF07A6E, 0x0C1213 }),
        BuiltIn(L"berry", TR_NOOP(L"Berry"),
            { 0x110D12, 0x1E1820, 0x312834, 0xF6F0F5, 0xADA1AB, 0xE88CC8, 0x7CC48D, 0xF07870, 0x141015 }),
        BuiltIn(L"ember", TR_NOOP(L"Ember"),
            { 0x120E0C, 0x211A17, 0x352A25, 0xF7F0EB, 0xAEA39C, 0xFF9150, 0x7CC48D, 0xF0607A, 0x15100E }),
        BuiltIn(L"daylight", TR_NOOP(L"Daylight"),
            { 0xF2F0EB, 0xFFFFFF, 0xE2DFD8, 0x17181A, 0x5F5C57, 0xA85F18, 0x2E8B4E, 0xC4382A, 0xF7F5F0 }),
    };
}

std::wstring SavedPath() {
    const std::wstring dir = GetLocalStateDir();
    return dir.empty() ? dir : dir + L"\\theme.txt";
}

bool SameId(const std::wstring& a, const std::wstring& b) {
    return ToLowerW(a) == ToLowerW(b);
}

const Theme* FindLocked(const std::wstring& id) {
    for (const Theme& theme : g_themes) {
        if (SameId(theme.id, id)) return &theme;
    }
    return nullptr;
}

bool ParseColour(std::wstring text, UINT32& out) {
    text = TrimWhitespace(text);
    if (!text.empty() && text.front() == L'#') text.erase(0, 1);
    if (text.size() == 3) {
        text = { text[0], text[0], text[1], text[1], text[2], text[2] };
    }
    if (text.size() != 6) return false;
    UINT32 value = 0;
    for (wchar_t ch : text) {
        value <<= 4;
        if (ch >= L'0' && ch <= L'9') value |= ch - L'0';
        else if (ch >= L'a' && ch <= L'f') value |= ch - L'a' + 10;
        else if (ch >= L'A' && ch <= L'F') value |= ch - L'A' + 10;
        else return false;
    }
    out = value;
    return true;
}

bool ReadThemeFile(const std::wstring& path, const std::wstring& stem, const std::vector<Theme>& builtIns, Theme& out) {
    std::wstring text;
    if (!ReadTextFile(path, text)) return false;
    if (!text.empty() && text.front() == 0xFEFF) text.erase(0, 1);

    try {
        using namespace winrt::Windows::Data::Json;
        JsonObject root;
        if (!JsonObject::TryParse(winrt::hstring(text), root)) {
            WriteLogF(L"theme file %s is not valid json", path.c_str());
            return false;
        }
        auto str = [&](const wchar_t* key) -> std::wstring {
            if (!root.HasKey(key)) return {};
            const IJsonValue value = root.Lookup(key);
            return value.ValueType() == JsonValueType::String ? std::wstring(value.GetString().c_str()) : std::wstring();
        };

        Palette palette = builtIns.front().palette;
        const std::wstring base = str(L"base");
        if (!base.empty()) {
            bool found = false;
            for (const Theme& theme : builtIns) {
                if (SameId(theme.id, base)) {
                    palette = theme.palette;
                    found = true;
                }
            }
            if (!found) WriteLogF(L"theme file %s starts from unknown theme %s", path.c_str(), base.c_str());
        }

        struct Field { const wchar_t* key; UINT32* slot; };
        const Field fields[] = {
            { L"background", &palette.bg },
            { L"surface", &palette.surface },
            { L"raised", &palette.raised },
            { L"text", &palette.text },
            { L"mutedText", &palette.textMuted },
            { L"accent", &palette.accent },
            { L"success", &palette.ok },
            { L"danger", &palette.danger },
            { L"focusText", &palette.onLight },
        };
        bool setFocusText = false;
        for (const Field& field : fields) {
            const std::wstring value = str(field.key);
            if (value.empty()) continue;
            if (!ParseColour(value, *field.slot)) {
                WriteLogF(L"theme file %s has a bad colour for %s: %s", path.c_str(), field.key, value.c_str());
                continue;
            }
            if (field.slot == &palette.onLight) setFocusText = true;
        }
        if (!setFocusText) palette.onLight = IsLight(palette.text) ? kDarkInk : kLightInk;
        palette.shade = IsLight(palette.bg) ? 0xFFFFFF : 0x000000;

        out.id = std::wstring(kCustomPrefix) + stem;
        out.name = str(L"name");
        if (out.name.empty()) out.name = stem;
        out.palette = palette;
        out.custom = true;
        return true;
    } catch (const winrt::hresult_error& ex) {
        WriteLogF(L"theme file %s could not be read hr=0x%08X", path.c_str(), static_cast<unsigned int>(ex.code()));
        return false;
    }
}

std::vector<Theme> ScanFolder(const std::wstring& dir, const std::vector<Theme>& builtIns) {
    std::vector<Theme> found;
    if (dir.empty()) return found;

    WIN32_FIND_DATAW data{};
    HANDLE handle = FindFirstFileW((dir + L"\\*.json").c_str(), &data);
    if (handle == INVALID_HANDLE_VALUE) return found;
    do {
        if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        std::wstring stem = data.cFileName;
        stem.resize(stem.size() - 5);
        if (stem.empty()) continue;
        Theme theme;
        if (ReadThemeFile(dir + L"\\" + data.cFileName, stem, builtIns, theme)) found.push_back(std::move(theme));
    } while (FindNextFileW(handle, &data));
    FindClose(handle);

    std::sort(found.begin(), found.end(),
        [](const Theme& a, const Theme& b) { return ToLowerW(a.name) < ToLowerW(b.name); });
    return found;
}

void LoadLocked(const std::wstring& dir) {
    std::vector<Theme> all = BuiltIns();
    if (!dir.empty()) EnsureDirectoryTree(dir);
    std::vector<Theme> custom = ScanFolder(dir, all);
    all.insert(all.end(), custom.begin(), custom.end());

    std::wstring want = g_loaded && g_current < g_themes.size() ? g_themes[g_current].id : std::wstring();
    if (want.empty()) {
        const std::wstring path = SavedPath();
        if (!path.empty() && ReadTextFile(path, want)) want = TrimWhitespace(want);
    }

    g_themes = std::move(all);
    g_current = 0;
    for (size_t i = 0; i < g_themes.size(); ++i) {
        if (SameId(g_themes[i].id, want)) g_current = i;
    }
    if (!g_loaded) {
        WriteLogF(L"theme %s, %zu custom", g_themes[g_current].id.c_str(), custom.size());
    }
    g_loaded = true;
}

void EnsureLoadedLocked() {
    if (!g_loaded) LoadLocked(Folder());
}

}

std::wstring Folder() {
    const std::wstring dir = GetLocalStateDir();
    return dir.empty() ? dir : dir + L"\\themes";
}

void Reload() {
    Reload(Folder());
}

void Reload(const std::wstring& folder) {
    std::lock_guard<std::mutex> lock(g_mutex);
    LoadLocked(folder);
}

std::vector<Theme> Themes() {
    std::lock_guard<std::mutex> lock(g_mutex);
    EnsureLoadedLocked();
    return g_themes;
}

Palette Current() {
    std::lock_guard<std::mutex> lock(g_mutex);
    EnsureLoadedLocked();
    return g_themes[g_current].palette;
}

std::wstring CurrentId() {
    std::lock_guard<std::mutex> lock(g_mutex);
    EnsureLoadedLocked();
    return g_themes[g_current].id;
}

std::wstring CurrentName() {
    std::lock_guard<std::mutex> lock(g_mutex);
    EnsureLoadedLocked();
    const Theme& theme = g_themes[g_current];
    return theme.custom ? theme.name : std::wstring(i18n::Tr(theme.name.c_str()));
}

bool SetTheme(const std::wstring& id) {
    std::wstring chosen;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        EnsureLoadedLocked();
        bool found = false;
        for (size_t i = 0; i < g_themes.size(); ++i) {
            if (SameId(g_themes[i].id, id)) {
                g_current = i;
                found = true;
            }
        }
        if (!found) return false;
        chosen = g_themes[g_current].id;
    }

    const std::wstring path = SavedPath();
    if (path.empty() || !WriteTextFile(path, chosen + L"\n")) {
        WriteLog(L"the theme choice could not be saved");
        return false;
    }
    WriteLogF(L"theme set to %s", chosen.c_str());
    return true;
}

}
