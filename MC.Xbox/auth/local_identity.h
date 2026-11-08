#pragma once
#include "minecraft_auth.h"
#include <windows.h>
#include <bcrypt.h>

inline bool IsValidLocalName(const std::wstring& name) {
    if (name.size() < 3 || name.size() > 16) return false;
    for (wchar_t c : name) {
        if (!((c >= L'A' && c <= L'Z') || (c >= L'a' && c <= L'z') ||
              (c >= L'0' && c <= L'9') || c == L'_')) return false;
    }
    return true;
}

inline bool CreateLocalIdentity(const std::wstring& name, LaunchAuthConfig& out) {
    if (!IsValidLocalName(name)) return false;
    std::string username;
    for (wchar_t c : name) username += static_cast<char>(c); // Validated ASCII above.
    const std::string seed = "OfflinePlayer:" + username;
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_MD5_ALGORITHM, nullptr, 0) < 0) return false;
    unsigned char digest[16] = {};
    const auto status = BCryptHash(algorithm, nullptr, 0,
        reinterpret_cast<PUCHAR>(const_cast<char*>(seed.data())),
        static_cast<ULONG>(seed.size()), digest, sizeof(digest));
    BCryptCloseAlgorithmProvider(algorithm, 0);
    if (status < 0) return false;
    digest[6] = (digest[6] & 0x0f) | 0x30;
    digest[8] = (digest[8] & 0x3f) | 0x80;
    constexpr char hex[] = "0123456789abcdef";
    std::string uuid;
    for (unsigned char b : digest) { uuid += hex[b >> 4]; uuid += hex[b & 15]; }
    out = MakeLocalTrialAuthConfig();
    out.username = username;
    out.uuid = uuid;
    return true;
}
