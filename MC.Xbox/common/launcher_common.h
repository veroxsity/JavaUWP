#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <string>
#include <vector>

extern std::wstring g_logDir;

std::wstring GetExecutableDir();
std::wstring GetLocalStateDir();
std::wstring GetEnvVarString(const wchar_t* name);
void WriteLog(const wchar_t* msg);
void WriteLogF(const wchar_t* fmt, ...);
bool VerboseLoggingEnabled();

std::string w2a(const std::wstring& w);
std::wstring a2w(const char* utf8);

bool EnsureDirectoryTree(const std::wstring& path);
bool DirectoryExists(const std::wstring& path);
std::wstring GetParentDir(const std::wstring& path);
std::wstring GetFileName(const std::wstring& path);
std::wstring FileStamp(const std::wstring& path);
bool ReadTextFile(const std::wstring& path, std::wstring& out);
bool WriteTextFile(const std::wstring& path, const std::wstring& value);
bool ReadBinaryFileLimited(
    const std::wstring& path,
    std::vector<unsigned char>& out,
    unsigned long long maxBytes = 64ull * 1024ull * 1024ull);

std::wstring TrimWhitespace(std::wstring value);
std::wstring ToLowerW(std::wstring value);
std::string ToLowerAscii(std::string value);
bool WriteAllBytes(const std::wstring& path, const void* data, size_t size);
int CompareVersionNumbers(const std::string& lhs, const std::string& rhs);

bool ReadZipTextFile(const std::wstring& zipPath, const char* entryName, std::wstring& out);

std::wstring AppStateDir(const std::wstring& runtimeRoot);
std::wstring LogsCurrentDir(const std::wstring& runtimeRoot);
std::wstring LogsPreviousDir(const std::wstring& runtimeRoot);
std::wstring CrashLaunchMarkerPath(const std::wstring& runtimeRoot);
std::wstring CrashReportsDir(const std::wstring& runtimeRoot);
std::wstring CrashTimestampForFileName();

bool DeleteDirectoryTree(const std::wstring& path);
bool MovePathIfExists(const std::wstring& source, const std::wstring& dest, bool replaceExisting = true);
bool CopyDirectoryTree(const std::wstring& source, const std::wstring& dest);

std::wstring SafeFileName(std::wstring value);
std::wstring StripNewlines(std::wstring value);
