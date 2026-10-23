#include "minecraft_auth.h"

#include "http_client.h"
#include "launcher_common.h"

#include <winrt/base.h>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Security.Credentials.h>

static constexpr char kMicrosoftAuthClientId[] = "c36a9fb6-4f2a-41ff-90bd-ae7cc92031eb";
static constexpr char kMicrosoftAuthScopes[] = "XboxLive.signin offline_access";
static constexpr wchar_t kRefreshTokenResource[] = L"MinecraftJavaUWP.MicrosoftRefreshToken";
static constexpr wchar_t kRefreshTokenUser[] = L"default";

bool SaveRefreshToken(const std::string& refreshToken) {
    if (refreshToken.empty()) return false;
    try {
        winrt::Windows::Security::Credentials::PasswordVault vault;
        try {
            auto existing = vault.Retrieve(kRefreshTokenResource, kRefreshTokenUser);
            vault.Remove(existing);
        } catch (...) {
            // Retrieve throws when no credential exists, so this is the nothing-to-remove path
        }
        vault.Add(winrt::Windows::Security::Credentials::PasswordCredential(
            kRefreshTokenResource,
            kRefreshTokenUser,
            winrt::to_hstring(refreshToken)));
        WriteLog(L"Saved Microsoft refresh token to Credential Locker");
        return true;
    } catch (const winrt::hresult_error& ex) {
        WriteLogF(L"Failed to save refresh token hr=0x%08X msg=%s",
            static_cast<unsigned int>(ex.code()), ex.message().c_str());
        return false;
    }
}

std::string LoadRefreshToken() {
    try {
        winrt::Windows::Security::Credentials::PasswordVault vault;
        auto credential = vault.Retrieve(kRefreshTokenResource, kRefreshTokenUser);
        credential.RetrievePassword();
        return winrt::to_string(credential.Password());
    } catch (const winrt::hresult_error& ex) {
        WriteLogF(L"No usable refresh token hr=0x%08X msg=%s",
            static_cast<unsigned int>(ex.code()), ex.message().c_str());
        return {};
    }
}

void ClearRefreshToken() {
    try {
        winrt::Windows::Security::Credentials::PasswordVault vault;
        auto credential = vault.Retrieve(kRefreshTokenResource, kRefreshTokenUser);
        vault.Remove(credential);
    } catch (...) {
        // Retrieve throws when no credential exists, so this is the nothing-to-clear path
    }
}

namespace {

using winrt::Windows::Data::Json::JsonArray;
using winrt::Windows::Data::Json::JsonObject;
using winrt::Windows::Data::Json::JsonValueType;

bool ParseAuthJson(const std::string& body, JsonObject& out) {
    return JsonObject::TryParse(winrt::to_hstring(body), out);
}

std::string JsonText(const JsonObject& obj, const wchar_t* key) {
    if (!obj.HasKey(key)) return {};
    const auto value = obj.GetNamedValue(key);
    if (value.ValueType() != JsonValueType::String) return {};
    return winrt::to_string(value.GetString());
}

int JsonInt(const JsonObject& obj, const wchar_t* key, int fallback) {
    if (!obj.HasKey(key)) return fallback;
    const auto value = obj.GetNamedValue(key);
    if (value.ValueType() != JsonValueType::Number) return fallback;
    return static_cast<int>(value.GetNumber());
}

// failure bodies carry account state, so only named fields are summarised
std::string HttpFailureSummary(const HttpResult& response) {
    std::string summary = "HTTP " + std::to_string(response.status);
    JsonObject json = nullptr;
    if (!ParseAuthJson(response.body, json)) {
        return summary;
    }

    const std::string code = JsonText(json, L"error");
    if (!code.empty()) summary += " " + code;
    const std::string description = JsonText(json, L"error_description");
    if (!description.empty()) summary += ": " + description;
    if (json.HasKey(L"XErr")) {
        const auto xerr = json.GetNamedValue(L"XErr");
        // XErr exceeds int range, 2148916233 is the common no-Xbox-account code
        if (xerr.ValueType() == JsonValueType::Number) {
            summary += " XErr=" + std::to_string(static_cast<long long>(xerr.GetNumber()));
        }
    }
    return summary;
}

// uhs sits at DisplayClaims.xui[0].uhs, the old substring scan matched it at any depth
std::string XboxUserHash(const JsonObject& root) {
    if (!root.HasKey(L"DisplayClaims")) return {};
    const auto claims = root.GetNamedValue(L"DisplayClaims");
    if (claims.ValueType() != JsonValueType::Object) return {};
    const JsonObject claimsObject = claims.GetObject();
    if (!claimsObject.HasKey(L"xui")) return {};
    const auto xui = claimsObject.GetNamedValue(L"xui");
    if (xui.ValueType() != JsonValueType::Array) return {};
    const JsonArray entries = xui.GetArray();
    for (uint32_t i = 0; i < entries.Size(); ++i) {
        const auto entry = entries.GetAt(i);
        if (entry.ValueType() != JsonValueType::Object) continue;
        const std::string hash = JsonText(entry.GetObject(), L"uhs");
        if (!hash.empty()) return hash;
    }
    return {};
}

}

