#include "launcher_common.h"

#include <cstdarg>
#include <algorithm>
#include <cctype>
#include <cwctype>
#include <fstream>
#include <io.h>
#include <fcntl.h>
#include <share.h>
#include <vector>

#include <roapi.h>
#include <wrl.h>
#include <wrl/wrappers/corewrappers.h>
#include <windows.foundation.h>
#include <windows.storage.h>

#include "third_party/miniz/miniz.h"

using Microsoft::WRL::ComPtr;
using Microsoft::WRL::Wrappers::HStringReference;
using namespace ABI::Windows::Storage;

std::wstring g_logDir;

std::wstring GetExecutableDir() {
    wchar_t buf[MAX_PATH];
    const DWORD len = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) return std::wstring();

    wchar_t* sl = wcsrchr(buf, L'\\');
    if (sl) *sl = L'\0';
    return std::wstring(buf);
}

static std::wstring HStringToWString(HSTRING value) {
    UINT32 len = 0;
    const wchar_t* raw = WindowsGetStringRawBuffer(value, &len);
    return raw ? std::wstring(raw, len) : std::wstring();
}

std::wstring GetLocalStateDir() {
    ComPtr<IApplicationDataStatics> appDataStatics;
    HRESULT hr = RoGetActivationFactory(
        HStringReference(RuntimeClass_Windows_Storage_ApplicationData).Get(),
        IID_PPV_ARGS(&appDataStatics));
    if (FAILED(hr)) {
        WriteLogF(L"ApplicationData activation failed hr=0x%08X", hr);
        return std::wstring();
    }

    ComPtr<IApplicationData> appData;
    hr = appDataStatics->get_Current(appData.GetAddressOf());
    if (FAILED(hr)) {
        WriteLogF(L"ApplicationData.Current failed hr=0x%08X", hr);
        return std::wstring();
    }

    ComPtr<IStorageFolder> localFolder;
    hr = appData->get_LocalFolder(localFolder.GetAddressOf());
    if (FAILED(hr)) {
        WriteLogF(L"ApplicationData.LocalFolder failed hr=0x%08X", hr);
        return std::wstring();
    }

    ComPtr<IStorageItem> localItem;
    hr = localFolder.As(&localItem);
    if (FAILED(hr)) {
        WriteLogF(L"LocalFolder As(IStorageItem) failed hr=0x%08X", hr);
        return std::wstring();
    }

    HSTRING path = nullptr;
    hr = localItem->get_Path(&path);
    if (FAILED(hr)) {
        WriteLogF(L"LocalFolder.Path failed hr=0x%08X", hr);
        return std::wstring();
    }

    std::wstring result = HStringToWString(path);
    WindowsDeleteString(path);
    return result;
}

std::wstring GetEnvVarString(const wchar_t* name) {
    if (!name || !*name) return std::wstring();

    wchar_t buffer[32768];
    const DWORD len = GetEnvironmentVariableW(
        name,
        buffer,
        static_cast<DWORD>(sizeof(buffer) / sizeof(buffer[0])));
    if (len == 0 || len >= sizeof(buffer) / sizeof(buffer[0])) {
        return std::wstring();
    }
    return std::wstring(buffer, len);
}

void WriteLog(const wchar_t* msg) {
    if (g_logDir.empty()) {
        g_logDir = GetExecutableDir();
    }
    if (g_logDir.empty()) return;

    EnsureDirectoryTree(g_logDir);

    wchar_t path[MAX_PATH];
    swprintf_s(path, L"%s\\mc_launch.log", g_logDir.c_str());
    FILE* f = nullptr;
    _wfopen_s(&f, path, L"a");
    if (f) {
        SYSTEMTIME st;
        GetLocalTime(&st);
        fwprintf(f, L"[%02d:%02d:%02d.%03d] %s\n",
            st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, msg);
        fclose(f);
    }
}

