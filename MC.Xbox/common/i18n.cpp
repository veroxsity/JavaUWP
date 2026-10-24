#include "i18n.h"

#include "launcher_common.h"

#include <windows.h>

#include <winrt/base.h>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>

#include <algorithm>
#include <atomic>
#include <memory>
#include <mutex>
#include <unordered_map>

namespace i18n {
namespace {

using Table = std::unordered_map<std::wstring, std::wstring>;

struct Entry {
    Language language;
    Table table;
};

std::mutex g_mutex;
// kept for the life of the process so a pointer handed out by Tr never goes stale
std::vector<std::unique_ptr<Entry>> g_entries;
std::atomic<const Table*> g_current{ nullptr };
std::wstring g_currentCode = L"en";
bool g_initialised = false;

std::wstring LangDir(const std::wstring& root) {
    return root.empty() ? root : root + L"\\lang";
}

std::wstring SavedPath() {
    const std::wstring localDir = GetLocalStateDir();
    return localDir.empty() ? localDir : localDir + L"\\language.txt";
}

bool SameCode(const std::wstring& a, const std::wstring& b) {
    return ToLowerW(a) == ToLowerW(b);
}

Entry* Find(const std::wstring& code) {
    for (const auto& entry : g_entries) {
        if (SameCode(entry->language.code, code)) return entry.get();
    }
    return nullptr;
}

bool ReadLanguageFile(const std::wstring& path, const std::wstring& code, Entry& out) {
    std::wstring text;
    if (!ReadTextFile(path, text)) return false;
    if (!text.empty() && text.front() == 0xFEFF) text.erase(0, 1);

    try {
        using namespace winrt::Windows::Data::Json;
        JsonObject root;
        if (!JsonObject::TryParse(winrt::hstring(text), root)) {
            WriteLogF(L"language file %s is not valid json", path.c_str());
            return false;
        }

        out.language.code = code;
        out.language.name = code;
        for (const auto& pair : root) {
            const IJsonValue value = pair.Value();
            if (value.ValueType() != JsonValueType::String) continue;

            const std::wstring key(pair.Key().c_str());
            const std::wstring translated(value.GetString().c_str());
            if (key == L"_name") {
                if (!translated.empty()) out.language.name = translated;
            } else if (!key.empty() && key.front() != L'_' && !translated.empty()) {
                out.table[key] = translated;
            }
        }
        return true;
    } catch (const winrt::hresult_error& ex) {
        WriteLogF(L"language file %s could not be read hr=0x%08X", path.c_str(), static_cast<unsigned int>(ex.code()));
        return false;
    }
}

void ScanDirectory(const std::wstring& dir) {
    if (dir.empty()) return;

    WIN32_FIND_DATAW data{};
    HANDLE handle = FindFirstFileW((dir + L"\\*.json").c_str(), &data);
    if (handle == INVALID_HANDLE_VALUE) return;
    do {
        if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;

        std::wstring code = data.cFileName;
        code.resize(code.size() - 5);
        // en.json only lists the English text for translators to copy
        if (code.empty() || SameCode(code, L"en")) continue;

        auto entry = std::make_unique<Entry>();
        if (!ReadLanguageFile(dir + L"\\" + data.cFileName, code, *entry)) continue;

        // a file in LocalState replaces the one in the package, so a translation can be tried without a rebuild
        g_entries.erase(std::remove_if(g_entries.begin(), g_entries.end(),
            [&](const std::unique_ptr<Entry>& existing) { return SameCode(existing->language.code, code); }),
            g_entries.end());
        g_entries.push_back(std::move(entry));
    } while (FindNextFileW(handle, &data));
    FindClose(handle);
}

std::wstring ChooseCode() {
    std::wstring saved;
    const std::wstring path = SavedPath();
    if (!path.empty() && ReadTextFile(path, saved)) {
        saved = TrimWhitespace(saved);
        if (Find(saved)) return saved;
    }

    wchar_t locale[LOCALE_NAME_MAX_LENGTH] = {};
    if (!GetUserDefaultLocaleName(locale, LOCALE_NAME_MAX_LENGTH)) return L"en";

    const std::wstring full = locale;
    if (Find(full)) return full;
    const size_t dash = full.find(L'-');
    if (dash != std::wstring::npos && Find(full.substr(0, dash))) return full.substr(0, dash);
    return L"en";
}

void Select(const std::wstring& code) {
    const Entry* entry = Find(code);
    g_currentCode = entry->language.code;
    g_current.store(&entry->table);
}

}

void Init() {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_initialised) return;
    g_initialised = true;

