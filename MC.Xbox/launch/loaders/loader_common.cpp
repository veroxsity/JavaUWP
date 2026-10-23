#include "loader_common.h"

#include "launcher_common.h"

#include <algorithm>
#include <cstring>
#include <sstream>

#include "third_party/miniz/miniz.h"

std::wstring fwd(const std::wstring& path) {
    std::wstring r = path;
    for (auto& c : r) {
        if (c == L'\\') c = L'/';
    }
    return r;
}

LoaderId ParseLoaderId(const std::wstring& loader) {
    if (_wcsicmp(loader.c_str(), L"fabric") == 0) return LoaderId::Fabric;
    if (_wcsicmp(loader.c_str(), L"neoforge") == 0) return LoaderId::NeoForge;
    if (_wcsicmp(loader.c_str(), L"forge") == 0) return LoaderId::Forge;
    return LoaderId::Unknown;
}

bool IsLoader(const std::wstring& loader, LoaderId id) {
    return ParseLoaderId(loader) == id;
}

std::wstring FirstArgValue(const std::vector<std::wstring>& args, const std::wstring& name) {
    for (size_t i = 0; i + 1 < args.size(); ++i) {
        if (args[i] == name) return args[i + 1];
    }
    return {};
}

std::wstring MavenPath(
    const std::wstring& group,
    const std::wstring& artifact,
    const std::wstring& version,
    const std::wstring& classifier,
    const std::wstring& extension) {
    std::wstring groupPath = group;
    std::replace(groupPath.begin(), groupPath.end(), L'.', L'\\');
    return groupPath + L"\\" + artifact + L"\\" + version + L"\\" +
        artifact + L"-" + version + (classifier.empty() ? L"" : (L"-" + classifier)) + L"." + extension;
}

bool ExtractZipEntryToFile(const std::wstring& zipPath, const char* entryName, const std::wstring& outputPath) {
    std::vector<unsigned char> zipBytes;
    if (!ReadBinaryFileLimited(zipPath, zipBytes, 256ull * 1024ull * 1024ull)) {
        WriteLogF(L"Could not read zip for extraction: %s", zipPath.c_str());
        return false;
    }

    mz_zip_archive zip{};
    if (!mz_zip_reader_init_mem(&zip, zipBytes.data(), zipBytes.size(), 0)) {
        WriteLogF(L"Could not open zip for extraction: %s", zipPath.c_str());
        return false;
    }

    const int idx = mz_zip_reader_locate_file(&zip, entryName, nullptr, 0);
    if (idx < 0) {
        mz_zip_reader_end(&zip);
        WriteLogF(L"Zip entry not found: %s in %s", a2w(entryName).c_str(), zipPath.c_str());
        return false;
    }

    size_t outSize = 0;
    void* p = mz_zip_reader_extract_to_heap(&zip, static_cast<mz_uint>(idx), &outSize, 0);
    mz_zip_reader_end(&zip);
    if (!p) {
        WriteLogF(L"Could not extract zip entry: %s", a2w(entryName).c_str());
        return false;
    }

    const bool ok = WriteAllBytes(outputPath, p, outSize);
    mz_free(p);
    if (!ok) {
        WriteLogF(L"Could not write extracted zip entry: %s", outputPath.c_str());
    }
    return ok;
}

bool FileExistsNonEmpty(const std::wstring& path) {
    WIN32_FILE_ATTRIBUTE_DATA data = {};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data)) return false;
    if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) return false;
    return data.nFileSizeHigh != 0 || data.nFileSizeLow != 0;
}

bool EndsWithAscii(const char* text, const char* suffix) {
    const size_t textLen = strlen(text);
    const size_t suffixLen = strlen(suffix);
    return textLen >= suffixLen && strcmp(text + textLen - suffixLen, suffix) == 0;
}

bool ZipIsValid(const std::wstring& zipPath) {
    std::vector<unsigned char> zipBytes;
    if (!ReadBinaryFileLimited(zipPath, zipBytes, 256ull * 1024ull * 1024ull)) return false;
    mz_zip_archive zip{};
    if (!mz_zip_reader_init_mem(&zip, zipBytes.data(), zipBytes.size(), 0)) return false;
    const mz_uint count = mz_zip_reader_get_num_files(&zip);
    mz_zip_reader_end(&zip);
    return count > 0;
}

bool EnsureFmlConfig(const std::wstring& gameDir) {
    const std::wstring configPath = gameDir + L"\\config\\fml.toml";
    std::wstring body;
    if (GetFileAttributesW(configPath.c_str()) != INVALID_FILE_ATTRIBUTES && !ReadTextFile(configPath, body)) return false;
    std::wstringstream in(body);
    std::wstring line, out;
    bool found = false;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == L'\r') line.pop_back();
        const auto equals = line.find(L'=');
        if (equals != std::wstring::npos && TrimWhitespace(line.substr(0, equals)) == L"earlyWindowControl") {
            out += L"earlyWindowControl = false\n";
            found = true;
        } else {
            out += line + L"\n";
        }
    }
    if (!found) out += L"earlyWindowControl = false\n";
    if (WriteTextFile(configPath, out)) return true;
    WriteLogF(L"Failed to write FML config %s err=%u", configPath.c_str(), GetLastError());
    return false;
}