void WriteLogF(const wchar_t* fmt, ...) {
    va_list args;
    va_start(args, fmt);

    va_list sizeArgs;
    va_copy(sizeArgs, args);
    const int needed = _vscwprintf(fmt, sizeArgs);
    va_end(sizeArgs);

    if (needed <= 0) {
        va_end(args);
        WriteLog(L"WriteLogF: failed to format message");
        return;
    }

    std::vector<wchar_t> buf(static_cast<size_t>(needed) + 1);
    vswprintf_s(buf.data(), buf.size(), fmt, args);
    va_end(args);
    WriteLog(buf.data());
}

std::string w2a(const std::wstring& w) {
    if (w.empty()) return {};
    int sz = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string s(sz, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, &s[0], sz, nullptr, nullptr);
    if (!s.empty() && s.back() == 0) s.pop_back();
    return s;
}

std::wstring a2w(const char* utf8) {
    if (!utf8 || !*utf8) return {};
    int sz = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, nullptr, 0);
    if (sz <= 0) return {};

    std::wstring w(sz, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8, -1, &w[0], sz);
    if (!w.empty() && w.back() == L'\0') w.pop_back();
    return w;
}

bool EnsureDirectoryTree(const std::wstring& path) {
    if (path.empty()) return false;
    if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES) return true;

    std::wstring current;
    size_t start = 0;
    if (path.size() >= 2 && path[1] == L':') {
        current = path.substr(0, 2);
        start = 2;
    }

    while (start < path.size()) {
        size_t next = path.find(L'\\', start);
        std::wstring part = path.substr(
            start,
            next == std::wstring::npos ? path.size() - start : next - start);
        if (!part.empty()) {
            if (!current.empty() && current.back() != L'\\') current += L'\\';
            current += part;
            if (GetFileAttributesW(current.c_str()) == INVALID_FILE_ATTRIBUTES) {
                if (!CreateDirectoryW(current.c_str(), nullptr) &&
                    GetLastError() != ERROR_ALREADY_EXISTS) {
                    return false;
                }
            }
        }
        if (next == std::wstring::npos) break;
        start = next + 1;
    }

    return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

bool DirectoryExists(const std::wstring& path) {
    const DWORD attrs = GetFileAttributesW(path.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY);
}

std::wstring GetParentDir(const std::wstring& path) {
    const size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? std::wstring() : path.substr(0, slash);
}

std::wstring GetFileName(const std::wstring& path) {
    const size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? path : path.substr(slash + 1);
}

std::wstring FileStamp(const std::wstring& path) {
    WIN32_FILE_ATTRIBUTE_DATA data = {};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data)) {
        return L"missing";
    }

    wchar_t stamp[96] = {};
    swprintf_s(stamp, L"%08X%08X:%08X%08X",
        data.ftLastWriteTime.dwHighDateTime,
        data.ftLastWriteTime.dwLowDateTime,
        data.nFileSizeHigh,
        data.nFileSizeLow);
    return stamp;
}

bool ReadTextFile(const std::wstring& path, std::wstring& out) {
    int fd = -1;
    if (_wsopen_s(&fd, path.c_str(), _O_RDONLY | _O_BINARY, _SH_DENYNO, _S_IREAD) != 0 || fd < 0) {
        return false;
    }

    std::string bytes;
    char buffer[4096];
    while (true) {
        const int read = _read(fd, buffer, sizeof(buffer));
        if (read > 0) bytes.append(buffer, read);
        if (read < static_cast<int>(sizeof(buffer))) break;
    }
    _close(fd);

    out = a2w(bytes.c_str());
    return true;
}

bool WriteTextFile(const std::wstring& path, const std::wstring& value) {
    EnsureDirectoryTree(GetParentDir(path));
    SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_NORMAL);
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"wb") != 0 || !f) return false;

    const std::string bytes = w2a(value);
    const bool ok = bytes.empty() || fwrite(bytes.data(), 1, bytes.size(), f) == bytes.size();
    fclose(f);
    return ok;
}