bool RequestDeviceCode(DeviceCodeResponse& out, std::string& error) {
    const std::string body = MakeFormBody({
        { "client_id", kMicrosoftAuthClientId },
        { "scope", kMicrosoftAuthScopes }
    });
    const HttpResult response = HttpPostString(
        L"https://login.microsoftonline.com/consumers/oauth2/v2.0/devicecode",
        body,
        L"application/x-www-form-urlencoded");
    if (!response.success()) {
        error = "Device code request failed: " + HttpFailureSummary(response);
        return false;
    }

    JsonObject json = nullptr;
    if (!ParseAuthJson(response.body, json)) {
        error = "Device code response was not valid JSON.";
        return false;
    }

    out.userCode = JsonText(json, L"user_code");
    out.deviceCode = JsonText(json, L"device_code");
    out.verificationUri = JsonText(json, L"verification_uri");
    out.expiresIn = JsonInt(json, L"expires_in", 900);
    out.interval = (std::max)(1, JsonInt(json, L"interval", 5));

    if (out.userCode.empty() || out.deviceCode.empty() || out.verificationUri.empty()) {
        error = "Device code response was missing required fields.";
        return false;
    }

    WriteLogF(L"Device auth code received user_code=%s expires=%d interval=%d",
        a2w(out.userCode.c_str()).c_str(), out.expiresIn, out.interval);
    return true;
}

DevicePollResult PollDeviceToken(const std::string& deviceCode) {
    DevicePollResult result;
    const std::string body = MakeFormBody({
        { "grant_type", "urn:ietf:params:oauth:grant-type:device_code" },
        { "client_id", kMicrosoftAuthClientId },
        { "device_code", deviceCode }
    });
    const HttpResult response = HttpPostString(
        L"https://login.microsoftonline.com/consumers/oauth2/v2.0/token",
        body,
        L"application/x-www-form-urlencoded");

    if (response.success()) {
        JsonObject json = nullptr;
        if (!ParseAuthJson(response.body, json)) {
            result.status = DevicePollStatus::Failed;
            result.error = "Microsoft token response was not valid JSON.";
            return result;
        }
        result.status = DevicePollStatus::Success;
        result.token.accessToken = JsonText(json, L"access_token");
        result.token.refreshToken = JsonText(json, L"refresh_token");
        result.token.expiresIn = JsonInt(json, L"expires_in", 0);
        if (result.token.accessToken.empty()) {
            result.status = DevicePollStatus::Failed;
            result.error = "Microsoft token response did not include access_token.";
        }
        return result;
    }

    // pending and slow_down still need to work, so it degrades to the http status
    JsonObject errorJson = nullptr;
    const bool parsed = ParseAuthJson(response.body, errorJson);
    const std::string code = parsed ? JsonText(errorJson, L"error") : std::string();
    if (code == "authorization_pending") {
        result.status = DevicePollStatus::Pending;
    } else if (code == "slow_down") {
        result.status = DevicePollStatus::SlowDown;
    } else {
        result.status = DevicePollStatus::Failed;
        result.error = code.empty()
            ? "Microsoft token polling failed: HTTP " + std::to_string(response.status)
            : code + ": " + JsonText(errorJson, L"error_description");
    }
    return result;
}