    auto english = std::make_unique<Entry>();
    english->language = { L"en", L"English" };
    g_entries.push_back(std::move(english));

    const std::wstring localDir = GetLocalStateDir();
    // the folder is made so it shows up in Remote Files for dropping a language file into
    if (!localDir.empty()) EnsureDirectoryTree(LangDir(localDir));
    ScanDirectory(LangDir(GetExecutableDir()));
    ScanDirectory(LangDir(localDir));

    Select(ChooseCode());
    WriteLogF(L"language %s, %zu available", g_currentCode.c_str(), g_entries.size());
}

std::vector<Language> Languages() {
    std::lock_guard<std::mutex> lock(g_mutex);
    std::vector<Language> languages;
    for (size_t i = 1; i < g_entries.size(); ++i) languages.push_back(g_entries[i]->language);
    std::sort(languages.begin(), languages.end(),
        [](const Language& a, const Language& b) { return ToLowerW(a.name) < ToLowerW(b.name); });
    languages.insert(languages.begin(), g_entries.empty() ? Language{ L"en", L"English" } : g_entries[0]->language);
    return languages;
}

std::wstring CurrentCode() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_currentCode;
}

bool Active() {
    const Table* table = g_current.load();
    return table && !table->empty();
}

bool SetLanguage(const std::wstring& code) {
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (!Find(code)) return false;
        Select(code);
    }

    const std::wstring path = SavedPath();
    if (path.empty() || !WriteTextFile(path, CurrentCode() + L"\n")) {
        WriteLog(L"the language choice could not be saved");
        return false;
    }
    WriteLogF(L"language set to %s", CurrentCode().c_str());
    return true;
}

const wchar_t* Tr(const wchar_t* english) {
    const Table* table = g_current.load();
    if (!table || table->empty()) return english;

    const auto found = table->find(english);
    return found == table->end() ? english : found->second.c_str();
}

std::string TrU(const char* english) {
    std::string translated;
    return TryTrU(english, translated) ? translated : std::string(english);
}

std::string TrFormatU(const char* english, std::initializer_list<std::string> args) {
    std::vector<std::wstring> wide;
    for (const std::string& arg : args) wide.push_back(a2w(arg.c_str()));
    const std::wstring key = a2w(english);
    std::wstring text = Tr(key.c_str());
    for (size_t i = 0; i < wide.size(); ++i) {
        const std::wstring token = L"{" + std::to_wstring(i) + L"}";
        for (size_t at = text.find(token); at != std::wstring::npos; at = text.find(token, at + wide[i].size())) {
            text.replace(at, token.size(), wide[i]);
        }
    }
    return w2a(text);
}

bool TryTrU(const std::string& english, std::string& translated) {
    const Table* table = g_current.load();
    if (!table || table->empty()) return false;

    const auto found = table->find(a2w(english.c_str()));
    if (found == table->end()) return false;
    translated = w2a(found->second);
    return true;
}

std::string TableJson() {
    // quotes a string for JSON, and keeps a translation from closing the script tag it is written into
    const auto quote = [](const std::wstring& text) {
        std::string out = "\"";
        for (const char ch : w2a(text)) {
            switch (ch) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': break;
            case '\t': out += "\\t"; break;
            case '<': out += "\\u003c"; break;
            default: out += ch;
            }
        }
        return out + "\"";
    };

    std::string json = "{";
    if (const Table* table = g_current.load()) {
        for (const auto& pair : *table) {
            if (json.size() > 1) json += ",";
            json += quote(pair.first) + ":" + quote(pair.second);
        }
    }
    return json + "}";
}
std::wstring TrFormat(const wchar_t* english, std::initializer_list<std::wstring> args) {
    std::wstring text = Tr(english);
    size_t index = 0;
    for (const std::wstring& arg : args) {
        const std::wstring token = L"{" + std::to_wstring(index++) + L"}";
        for (size_t at = text.find(token); at != std::wstring::npos; at = text.find(token, at + arg.size())) {
            text.replace(at, token.size(), arg);
        }
    }
    return text;
}

}
