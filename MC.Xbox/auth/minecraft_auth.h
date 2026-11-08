#pragma once

#include <string>
#include <chrono>

struct LaunchAuthConfig {
    std::string username;
    std::string uuid;
    std::string accessToken;
    std::chrono::steady_clock::time_point expiresAt{};
    bool isLocalTrial = false;
};

// Local trial identity, distinct from a Microsoft-authenticated account.
inline LaunchAuthConfig MakeLocalTrialAuthConfig() {
    LaunchAuthConfig out;
    out.username = "LocalPlayer";
    // Java's UUID.nameUUIDFromBytes("OfflinePlayer:LocalPlayer" in UTF-8).
    out.uuid = "c6d762506d42328ebfce7a3312675f70";
    out.accessToken = "0"; // Required launch argument; not an account credential.
    out.expiresAt = (std::chrono::steady_clock::time_point::max)();
    out.isLocalTrial = true;
    return out;
}

struct DeviceCodeResponse {
    std::string userCode;
    std::string deviceCode;
    std::string verificationUri;
    int expiresIn = 900;
    int interval = 5;
};

LaunchAuthConfig LoadLocalTrialAuthConfig();
bool SaveLocalTrialName(const std::wstring& name, LaunchAuthConfig& out);

struct MicrosoftTokenResponse {
    std::string accessToken;
    std::string refreshToken;
    int expiresIn = 0;
};

struct XboxAuthResponse {
    std::string token;
    std::string userHash;
};

enum class DevicePollStatus {
    Pending,
    SlowDown,
    Success,
    Failed
};

struct DevicePollResult {
    DevicePollStatus status = DevicePollStatus::Failed;
    MicrosoftTokenResponse token;
    std::string error;
};

bool SaveRefreshToken(const std::string& refreshToken);
std::string LoadRefreshToken();
void ClearRefreshToken();

bool RequestDeviceCode(DeviceCodeResponse& out, std::string& error);
DevicePollResult PollDeviceToken(const std::string& deviceCode);
bool RefreshMicrosoftToken(const std::string& refreshToken, MicrosoftTokenResponse& out, std::string& error, bool* invalidGrant = nullptr);
bool AuthenticateWithXboxLive(const std::string& microsoftAccessToken, XboxAuthResponse& out, std::string& error);
bool AuthorizeWithXsts(const std::string& xboxToken, const char* relyingParty, XboxAuthResponse& out, std::string& error);
bool LoginToMinecraft(const std::string& userHash, const std::string& xstsToken, MicrosoftTokenResponse& out, std::string& error);
bool EnsureMinecraftEntitlement(const std::string& minecraftAccessToken, std::string& error);
bool FetchMinecraftProfile(const std::string& minecraftAccessToken, LaunchAuthConfig& out, std::string& error);
bool BuildMinecraftAuth(const std::string& microsoftAccessToken, LaunchAuthConfig& out, std::string& error);