bool RefreshMicrosoftToken(const std::string& refreshToken, MicrosoftTokenResponse& out, std::string& error, bool* invalidGrant) {
    if (invalidGrant) *invalidGrant = false;
    const std::string body = MakeFormBody({
        { "grant_type", "refresh_token" },
        { "client_id", kMicrosoftAuthClientId },
        { "refresh_token", refreshToken },
        { "scope", kMicrosoftAuthScopes }
    });
    const HttpResult response = HttpPostString(
        L"https://login.microsoftonline.com/consumers/oauth2/v2.0/token",
        body,
        L"application/x-www-form-urlencoded");
    if (!response.success()) {
        JsonObject failure = nullptr;
        if (invalidGrant && ParseAuthJson(response.body, failure)) {
            *invalidGrant = JsonText(failure, L"error") == "invalid_grant";
        }
        error = "Microsoft session refresh failed. " + HttpFailureSummary(response);
        return false;
    }

    JsonObject json = nullptr;
    if (!ParseAuthJson(response.body, json)) {
        error = "Microsoft refresh response was not valid JSON.";
        return false;
    }

    out.accessToken = JsonText(json, L"access_token");
    out.refreshToken = JsonText(json, L"refresh_token");
    out.expiresIn = JsonInt(json, L"expires_in", 0);
    if (out.accessToken.empty()) {
        error = "Microsoft refresh response did not include access_token.";
        return false;
    }
    return true;
}

bool AuthenticateWithXboxLive(const std::string& microsoftAccessToken, XboxAuthResponse& out, std::string& error) {
    const std::string payload =
        "{\"Properties\":{\"AuthMethod\":\"RPS\",\"SiteName\":\"user.auth.xboxlive.com\",\"RpsTicket\":\"d=" +
        JsonEscape(microsoftAccessToken) +
        "\"},\"RelyingParty\":\"http://auth.xboxlive.com\",\"TokenType\":\"JWT\"}";
    const HttpResult response = HttpPostString(
        L"https://user.auth.xboxlive.com/user/authenticate",
        payload,
        L"application/json");
    if (!response.success()) {
        error = "Xbox Live auth failed: " + HttpFailureSummary(response);
        return false;
    }

    JsonObject json = nullptr;
    if (!ParseAuthJson(response.body, json)) {
        error = "Xbox Live auth response was not valid JSON.";
        return false;
    }

    out.token = JsonText(json, L"Token");
    out.userHash = XboxUserHash(json);
    if (out.token.empty() || out.userHash.empty()) {
        error = "Xbox Live auth response was missing token fields.";
        return false;
    }
    return true;
}

bool AuthorizeWithXsts(const std::string& xboxToken, const char* relyingParty, XboxAuthResponse& out, std::string& error) {
    const std::string payload =
        "{\"Properties\":{\"SandboxId\":\"RETAIL\",\"UserTokens\":[\"" +
        JsonEscape(xboxToken) +
        "\"]},\"RelyingParty\":\"" +
        JsonEscape(relyingParty) +
        "\",\"TokenType\":\"JWT\"}";
    const HttpResult response = HttpPostString(
        L"https://xsts.auth.xboxlive.com/xsts/authorize",
        payload,
        L"application/json");
    if (!response.success()) {
        error = "XSTS auth failed: " + HttpFailureSummary(response);
        return false;
    }

    JsonObject json = nullptr;
    if (!ParseAuthJson(response.body, json)) {
        error = "XSTS response was not valid JSON.";
        return false;
    }

    out.token = JsonText(json, L"Token");
    out.userHash = XboxUserHash(json);
    if (out.token.empty() || out.userHash.empty()) {
        error = "XSTS response was missing token fields.";
        return false;
    }
    return true;
}

