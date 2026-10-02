#include "mod_source.h"

#include "launcher_common.h"

#include <mutex>

namespace modsource {

namespace {

std::mutex g_mutex;
bool g_loaded = false;
ModSource g_source = ModSource::Modrinth;
std::string g_key;

std::wstring SettingsDir() {
    const std::wstring localDir = GetLocalStateDir();
    if (localDir.empty()) return std::wstring();
    return localDir + L"\\mod-source";
}

std::wstring SourcePath() {
    const std::wstring dir = SettingsDir();
    return dir.empty() ? std::wstring() : dir + L"\\source.txt";
}

std::wstring KeyPath() {
    const std::wstring dir = SettingsDir();
    return dir.empty() ? std::wstring() : dir + L"\\curseforge-key.txt";
}

void LoadLocked() {
    if (g_loaded) return;
    g_loaded = true;

    std::wstring text;
    const std::wstring sourcePath = SourcePath();
    if (!sourcePath.empty() && ReadTextFile(sourcePath, text)) {
        if (ToLowerW(TrimWhitespace(StripNewlines(text))) == L"curseforge") {
            g_source = ModSource::CurseForge;
        }
    }

    std::wstring keyText;
    const std::wstring keyPath = KeyPath();
    if (!keyPath.empty() && ReadTextFile(keyPath, keyText)) {
        g_key = w2a(TrimWhitespace(StripNewlines(keyText)));
    }
}

}

ModSource Current() {
    std::lock_guard<std::mutex> lk(g_mutex);
    LoadLocked();
    return g_source;
}

bool SetCurrent(ModSource source) {
    const std::wstring path = SourcePath();
    if (path.empty()) return false;

    const wchar_t* value = source == ModSource::CurseForge ? L"curseforge" : L"modrinth";
    if (!EnsureDirectoryTree(SettingsDir()) || !WriteTextFile(path, std::wstring(value) + L"\n")) {
        WriteLogF(L"mod source could not be saved as %s", value);
        return false;
    }

    {
        std::lock_guard<std::mutex> lk(g_mutex);
        LoadLocked();
        g_source = source;
    }
    WriteLogF(L"mod source set to %s", value);
    return true;
}

const wchar_t* DisplayName(ModSource source) {
    return source == ModSource::CurseForge ? L"CurseForge" : L"Modrinth";
}

std::string CurseForgeKey() {
    std::lock_guard<std::mutex> lk(g_mutex);
    LoadLocked();
    return g_key;
}

bool HasCurseForgeKey() {
    return !CurseForgeKey().empty();
}

bool SetCurseForgeKey(const std::string& key) {
    const std::wstring path = KeyPath();
    if (path.empty()) return false;

    const std::wstring trimmed = TrimWhitespace(StripNewlines(a2w(key.c_str())));
    if (trimmed.empty()) {
        const bool removed = DeleteFileW(path.c_str()) || GetLastError() == ERROR_FILE_NOT_FOUND;
        if (!removed) {
            WriteLogF(L"curseforge key could not be cleared error=%lu", GetLastError());
            return false;
        }
        std::lock_guard<std::mutex> lk(g_mutex);
        LoadLocked();
        g_key.clear();
        WriteLog(L"curseforge key cleared");
        return true;
    }

    if (!EnsureDirectoryTree(SettingsDir()) || !WriteTextFile(path, trimmed + L"\n")) {
        WriteLog(L"curseforge key could not be saved");
        return false;
    }

    {
        std::lock_guard<std::mutex> lk(g_mutex);
        LoadLocked();
        g_key = w2a(trimmed);
    }
    WriteLog(L"curseforge key saved");
    return true;
}

std::wstring CurseForgeKeyHint() {
    const std::string key = CurseForgeKey();
    if (key.empty()) return std::wstring();
    const std::string tail = key.size() <= 4 ? key : key.substr(key.size() - 4);
    return L"ends " + a2w(tail.c_str());
}

std::wstring QualifyProjectId(ModSource source, const std::wstring& projectId) {
    if (projectId.empty()) return projectId;
    if (source != ModSource::CurseForge) return projectId;
    if (projectId.compare(0, 3, L"cf:") == 0) return projectId;
    return L"cf:" + projectId;
}

}