bool ReadBinaryFileLimited(
    const std::wstring& path,
    std::vector<unsigned char>& out,
    unsigned long long maxBytes) {
    int fd = -1;
    errno_t openErr = _wsopen_s(&fd, path.c_str(), _O_RDONLY | _O_BINARY, _SH_DENYNO, _S_IREAD);
    if (openErr != 0 || fd < 0) return false;

    const __int64 size = _filelengthi64(fd);
    if (size < 0 || static_cast<unsigned long long>(size) > maxBytes) {
        _close(fd);
        return false;
    }

    out.resize(static_cast<size_t>(size));
    size_t offset = 0;
    while (offset < out.size()) {
        const unsigned remaining = static_cast<unsigned>(std::min<size_t>(out.size() - offset, 1024u * 1024u));
        const int read = _read(fd, out.data() + offset, remaining);
        if (read <= 0) {
            _close(fd);
            out.clear();
            return false;
        }
        offset += static_cast<size_t>(read);
    }

    _close(fd);
    return true;
}

std::wstring TrimWhitespace(std::wstring value) {
    while (!value.empty() && iswspace(value.front())) value.erase(value.begin());
    while (!value.empty() && iswspace(value.back())) value.pop_back();
    return value;
}

std::wstring ToLowerW(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t c) { return static_cast<wchar_t>(towlower(c)); });
    return value;
}

std::string ToLowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(tolower(c)); });
    return value;
}

bool WriteAllBytes(const std::wstring& path, const void* data, size_t size) {
    EnsureDirectoryTree(GetParentDir(path));
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    if (size) f.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size));
    return f.good();
}

static std::vector<int> ParseVersionNumbers(const std::string& value) {
    std::vector<int> parts;
    size_t i = 0;
    while (i < value.size()) {
        while (i < value.size() && (value[i] < '0' || value[i] > '9')) ++i;
        if (i >= value.size()) break;
        int n = 0;
        while (i < value.size() && value[i] >= '0' && value[i] <= '9') {
            n = (n * 10) + (value[i] - '0');
            ++i;
        }
        parts.push_back(n);
    }
    return parts;
}

int CompareVersionNumbers(const std::string& lhs, const std::string& rhs) {
    std::vector<int> a = ParseVersionNumbers(lhs);
    std::vector<int> b = ParseVersionNumbers(rhs);
    const size_t n = (std::max)(a.size(), b.size());
    for (size_t i = 0; i < n; ++i) {
        const int av = i < a.size() ? a[i] : 0;
        const int bv = i < b.size() ? b[i] : 0;
        if (av < bv) return -1;
        if (av > bv) return 1;
    }
    return 0;
}

bool ReadZipTextFile(const std::wstring& zipPath, const char* entryName, std::wstring& out) {
    out.clear();
    std::ifstream in(zipPath, std::ios::binary);
    if (!in) return false;
    std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (bytes.empty()) return false;

    mz_zip_archive zip{};
    if (!mz_zip_reader_init_mem(&zip, bytes.data(), bytes.size(), 0)) return false;
    const int idx = mz_zip_reader_locate_file(&zip, entryName, nullptr, 0);
    if (idx < 0) {
        mz_zip_reader_end(&zip);
        return false;
    }

    size_t outSize = 0;
    void* p = mz_zip_reader_extract_to_heap(&zip, static_cast<mz_uint>(idx), &outSize, 0);
    mz_zip_reader_end(&zip);
    if (!p) return false;

    std::string text(static_cast<const char*>(p), static_cast<const char*>(p) + outSize);
    mz_free(p);
    out = a2w(text.c_str());
    return true;
}

std::wstring AppStateDir(const std::wstring& runtimeRoot) {
    return runtimeRoot + L"\\state";
}

std::wstring LogsCurrentDir(const std::wstring& runtimeRoot) {
    return runtimeRoot + L"\\logs\\current";
}

std::wstring LogsPreviousDir(const std::wstring& runtimeRoot) {
    return runtimeRoot + L"\\logs_previous";
}