bool LoginToMinecraft(const std::string& userHash, const std::string& xstsToken, MicrosoftTokenResponse& out, std::string& error) {
    const std::string identity = "XBL3.0 x=" + userHash + ";" + xstsToken;
    const std::string payload = "{\"identityToken\":\"" + JsonEscape(identity) + "\"}";
    const HttpResult response = HttpPostString(
        L"https://api.minecraftservices.com/authentication/login_with_xbox",
        payload,
        L"application/json");
    if (!response.success()) {
        error = "Minecraft login failed: " + HttpFailureSummary(response);
        return false;
    }

    JsonObject json = nullptr;
    if (!ParseAuthJson(response.body, json)) {
        error = "Minecraft login response was not valid JSON.";
        return false;
    }

    out.accessToken = JsonText(json, L"access_token");
    out.expiresIn = JsonInt(json, L"expires_in", 0);
    if (out.accessToken.empty()) {
        error = "Minecraft login response did not include access_token.";
        return false;
    }
    return true;
}

bool EnsureMinecraftEntitlement(const std::string& minecraftAccessToken, std::string& error) {
    const HttpResult response = HttpGetBearer(
        L"https://api.minecraftservices.com/entitlements/mcstore",
        minecraftAccessToken);
    if (!response.success()) {
        error = "Minecraft entitlement check failed: " + HttpFailureSummary(response);
        return false;
    }

    JsonObject json = nullptr;
    if (!ParseAuthJson(response.body, json)) {
        error = "Minecraft entitlement response was not valid JSON.";
        return false;
    }

    if (!json.HasKey(L"items") || json.GetNamedValue(L"items").ValueType() != JsonValueType::Array) {
        error = "Minecraft entitlement response did not contain an items list.";
        return false;
    }

    const JsonArray items = json.GetNamedArray(L"items");
    for (uint32_t i = 0; i < items.Size(); ++i) {
        const auto item = items.GetAt(i);
        if (item.ValueType() != JsonValueType::Object) continue;
        const std::string name = JsonText(item.GetObject(), L"name");
        if (name == "game_minecraft" || name == "product_minecraft") {
            return true;
        }
    }

    error = "This Microsoft account does not appear to own Minecraft Java Edition.";
    return false;
}

bool FetchMinecraftProfile(const std::string& minecraftAccessToken, LaunchAuthConfig& out, std::string& error) {
    const HttpResult response = HttpGetBearer(
        L"https://api.minecraftservices.com/minecraft/profile",
        minecraftAccessToken);
    if (!response.success()) {
        error = "Minecraft profile request failed: " + HttpFailureSummary(response);
        return false;
    }

    JsonObject json = nullptr;
    if (!ParseAuthJson(response.body, json)) {
        error = "Minecraft profile response was not valid JSON.";
        return false;
    }

    out.uuid = NormalizeMinecraftUuid(JsonText(json, L"id"));
    out.username = JsonText(json, L"name");
    out.accessToken = minecraftAccessToken;
    if (out.uuid.empty() || out.username.empty()) {
        error = "Minecraft profile response was missing id or name.";
        return false;
    }
    return true;
}

bool BuildMinecraftAuth(const std::string& microsoftAccessToken, LaunchAuthConfig& out, std::string& error) {
    XboxAuthResponse xbl;
    if (!AuthenticateWithXboxLive(microsoftAccessToken, xbl, error)) {
        return false;
    }

    XboxAuthResponse xsts;
    if (!AuthorizeWithXsts(xbl.token, "rp://api.minecraftservices.com/", xsts, error)) {
        return false;
    }

    MicrosoftTokenResponse minecraftToken;
    if (!LoginToMinecraft(xsts.userHash, xsts.token, minecraftToken, error)) {
        return false;
    }

    if (minecraftToken.expiresIn <= 0) {
        error = "Minecraft token response did not include a valid lifetime.";
        return false;
    }
    const auto expiresAt = std::chrono::steady_clock::now() + std::chrono::seconds(minecraftToken.expiresIn);
    if (!EnsureMinecraftEntitlement(minecraftToken.accessToken, error)) {
        return false;
    }

    if (!FetchMinecraftProfile(minecraftToken.accessToken, out, error)) {
        return false;
    }

    out.expiresAt = expiresAt;
    WriteLogF(L"Minecraft auth resolved username=%s uuid=%s",
        a2w(out.username.c_str()).c_str(),
        a2w(out.uuid.c_str()).c_str());
    return true;
}
