#include "manual_downloads.h"

#include "launcher_common.h"
#include "profiles.h"

namespace {

std::wstring ManualDir() {
    const std::wstring localDir = GetLocalStateDir();
    if (localDir.empty()) return std::wstring();
    return localDir + L"\\mod-source\\manual";
}

std::wstring ManualPath(const std::wstring& profileId) {
    const std::wstring dir = ManualDir();
    if (dir.empty()) return std::wstring();
    return dir + L"\\" + SafeFileName(profileId) + L".tsv";
}

std::wstring EscapeField(const std::wstring& value) {
    std::wstring out;
    out.reserve(value.size());
    for (const wchar_t ch : value) {
        if (ch == L'\t' || ch == L'\r' || ch == L'\n') out += L' ';
        else out += ch;
    }
    return out;
}

// ends up in a path, so only the known folders are trusted
bool IsKnownFolder(const std::wstring& folder) {
    return folder == L"mods" || folder == L"resourcepacks" || folder == L"shaderpacks";
}

std::vector<std::wstring> SplitTabs(const std::wstring& line) {
    std::vector<std::wstring> fields;
    size_t start = 0;
    while (true) {
        const size_t tab = line.find(L'\t', start);
        fields.push_back(line.substr(start, tab == std::wstring::npos ? std::wstring::npos : tab - start));
        if (tab == std::wstring::npos) break;
        start = tab + 1;
    }
    return fields;
}

std::vector<ManualDownload> ReadAll(const std::wstring& profileId) {
    std::vector<ManualDownload> items;
    const std::wstring path = ManualPath(profileId);
    if (path.empty()) return items;

    std::wstring text;
    if (!ReadTextFile(path, text)) return items;

    size_t pos = 0;
    while (pos <= text.size()) {
        const size_t eol = text.find(L'\n', pos);
        std::wstring line = text.substr(pos, eol == std::wstring::npos ? std::wstring::npos : eol - pos);
        if (!line.empty() && line.back() == L'\r') line.pop_back();
        // lists from before the folder column have three fields and are all mods
        const std::vector<std::wstring> fields = SplitTabs(line);
        if (fields.size() >= 3 && !fields[0].empty()) {
            ManualDownload item;
            item.fileName = fields[0];
            item.modName = fields[1];
            item.url = fields[2];
            if (fields.size() >= 4 && IsKnownFolder(fields[3])) item.folder = fields[3];
            items.push_back(item);
        }
        if (eol == std::wstring::npos) break;
        pos = eol + 1;
    }
    return items;
}

bool WriteAll(const std::wstring& profileId, const std::vector<ManualDownload>& items) {
    const std::wstring path = ManualPath(profileId);
    if (path.empty()) return false;
    if (items.empty()) {
        return DeleteFileW(path.c_str()) || GetLastError() == ERROR_FILE_NOT_FOUND;
    }
    if (!EnsureDirectoryTree(ManualDir())) return false;

    std::wstring text;
    for (const ManualDownload& item : items) {
        text += EscapeField(item.fileName) + L"\t" + EscapeField(item.modName) + L"\t" +
            EscapeField(item.url) + L"\t" + EscapeField(item.folder) + L"\n";
    }
    return WriteTextFile(path, text);
}

}

bool RecordManualDownloads(const std::wstring& profileId, const std::vector<ManualDownload>& items) {
    if (items.empty()) return true;

    std::vector<ManualDownload> merged = ReadAll(profileId);
    for (const ManualDownload& item : items) {
        bool known = false;
        for (const ManualDownload& existing : merged) {
            if (existing.folder == item.folder && ToLowerW(existing.fileName) == ToLowerW(item.fileName)) {
                known = true;
                break;
            }
        }
        if (!known) merged.push_back(item);
    }
    WriteLogF(L"Manual download list for %s now holds %zu entries", profileId.c_str(), merged.size());
    return WriteAll(profileId, merged);
}

std::vector<ManualDownload> PendingManualDownloads(const std::wstring& runtimeRoot, const std::wstring& profileId) {
    std::vector<ManualDownload> items = ReadAll(profileId);
    if (items.empty()) return items;

    const std::wstring gameDir = ProfileGameDir(runtimeRoot, profileId);
    std::vector<ManualDownload> pending;
    for (const ManualDownload& item : items) {
        const std::wstring file = gameDir + L"\\" + item.folder + L"\\" + SafeFileName(item.fileName);
        if (GetFileAttributesW(file.c_str()) != INVALID_FILE_ATTRIBUTES) continue;
        pending.push_back(item);
    }

    // the file is the source of truth, so drop anything that has since been uploaded
    if (pending.size() != items.size()) WriteAll(profileId, pending);
    return pending;
}

bool ClearManualDownloads(const std::wstring& profileId) {
    return WriteAll(profileId, {});
}