std::wstring CrashLaunchMarkerPath(const std::wstring& runtimeRoot) {
    return AppStateDir(runtimeRoot) + L"\\minecraft_launch_active.txt";
}

std::wstring CrashReportsDir(const std::wstring& runtimeRoot) {
    return runtimeRoot + L"\\crash-reports";
}

std::wstring CrashTimestampForFileName() {
    SYSTEMTIME st = {};
    GetLocalTime(&st);
    wchar_t stamp[32] = {};
    swprintf_s(stamp, L"%04u%02u%02u-%02u%02u%02u",
        st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    return stamp;
}

bool DeleteDirectoryTree(const std::wstring& path) {
    if (path.empty() || path.size() < 4) return false;

    const DWORD attrs = GetFileAttributesW(path.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES) return true;
    if ((attrs & FILE_ATTRIBUTE_DIRECTORY) == 0) {
        SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_NORMAL);
        return DeleteFileW(path.c_str()) || GetLastError() == ERROR_FILE_NOT_FOUND;
    }

    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((path + L"\\*").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
            const std::wstring child = path + L"\\" + fd.cFileName;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                DeleteDirectoryTree(child);
            } else {
                SetFileAttributesW(child.c_str(), FILE_ATTRIBUTE_NORMAL);
                DeleteFileW(child.c_str());
            }
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }

    SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_NORMAL);
    return RemoveDirectoryW(path.c_str()) || GetLastError() == ERROR_FILE_NOT_FOUND;
}

bool MovePathIfExists(const std::wstring& source, const std::wstring& dest, bool replaceExisting) {
    if (source.empty() || dest.empty()) return false;
    if (GetFileAttributesW(source.c_str()) == INVALID_FILE_ATTRIBUTES) return false;
    EnsureDirectoryTree(GetParentDir(dest));
    const DWORD flags = replaceExisting ? MOVEFILE_REPLACE_EXISTING : 0;
    return MoveFileExW(source.c_str(), dest.c_str(), flags) != FALSE;
}

bool CopyDirectoryTree(const std::wstring& source, const std::wstring& dest) {
    if (source.empty() || dest.empty()) return false;
    const DWORD attrs = GetFileAttributesW(source.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES || (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0) return false;

    EnsureDirectoryTree(dest);
    WIN32_FIND_DATAW fd = {};
    HANDLE h = FindFirstFileW((source + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return true;

    bool ok = true;
    do {
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
        const std::wstring childSource = source + L"\\" + fd.cFileName;
        const std::wstring childDest = dest + L"\\" + fd.cFileName;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            ok = CopyDirectoryTree(childSource, childDest) && ok;
        } else {
            EnsureDirectoryTree(GetParentDir(childDest));
            SetFileAttributesW(childDest.c_str(), FILE_ATTRIBUTE_NORMAL);
            ok = CopyFileW(childSource.c_str(), childDest.c_str(), FALSE) != FALSE && ok;
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return ok;
}

std::wstring SafeFileName(std::wstring value) {
    for (wchar_t& ch : value) {
        if (ch < 32 || wcschr(L"<>:\"/\\|?*", ch)) {
            ch = L'_';
        }
    }
    if (value.empty()) value = L"mod";
    return value;
}

std::wstring StripNewlines(std::wstring value) {
    for (wchar_t& ch : value) {
        if (ch == L'\r' || ch == L'\n' || ch == L'\t') ch = L' ';
    }
    return value;
}

bool VerboseLoggingEnabled() {
    // read once, drop a verbose_logging file in localstate or set MC_VERBOSE_LOGGING=1 to re-enable loader debug logging
    static const bool enabled = [] {
        if (GetEnvVarString(L"MC_VERBOSE_LOGGING") == L"1") return true;
        const std::wstring localDir = GetLocalStateDir();
        if (localDir.empty()) return false;
        const std::wstring marker = localDir + L"\\verbose_logging";
        return GetFileAttributesW(marker.c_str()) != INVALID_FILE_ATTRIBUTES;
    }();
    return enabled;
}
