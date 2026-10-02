#include "remote_file_server.h"

#include "http_client.h"
#include "launcher_common.h"
#include "manual_downloads.h"
#include "mod_source.h"
#include "modpack_io.h"
#include "profiles.h"
#include "world_io.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <thread>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <bcrypt.h>

static std::string HtmlEscape(const std::wstring& value) {
    std::string out;
    for (wchar_t ch : value) {
        switch (ch) {
        case L'&': out += "&amp;"; break;
        case L'<': out += "&lt;"; break;
        case L'>': out += "&gt;"; break;
        case L'"': out += "&quot;"; break;
        default: out += w2a(std::wstring(1, ch)); break;
        }
    }
    return out;
}

// also escapes < so a value can never close the script tag the json is embedded in
static std::string ScriptJsonEscape(const std::string& s) {
    std::string o;
    for (char c : s) {
        switch (c) {
        case '"': o += "\\\""; break;
        case '\\': o += "\\\\"; break;
        case '\n': o += "\\n"; break;
        case '\r': o += "\\r"; break;
        case '\t': o += "\\t"; break;
        case '<': o += "\\u003c"; break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                char b[8];
                sprintf_s(b, "\\u%04x", static_cast<unsigned>(static_cast<unsigned char>(c)));
                o += b;
            } else {
                o += c;
            }
        }
    }
    return o;
}

static std::string FormatModified(const FILETIME& ft) {
    FILETIME local{};
    SYSTEMTIME st{};
    if (!FileTimeToLocalFileTime(&ft, &local) || !FileTimeToSystemTime(&local, &st)) return {};
    char buf[32];
    sprintf_s(buf, "%04d-%02d-%02d %02d:%02d", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute);
    return buf;
}

static bool IsEditableTextFile(const std::wstring& name) {
    const std::wstring lo = ToLowerW(name);
    static const wchar_t* exts[] = {
        L".yml", L".yaml", L".json", L".properties", L".txt", L".log", L".toml",
        L".cfg", L".conf", L".ini", L".xml", L".md", L".csv", L".sh", L".bat",
        L".mcmeta", L".accesswidener", L".lang", L".css", L".js", L".html", L".tsv"
    };
    for (auto e : exts) {
        const size_t n = wcslen(e);
        if (lo.size() >= n && lo.compare(lo.size() - n, n, e) == 0) return true;
    }
    return false;
}

static std::string UrlDecode(const std::string& value) {
    std::string out;
    for (size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '%' && i + 2 < value.size()) {
            const char hex[3] = { value[i + 1], value[i + 2], 0 };
            char* end = nullptr;
            const long v = strtol(hex, &end, 16);
            if (end && *end == 0) {
                out.push_back(static_cast<char>(v));
                i += 2;
                continue;
            }
        } else if (value[i] == '+') {
            out.push_back(' ');
            continue;
        }
        out.push_back(value[i]);
    }
    return out;
}

static std::string QueryValue(const std::string& query, const std::string& key) {
    size_t pos = 0;
    while (pos <= query.size()) {
        const size_t amp = query.find('&', pos);
        const std::string part = query.substr(pos, amp == std::string::npos ? std::string::npos : amp - pos);
        const size_t eq = part.find('=');
        const std::string k = UrlDecode(eq == std::string::npos ? part : part.substr(0, eq));
        if (k == key) return UrlDecode(eq == std::string::npos ? std::string() : part.substr(eq + 1));
        if (amp == std::string::npos) break;
        pos = amp + 1;
    }
    return {};
}

static bool SendAll(SOCKET s, const char* data, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        const int chunk = send(s, data + sent, static_cast<int>((std::min)(len - sent, static_cast<size_t>(64 * 1024))), 0);
        if (chunk <= 0) return false;
        sent += static_cast<size_t>(chunk);
    }
    return true;
}

static void SendHttpResponse(SOCKET s, int status, const char* statusText, const std::string& contentType, const std::string& body) {
    std::ostringstream head;
    head << "HTTP/1.1 " << status << " " << statusText << "\r\n"
        << "Content-Type: " << contentType << "\r\n"
        << "Content-Length: " << body.size() << "\r\n"
        << "Cache-Control: no-store\r\n"
        << "Connection: close\r\n\r\n";
    const std::string h = head.str();
    SendAll(s, h.data(), h.size());
    SendAll(s, body.data(), body.size());
}

// 303 so a refresh of the page it lands on can never resubmit the form behind it
static void SendHttpRedirect(SOCKET s, const std::string& location) {
    std::ostringstream head;
    head << "HTTP/1.1 303 See Other\r\n"
        << "Location: " << location << "\r\n"
        << "Content-Length: 0\r\n"
        << "Cache-Control: no-store\r\n"
        << "Connection: close\r\n\r\n";
    const std::string h = head.str();
    SendAll(s, h.data(), h.size());
}

static void SendHttpFile(SOCKET s, const std::wstring& path, const std::string& downloadName, const std::string& contentType) {
    WIN32_FILE_ATTRIBUTE_DATA fad = {};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fad) ||
        (fad.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
        SendHttpResponse(s, 404, "Not Found", "text/plain; charset=utf-8", "File not found.");
        return;
    }
    const unsigned long long size =
        (static_cast<unsigned long long>(fad.nFileSizeHigh) << 32) | fad.nFileSizeLow;

    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"rb") != 0 || !f) {
        SendHttpResponse(s, 404, "Not Found", "text/plain; charset=utf-8", "File not found.");
        return;
    }

    std::ostringstream head;
    head << "HTTP/1.1 200 OK\r\n"
        << "Content-Type: " << contentType << "\r\n"
        << "Content-Length: " << size << "\r\n"
        << "Content-Disposition: attachment; filename=\"" << downloadName << "\"\r\n"
        << "Cache-Control: no-store\r\n"
        << "Connection: close\r\n\r\n";
    const std::string h = head.str();
    if (!SendAll(s, h.data(), h.size())) {
        fclose(f);
        return;
    }

    unsigned char buffer[256 * 1024];
    size_t remaining = static_cast<size_t>(size);
    if (remaining == 0 && size > 0) {
        fclose(f);
        SendHttpResponse(s, 500, "Internal Server Error", "text/plain; charset=utf-8", "File too large.");
        return;
    }
    while (remaining > 0) {
        const size_t chunk = (std::min)(remaining, sizeof(buffer));
        const size_t read = fread(buffer, 1, chunk, f);
        if (read == 0) break;
        if (!SendAll(s, reinterpret_cast<const char*>(buffer), read)) break;
        remaining -= read;
    }
    fclose(f);
}

static std::string GuessDownloadContentType(const std::wstring& name) {
    std::wstring lower = ToLowerW(name);
    if (lower.size() >= 4 && lower.substr(lower.size() - 4) == L".zip") return "application/zip";
    if (lower.size() >= 4 && lower.substr(lower.size() - 4) == L".jar") return "application/java-archive";
    if (lower.size() >= 5 && lower.substr(lower.size() - 5) == L".json") return "application/json";
    if (lower.size() >= 4 && lower.substr(lower.size() - 4) == L".log") return "text/plain; charset=utf-8";
    if (lower.size() >= 4 && lower.substr(lower.size() - 4) == L".txt") return "text/plain; charset=utf-8";
    if (lower.size() >= 7 && lower.substr(lower.size() - 7) == L".mrpack") return "application/zip";
    return "application/octet-stream";
}

static bool GenerateRemotePin(std::string& pin) {
    unsigned value = 0;
    if (BCryptGenRandom(nullptr, reinterpret_cast<PUCHAR>(&value), sizeof(value), BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0) {
        return false;
    }
    value = 100000 + (value % 900000);
    char buf[16] = {};
    sprintf_s(buf, "%06u", value);
    pin = buf;
    return true;
}

class RemoteFileServer {
public:
    void Start(const std::wstring& runtimeRoot) {
        if (running_.load()) return;
        if (thread_.joinable()) thread_.join();
        pin_.clear();
        if (!GenerateRemotePin(pin_)) {
            WriteLog(L"Remote file server not started, secure PIN generation failed");
            return;
        }
        runtimeRoot_ = runtimeRoot;
        attempts_.clear();
        stop_.store(false);
        running_.store(true);
        thread_ = std::thread([this]() { ThreadMain(); });
    }

    void Stop() {
        if (!running_.load()) {
            if (thread_.joinable()) thread_.join();
            return;
        }
        stop_.store(true);
        SOCKET clientSocket = clientSocket_.exchange(INVALID_SOCKET);
        if (clientSocket != INVALID_SOCKET) {
            shutdown(clientSocket, SD_BOTH);
        }
        WakeListener();
        if (thread_.joinable()) thread_.join();
        running_.store(false);
    }

    bool Running() const { return running_.load(); }
    std::string Pin() const { return pin_; }
    int Port() const { return port_; }

    std::wstring BaseUrl() const {
        return L"http://" + a2w(LocalAddress().c_str()) + L":" + std::to_wstring(port_);
    }

    std::wstring Url() const {
        return BaseUrl() + L"/?pin=" + a2w(pin_.c_str());
    }

private:
    static constexpr int kPort = 27632;
    std::atomic<bool> running_{ false };
    std::atomic<bool> stop_{ false };
    std::atomic<SOCKET> listenSocket_{ INVALID_SOCKET };
    std::atomic<SOCKET> clientSocket_{ INVALID_SOCKET };
    std::thread thread_;
    std::wstring runtimeRoot_;
    std::string pin_;

    // a pack installs off the server thread, which serves one request at a time and would
    // otherwise freeze every page until the install ends
    struct PackImport {
        std::mutex lock;
        bool running = false;
        bool finished = false;
        bool ok = false;
        std::wstring packName;
        std::wstring profileName;
        std::wstring step;
        unsigned long long stepDone = 0;
        unsigned long long stepTotal = 0;
        std::wstring message;
    };
    std::shared_ptr<PackImport> packImport_ = std::make_shared<PackImport>();

    int port_ = kPort;

    struct PinAttempts {
        int failures = 0;
        int lockouts = 0;
        unsigned long long lockedUntilMs = 0;
    };

    static constexpr int kMaxPinAttempts = 10;
    static constexpr unsigned long long kPinLockoutBaseMs = 30ull * 1000ull;
    static constexpr unsigned long long kPinLockoutMaxMs = 15ull * 60ull * 1000ull;

    // only touched from the accept loop thread, which handles one client at a time
    std::map<unsigned long, PinAttempts> attempts_;

    std::string LocalAddress() const {
        char host[256] = {};
        if (gethostname(host, sizeof(host)) != 0) return "127.0.0.1";
        addrinfo hints = {};
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        addrinfo* result = nullptr;
        if (getaddrinfo(host, nullptr, &hints, &result) != 0 || !result) return "127.0.0.1";
        std::string fallback = "127.0.0.1";
        for (addrinfo* p = result; p; p = p->ai_next) {
            sockaddr_in* sin = reinterpret_cast<sockaddr_in*>(p->ai_addr);
            char ip[INET_ADDRSTRLEN] = {};
            if (inet_ntop(AF_INET, &sin->sin_addr, ip, sizeof(ip))) {
                std::string s = ip;
                if (s.rfind("127.", 0) != 0 && s.rfind("169.254.", 0) != 0) {
                    freeaddrinfo(result);
                    return s;
                }
                fallback = s;
            }
        }
        freeaddrinfo(result);
        return fallback;
    }

    void WakeListener() {
        SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (s == INVALID_SOCKET) return;
        sockaddr_in addr = {};
        addr.sin_family = AF_INET;
        inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
        addr.sin_port = htons(kPort);
        connect(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
        closesocket(s);
    }

    void ThreadMain() {
        WSADATA wsa = {};
        if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
            WriteLog(L"Remote file server WSAStartup failed");
            running_.store(false);
            return;
        }

        SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (s == INVALID_SOCKET) {
            WriteLogF(L"Remote file server socket failed err=%d", WSAGetLastError());
            WSACleanup();
            running_.store(false);
            return;
        }

        BOOL reuse = TRUE;
        setsockopt(s, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));

        sockaddr_in addr = {};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
        addr.sin_port = htons(kPort);
        if (bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0 ||
            listen(s, 8) != 0) {
            WriteLogF(L"Remote file server bind/listen failed port=%d err=%d", kPort, WSAGetLastError());
            closesocket(s);
            WSACleanup();
            running_.store(false);
            return;
        }

        listenSocket_.store(s);
        WriteLogF(L"Remote file server started url=%s", BaseUrl().c_str());

        while (!stop_.load()) {
            fd_set readSet;
            FD_ZERO(&readSet);
            FD_SET(s, &readSet);
            timeval tv = {};
            tv.tv_sec = 0;
            tv.tv_usec = 250000;
            const int ready = select(0, &readSet, nullptr, nullptr, &tv);
            if (ready <= 0) continue;
            sockaddr_in peerAddr = {};
            int peerLen = static_cast<int>(sizeof(peerAddr));
            SOCKET client = accept(s, reinterpret_cast<sockaddr*>(&peerAddr), &peerLen);
            if (client == INVALID_SOCKET) continue;
            if (stop_.load()) {
                closesocket(client);
                break;
            }
            clientSocket_.store(client);
            HandleClient(client, peerAddr.sin_addr.s_addr);
            clientSocket_.compare_exchange_strong(client, INVALID_SOCKET);
            closesocket(client);
        }

        SOCKET old = listenSocket_.exchange(INVALID_SOCKET);
        if (old != INVALID_SOCKET) closesocket(old);
        WSACleanup();
        WriteLog(L"Remote file server stopped");
    }

    static constexpr unsigned long long kMaxUploadBytes = 512ull * 1024ull * 1024ull;

    bool ReadRequest(SOCKET s, std::string& request, std::string& body, std::map<std::string, std::string>& headers) {
        std::string data;
        char buffer[8192];
        size_t headerEnd = std::string::npos;
        while (data.size() < 1024 * 1024) {
            const int read = recv(s, buffer, sizeof(buffer), 0);
            if (read <= 0) return false;
            data.append(buffer, read);
            headerEnd = data.find("\r\n\r\n");
            if (headerEnd != std::string::npos) break;
        }
        if (headerEnd == std::string::npos) return false;

        request = data.substr(0, headerEnd);
        size_t lineStart = request.find("\r\n");
        size_t pos = lineStart == std::string::npos ? request.size() : lineStart + 2;
        while (pos < request.size()) {
            const size_t next = request.find("\r\n", pos);
            const std::string line = request.substr(pos, next == std::string::npos ? std::string::npos : next - pos);
            const size_t colon = line.find(':');
            if (colon != std::string::npos) {
                std::string key = line.substr(0, colon);
                std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) { return static_cast<char>(tolower(c)); });
                size_t valueStart = colon + 1;
                while (valueStart < line.size() && line[valueStart] == ' ') ++valueStart;
                headers[key] = line.substr(valueStart);
            }
            if (next == std::string::npos) break;
            pos = next + 2;
        }

        unsigned long long contentLength = 0;
        auto it = headers.find("content-length");
        if (it != headers.end()) {
            contentLength = strtoull(it->second.c_str(), nullptr, 10);
        }
        if (contentLength > kMaxUploadBytes) {
            SendHttpResponse(s, 413, "Payload Too Large", "text/html; charset=utf-8",
                Layout("Upload too large",
                    "<h1>Upload too large</h1><p>The console accepts uploads up to "
                    + FormatBytes(kMaxUploadBytes) + ".</p>"));
            return false;
        }

        body = data.substr(headerEnd + 4);
        // string doubles its way up to contentLength without this
        if (contentLength > body.size()) body.reserve(static_cast<size_t>(contentLength));
        while (body.size() < contentLength) {
            const int read = recv(s, buffer, sizeof(buffer), 0);
            if (read <= 0) return false;
            body.append(buffer, read);
        }
        if (body.size() > contentLength) body.resize(static_cast<size_t>(contentLength));
        return true;
    }

    bool Authorized(const std::string& query, const std::string& body) const {
        if (QueryValue(query, "pin") == pin_) return true;
        // multipart uploads carry the pin as a part, plain forms as an urlencoded field
        if (body.find("name=\"pin\"\r\n\r\n" + pin_) != std::string::npos) return true;
        return !pin_.empty() && FormFieldValue(body, "pin") == pin_;
    }

    static bool SuppliedPin(const std::string& query, const std::string& body) {
        if (!QueryValue(query, "pin").empty()) return true;
        if (body.find("name=\"pin\"\r\n\r\n") != std::string::npos) return true;
        if (body.compare(0, 4, "pin=") == 0) return true;
        return body.find("&pin=") != std::string::npos;
    }

    static std::string FormatPeer(unsigned long peer) {
        in_addr addr = {};
        addr.s_addr = static_cast<ULONG>(peer);
        char ip[INET_ADDRSTRLEN] = {};
        if (!inet_ntop(AF_INET, &addr, ip, sizeof(ip))) return "unknown";
        return ip;
    }

    bool PeerLockedOut(unsigned long peer) {
        auto it = attempts_.find(peer);
        if (it == attempts_.end() || it->second.lockedUntilMs == 0) return false;
        if (GetTickCount64() < it->second.lockedUntilMs) return true;
        it->second.lockedUntilMs = 0;
        it->second.failures = 0;
        return false;
    }

    void NotePinFailure(unsigned long peer) {
        PinAttempts& state = attempts_[peer];
        if (++state.failures < kMaxPinAttempts) return;
        state.failures = 0;
        const unsigned long long lockMs =
            (std::min)(kPinLockoutBaseMs << (std::min)(state.lockouts, 5), kPinLockoutMaxMs);
        ++state.lockouts;
        state.lockedUntilMs = GetTickCount64() + lockMs;
        WriteLogF(L"Remote file server locked out %s for %llus after %d wrong PINs",
            a2w(FormatPeer(peer).c_str()).c_str(), lockMs / 1000ull, kMaxPinAttempts);
    }

    void NotePinSuccess(unsigned long peer) {
        attempts_.erase(peer);
    }

    static std::string FormatBytes(unsigned long long bytes) {
        if (bytes < 1024ull) return std::to_string(bytes) + " B";
        if (bytes < 1024ull * 1024ull) return std::to_string(bytes / 1024ull) + " KB";
        if (bytes < 1024ull * 1024ull * 1024ull) {
            char buf[32] = {};
            sprintf_s(buf, "%.1f MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
            return buf;
        }
        char buf[32] = {};
        sprintf_s(buf, "%.2f GB", static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0));
        return buf;
    }

    static const char* RemoteStyles() {
        return R"RFSTYLE(
:root{color-scheme:dark;--bg:#0b0c0e;--bar:#111316;--panel:#1a1c20;--hover:#22252a;--line:#23262b;--line-soft:#1d2024;--line-strong:#2e3238;--muted:#9ba1a6;--placeholder:#787f85;--text:#f2f4f5;--brand:#70c486;--brand-hover:#5fb176;--on-brand:#0b0c0e;--accent:#70c486;--accent-soft:rgba(112,196,134,.14);--danger:#e36a5c;--danger-bg:#241d1c;--danger-line:#553d39;--overlay:#0b0c0ef5;--shadow:0 10px 28px rgba(0,0,0,.55);--radius:4px;--space-sm:8px;--space-md:16px;--space-lg:24px;--space-xl:32px}:root[data-theme=light]{color-scheme:light;--bg:#f4f5f7;--bar:#ffffff;--panel:#ffffff;--hover:#eceef1;--line:#d9dde2;--line-soft:#e6e9ed;--line-strong:#c9cfd6;--muted:#5a626b;--placeholder:#8c939b;--text:#14171a;--brand:#2c7449;--brand-hover:#24603c;--on-brand:#ffffff;--accent:#1f6b3f;--accent-soft:rgba(44,116,73,.12);--danger:#b3261e;--danger-bg:#fdeceb;--danger-line:#f0c0bc;--overlay:#f4f5f7f5;--shadow:0 10px 28px rgba(16,24,32,.16)}:root[data-theme=xbox]{--bg:#0d100d;--bar:#131713;--panel:#191e19;--hover:#222922;--line:#2a322a;--line-soft:#1f251f;--line-strong:#354035;--muted:#a3ada3;--placeholder:#7d867d;--text:#eff3ee;--brand:#107c10;--brand-hover:#0e6b0e;--on-brand:#ffffff;--accent:#9bf00b;--accent-soft:rgba(155,240,11,.13);--danger:#ff8a80;--danger-bg:#2a1c1a;--danger-line:#5e3b36;--overlay:#0d100df5}:root[data-theme=playstation]{--bg:#0a0d14;--bar:#10141d;--panel:#161b26;--hover:#1e2433;--line:#262d3c;--line-soft:#1c2230;--line-strong:#323b4d;--muted:#9aa4b8;--placeholder:#757f92;--text:#f0f3f9;--brand:#0070d1;--brand-hover:#0060b4;--on-brand:#ffffff;--accent:#4d9fff;--accent-soft:rgba(0,112,209,.22);--danger:#ff7a6e;--danger-bg:#241d1f;--danger-line:#57393c;--overlay:#0a0d14f5}
*{box-sizing:border-box}html{min-height:100%;scrollbar-gutter:stable}html:has(.app){scrollbar-gutter:auto}body{margin:0;background:var(--bg);color:var(--text);font:14px/1.55 "Segoe UI",sans-serif}button,input,select,textarea{font:inherit}button,a,input,select,summary,textarea{-webkit-tap-highlight-color:transparent}button,a,summary{touch-action:manipulation}a{color:var(--accent);text-decoration:none}a:hover{text-decoration:underline}button,summary{cursor:pointer}button:disabled{cursor:wait;opacity:.55}:focus-visible{outline:2px solid var(--accent);outline-offset:3px}::selection{background:var(--brand);color:var(--on-brand)}[hidden]{display:none!important}
h1,h2,h3,p{margin:0}h1,h2,h3{line-height:1.2}h1{font:600 28px/1.2 "Bahnschrift","Segoe UI",sans-serif;letter-spacing:-.5px}h2{font-size:20px;font-weight:600}h3{font-size:16px;font-weight:600}p{max-width:68ch}p+p{margin-top:12px}.muted,.section-note{color:var(--muted)}.eyebrow,.side-title,.grp{font-size:11px;font-weight:600;text-transform:uppercase;letter-spacing:.12em;color:var(--muted)}.eyebrow{margin-bottom:8px}code,.pin{font:13px/1.5 Consolas,monospace}.pin{letter-spacing:.12em;color:var(--text)}.sr-only{position:absolute;width:1px;height:1px;padding:0;overflow:hidden;clip:rect(0,0,0,0);white-space:nowrap}
.top{min-height:68px;padding:12px 28px;display:flex;align-items:center;gap:20px;border-bottom:1px solid var(--line);background:var(--bar)}.brand{display:flex;align-items:center;gap:12px;color:var(--text);flex-shrink:0}.brand:hover{text-decoration:none}.brand strong{font:600 18px "Bahnschrift","Segoe UI",sans-serif;letter-spacing:.5px}.brand-label{padding-left:16px;border-left:1px solid var(--line);color:var(--muted);font-size:13px}.top-actions,.hero-actions,.toolbar,.world-actions{display:flex;align-items:center;gap:8px;flex-wrap:wrap}.top-actions{margin-left:auto}.session{display:flex;align-items:center;gap:8px;font-size:12px;color:var(--muted)}.pill{display:inline-flex;gap:8px;align-items:center;font-size:12px;color:var(--muted)}
button,.button,.btn{display:inline-flex;align-items:center;justify-content:center;gap:8px;min-height:40px;padding:8px 14px;border:1px solid transparent;border-radius:var(--radius);background:var(--brand);color:var(--on-brand);font-weight:600;line-height:1.3;text-align:center;text-decoration:none}button:hover,.button:hover,.btn:hover{background:var(--brand-hover);text-decoration:none}.secondary,.btn{background:var(--panel);border-color:var(--line);color:var(--text);font-weight:400}.secondary:hover,.btn:hover{background:var(--hover)}.primary{background:var(--brand);border-color:var(--brand);color:var(--on-brand);font-weight:600}.primary:hover{background:var(--brand-hover)}.ghost{background:transparent;border-color:transparent;color:var(--muted)}.ghost:hover{background:var(--hover);color:var(--text)}.danger{color:var(--danger)}.button.danger{background:var(--danger-bg);border-color:var(--danger-line)}.themes{display:flex;flex-wrap:wrap;gap:8px;margin-top:16px}.themes button{gap:10px;background:var(--panel);border:1px solid var(--line);color:var(--text);font-weight:500}.themes button:hover{background:var(--hover)}.themes button.on{border-color:var(--accent);background:var(--accent-soft)}.swatch{width:14px;height:14px;border-radius:3px;border:1px solid var(--line-strong);flex-shrink:0}.sw-dark{background:linear-gradient(135deg,#0b0c0e 50%,#70c486 50%)}.sw-light{background:linear-gradient(135deg,#f4f5f7 50%,#2c7449 50%)}.sw-xbox{background:linear-gradient(135deg,#0d100d 50%,#107c10 50%)}.sw-ps{background:linear-gradient(135deg,#0a0d14 50%,#0070d1 50%)}.sm{min-height:36px;padding:6px 10px;font-size:13px}
input,select{min-height:42px;min-width:0;max-width:100%;padding:8px 12px;background:var(--bg);color:var(--text);border:1px solid var(--line);border-radius:var(--radius)}input::placeholder{color:var(--placeholder)}input:not([type=checkbox]):not([type=hidden]),select{width:100%}input[type=file]{font-size:13px;padding:5px;overflow:hidden}input::file-selector-button{padding:7px 10px;margin-right:10px;border:0;border-radius:2px;background:var(--hover);color:var(--text);cursor:pointer}label{display:block;font-size:12px;font-weight:600;color:var(--muted)}.field{display:grid;gap:8px;min-width:0}.field+.field{margin-top:16px}.upload{display:grid;grid-template-columns:minmax(0,1fr) auto;gap:8px;align-items:start}.checkline{display:flex;align-items:center;gap:8px;margin-top:16px;font-weight:400}.checkline input{min-height:20px;width:20px;accent-color:var(--brand)}form{margin:0;min-width:0}.field+.toolbar{margin-top:16px}
.shell{display:grid;grid-template-columns:212px minmax(0,1fr);min-height:calc(100vh - 68px)}.side{margin:0;border:0;border-right:1px solid var(--line);background:var(--bar);padding:24px 12px;min-width:0}.side>summary{display:none}.side:not([open])>.navbody{display:block}.side-title,.grp{padding:0 12px;margin:20px 0 6px}.side-title:first-child,.grp:first-child{margin-top:0}.nav,.rail{display:grid;gap:2px}.nav a,.rail a{display:flex;align-items:center;gap:12px;padding:8px 12px;min-height:34px;border-radius:var(--radius);color:var(--muted);font-size:13px}.nav a:hover,.rail a:hover{color:var(--text);background:var(--hover);text-decoration:none}.nav a.active,.rail a.active{color:var(--text);background:var(--accent-soft)}.nav a small,.rail a small{margin-left:auto;color:var(--muted);font-size:11px}
.content{width:100%;max-width:1280px;padding:24px clamp(20px,3vw,48px) 24px;min-width:0}.page-head{display:flex;justify-content:space-between;align-items:flex-start;gap:24px;padding-bottom:16px}.page-head>div{min-width:0}.page-head h1{overflow-wrap:anywhere}.profile-meta{display:flex;gap:8px 20px;flex-wrap:wrap;margin-top:12px;color:var(--muted);font-size:12px}.profile-meta span{overflow-wrap:anywhere}.tabs{display:flex;gap:24px;border-bottom:1px solid var(--line);margin-bottom:24px}.tabs a{padding:12px 0;color:var(--muted);border-bottom:2px solid transparent;margin-bottom:-1px;font-weight:600;white-space:nowrap}.tabs a:hover{color:var(--text);text-decoration:none}.tabs a.active{color:var(--text);border-color:var(--accent)}
.section-head{display:flex;justify-content:space-between;align-items:flex-start;gap:16px;margin-bottom:16px}.section-note{font-size:13px;margin-top:8px}.section-head>div{min-width:0}.world-list{border-top:1px solid var(--line)}.world-card{display:flex;align-items:center;justify-content:space-between;gap:16px;min-height:68px;padding:12px 0;border-bottom:1px solid var(--line)}.world-card>div:first-child{min-width:0}.world-card strong{font-size:15px;overflow-wrap:anywhere;font-weight:600}.world-card .muted{font-size:12px;margin-top:4px}.world-actions{flex-shrink:0}.world-card .world-actions .button,.world-card .world-actions button{font-size:12px;min-height:36px;padding:7px 12px}.grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:24px}.grid>*{min-width:0}.panel{min-width:0;padding:24px 0;border-top:1px solid var(--line)}.panel h3{margin-bottom:16px}.panel p{font-size:13px;color:var(--muted);margin:12px 0}.panel .empty{padding:20px 0}.section>.panel{margin-top:24px}.disclosure{margin-top:16px;padding:0;background:var(--bar);border:1px solid var(--line);border-radius:6px}.disclosure>summary{display:flex;align-items:center;justify-content:space-between;gap:16px;list-style:none;padding:16px 20px;font-size:14px;font-weight:600}.disclosure>summary::-webkit-details-marker{display:none}.disclosure>summary:after{content:"+";color:var(--muted);font-size:20px;font-weight:400;line-height:1}.disclosure[open]>summary:after{content:"\2212"}.disclosure[open]>summary{border-bottom:1px solid var(--line)}.disclosure-body{padding:20px}.disclosure-body>p,.disclosure-body form>p{font-size:13px;color:var(--muted);margin-top:16px}.disclosure .grid{gap:20px}.disclosure .field{margin-top:0}.empty,.err{padding:48px 20px;text-align:center;color:var(--muted)}.err p{margin:0 auto 16px}.empty strong,.err strong{display:block;font-size:16px;font-weight:600;color:var(--text);margin-bottom:8px}.notice{margin-bottom:24px;padding:12px 16px;border:1px solid var(--line);border-radius:var(--radius);color:var(--muted);font-size:13px}.settings{max-width:720px}.result{max-width:720px;margin:64px auto;padding:32px}.result:has(.shell),.result:has(.login){max-width:none;margin:0;padding:0}.result>p{margin-top:16px}.card{max-width:440px;padding:28px;background:var(--bar);border:1px solid var(--line);border-radius:8px}.card .field{margin:24px 0 16px}.card button{width:100%}.login{min-height:100dvh;display:grid;place-items:center;padding:24px}.login .card{width:100%}.login p{margin-top:12px;color:var(--muted)}.login input{font-size:24px;letter-spacing:.4em;text-align:center}
)RFSTYLE"
        R"RFSTYLE(.app{height:100vh;height:100dvh;display:flex;flex-direction:column;overflow:hidden}.app .top{flex-shrink:0}.app .shell{min-height:0;flex:1}.app .side{overflow-y:auto}.main{display:flex;flex-direction:column;min-width:0;min-height:0}.browser-head{padding:24px 32px 16px;display:flex;align-items:center;justify-content:space-between;gap:20px}.browser-head h1{font-size:24px}.browser-head .muted{font-size:12px;margin-top:6px}.profsel{display:flex;align-items:center;gap:10px;min-width:0;max-width:320px;margin-left:auto}.profsel select{width:230px}.title-row{display:flex;align-items:center;gap:12px;flex-wrap:wrap;min-width:0}.bar2{display:flex;align-items:center;gap:12px;padding:0 32px 16px;min-width:0}.bar2 #extra{margin-left:auto}.crumbs{display:flex;align-items:center;gap:4px;min-width:0;flex:1;flex-wrap:wrap;overflow-wrap:anywhere}.crumbs button{color:var(--muted);background:transparent;min-height:32px;padding:4px 8px;font-size:12px;font-weight:400;max-width:100%;overflow-wrap:anywhere;text-align:left}.crumbs button:last-child{color:var(--text)}.crumbs button:hover{background:var(--hover)}.crumbs button+button:before{content:"/";margin-right:6px;color:var(--muted)}.filter{width:200px;flex-shrink:0}.filter input{min-height:36px;font-size:12px}.access{font-size:11px;color:var(--muted);padding:4px 8px;background:var(--panel);border:1px solid var(--line);border-radius:3px;white-space:nowrap}.list{flex:1;overflow-y:auto;min-height:0;padding:0 32px 100px;scrollbar-gutter:stable}.list-head,.row{display:grid;grid-template-columns:minmax(0,1fr) 88px 140px 40px;align-items:center;gap:20px}.list-head{overflow-y:auto;scrollbar-gutter:stable;padding:12px 32px;border-top:1px solid var(--line);border-bottom:1px solid var(--line);font-size:11px;color:var(--muted)}.row{min-height:56px;padding:8px 0;border-bottom:1px solid var(--line-soft);position:relative}.row:hover{background:var(--bar)}.nm{display:flex;align-items:center;gap:12px;min-width:0}.nm .t{display:block;min-width:0;text-align:left;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;padding:8px 0;background:transparent;font-size:13px;font-weight:400;border:0;color:var(--text)}.nm .t:hover{color:var(--accent)}.ic{width:20px;height:20px;flex-shrink:0;color:var(--muted)}.ic.dir{color:var(--accent)}.size,.mod{font-size:12px;color:var(--muted);font-variant-numeric:tabular-nums}.size{text-align:right}.acts{position:relative}.acts>summary{display:grid;place-items:center;list-style:none;min-height:40px;width:40px;border-radius:var(--radius);color:var(--muted);font-size:20px;letter-spacing:1px}.acts>summary::-webkit-details-marker{display:none}.acts[open]>summary,.acts>summary:hover{background:var(--hover);color:var(--text)}.row:has(.acts[open]){z-index:5}.acts:not([open]) .action-menu{display:none}.action-menu{position:fixed;width:168px;padding:6px;background:var(--panel);border:1px solid var(--line-strong);border-radius:6px;box-shadow:var(--shadow);z-index:10}.action-menu button,.action-menu a{display:flex;justify-content:flex-start;width:100%;min-height:40px;padding:8px 12px;background:transparent;font-size:13px;font-weight:400;border:0;border-radius:3px;color:var(--text)}.action-menu button:hover,.action-menu a:hover{background:var(--hover);text-decoration:none}.action-menu .danger{color:var(--danger)}.list-footer{padding:10px 32px;border-top:1px solid var(--line);font-size:11px;color:var(--muted);display:flex;justify-content:space-between;gap:16px;background:var(--bar)}
.drop{position:fixed;inset:12px;background:var(--overlay);border:2px dashed var(--accent);border-radius:8px;display:none;align-items:center;justify-content:center;font-size:20px;color:var(--text);z-index:50;pointer-events:none}.drop.show{display:flex}.editor{position:fixed;inset:0;background:var(--bg);display:none;flex-direction:column;z-index:40}.editor.show{display:flex}.ehead{display:flex;align-items:center;gap:12px;padding:16px 24px;border-bottom:1px solid var(--line);background:var(--bar);flex-wrap:wrap}.ehead .pa{font-size:13px;flex:1;min-width:140px;overflow-wrap:anywhere}.ehead .toolbar{margin-left:auto}#ta{flex:1;min-height:0;width:100%;border:0;outline:0;resize:none;background:var(--bg);color:var(--text);font:14px/1.65 Consolas,monospace;padding:24px;white-space:pre;tab-size:4}#ta:focus-visible{outline:2px solid var(--brand);outline-offset:-2px}.editor-footer{display:flex;justify-content:space-between;gap:16px;padding:10px 24px;border-top:1px solid var(--line);font-size:12px;color:var(--muted)}.toast{position:fixed;bottom:24px;left:50%;transform:translateX(-50%);max-width:calc(100vw - 32px);padding:12px 20px;border:1px solid var(--line-strong);border-radius:6px;background:var(--panel);color:var(--text);opacity:0;pointer-events:none;z-index:60;font-size:13px;transition:opacity .15s}.toast.show{opacity:1}
@media(min-width:761px){.side{position:sticky;top:0;align-self:start;min-height:calc(100vh - 68px)}.app .side{position:static;align-self:stretch;min-height:0}.profsel+.top-actions{margin-left:0}}
@media(max-width:1100px){.content{padding:28px 24px}.grid{grid-template-columns:1fr}.world-card{align-items:flex-start;flex-wrap:wrap}.profsel{max-width:250px}.profsel select{width:180px}.browser-head,.bar2{padding-left:24px;padding-right:24px}.list,.list-head{padding-left:24px;padding-right:24px}.list-head,.row{grid-template-columns:minmax(0,1fr) 72px 120px 40px;gap:12px}}
@media(max-width:760px){.top{min-height:64px;padding:12px 16px;gap:12px;flex-wrap:wrap}.brand-label{border:0;padding:0}.brand{gap:8px}.brand strong{font-size:16px}.top-actions{gap:6px}.session{font-size:11px}.shell{grid-template-columns:minmax(0,1fr);min-height:0}.side{padding:0;border-right:0;border-bottom:1px solid var(--line)}.side>summary{display:flex;align-items:center;justify-content:space-between;padding:12px 16px;min-height:44px;font-size:13px;color:var(--muted);list-style:none}.side>summary:after{content:"+";font-size:18px}.side[open]>summary:after{content:"\2212"}.side:not([open])>.navbody{display:none}.navbody{padding:16px;max-height:40vh;overflow:auto}.nav,.rail{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:4px}.rail .grp,.rail #overview{grid-column:1/-1}.side-title,.grp{margin-top:16px}.content{padding:24px 16px 32px}.page-head{gap:16px;flex-direction:column;padding-bottom:16px}.page-head h1{font-size:24px}.tabs{gap:24px;margin-bottom:24px}.section-head{gap:12px;flex-wrap:wrap}.section-head .button{min-height:36px;font-size:12px}.world-card{padding:16px 0;gap:12px}.world-actions{flex-shrink:1}.grid{gap:16px}.disclosure>summary{padding:16px}.disclosure-body{padding:16px}.upload{grid-template-columns:minmax(0,1fr)}.upload button{justify-self:start}.profsel{order:3;width:100%;max-width:none;margin:0}.profsel select{width:100%;flex:1}.app .shell{display:flex;flex-direction:column}.app .side{flex-shrink:0;overflow:visible}.app .main{flex:1}.browser-head .eyebrow{display:none}.browser-head{padding:16px;align-items:flex-start;gap:12px;flex-wrap:wrap}.browser-head h1{font-size:22px}.browser-head .toolbar{gap:6px}.browser-head .filter{order:2;width:100%;flex:1 1 100%}.browser-head button{min-height:40px;padding:8px 12px;font-size:12px}.bar2{padding:0 16px 12px;gap:8px;flex-wrap:wrap}.crumbs{flex-basis:100%}.crumbs button{display:block;padding:8px;max-width:46vw;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}.list-head{padding:10px 16px;grid-template-columns:1fr 40px}.list-head .size,.list-head .mod{display:none}.list{padding:0 16px 100px}.row{grid-template-columns:minmax(0,1fr) auto 40px;gap:0 12px;padding:10px 0;min-height:72px}.nm{grid-column:1 / 3}.row .size{grid-column:2;grid-row:2}.row .mod{grid-column:1;grid-row:2;padding-left:32px}.row .acts{grid-column:3;grid-row:1 / 3}.row .nm .t{min-height:32px;padding:4px 0}.list-footer{padding:10px 16px}.list-footer .drop-hint{display:none}.ehead{padding:12px 16px}.ehead .pa{flex-basis:100%}.ehead .toolbar{margin-left:0}#ta{padding:16px;font-size:13px}.editor-footer{padding:10px 16px}.result{margin:24px auto;padding:16px}.card{padding:24px}.login{padding:16px}}
@media(prefers-reduced-motion:reduce){*{scroll-behavior:auto!important;transition:none!important}}
)RFSTYLE";
    }

    static std::string ThemeBootHtml() {
        return R"RFBOOT(<script>try{var t=localStorage.getItem('rf-theme');if(t&&/^[a-z]+$/.test(t))document.documentElement.dataset.theme=t}catch(e){}</script>)RFBOOT";
    }

    std::string Layout(const std::string& title, const std::string& body) {
        std::ostringstream html;
        html << "<!doctype html><html lang=\"en\"><head><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
            << "<title>" << title << "</title><style>" << RemoteStyles()
            << "</style>" << ThemeBootHtml() << "</head><body><div class=\"result\">" << body << "</div><script>"
            << "var nav=document.querySelector('.side');if(nav){var wide=matchMedia('(min-width:761px)');"
            << "function fitNav(){nav.open=wide.matches}fitNav();wide.addEventListener('change',fitNav)}"
            << "</script></body></html>";
        return html.str();
    }

    void HandleClient(SOCKET s, unsigned long peer) {
        std::string request;
        std::string body;
        std::map<std::string, std::string> headers;
        if (!ReadRequest(s, request, body, headers)) {
            SendHttpResponse(s, 400, "Bad Request", "text/plain; charset=utf-8", "Bad request.");
            return;
        }

        const size_t firstLineEnd = request.find("\r\n");
        const std::string firstLine = request.substr(0, firstLineEnd);
        std::istringstream first(firstLine);
        std::string method, target, version;
        first >> method >> target >> version;
        const size_t q = target.find('?');
        const std::string path = q == std::string::npos ? target : target.substr(0, q);
        const std::string query = q == std::string::npos ? std::string() : target.substr(q + 1);

        if (PeerLockedOut(peer)) {
            SendHttpResponse(s, 429, "Too Many Requests", "text/html; charset=utf-8",
                Layout("Bandit Remote Files",
                    "<div class=\"card\"><h1>Too many attempts</h1>"
                    "<p class=\"muted\">Too many wrong PINs from this device. Wait and try again, or reopen the remote files page on your Xbox for a new PIN.</p></div>"));
            return;
        }

        if (!Authorized(query, body)) {
            if (SuppliedPin(query, body)) NotePinFailure(peer);
            std::string form = "<main class=\"login\"><div class=\"card\"><div class=\"eyebrow\">Bandit Remote Files</div>"
                "<h1>Connect to your Xbox</h1><p>Enter the six-digit PIN shown in Remote Files on the launcher.</p>"
                "<form method=\"get\"><div class=\"field\"><label for=\"pin\">Session PIN</label>"
                "<input id=\"pin\" name=\"pin\" inputmode=\"numeric\" pattern=\"[0-9]{6}\" maxlength=\"6\" autocomplete=\"one-time-code\" required autofocus></div>"
                "<button>Connect</button></form></div></main>";
            SendHttpResponse(s, 401, "Unauthorized", "text/html; charset=utf-8", Layout("Bandit Remote Files", form));
            return;
        }
        NotePinSuccess(peer);

        if (method == "GET" && path == "/") {
            SendHttpResponse(s, 200, "OK", "text/html; charset=utf-8", Layout("Bandit Launcher", HomeHtml(query)));
        } else if (method == "GET" && path == "/browse") {
            SendHttpResponse(s, 200, "OK", "text/html; charset=utf-8", ExplorerHtml(query));
        } else if (method == "GET" && path == "/api/list") {
            SendHttpResponse(s, 200, "OK", "application/json; charset=utf-8", ApiListJson(query));
        } else if (method == "GET" && path == "/api/raw") {
            ServeApiRaw(s, query);
        } else if (method == "POST" && path == "/api/write") {
            HandleApiWrite(s, query, body);
        } else if (method == "POST" && path == "/api/rename") {
            HandleApiRename(s, body);
        } else if (method == "POST" && path == "/api/delete") {
            HandleApiDelete(s, body);
        } else if (method == "POST" && path == "/api/mkdir") {
            HandleApiMkdir(s, body);
        } else if (method == "POST" && path == "/api/upload") {
            HandleApiUpload(s, query, headers, body);
        } else if (method == "GET" && path == "/download") {
            ServeDownload(s, query);
        } else if (method == "GET" && path == "/download-path") {
            ServeBrowseDownload(s, query);
        } else if (method == "POST" && path == "/upload-mod") {
            HandleUpload(s, headers, body, true);
        } else if (method == "POST" && path == "/upload-resourcepack") {
            HandleUpload(s, headers, body, false);
        } else if (method == "POST" && path == "/upload-datapack") {
            HandleDatapackUpload(s, headers, body);
        } else if (method == "POST" && path == "/upload-modpack") {
            HandleModpackUpload(s, headers, body);
        } else if (method == "GET" && path == "/pack-import") {
            SendHttpResponse(s, 200, "OK", "text/html; charset=utf-8", Layout("Installing pack", PackImportPageHtml()));
        } else if (method == "GET" && path == "/api/pack-import") {
            SendHttpResponse(s, 200, "OK", "application/json; charset=utf-8", PackImportJson());
        } else if (method == "POST" && path == "/export-pack") {
            HandleExportPack(s, body);
        } else if (method == "GET" && path == "/set-curseforge-key") {
            ServeCurseForgeKeyPage(s, query);
        } else if (method == "POST" && path == "/set-curseforge-key") {
            HandleSetCurseForgeKey(s, body);
        } else if (method == "POST" && path == "/clear-manual-downloads") {
            HandleClearManualDownloads(s, body);
        } else if (method == "POST" && path == "/upload-manual-download") {
            HandleManualDownloadUpload(s, query, headers, body);
        } else if (method == "POST" && path == "/export-world") {
            HandleExportWorld(s, body);
        } else if (method == "POST" && path == "/upload-world") {
            HandleWorldUpload(s, headers, body);
        } else {
            SendHttpResponse(s, 404, "Not Found", "text/plain; charset=utf-8", "Not found.");
        }
    }

    std::string LinkFor(const std::wstring& label, const std::string& key) {
        return "<li><a href=\"/download?pin=" + pin_ + "&file=" + key + "\">" + HtmlEscape(label) + "</a></li>";
    }

    std::string UrlWithPin(const std::string& pathAndQuery) const {
        return pathAndQuery + (pathAndQuery.find('?') == std::string::npos ? "?pin=" : "&pin=") + pin_;
    }

    std::string UrlWithPinProfile(const std::string& pathAndQuery, const std::wstring& profileId) const {
        return UrlWithPin(pathAndQuery) + "&profile=" + FormUrlEncode(w2a(profileId));
    }

    std::string FormFieldValue(const std::string& body, const std::string& key) const {
        size_t pos = 0;
        while (pos <= body.size()) {
            const size_t amp = body.find('&', pos);
            const std::string part = body.substr(pos, amp == std::string::npos ? std::string::npos : amp - pos);
            const size_t eq = part.find('=');
            const std::string k = UrlDecode(eq == std::string::npos ? part : part.substr(0, eq));
            if (k == key) return UrlDecode(eq == std::string::npos ? std::string() : part.substr(eq + 1));
            if (amp == std::string::npos) break;
            pos = amp + 1;
        }
        return {};
    }

    std::string SidebarHtml(const std::wstring& profileId) const {
        auto nav = [&](const char* scope, const std::wstring& label) {
            return "<a href=\"" + UrlWithPinProfile("/browse?scope=" + std::string(scope), profileId) +
                "\">" + HtmlEscape(label) + "</a>";
        };
        std::ostringstream out;
        out << "<details class=\"side\" open><summary>Browse &amp; diagnostics</summary><div class=\"navbody\">"
            << "<div class=\"side-title\">Workspace</div><nav class=\"nav\" aria-label=\"Workspace\">"
            << "<a class=\"active\" aria-current=\"page\" href=\"" << UrlWithPinProfile("/", profileId) << "\">Overview</a>"
            << nav("profile", L"Game files") << nav("saves", L"Worlds")
            << nav("mods", L"Mods") << nav("resourcepacks", L"Resource packs")
            << "</nav><div class=\"side-title\">Diagnostics</div><nav class=\"nav\" aria-label=\"Diagnostics\">"
            << nav("logs", L"Current logs") << nav("previous", L"Previous logs")
            << nav("crash", L"Crash reports") << nav("runtime", L"Runtime cache")
            << "</nav></div></details>";
        return out.str();
    }

    bool IsSafeRelativePath(const std::wstring& rel) const {
        if (rel.empty()) return true;
        if (rel.find(L':') != std::wstring::npos) return false;
        std::wstring normalized = rel;
        std::replace(normalized.begin(), normalized.end(), L'/', L'\\');
        if (!normalized.empty() && normalized.front() == L'\\') return false;
        size_t start = 0;
        while (start <= normalized.size()) {
            const size_t slash = normalized.find(L'\\', start);
            const std::wstring part = normalized.substr(start, slash == std::wstring::npos ? std::wstring::npos : slash - start);
            if (part == L"." || part == L"..") return false;
            if (slash == std::wstring::npos) break;
            start = slash + 1;
        }
        return true;
    }

    std::wstring NormalizeProfileId(const std::string& req) {
        EnsureProfilesInitialized(runtimeRoot_);
        if (!req.empty()) {
            const std::wstring want = a2w(req.c_str());
            if (want == kVanillaProfileId) return want;
            for (const auto& p : LoadProfiles(runtimeRoot_)) {
                if (p.id == want) return want;
            }
        }
        return GetActiveProfileId(runtimeRoot_);
    }

    bool ResolveBrowseScope(const std::string& scope, const std::wstring& profileId, std::wstring& root, std::wstring& title, bool& writable) {
        EnsureProfilesInitialized(runtimeRoot_);
        EnsureProfileGameDataInitialized(runtimeRoot_, profileId);
        writable = false;
        const bool profileWritable = profileId != kVanillaProfileId;

        if (scope == "profile") {
            root = ProfileGameDir(runtimeRoot_, profileId);
            title = L"Game files";
            writable = profileWritable;
        } else if (scope == "mods") {
            root = ProfileModsDir(runtimeRoot_, profileId);
            title = L"Mods";
            writable = profileWritable;
        } else if (scope == "resourcepacks") {
            root = ProfileGameDir(runtimeRoot_, profileId) + L"\\resourcepacks";
            title = L"Resource packs";
            writable = profileWritable;
        } else if (scope == "saves") {
            root = ProfileGameDir(runtimeRoot_, profileId) + L"\\saves";
            title = L"Worlds";
            writable = profileWritable;
        } else if (scope == "logs") {
            root = LogsCurrentDir(runtimeRoot_);
            title = L"Current logs";
        } else if (scope == "previous") {
            root = LogsPreviousDir(runtimeRoot_);
            title = L"Previous logs";
        } else if (scope == "crash") {
            root = CrashReportsDir(runtimeRoot_);
            title = L"Crash reports";
        } else if (scope == "runtime") {
            root = runtimeRoot_ + L"\\game";
            title = L"Runtime cache";
        } else {
            return false;
        }
        return true;
    }

    bool ResolveScopePath(const std::string& scope, const std::wstring& profileId, const std::string& relRaw,
        std::wstring& outFull, std::wstring& outRoot, std::wstring& outRelNorm, bool& writable) {
        std::wstring title;
        if (!ResolveBrowseScope(scope, profileId, outRoot, title, writable)) return false;
        std::wstring rel = a2w(UrlDecode(relRaw).c_str());
        std::replace(rel.begin(), rel.end(), L'/', L'\\');
        if (!IsSafeRelativePath(rel)) return false;
        outFull = rel.empty() ? outRoot : outRoot + L"\\" + rel;
        outRelNorm = rel;
        std::replace(outRelNorm.begin(), outRelNorm.end(), L'\\', L'/');
        return true;
    }

    bool IsWorldFolder(const std::wstring& dir) const {
        return GetFileAttributesW((dir + L"\\level.dat").c_str()) != INVALID_FILE_ATTRIBUTES ||
            GetFileAttributesW((dir + L"\\level.dat_old").c_str()) != INVALID_FILE_ATTRIBUTES;
    }

    std::string ExportPackHtml(const std::wstring& profileId) {
        std::ostringstream out;
        out << "<details class=\"disclosure\"><summary>Export this profile as a modpack</summary><div class=\"disclosure-body\">";
        if (profileId == kVanillaProfileId) {
            out << "<div class=\"empty\">Vanilla cannot be exported. Pick a mod profile above first.</div></div></details>";
            return out.str();
        }
        const std::wstring exportPath = DefaultProfileExportPath(runtimeRoot_, profileId);
        const bool hasExport = GetFileAttributesW(exportPath.c_str()) != INVALID_FILE_ATTRIBUTES;
        const std::string profQ = FormUrlEncode(w2a(profileId));
        out << "<p class=\"muted\">Create a Modrinth-compatible <code>.mrpack</code> for the Modrinth App on PC.</p>"
            << "<div class=\"toolbar\" style=\"margin-top:12px\">"
            << "<form method=\"post\" action=\"/export-pack\"><input type=\"hidden\" name=\"pin\" value=\"" << pin_
            << "\"><input type=\"hidden\" name=\"profile\" value=\"" << HtmlEscape(profileId) << "\"><button>Build .mrpack</button></form>";
        if (hasExport) {
            out << "<a class=\"button secondary\" href=\"/download?pin=" << pin_ << "&amp;profile=" << profQ << "&amp;file=export:profile\">Download</a>";
        }
        out << "</div></div></details>";
        return out.str();
    }

    std::string ModpackImportHtml(const std::wstring& profileId) {
        std::ostringstream out;
        out << "<details class=\"disclosure\"><summary>Import a modpack</summary><div class=\"disclosure-body\">"
            << "<form method=\"post\" action=\"/upload-modpack\" enctype=\"multipart/form-data\">"
            << "<input type=\"hidden\" name=\"pin\" value=\"" << pin_ << "\">"
            << "<input type=\"hidden\" name=\"profile\" value=\"" << HtmlEscape(profileId) << "\">"
            << "<div class=\"field\"><label for=\"modpackfile\">Modrinth .mrpack or CurseForge .zip</label>"
            << "<div class=\"upload\"><input id=\"modpackfile\" type=\"file\" name=\"file\" accept=\".mrpack,.zip\" required><button>Import pack</button></div></div>"
            << "</form>"
            << "<p class=\"muted\">Installs into the selected profile. Large packs can take several minutes. "
            << "For CurseForge packs, add an API key in Settings.</p></div></details>";
        return out.str();
    }

    std::string ManualDownloadsHtml(const std::wstring& profileId) {
        const std::vector<ManualDownload> pending = PendingManualDownloads(runtimeRoot_, profileId);
        if (pending.empty()) return std::string();

        std::ostringstream out;
        out << "<section class=\"panel\"><h3>Manual downloads ("
            << pending.size() << ")</h3>"
            << "<p class=\"muted\">These files block third party launchers, so the Xbox cannot fetch them. "
            << "Each Download link saves the file on this computer in one click. "
            << "Then upload them all at once below and each goes into the right folder. "
            << "A row disappears once its file lands in the profile.</p><div class=\"world-list\">";
        for (const ManualDownload& item : pending) {
            // url comes from the api or a tsv on disk, only https goes in an href next to the pin
            const bool safeUrl = item.url.rfind(L"https://", 0) == 0;
            const std::wstring href = safeUrl ? item.url : std::wstring(L"https://www.curseforge.com/minecraft");
            out << "<div class=\"world-card\"><div><strong>" << HtmlEscape(item.modName) << "</strong>"
                << "<div class=\"muted\">" << HtmlEscape(item.fileName) << " goes in " << HtmlEscape(item.folder)
                << "</div></div><div class=\"world-actions\">"
                << "<a class=\"button secondary\" href=\"" << HtmlEscape(href)
                << "\" target=\"_blank\" rel=\"noopener noreferrer\">Download</a></div></div>";
        }
        out << "</div><div class=\"toolbar\" style=\"margin-top:12px\">"
            << "<label class=\"button\" for=\"manualfiles\">Upload downloaded files</label>"
            << "<input id=\"manualfiles\" type=\"file\" multiple hidden data-url=\""
            << UrlWithPinProfile("/upload-manual-download", profileId) << "\" onchange=\"uploadManual(this)\">"
            << "<span class=\"muted\" id=\"manualstatus\" role=\"status\"></span>"
            << "<form method=\"post\" action=\"/clear-manual-downloads\">"
            << "<input type=\"hidden\" name=\"pin\" value=\"" << pin_ << "\">"
            << "<input type=\"hidden\" name=\"profile\" value=\"" << HtmlEscape(profileId) << "\">"
            << "<button class=\"secondary\">Forget this list</button></form></div></section>"
            << R"RFMD(<script>
async function uploadManual(input){
 const status=document.getElementById('manualstatus');
 const files=Array.from(input.files);
 let placed=0,skipped=[];
 for(let i=0;i<files.length;i++){
  status.textContent='Uploading '+(i+1)+' of '+files.length;
  const form=new FormData();form.append('file',files[i]);
  const res=await fetch(input.dataset.url,{method:'POST',body:form}).catch(()=>null);
  if(res&&res.ok)placed++;else skipped.push(files[i].name);
 }
 status.textContent=placed+' placed'+(skipped.length?', not on the list: '+skipped.join(', '):'');
 if(!skipped.length)location.reload();
}
</script>)RFMD";
        return out.str();
    }

    void HandleClearManualDownloads(SOCKET s, const std::string& body) {
        if (!Authorized("", body)) {
            SendHttpResponse(s, 401, "Unauthorized", "text/html; charset=utf-8", Layout("Unauthorized", "<h1>Unauthorized</h1>"));
            return;
        }
        const std::wstring profileId = a2w(FormFieldValue(body, "profile").c_str());
        ClearManualDownloads(profileId);
        SendHttpResponse(s, 200, "OK", "text/html; charset=utf-8",
            Layout("List cleared", "<h1>List cleared</h1><p><a href=\"" + UrlWithPin("/") + "\">Back to the dashboard</a></p>"));
    }

    std::string AppearanceHtml() {
        return R"RFTHEME(<section class="panel"><h3>Theme</h3>
<p class="muted">Saved in this browser, so your phone and your PC can be set differently.</p>
<div class="themes" id="themes">
<button type="button" data-theme="dark"><span class="swatch sw-dark"></span>Dark</button>
<button type="button" data-theme="light"><span class="swatch sw-light"></span>Light</button>
<button type="button" data-theme="xbox"><span class="swatch sw-xbox"></span>Xbox</button>
<button type="button" data-theme="playstation"><span class="swatch sw-ps"></span>PlayStation</button>
</div></section>
<script>
(function(){
 var wrap=document.getElementById('themes');
 function mark(){
  var current=document.documentElement.dataset.theme||'dark';
  [].forEach.call(wrap.querySelectorAll('button'),function(b){b.classList.toggle('on',b.dataset.theme===current)});
 }
 wrap.addEventListener('click',function(e){
  var b=e.target.closest('button[data-theme]');
  if(!b)return;
  document.documentElement.dataset.theme=b.dataset.theme;
  try{localStorage.setItem('rf-theme',b.dataset.theme)}catch(err){}
  mark();
 });
 mark();
})();
</script>)RFTHEME";
    }

    std::string CurseForgeKeyHtml() {
        std::ostringstream out;
        const std::wstring hint = modsource::CurseForgeKeyHint();
        out << "<section class=\"panel\"><h3>CurseForge API key</h3>"
            << "<p class=\"muted\">CurseForge will not let a launcher ship its own key, so browsing and "
            << "downloading from CurseForge needs one of yours. Make a free Core API key at "
            << "<code>console.curseforge.com</code> and paste it here.</p>";
        if (hint.empty()) {
            out << "<p class=\"muted\">No key is set. CurseForge is unavailable until one is.</p>";
        } else {
            out << "<p class=\"muted\">A key is saved (" << HtmlEscape(hint) << ").</p>";
        }
        out << "<form id=\"cf-form\" method=\"post\" action=\"/set-curseforge-key\">"
            << "<input type=\"hidden\" name=\"pin\" value=\"" << pin_ << "\">"
            << "<div class=\"field\"><label for=\"cfkey\">API key</label>"
            << "<input id=\"cfkey\" name=\"key\" type=\"password\" autocomplete=\"off\" spellcheck=\"false\" placeholder=\"paste key\"></div>"
            << "</form><div class=\"toolbar\" style=\"margin-top:16px\"><button form=\"cf-form\">Save key</button>"
            << "<form method=\"post\" action=\"/set-curseforge-key\">"
            << "<input type=\"hidden\" name=\"pin\" value=\"" << pin_ << "\">"
            << "<input type=\"hidden\" name=\"key\" value=\"\">"
            << "<button class=\"secondary\">Clear key</button></form></div></section>";
        return out.str();
    }

    void ServeCurseForgeKeyPage(SOCKET s, const std::string& query) {
        if (!Authorized(query, "")) {
            SendHttpResponse(s, 401, "Unauthorized", "text/html; charset=utf-8", Layout("Unauthorized", "<h1>Unauthorized</h1>"));
            return;
        }
        SendHttpResponse(s, 200, "OK", "text/html; charset=utf-8", Layout("CurseForge API key", HomeHtml("section=settings&" + query)));
    }

    void HandleSetCurseForgeKey(SOCKET s, const std::string& body) {
        if (!Authorized("", body)) {
            SendHttpResponse(s, 401, "Unauthorized", "text/html; charset=utf-8", Layout("Unauthorized", "<h1>Unauthorized</h1>"));
            return;
        }
        const std::string key = FormFieldValue(body, "key");
        if (!modsource::SetCurseForgeKey(key)) {
            SendHttpResponse(s, 500, "Internal Server Error", "text/html; charset=utf-8",
                Layout("Key not saved", "<h1>Key not saved</h1><p>The key could not be written to storage.</p>"));
            return;
        }
        const std::string title = key.empty() ? "Key cleared" : "Key saved";
        SendHttpResponse(s, 200, "OK", "text/html; charset=utf-8",
            Layout(title, "<h1>" + title + "</h1><p><a href=\"" + UrlWithPin("/") + "\">Back to the dashboard</a></p>"));
    }

    void HandleExportWorld(SOCKET s, const std::string& body) {
        if (!Authorized("", body)) {
            SendHttpResponse(s, 401, "Unauthorized", "text/html; charset=utf-8", Layout("Unauthorized", "<h1>Unauthorized</h1>"));
            return;
        }
        const std::wstring worldName = a2w(FormFieldValue(body, "save").c_str());
        if (!IsSafeWorldName(worldName)) {
            SendHttpResponse(s, 400, "Bad Request", "text/html; charset=utf-8",
                Layout("Export failed", "<h1>Export failed</h1><p>Invalid world name.</p>"));
            return;
        }
        EnsureProfilesInitialized(runtimeRoot_);
        const std::wstring active = NormalizeProfileId(FormFieldValue(body, "profile"));
        const std::wstring exportPath = DefaultWorldExportPath(runtimeRoot_, worldName);
        std::wstring exportError;
        if (!ExportWorldZip(runtimeRoot_, active, worldName, exportPath, exportError)) {
            SendHttpResponse(s, 500, "Internal Server Error", "text/html; charset=utf-8",
                Layout("Export failed", "<h1>Export failed</h1><p>" + HtmlEscape(exportError) + "</p>"));
            return;
        }
        SendHttpResponse(s, 200, "OK", "text/html; charset=utf-8",
            Layout("World export complete",
                "<div class=\"top\"><h1>World export complete</h1><a class=\"pill\" href=\"/?pin=" + pin_ + "\">Files home</a></div>"
                "<p>Built a zip for <strong>" + HtmlEscape(worldName) + "</strong>.</p>"
                "<p><a class=\"button\" href=\"/download?pin=" + pin_ + "&amp;profile=" + FormUrlEncode(w2a(active)) + "&amp;file=export:world:" + FormUrlEncode(w2a(worldName)) + "\">Download world zip</a></p>"));
    }

    void HandleWorldUpload(SOCKET s, const std::map<std::string, std::string>& headers, const std::string& body) {
        std::wstring name;
        std::vector<unsigned char> data;
        if (!ExtractMultipartFile(headers, body, name, data)) {
            SendHttpResponse(s, 400, "Bad Request", "text/html; charset=utf-8",
                Layout("Import failed", "<h1>Import failed</h1><p>No world zip was received.</p>"));
            return;
        }
        const std::wstring lower = ToLowerW(name);
        if (lower.size() < 4 || lower.substr(lower.size() - 4) != L".zip") {
            SendHttpResponse(s, 400, "Bad Request", "text/html; charset=utf-8",
                Layout("Import failed", "<h1>Import failed</h1><p>World imports must be .zip files.</p>"));
            return;
        }

        std::string saveText;
        if (!ExtractMultipartTextField(headers, body, "save", saveText)) {
            SendHttpResponse(s, 400, "Bad Request", "text/html; charset=utf-8",
                Layout("Import failed", "<h1>Import failed</h1><p>Enter the world name to save as.</p>"));
            return;
        }
        const std::wstring saveName = a2w(saveText.c_str());
        if (!IsSafeWorldName(saveName)) {
            SendHttpResponse(s, 400, "Bad Request", "text/html; charset=utf-8",
                Layout("Import failed", "<h1>Import failed</h1><p>Invalid world name.</p>"));
            return;
        }

        std::string replaceText;
        const bool replaceExisting = ExtractMultipartTextField(headers, body, "replace", replaceText) && replaceText == "1";

        std::string profileText;
        ExtractMultipartTextField(headers, body, "profile", profileText);
        EnsureProfilesInitialized(runtimeRoot_);
        const std::wstring active = NormalizeProfileId(profileText);
        if (active == kVanillaProfileId) {
            SendHttpResponse(s, 400, "Bad Request", "text/html; charset=utf-8",
                Layout("Import failed", "<h1>Import failed</h1><p>Vanilla is read only. Create or select a profile on the console first.</p>"));
            return;
        }

        const std::wstring importDir = runtimeRoot_ + L"\\imports";
        EnsureDirectoryTree(importDir);
        const std::wstring path = importDir + L"\\" + SafeFileName(name);
        FILE* f = nullptr;
        if (_wfopen_s(&f, path.c_str(), L"wb") != 0 || !f) {
            SendHttpResponse(s, 500, "Internal Server Error", "text/html; charset=utf-8",
                Layout("Import failed", "<h1>Import failed</h1><p>Could not save the uploaded world zip.</p>"));
            return;
        }
        const bool wrote = fwrite(data.data(), 1, data.size(), f) == data.size();
        fclose(f);
        if (!wrote) {
            DeleteFileW(path.c_str());
            SendHttpResponse(s, 500, "Internal Server Error", "text/html; charset=utf-8",
                Layout("Import failed", "<h1>Import failed</h1><p>Could not finish writing the world zip.</p>"));
            return;
        }

        WriteLogF(L"Remote world upload saved: %s bytes=%zu", path.c_str(), data.size());
        std::wstring importError;
        const bool ok = ImportWorldFromZip(path, runtimeRoot_, active, saveName, replaceExisting, importError);
        DeleteFileW(path.c_str());
        if (!ok) {
            SendHttpResponse(s, 500, "Internal Server Error", "text/html; charset=utf-8",
                Layout("Import failed", "<h1>Import failed</h1><p>" + HtmlEscape(importError.empty() ? L"World import failed" : importError) + "</p>"));
            return;
        }

        const Profile profile = GetProfileById(runtimeRoot_, active);
        SendHttpResponse(s, 200, "OK", "text/html; charset=utf-8",
            Layout("World import complete",
                "<div class=\"top\"><h1>World import complete</h1><a class=\"pill\" href=\"/?pin=" + pin_ + "\">Files home</a></div>"
                "<p>Imported <strong>" + HtmlEscape(saveName) + "</strong> into profile <strong>" + HtmlEscape(profile.name) + "</strong>.</p>"
                "<p><a class=\"button secondary\" href=\"" + UrlWithPinProfile("/browse?scope=saves&path=" + FormUrlEncode(w2a(saveName)), active) + "\">Open world folder</a></p>"));
    }

    void HandleExportPack(SOCKET s, const std::string& body) {
        if (!Authorized("", body)) {
            SendHttpResponse(s, 401, "Unauthorized", "text/html; charset=utf-8", Layout("Unauthorized", "<h1>Unauthorized</h1>"));
            return;
        }
        EnsureProfilesInitialized(runtimeRoot_);
        const std::wstring active = NormalizeProfileId(FormFieldValue(body, "profile"));
        if (active == kVanillaProfileId) {
            SendHttpResponse(s, 400, "Bad Request", "text/html; charset=utf-8",
                Layout("Export failed", "<h1>Export failed</h1><p>Vanilla cannot be exported.</p>"));
            return;
        }
        const std::wstring exportPath = DefaultProfileExportPath(runtimeRoot_, active);
        std::wstring exportError;
        if (!ExportProfileMrpack(runtimeRoot_, active, exportPath, exportError)) {
            SendHttpResponse(s, 500, "Internal Server Error", "text/html; charset=utf-8",
                Layout("Export failed", "<h1>Export failed</h1><p>" + HtmlEscape(exportError) + "</p>"));
            return;
        }
        const Profile profile = GetProfileById(runtimeRoot_, active);
        SendHttpResponse(s, 200, "OK", "text/html; charset=utf-8",
            Layout("Export complete",
                "<div class=\"top\"><h1>Export complete</h1><a class=\"pill\" href=\"/?pin=" + pin_ + "\">Files home</a></div>"
                "<p>Built a Modrinth pack for <strong>" + HtmlEscape(profile.name) + "</strong>.</p>"
                "<p><a class=\"button\" href=\"/download?pin=" + pin_ + "&amp;profile=" + FormUrlEncode(w2a(active)) + "&amp;file=export:profile\">Download .mrpack</a></p>"));
    }

    void HandleModpackUpload(SOCKET s, const std::map<std::string, std::string>& headers, const std::string& body) {
        std::wstring name;
        std::vector<unsigned char> data;
        if (!ExtractMultipartFile(headers, body, name, data)) {
            SendHttpResponse(s, 400, "Bad Request", "text/html; charset=utf-8",
                Layout("Import failed", "<h1>Import failed</h1><p>No pack file was received.</p>"));
            return;
        }

        if (!EndsWithInsensitive(name, L".mrpack") && !EndsWithInsensitive(name, L".zip")) {
            SendHttpResponse(s, 400, "Bad Request", "text/html; charset=utf-8",
                Layout("Import failed", "<h1>Import failed</h1><p>Upload a Modrinth .mrpack or a CurseForge .zip.</p>"));
            return;
        }

        std::string profileText;
        ExtractMultipartTextField(headers, body, "profile", profileText);
        EnsureProfilesInitialized(runtimeRoot_);
        const std::wstring active = NormalizeProfileId(profileText);
        if (active == kVanillaProfileId) {
            SendHttpResponse(s, 400, "Bad Request", "text/html; charset=utf-8",
                Layout("Import failed", "<h1>Import failed</h1><p>Vanilla is read only. Create or select a profile on the console first.</p>"));
            return;
        }

        {
            std::lock_guard<std::mutex> guard(packImport_->lock);
            if (packImport_->running) {
                SendHttpRedirect(s, UrlWithPin("/pack-import"));
                return;
            }
        }

        const std::wstring importDir = runtimeRoot_ + L"\\imports";
        EnsureDirectoryTree(importDir);
        const std::wstring path = importDir + L"\\" + name;
        FILE* f = nullptr;
        if (_wfopen_s(&f, path.c_str(), L"wb") != 0 || !f) {
            SendHttpResponse(s, 500, "Internal Server Error", "text/html; charset=utf-8",
                Layout("Import failed", "<h1>Import failed</h1><p>Could not save the uploaded pack.</p>"));
            return;
        }
        const bool wrote = fwrite(data.data(), 1, data.size(), f) == data.size();
        fclose(f);
        if (!wrote) {
            DeleteFileW(path.c_str());
            SendHttpResponse(s, 500, "Internal Server Error", "text/html; charset=utf-8",
                Layout("Import failed", "<h1>Import failed</h1><p>Could not finish writing the pack.</p>"));
            return;
        }

        WriteLogF(L"Remote modpack upload saved: %s bytes=%zu", path.c_str(), data.size());
        {
            std::lock_guard<std::mutex> guard(packImport_->lock);
            packImport_->running = true;
            packImport_->finished = false;
            packImport_->ok = false;
            packImport_->packName = name;
            packImport_->profileName = GetProfileById(runtimeRoot_, active).name;
            packImport_->step = L"Reading the pack";
            packImport_->stepDone = 0;
            packImport_->stepTotal = 0;
            packImport_->message.clear();
        }

        std::thread([status = packImport_, path, root = runtimeRoot_, active]() {
            const ModpackProgressFactory progressFor = [status](const std::wstring& label, unsigned long long total) {
                {
                    std::lock_guard<std::mutex> guard(status->lock);
                    status->step = label;
                    status->stepDone = 0;
                    status->stepTotal = total;
                }
                return std::function<void(unsigned long long)>([status](unsigned long long done) {
                    std::lock_guard<std::mutex> guard(status->lock);
                    status->stepDone = done;
                });
            };
            std::wstring error;
            std::wstring note;
            const bool ok = InstallModpackFromFile(path, root, active, error, &note, progressFor);
            DeleteFileW(path.c_str());

            std::lock_guard<std::mutex> guard(status->lock);
            status->running = false;
            status->finished = true;
            status->ok = ok;
            status->message = ok ? note : (error.empty() ? std::wstring(L"Pack install failed") : error);
        }).detach();

        SendHttpRedirect(s, UrlWithPin("/pack-import"));
    }

    std::string PackImportJson() {
        std::lock_guard<std::mutex> guard(packImport_->lock);
        const PackImport& p = *packImport_;
        std::ostringstream out;
        out << "{\"running\":" << (p.running ? "true" : "false")
            << ",\"finished\":" << (p.finished ? "true" : "false")
            << ",\"ok\":" << (p.ok ? "true" : "false")
            << ",\"pack\":\"" << ScriptJsonEscape(w2a(p.packName)) << "\""
            << ",\"profile\":\"" << ScriptJsonEscape(w2a(p.profileName)) << "\""
            << ",\"step\":\"" << ScriptJsonEscape(w2a(p.step)) << "\""
            << ",\"done\":" << p.stepDone
            << ",\"total\":" << p.stepTotal
            << ",\"message\":\"" << ScriptJsonEscape(w2a(p.message)) << "\"}";
        return out.str();
    }

    std::string PackImportPageHtml() {
        return "<div class=\"top\"><h1 id=\"pi-title\">Installing pack</h1><a class=\"pill\" href=\"/?pin=" + pin_ + "\">Files home</a></div>"
            "<p class=\"muted\" id=\"pi-what\"></p>"
            "<p id=\"pi-step\" role=\"status\">Starting</p>"
            "<progress id=\"pi-bar\" max=\"1\" style=\"width:100%\"></progress>"
            "<p class=\"muted\" id=\"pi-msg\"></p>"
            "<script>const PACK_STATUS_URL='/api/pack-import?pin=" + pin_ + "';</script>"
            R"RFPI(<script>
const el = id => document.getElementById(id);
async function pollPackImport() {
 let s;
 try { s = await (await fetch(PACK_STATUS_URL, { cache: 'no-store' })).json(); }
 catch (e) { setTimeout(pollPackImport, 2000); return; }
 if (!s.running && !s.finished) { el('pi-title').textContent = 'No pack is installing'; el('pi-step').textContent = ''; el('pi-bar').hidden = true; return; }
 el('pi-what').textContent = s.pack + ' into ' + s.profile;
 if (s.running) {
  el('pi-step').textContent = s.step;
  const bar = el('pi-bar');
  if (s.total > 0) { bar.max = s.total; bar.value = s.done; } else { bar.removeAttribute('value'); }
  setTimeout(pollPackImport, 1000);
  return;
 }
 el('pi-title').textContent = s.ok ? 'Import complete' : 'Import failed';
 el('pi-step').textContent = s.ok ? 'Installed. Pick the profile on the console to play it.' : '';
 el('pi-bar').hidden = true;
 el('pi-msg').textContent = s.message;
}
pollPackImport();
</script>)RFPI";
    }

    // only names on the pending list are taken, and they go to the folder the pack recorded
    void HandleManualDownloadUpload(SOCKET s, const std::string& query, const std::map<std::string, std::string>& headers, const std::string& body) {
        const std::wstring profileId = NormalizeProfileId(QueryValue(query, "profile"));
        std::wstring name;
        std::vector<unsigned char> data;
        if (!ExtractMultipartFile(headers, body, name, data)) {
            SendHttpResponse(s, 400, "Bad Request", "text/plain; charset=utf-8", "No file was received.");
            return;
        }

        const std::vector<ManualDownload> pending = PendingManualDownloads(runtimeRoot_, profileId);
        const auto match = std::find_if(pending.begin(), pending.end(), [&name](const ManualDownload& item) {
            return _wcsicmp(SafeFileName(item.fileName).c_str(), name.c_str()) == 0;
        });
        if (match == pending.end()) {
            SendHttpResponse(s, 400, "Bad Request", "text/plain; charset=utf-8", "Not on this profile's list.");
            return;
        }

        const std::wstring folder = ProfileGameDir(runtimeRoot_, profileId) + L"\\" + match->folder;
        EnsureDirectoryTree(folder);
        const std::wstring path = folder + L"\\" + SafeFileName(match->fileName);
        FILE* f = nullptr;
        if (_wfopen_s(&f, path.c_str(), L"wb") != 0 || !f) {
            SendHttpResponse(s, 500, "Internal Server Error", "text/plain; charset=utf-8", "Could not save the file.");
            return;
        }
        const bool wrote = fwrite(data.data(), 1, data.size(), f) == data.size();
        fclose(f);
        if (!wrote) {
            DeleteFileW(path.c_str());
            SendHttpResponse(s, 500, "Internal Server Error", "text/plain; charset=utf-8", "Could not finish writing the file.");
            return;
        }
        WriteLogF(L"Manual download uploaded: %s bytes=%zu", path.c_str(), data.size());
        SendHttpResponse(s, 200, "OK", "text/plain; charset=utf-8", w2a(match->folder));
    }

    std::string WorldsSectionHtml(const std::vector<std::wstring>& saves, const std::wstring& profileId) {
        const std::string profEsc = HtmlEscape(profileId);
        const std::string profQ = FormUrlEncode(w2a(profileId));
        std::ostringstream out;
        out << "<section class=\"section\" id=\"worlds\"><div class=\"section-head\"><div><h2>Worlds</h2>"
            << "<p class=\"section-note\">Download a save for PC, or bring a world onto your Xbox.</p></div>"
            << "<a class=\"button secondary\" href=\"" << UrlWithPinProfile("/browse?scope=saves", profileId) << "\">Browse saves</a></div>";
        if (saves.empty()) {
            out << "<div class=\"empty\">No worlds yet. Play Minecraft on the console to create one, then refresh this page.</div>";
        } else {
            out << "<div class=\"world-list\">";
            for (const std::wstring& save : saves) {
                const std::wstring exportPath = DefaultWorldExportPath(runtimeRoot_, save);
                const bool hasExport = GetFileAttributesW(exportPath.c_str()) != INVALID_FILE_ATTRIBUTES;
                out << "<div class=\"world-card\"><div><strong>" << HtmlEscape(save) << "</strong>"
                    << "<div class=\"muted\">World save</div></div><div class=\"world-actions\">"
                    << "<form method=\"post\" action=\"/export-world\"><input type=\"hidden\" name=\"pin\" value=\"" << pin_
                    << "\"><input type=\"hidden\" name=\"profile\" value=\"" << profEsc << "\"><input type=\"hidden\" name=\"save\" value=\"" << HtmlEscape(save) << "\"><button class=\"secondary\">Build zip</button></form>";
                if (hasExport) {
                    out << "<a class=\"button secondary\" href=\"/download?pin=" << pin_ << "&amp;profile=" << profQ << "&amp;file=export:world:"
                        << FormUrlEncode(w2a(save)) << "\">Download</a>";
                }
                out << "<a class=\"button secondary\" href=\"" << UrlWithPinProfile("/browse?scope=saves&path=" + FormUrlEncode(w2a(save)), profileId)
                    << "\">Open</a></div></div>";
            }
            out << "</div>";
        }
        out << "<details class=\"disclosure\"><summary>Import a world</summary><div class=\"disclosure-body\">"
            << "<form method=\"post\" action=\"/upload-world\" enctype=\"multipart/form-data\">"
            << "<input type=\"hidden\" name=\"pin\" value=\"" << pin_ << "\">"
            << "<input type=\"hidden\" name=\"profile\" value=\"" << profEsc << "\">"
            << "<div class=\"grid\"><div class=\"field\"><label for=\"worldname\">Save as</label>"
            << "<input id=\"worldname\" name=\"save\" placeholder=\"World name\" required></div>"
            << "<div class=\"field\"><label for=\"worldzip\">World .zip</label>"
            << "<div class=\"upload\"><input id=\"worldzip\" type=\"file\" name=\"file\" accept=\".zip\" required><button>Import world</button></div></div></div>"
            << "<label class=\"checkline\"><input type=\"checkbox\" name=\"replace\" value=\"1\"> Replace existing world with the same name</label>"
            << "<p class=\"muted\">Accepts a zip with <code>level.dat</code> at the root or inside one folder. Large worlds may take several minutes.</p>"
            << "</form></div></details>"
            << "<details class=\"disclosure\"><summary>Add a datapack</summary><div class=\"disclosure-body\">";
        if (saves.empty()) {
            out << "<div class=\"empty\">Create a world first to upload datapacks.</div>";
        } else {
            out << "<form method=\"post\" action=\"/upload-datapack\" enctype=\"multipart/form-data\">"
                << "<input type=\"hidden\" name=\"pin\" value=\"" << pin_ << "\">"
                << "<input type=\"hidden\" name=\"profile\" value=\"" << profEsc << "\">"
                << "<div class=\"grid\"><div class=\"field\"><label for=\"datapacksave\">World</label><select id=\"datapacksave\" name=\"save\">";
            for (const std::wstring& save : saves) {
                out << "<option value=\"" << HtmlEscape(save) << "\">" << HtmlEscape(save) << "</option>";
            }
            out << "</select></div><div class=\"field\"><label for=\"datapack\">Datapack .zip</label>"
                << "<div class=\"upload\"><input id=\"datapack\" type=\"file\" name=\"file\" accept=\".zip\" required><button>Upload datapack</button></div></div></div>"
                << "</form>";
        }
        out << "</div></details></section>";
        return out.str();
    }

    std::string HomeHtml(const std::string& query) {
        EnsureProfilesInitialized(runtimeRoot_);
        const std::wstring profileId = NormalizeProfileId(QueryValue(query, "profile"));
        const Profile prof = GetProfileById(runtimeRoot_, profileId);
        const LaunchTarget target = ResolveProfileTarget(runtimeRoot_, prof);
        const std::vector<std::wstring> saves = ListProfileWorlds(runtimeRoot_, profileId);
        const std::vector<Profile> profiles = LoadProfiles(runtimeRoot_);
        const std::string profEsc = HtmlEscape(profileId);
        std::string section = QueryValue(query, "section");
        if (section != "mods" && section != "settings") section = "worlds";

        std::ostringstream sel;
        sel << "<select id=\"profile\" onchange=\"location='/?pin=" << pin_ << "&section=" << section
            << "&profile='+encodeURIComponent(this.value)\">";
        for (const auto& p : profiles) {
            sel << "<option value=\"" << HtmlEscape(p.id) << "\"" << (p.id == profileId ? " selected" : "") << ">"
                << HtmlEscape(p.name) << " (" << HtmlEscape(p.id) << ")</option>";
        }
        sel << "</select>";

        std::ostringstream out;
        out << "<header class=\"top\"><a class=\"brand\" href=\"" << UrlWithPinProfile("/", profileId)
            << "\"><strong>BANDIT</strong><span class=\"brand-label\">Remote Files</span></a>"
            << "<div class=\"profsel\"><label for=\"profile\">Profile</label>" << sel.str() << "</div>"
            << "<div class=\"top-actions\"><span class=\"session\">PIN <span class=\"pin\">" << pin_ << "</span></span>"
            << "<a class=\"btn ghost sm\" href=\"" << UrlWithPinProfile("/?section=" + section, profileId) << "\">Refresh</a></div></header>"
            << "<div class=\"shell\">" << SidebarHtml(profileId) << "<main class=\"content\">"
            << "<header class=\"page-head\"><div><div class=\"eyebrow\">Profile workspace</div><h1>" << HtmlEscape(prof.name) << "</h1>"
            << "<div class=\"profile-meta\"><span>" << HtmlEscape(TargetProfileText(target)) << "</span><span>" << saves.size()
            << " worlds</span><span>" << profEsc << "</span></div></div></header><nav class=\"tabs\" aria-label=\"Profile tools\">";
        auto tab = [&](const char* key, const char* label) {
            out << "<a" << (section == key ? " class=\"active\" aria-current=\"page\"" : "") << " href=\""
                << UrlWithPinProfile(std::string("/?section=") + key, profileId) << "\">" << label << "</a>";
        };
        tab("worlds", "Worlds");
        tab("mods", "Mods &amp; packs");
        tab("settings", "Settings");
        out << "</nav>";
        if (profileId == kVanillaProfileId) {
            out << "<div class=\"notice\">Vanilla files are read only. Select a mod profile to make changes or import files.</div>";
        }
        if (section == "worlds") {
            out << WorldsSectionHtml(saves, profileId);
        } else if (section == "mods") {
            out << "<section class=\"section\"><div class=\"section-head\"><div><h2>Mods &amp; packs</h2>"
                << "<p class=\"section-note\">Add files to this profile or transfer a complete modpack.</p></div>"
                << "<a class=\"button secondary\" href=\"" << UrlWithPinProfile("/browse?scope=mods", profileId) << "\">Browse mods</a></div>"
                << "<div class=\"grid\"><section class=\"panel\"><h3>Add a mod</h3>"
                << "<form method=\"post\" action=\"/upload-mod\" enctype=\"multipart/form-data\">"
                << "<input type=\"hidden\" name=\"pin\" value=\"" << pin_ << "\"><input type=\"hidden\" name=\"profile\" value=\"" << profEsc << "\">"
                << "<div class=\"field\"><label for=\"modfile\">Mod .jar</label><div class=\"upload\"><input id=\"modfile\" type=\"file\" name=\"file\" accept=\".jar\" required><button>Upload mod</button></div></div>"
                << "</form><p>Saved to this profile's mods folder.</p></section>"
                << "<section class=\"panel\"><h3>Add a resource pack</h3>"
                << "<form method=\"post\" action=\"/upload-resourcepack\" enctype=\"multipart/form-data\">"
                << "<input type=\"hidden\" name=\"pin\" value=\"" << pin_ << "\"><input type=\"hidden\" name=\"profile\" value=\"" << profEsc << "\">"
                << "<div class=\"field\"><label for=\"packfile\">Resource pack .zip</label><div class=\"upload\"><input id=\"packfile\" type=\"file\" name=\"file\" accept=\".zip\" required><button class=\"secondary\">Upload pack</button></div></div>"
                << "</form></section></div>" << ModpackImportHtml(profileId) << ExportPackHtml(profileId)
                << ManualDownloadsHtml(profileId) << "</section>";
        } else {
            out << "<section class=\"settings\"><div class=\"section-head\"><div><h2>Settings</h2>"
                << "<p class=\"section-note\">Appearance, and the connection used for CurseForge downloads.</p></div></div>"
                << AppearanceHtml() << CurseForgeKeyHtml() << "</section>";
        }
        out << "</main></div>";
        return out.str();
    }

    std::string ApiListJson(const std::string& query) {
        std::string scope = QueryValue(query, "scope");
        if (scope.empty()) scope = "profile";
        const std::wstring profileId = NormalizeProfileId(QueryValue(query, "profile"));
        std::wstring full, root, relNorm;
        bool writable = false;
        if (!ResolveScopePath(scope, profileId, QueryValue(query, "path"), full, root, relNorm, writable)) {
            return "{\"ok\":false}";
        }
        const bool savesRoot = (scope == "saves" && relNorm.empty());

        struct E { std::wstring name; bool dir; unsigned long long size; FILETIME mtime; };
        std::vector<E> dirs, files;
        WIN32_FIND_DATAW fd = {};
        HANDLE h = FindFirstFileW((full + L"\\*").c_str(), &fd);
        if (h != INVALID_HANDLE_VALUE) {
            do {
                if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
                E e;
                e.name = fd.cFileName;
                e.dir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
                e.size = (static_cast<unsigned long long>(fd.nFileSizeHigh) << 32) | fd.nFileSizeLow;
                e.mtime = fd.ftLastWriteTime;
                (e.dir ? dirs : files).push_back(e);
            } while (FindNextFileW(h, &fd));
            FindClose(h);
        }
        auto byName = [](const E& a, const E& b) { return _wcsicmp(a.name.c_str(), b.name.c_str()) < 0; };
        std::sort(dirs.begin(), dirs.end(), byName);
        std::sort(files.begin(), files.end(), byName);

        std::ostringstream js;
        js << "{\"ok\":true,\"scope\":\"" << ScriptJsonEscape(scope) << "\",\"path\":\"" << ScriptJsonEscape(w2a(relNorm))
            << "\",\"writable\":" << (writable ? "true" : "false")
            << ",\"world\":" << ((scope == "saves" && IsWorldFolder(full)) ? "true" : "false")
            << ",\"entries\":[";
        bool firstOut = true;
        auto emit = [&](const E& e) {
            if (!firstOut) js << ",";
            firstOut = false;
            js << "{\"name\":\"" << ScriptJsonEscape(w2a(e.name)) << "\",\"dir\":" << (e.dir ? "true" : "false")
                << ",\"size\":" << e.size << ",\"sizeText\":\"" << FormatBytes(e.size)
                << "\",\"modified\":\"" << FormatModified(e.mtime) << "\",\"text\":"
                << ((!e.dir && IsEditableTextFile(e.name)) ? "true" : "false")
                << (e.dir && savesRoot ? ",\"world\":true" : "") << "}";
        };
        for (const auto& e : dirs) emit(e);
        for (const auto& e : files) emit(e);
        js << "]}";
        return js.str();
    }

    void ServeApiRaw(SOCKET s, const std::string& query) {
        std::string scope = QueryValue(query, "scope");
        if (scope.empty()) scope = "profile";
        const std::wstring profileId = NormalizeProfileId(QueryValue(query, "profile"));
        std::wstring full, root, relNorm;
        bool writable = false;
        if (!ResolveScopePath(scope, profileId, QueryValue(query, "path"), full, root, relNorm, writable) || relNorm.empty()) {
            SendHttpResponse(s, 400, "Bad Request", "text/plain; charset=utf-8", "Bad path.");
            return;
        }
        WIN32_FILE_ATTRIBUTE_DATA fa = {};
        if (!GetFileAttributesExW(full.c_str(), GetFileExInfoStandard, &fa) || (fa.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            SendHttpResponse(s, 404, "Not Found", "text/plain; charset=utf-8", "Not found.");
            return;
        }
        const unsigned long long size = (static_cast<unsigned long long>(fa.nFileSizeHigh) << 32) | fa.nFileSizeLow;
        if (size > 8ull * 1024ull * 1024ull) {
            SendHttpResponse(s, 413, "Payload Too Large", "text/plain; charset=utf-8", "File too large to edit.");
            return;
        }
        FILE* f = nullptr;
        if (_wfopen_s(&f, full.c_str(), L"rb") != 0 || !f) {
            SendHttpResponse(s, 404, "Not Found", "text/plain; charset=utf-8", "Not found.");
            return;
        }
        std::string data(static_cast<size_t>(size), '\0');
        if (size > 0) fread(data.data(), 1, static_cast<size_t>(size), f);
        fclose(f);
        SendHttpResponse(s, 200, "OK", "text/plain; charset=utf-8", data);
    }

    void HandleApiWrite(SOCKET s, const std::string& query, const std::string& body) {
        std::string scope = QueryValue(query, "scope");
        if (scope.empty()) scope = "profile";
        const std::wstring profileId = NormalizeProfileId(QueryValue(query, "profile"));
        std::wstring full, root, relNorm;
        bool writable = false;
        if (!ResolveScopePath(scope, profileId, QueryValue(query, "path"), full, root, relNorm, writable) || relNorm.empty()) {
            SendHttpResponse(s, 400, "Bad Request", "text/plain; charset=utf-8", "Bad path.");
            return;
        }
        if (!writable) {
            SendHttpResponse(s, 403, "Forbidden", "text/plain; charset=utf-8", "This area is read only.");
            return;
        }
        FILE* f = nullptr;
        if (_wfopen_s(&f, full.c_str(), L"wb") != 0 || !f) {
            SendHttpResponse(s, 500, "Internal Server Error", "text/plain; charset=utf-8", "Write failed.");
            return;
        }
        if (!body.empty()) fwrite(body.data(), 1, body.size(), f);
        fclose(f);
        SendHttpResponse(s, 200, "OK", "text/plain; charset=utf-8", "ok");
    }

    void HandleApiRename(SOCKET s, const std::string& body) {
        std::string scope = FormFieldValue(body, "scope");
        if (scope.empty()) scope = "profile";
        const std::wstring profileId = NormalizeProfileId(FormFieldValue(body, "profile"));
        const std::wstring newName = SafeFileName(a2w(FormFieldValue(body, "name").c_str()));
        std::wstring full, root, relNorm;
        bool writable = false;
        if (!ResolveScopePath(scope, profileId, FormFieldValue(body, "path"), full, root, relNorm, writable) || relNorm.empty() || newName.empty()) {
            SendHttpResponse(s, 400, "Bad Request", "text/plain; charset=utf-8", "Bad request.");
            return;
        }
        if (!writable) {
            SendHttpResponse(s, 403, "Forbidden", "text/plain; charset=utf-8", "This area is read only.");
            return;
        }
        const size_t slash = full.find_last_of(L'\\');
        const std::wstring dest = full.substr(0, slash) + L"\\" + newName;
        if (GetFileAttributesW(dest.c_str()) != INVALID_FILE_ATTRIBUTES) {
            SendHttpResponse(s, 409, "Conflict", "text/plain; charset=utf-8", "Name already exists.");
            return;
        }
        if (MoveFileExW(full.c_str(), dest.c_str(), 0)) SendHttpResponse(s, 200, "OK", "text/plain; charset=utf-8", "ok");
        else SendHttpResponse(s, 500, "Internal Server Error", "text/plain; charset=utf-8", "Rename failed.");
    }

    void HandleApiDelete(SOCKET s, const std::string& body) {
        std::string scope = FormFieldValue(body, "scope");
        if (scope.empty()) scope = "profile";
        const std::wstring profileId = NormalizeProfileId(FormFieldValue(body, "profile"));
        std::wstring full, root, relNorm;
        bool writable = false;
        if (!ResolveScopePath(scope, profileId, FormFieldValue(body, "path"), full, root, relNorm, writable) || relNorm.empty()) {
            SendHttpResponse(s, 400, "Bad Request", "text/plain; charset=utf-8", "Bad path.");
            return;
        }
        if (!writable) {
            SendHttpResponse(s, 403, "Forbidden", "text/plain; charset=utf-8", "This area is read only.");
            return;
        }
        const DWORD attrs = GetFileAttributesW(full.c_str());
        if (attrs == INVALID_FILE_ATTRIBUTES) {
            SendHttpResponse(s, 404, "Not Found", "text/plain; charset=utf-8", "Not found.");
            return;
        }
        const bool ok = (attrs & FILE_ATTRIBUTE_DIRECTORY) ? DeleteDirectoryTree(full) : (DeleteFileW(full.c_str()) != 0);
        if (ok) SendHttpResponse(s, 200, "OK", "text/plain; charset=utf-8", "ok");
        else SendHttpResponse(s, 500, "Internal Server Error", "text/plain; charset=utf-8", "Delete failed.");
    }

    void HandleApiMkdir(SOCKET s, const std::string& body) {
        std::string scope = FormFieldValue(body, "scope");
        if (scope.empty()) scope = "profile";
        const std::wstring profileId = NormalizeProfileId(FormFieldValue(body, "profile"));
        const std::wstring name = SafeFileName(a2w(FormFieldValue(body, "name").c_str()));
        std::wstring full, root, relNorm;
        bool writable = false;
        if (!ResolveScopePath(scope, profileId, FormFieldValue(body, "path"), full, root, relNorm, writable) || name.empty()) {
            SendHttpResponse(s, 400, "Bad Request", "text/plain; charset=utf-8", "Bad request.");
            return;
        }
        if (!writable) {
            SendHttpResponse(s, 403, "Forbidden", "text/plain; charset=utf-8", "This area is read only.");
            return;
        }
        if (CreateDirectoryW((full + L"\\" + name).c_str(), nullptr)) SendHttpResponse(s, 200, "OK", "text/plain; charset=utf-8", "ok");
        else SendHttpResponse(s, 500, "Internal Server Error", "text/plain; charset=utf-8", "Could not create folder.");
    }

    void HandleApiUpload(SOCKET s, const std::string& query, const std::map<std::string, std::string>& headers, const std::string& body) {
        std::string scope = QueryValue(query, "scope");
        if (scope.empty()) scope = "profile";
        const std::wstring profileId = NormalizeProfileId(QueryValue(query, "profile"));
        std::wstring full, root, relNorm;
        bool writable = false;
        if (!ResolveScopePath(scope, profileId, QueryValue(query, "path"), full, root, relNorm, writable)) {
            SendHttpResponse(s, 400, "Bad Request", "text/plain; charset=utf-8", "Bad path.");
            return;
        }
        if (!writable) {
            SendHttpResponse(s, 403, "Forbidden", "text/plain; charset=utf-8", "This area is read only.");
            return;
        }
        std::wstring name;
        std::vector<unsigned char> data;
        if (!ExtractMultipartFile(headers, body, name, data)) {
            SendHttpResponse(s, 400, "Bad Request", "text/plain; charset=utf-8", "Upload failed.");
            return;
        }
        EnsureDirectoryTree(full);
        const std::wstring path = full + L"\\" + name;
        FILE* f = nullptr;
        if (_wfopen_s(&f, path.c_str(), L"wb") != 0 || !f) {
            SendHttpResponse(s, 500, "Internal Server Error", "text/plain; charset=utf-8", "Write failed.");
            return;
        }
        if (!data.empty()) fwrite(data.data(), 1, data.size(), f);
        fclose(f);
        WriteLogF(L"Remote explorer upload: %s bytes=%zu", path.c_str(), data.size());
        SendHttpResponse(s, 200, "OK", "text/plain; charset=utf-8", "ok");
    }

    static std::string ExplorerHead() {
        return std::string(R"RFSPA(<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Bandit Remote Files</title><style>)RFSPA") + RemoteStyles() + "</style>" + ThemeBootHtml() + R"RFSPA(</head><body><div class="app">
<header class="top"><a class="brand" id="homebtn"><strong>BANDIT</strong><span class="brand-label">Remote Files</span></a>
<div class="profsel"><label for="prof">Profile</label><select id="prof" onchange="switchProfile(this.value)"></select></div>
<div class="top-actions"><span class="session">PIN <span class="pin" id="pinv"></span></span></div></header>
<div class="shell"><details class="side" id="navigation" open><summary>Browse &amp; diagnostics</summary><div class="navbody"><nav class="rail" id="rail" aria-label="File locations"></nav></div></details>
<main class="main"><div class="browser-head"><div><div class="eyebrow">File browser</div><div class="title-row"><h1 id="folder-title">Game files</h1><span class="access" id="access">Loading</span></div></div>
<div class="toolbar"><label class="filter"><span class="sr-only">Filter files</span><input id="filter" type="search" placeholder="Filter files" oninput="filterList()"></label><button class="btn" id="mkbtn" onclick="mkdir()" hidden>New folder</button><button class="btn primary" id="upbtn" onclick="document.getElementById('up').click()" hidden>Upload files</button><button class="btn ghost" onclick="reload()" aria-label="Refresh folder">Refresh</button></div></div>
<input id="up" type="file" multiple hidden onchange="upload(this.files)">
<div class="bar2" id="bar2"><nav class="crumbs" id="crumbs" aria-label="Folder path"></nav><div id="extra"></div></div>
<div class="list-head" aria-hidden="true"><span>Name</span><span class="size">Size</span><span class="mod">Modified</span><span></span></div>
<div class="list" id="list" aria-label="Files" aria-busy="true"></div>
<footer class="list-footer"><span id="count" role="status">Loading folder</span><span class="drop-hint" id="drop-hint"></span></footer></main></div>
<div id="drop" class="drop">Drop files or folders to upload</div>
<section id="editor" class="editor" aria-label="Text editor"><header class="ehead"><span id="epath" class="pa"></span><div class="toolbar"><button class="btn primary" id="savebtn" onclick="saveEd()">Save changes</button><button class="btn" onclick="reloadEd()">Reload</button><button class="btn ghost" onclick="closeEd()">Close</button></div></header>
<label class="sr-only" for="ta">File contents</label><textarea id="ta" spellcheck="false"></textarea><footer class="editor-footer"><span id="edit-state" role="status">Saved</span><span>Ctrl / Cmd + S to save</span></footer></section>
<div id="toast" class="toast" role="status" aria-live="polite"></div></div><script>)RFSPA";
    }

    static const char* ExplorerScript() {
        return R"RFSPA(
var cur='',edPath='',edOriginal='',canWrite=false,busy=false,saving=false,loadId=0,listing=null,editorFocus=null;
var SCOPES=[
 {g:'Workspace',items:[{s:'profile',l:'Game files'},{s:'saves',l:'Worlds'},{s:'mods',l:'Mods'},{s:'resourcepacks',l:'Resource packs'}]},
 {g:'Diagnostics',items:[{s:'logs',l:'Current logs'},{s:'previous',l:'Previous logs'},{s:'crash',l:'Crash reports'},{s:'runtime',l:'Runtime cache'}]}
];
var ICON_DIR='<svg class="ic dir" aria-hidden="true" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.5"><path d="M3 7V5h7l2 3h9v12H3V7Z"/></svg>';
var ICON_FILE='<svg class="ic" aria-hidden="true" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.5"><path d="M14 3H5v18h14V8Z"/><path d="M14 3v5h5"/></svg>';
function enc(p){return encodeURIComponent(p)}
function join(b,n){return b?b+'/'+n:n}
function q(extra){return '?pin='+enc(CFG.pin)+'&profile='+enc(CFG.profile)+'&scope='+enc(CFG.scope)+(extra||'')}
function el(id){return document.getElementById(id)}
function button(label,run,cls){var b=document.createElement('button');b.type='button';b.className=cls||'';b.textContent=label;b.onclick=run;return b}
function toast(message,persistent){var t=el('toast');t.textContent=message;t.classList.add('show');clearTimeout(t._t);if(!persistent)t._t=setTimeout(function(){t.classList.remove('show')},4000)}
function scopeLabel(sc){for(var g of SCOPES)for(var it of g.items)if(it.s===sc)return it.l;return 'Game files'}
function homeHref(){return '/?pin='+enc(CFG.pin)+'&profile='+enc(CFG.profile)}
function initChrome(){
 var wide=matchMedia('(min-width:761px)');function fitNav(){el('navigation').open=wide.matches}fitNav();wide.addEventListener('change',fitNav);
 el('pinv').textContent=CFG.pin;el('homebtn').href=homeHref();
 CFG.profiles.forEach(function(p){var o=document.createElement('option');o.value=p.id;o.textContent=p.name+' ('+p.id+')';o.selected=p.id===CFG.profile;el('prof').appendChild(o)});
 var rail=el('rail');
 SCOPES.forEach(function(group,index){
  var title=document.createElement('div');title.className='grp';title.textContent=group.g;rail.appendChild(title);
  if(!index){var home=document.createElement('a');home.href=homeHref();home.textContent='Overview';home.id='overview';rail.appendChild(home)}
  group.items.forEach(function(item){var a=document.createElement('a');a.textContent=item.l;a.dataset.scope=item.s;a.href='/browse?pin='+enc(CFG.pin)+'&profile='+enc(CFG.profile)+'&scope='+item.s;a.onclick=function(e){e.preventDefault();go(item.s)};rail.appendChild(a)});
 });
}
function navigationReady(){if(busy||saving){toast('Wait for the transfer to finish');return false}return true}
function switchProfile(id){if(!navigationReady()){el('prof').value=CFG.profile;return}CFG.profile=id;el('homebtn').href=homeHref();el('overview').href=homeHref();load('')}
function go(scope){if(!navigationReady())return;CFG.scope=scope;if(matchMedia('(max-width:760px)').matches)el('navigation').open=false;load('')}
function reload(){if(navigationReady())load(cur)}
function showError(message){
 var box=document.createElement('div');box.className='err';var title=document.createElement('strong');title.textContent='Could not open folder';box.appendChild(title);
 var text=document.createElement('p');text.textContent=message;box.appendChild(text);box.appendChild(button('Try again',reload,'btn secondary'));
 var home=document.createElement('a');home.href=homeHref();home.className='btn ghost';home.textContent='Reconnect';box.appendChild(home);el('list').replaceChildren(box);el('count').textContent='Folder unavailable';
}
async function load(path){
 if(!navigationReady())return;
 var id=++loadId;cur=path||'';listing=null;canWrite=false;
 el('mkbtn').hidden=true;el('upbtn').hidden=true;el('filter').value='';el('filter').disabled=true;el('access').textContent='Loading';el('count').textContent='Loading folder';el('drop-hint').textContent='';el('extra').replaceChildren();
 el('folder-title').textContent=scopeLabel(CFG.scope);el('list').setAttribute('aria-busy','true');el('list').replaceChildren();
 document.querySelectorAll('.rail a[data-scope]').forEach(function(a){var active=a.dataset.scope===CFG.scope;a.classList.toggle('active',active);if(active)a.setAttribute('aria-current','page');else a.removeAttribute('aria-current');a.href='/browse?pin='+enc(CFG.pin)+'&profile='+enc(CFG.profile)+'&scope='+a.dataset.scope});
 renderCrumbs();el('bar2').hidden=!cur;history.replaceState(null,'','/browse'+q('&path='+enc(cur)));
 try{
  var r=await fetch('/api/list'+q('&path='+enc(cur)));
  if(!r.ok)throw Error(r.status===401?'The PIN has expired. Reconnect using the PIN on your Xbox.':'Check the Xbox connection and try again.');
  var d=await r.json();if(!d.ok)throw Error('This folder is unavailable. Choose another location.');if(id!==loadId)return;
  canWrite=!!d.writable;listing=d;el('filter').disabled=false;el('mkbtn').hidden=!canWrite;el('upbtn').hidden=!canWrite;el('access').textContent=canWrite?'Read & write':'Read only';
  el('drop-hint').textContent=canWrite?'Drop files or folders to upload':'Downloads available';renderExtra(d);filterList();
 }catch(e){if(id!==loadId)return;el('access').textContent='Unavailable';showError(e.message||'Connection lost')}
 finally{if(id===loadId)el('list').setAttribute('aria-busy','false')}
}
function renderCrumbs(){
 var c=el('crumbs');c.hidden=!cur;var root=button(scopeLabel(CFG.scope),function(){load('')});root.title=scopeLabel(CFG.scope);c.replaceChildren(root);var acc='';
 cur.split('/').filter(Boolean).forEach(function(segment){acc=join(acc,segment);var path=acc,crumb=button(segment,function(){load(path)});crumb.title=segment;c.appendChild(crumb)});
}
function renderExtra(d){
 el('extra').replaceChildren();if(!d.world)return;
 var a=document.createElement('a');a.className='btn sm';a.textContent='Export world zip';a.href='/download?file=export:world:'+enc(cur.split('/')[0])+'&profile='+enc(CFG.profile)+'&pin='+enc(CFG.pin);el('extra').appendChild(a);
}
function rowActs(entry,path){
 var details=document.createElement('details');details.className='acts';
 var summary=document.createElement('summary');summary.textContent='\u00b7\u00b7\u00b7';summary.setAttribute('aria-label','Actions for '+entry.name);details.appendChild(summary);
 var menu=document.createElement('div');menu.className='action-menu';
 if(entry.dir){
  menu.appendChild(button('Open folder',function(){load(path)}));
  if(entry.world){var a=document.createElement('a');a.textContent='Export world zip';a.href='/download?file=export:world:'+enc(entry.name)+'&profile='+enc(CFG.profile)+'&pin='+enc(CFG.pin);menu.appendChild(a)}
 }else{
  if(entry.text)menu.appendChild(button(canWrite?'Edit file':'View file',function(){openEd(path)}));
  menu.appendChild(button('Download',function(){dl(path)}));
 }
 if(canWrite){menu.appendChild(button('Rename',function(){rename(path,entry.name)}));menu.appendChild(button('Delete',function(){del(path,entry.dir)},'danger'))}
 menu.addEventListener('click',function(){details.open=false});details.appendChild(menu);
 details.addEventListener('toggle',function(){
  if(!details.open)return;
  document.querySelectorAll('.acts[open]').forEach(function(other){if(other!==details)other.open=false});
  var rect=summary.getBoundingClientRect(),height=menu.offsetHeight;
  menu.style.left=Math.max(8,Math.min(innerWidth-176,rect.right-168))+'px';
  menu.style.top=Math.max(8,Math.min(innerHeight-height-8,rect.bottom+height+4<innerHeight?rect.bottom+4:rect.top-height-4))+'px';
 });return details;
}
function filterList(){
 if(!listing)return;
 var needle=el('filter').value.toLocaleLowerCase(),entries=listing.entries.filter(function(entry){return entry.name.toLocaleLowerCase().includes(needle)}),list=el('list');list.replaceChildren();
 el('count').textContent=needle?entries.length+' of '+listing.entries.length+' items':entries.length+' '+(entries.length===1?'item':'items');
 if(!entries.length){var empty=document.createElement('div');empty.className='empty';var heading=document.createElement('strong');heading.textContent=needle?'No matching files':'This folder is empty';empty.appendChild(heading);empty.appendChild(document.createTextNode(needle?'Try a different filename.':canWrite?'Upload files or drop them into this window.':'Files will appear here when the launcher creates them.'));list.appendChild(empty);return}
 entries.forEach(function(entry){
  var path=join(cur,entry.name),row=document.createElement('div');row.className='row';
  var name=document.createElement('div');name.className='nm';name.innerHTML=entry.dir?ICON_DIR:ICON_FILE;
  var open=button(entry.name,function(){if(entry.dir)load(path);else if(entry.text)openEd(path);else dl(path)},'t');open.title=entry.name;name.appendChild(open);row.appendChild(name);
  var size=document.createElement('div');size.className='size';size.textContent=entry.dir?'':entry.sizeText;row.appendChild(size);
  var modified=document.createElement('div');modified.className='mod';modified.textContent=entry.modified||'';row.appendChild(modified);row.appendChild(rowActs(entry,path));list.appendChild(row);
 });
}
function dl(path){window.location='/download-path'+q('&path='+enc(path))}
async function change(action,path,extra,success){
 if(!canWrite||!navigationReady())return;
 busy=true;
 try{var r=await fetch('/api/'+action,{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'pin='+enc(CFG.pin)+'&profile='+enc(CFG.profile)+'&scope='+enc(CFG.scope)+'&path='+enc(path)+(extra||'')});if(!r.ok)throw Error();toast(success)}
 catch(e){toast('Could not '+action+'. Check the connection and try again.')}
 finally{busy=false;load(cur)}
}
function del(path,isDir){if(!canWrite||!navigationReady())return;if(confirm('Delete '+(isDir?'folder':'file')+' "'+path.split('/').pop()+'"?'+(isDir?' Removes everything inside.':'')))change('delete',path,'','Deleted')}
function rename(path,old){if(!canWrite||!navigationReady())return;var name=prompt('Rename to',old);if(name&&name!==old)change('rename',path,'&name='+enc(name),'Renamed')}
function mkdir(){if(!canWrite||!navigationReady())return;var name=prompt('New folder name');if(name)change('mkdir',cur,'&name='+enc(name),'Folder created')}
)RFSPA"
        R"RFSPA(async function uploadOne(file,sub){
 var data=new FormData();data.append('file',file);
 var r=await fetch('/api/upload'+q('&path='+enc(sub?join(cur,sub):cur)),{method:'POST',body:data});return r.ok;
}
async function upload(files){
 if(!canWrite||!files||!files.length||!navigationReady())return;
 busy=true;var ok=0,total=files.length;
 try{for(var i=0;i<total;i++){toast('Uploading '+(i+1)+' of '+total,true);try{if(await uploadOne(files[i],''))ok++}catch(e){}}}
 finally{busy=false;toast(ok+' of '+total+' files uploaded'+(ok<total?'. Some uploads failed.':''));el('up').value='';load(cur)}
}
function entryFile(entry){return new Promise(function(resolve,reject){entry.file(resolve,reject)})}
function entryBatch(reader){return new Promise(function(resolve,reject){reader.readEntries(resolve,reject)})}
async function walkEntry(entry,sub,out){
 if(entry.isFile){out.push({entry:entry,sub:sub});return}if(!entry.isDirectory)return;
 var next=join(sub,entry.name),reader=entry.createReader(),batch;
 // readEntries returns partial batches until the directory is exhausted
 do{batch=await entryBatch(reader);for(var i=0;i<batch.length;i++)await walkEntry(batch[i],next,out)}while(batch.length)
}
async function uploadDropped(roots){
 if(!canWrite||!navigationReady())return;
 busy=true;var flat=[],ok=0,scanFailed=false;
 try{
  toast('Reading dropped folders',true);
  for(var root of roots){try{await walkEntry(root,'',flat)}catch(e){scanFailed=true}}
  for(var i=0;i<flat.length;i++){toast('Uploading '+(i+1)+' of '+flat.length,true);try{if(await uploadOne(await entryFile(flat[i].entry),flat[i].sub))ok++}catch(e){}}
 }finally{busy=false;toast(ok+' of '+flat.length+' files uploaded'+(scanFailed||ok<flat.length?'. Some files could not be uploaded.':''));load(cur)}
}
function dirty(){return el('editor').classList.contains('show')&&el('ta').value!==edOriginal}
function updateEditState(){var state=el('edit-state');state.classList.remove('danger');state.textContent=!canWrite?'Read only':dirty()?'Unsaved changes':'Saved'}
async function openEd(path){
 if(!navigationReady())return;
 var requestId=loadId;
 try{
  var r=await fetch('/api/raw'+q('&path='+enc(path)));if(!r.ok)throw Error();var text=await r.text();if(requestId!==loadId)return;
  if(!el('editor').classList.contains('show'))editorFocus=document.activeElement.closest('.acts')?.querySelector('summary')||document.activeElement;edPath=path;edOriginal=text;el('epath').textContent=scopeLabel(CFG.scope)+' / '+path;el('ta').value=text;el('ta').readOnly=!canWrite;
  el('savebtn').hidden=!canWrite;el('editor').classList.add('show');document.querySelector('.app>.shell').inert=true;document.querySelector('.app>.top').inert=true;updateEditState();el('ta').focus();
 }catch(e){toast('Could not open file')}
}
function reloadEd(){if(navigationReady()&&(!dirty()||confirm('Discard unsaved changes and reload this file?')))openEd(edPath)}
function closeEd(){
 if(!navigationReady()||(dirty()&&!confirm('Discard unsaved changes?')))return;
 el('editor').classList.remove('show');document.querySelector('.app>.shell').inert=false;document.querySelector('.app>.top').inert=false;if(editorFocus&&editorFocus.isConnected)editorFocus.focus();
}
async function saveEd(){
 if(!canWrite||saving||!el('editor').classList.contains('show'))return;
 saving=true;el('savebtn').disabled=true;el('edit-state').textContent='Saving';var text=el('ta').value;
 try{var r=await fetch('/api/write'+q('&path='+enc(edPath)),{method:'POST',body:text});if(!r.ok)throw Error();edOriginal=text;updateEditState();toast('Saved')}
 catch(e){el('edit-state').classList.add('danger');el('edit-state').textContent='Save failed. Changes are still here.';toast('Could not save. Check the connection and try again.')}
 finally{saving=false;el('savebtn').disabled=false}
}
el('ta').addEventListener('input',updateEditState);
el('ta').addEventListener('keydown',function(e){
 if(e.key==='Tab'&&!e.shiftKey&&!this.readOnly){e.preventDefault();var start=this.selectionStart;this.setRangeText('  ',start,this.selectionEnd,'end');updateEditState()}
 if((e.ctrlKey||e.metaKey)&&e.key==='s'){e.preventDefault();saveEd()}
});
document.addEventListener('keydown',function(e){if(e.key==='Escape'){document.querySelectorAll('.acts[open]').forEach(function(menu){menu.open=false;menu.querySelector('summary').focus()});if(el('editor').classList.contains('show'))closeEd()}});
function closeMenus(){document.querySelectorAll('.acts[open]').forEach(function(menu){menu.open=false})}
el('list').addEventListener('scroll',closeMenus);window.addEventListener('resize',closeMenus);
document.addEventListener('click',function(e){document.querySelectorAll('.acts[open]').forEach(function(menu){if(!menu.contains(e.target))menu.open=false})});
window.addEventListener('beforeunload',function(e){if(dirty()||busy||saving){e.preventDefault();e.returnValue=''}});
var dz=el('drop'),dragCount=0;
function isFileDrag(e){return e.dataTransfer&&Array.from(e.dataTransfer.types).includes('Files')}
window.addEventListener('dragenter',function(e){if(!isFileDrag(e))return;e.preventDefault();if(!canWrite||busy||el('editor').classList.contains('show'))return;dragCount++;dz.classList.add('show')});
window.addEventListener('dragover',function(e){if(isFileDrag(e))e.preventDefault()});
window.addEventListener('dragleave',function(e){if(!isFileDrag(e))return;dragCount--;if(dragCount<=0)dz.classList.remove('show')});
window.addEventListener('drop',function(e){
 if(!isFileDrag(e))return;e.preventDefault();dragCount=0;dz.classList.remove('show');if(!canWrite||busy||el('editor').classList.contains('show'))return;
 var data=e.dataTransfer,roots=[];
 // browser entries must be captured before the first await
 if(data.items)for(var item of data.items){var entry=item.webkitGetAsEntry?item.webkitGetAsEntry():null;if(entry)roots.push(entry)}
 if(roots.length)uploadDropped(roots);else if(data.files.length)upload(data.files);
});
initChrome();load(CFG.path||'');
)RFSPA";
    }

    std::string ExplorerHtml(const std::string& query) {
        std::string scope = QueryValue(query, "scope");
        if (scope.empty()) scope = "profile";
        const std::wstring profileId = NormalizeProfileId(QueryValue(query, "profile"));

        std::ostringstream cfg;
        cfg << "var CFG={pin:\"" << pin_ << "\",scope:\"" << ScriptJsonEscape(scope) << "\",profile:\""
            << ScriptJsonEscape(w2a(profileId)) << "\",path:\"" << ScriptJsonEscape(QueryValue(query, "path")) << "\",profiles:[";
        const std::vector<Profile> profiles = LoadProfiles(runtimeRoot_);
        bool firstP = true;
        for (const auto& p : profiles) {
            if (!firstP) cfg << ",";
            firstP = false;
            cfg << "{id:\"" << ScriptJsonEscape(w2a(p.id)) << "\",name:\"" << ScriptJsonEscape(w2a(p.name)) << "\"}";
        }
        cfg << "]};";

        std::ostringstream out;
        out << ExplorerHead() << cfg.str() << ExplorerScript() << "</script></body></html>";
        return out.str();
    }

    void ServeBrowseDownload(SOCKET s, const std::string& query) {
        const std::string scope = QueryValue(query, "scope");
        std::wstring rel = a2w(UrlDecode(QueryValue(query, "path")).c_str());
        std::replace(rel.begin(), rel.end(), L'/', L'\\');
        if (!IsSafeRelativePath(rel) || rel.empty()) {
            SendHttpResponse(s, 400, "Bad Request", "text/plain; charset=utf-8", "Bad path.");
            return;
        }

        const std::wstring profileId = NormalizeProfileId(QueryValue(query, "profile"));
        std::wstring root, title;
        bool writable = false;
        if (!ResolveBrowseScope(scope, profileId, root, title, writable)) {
            SendHttpResponse(s, 400, "Bad Request", "text/plain; charset=utf-8", "Bad scope.");
            return;
        }
        const std::wstring path = root + L"\\" + rel;
        const DWORD attrs = GetFileAttributesW(path.c_str());
        if (attrs == INVALID_FILE_ATTRIBUTES || (attrs & FILE_ATTRIBUTE_DIRECTORY)) {
            SendHttpResponse(s, 404, "Not Found", "text/plain; charset=utf-8", "File not found.");
            return;
        }
        const size_t slash = rel.find_last_of(L'\\');
        const std::wstring name = slash == std::wstring::npos ? rel : rel.substr(slash + 1);
        SendHttpFile(s, path, w2a(name), GuessDownloadContentType(name));
    }

    void ServeDownload(SOCKET s, const std::string& query) {
        const std::string file = QueryValue(query, "file");
        const std::wstring profileId = NormalizeProfileId(QueryValue(query, "profile"));
        std::wstring path;
        std::wstring name;
        if (file.rfind("log:", 0) == 0) {
            name = a2w(file.substr(4).c_str());
            if (name.find(L'\\') != std::wstring::npos || name.find(L'/') != std::wstring::npos) {
                SendHttpResponse(s, 400, "Bad Request", "text/plain; charset=utf-8", "Bad file.");
                return;
            }
            path = LogsCurrentDir(runtimeRoot_) + L"\\" + name;
        } else if (file.rfind("game-log:", 0) == 0) {
            name = a2w(file.substr(9).c_str());
            if (name.find(L'\\') != std::wstring::npos || name.find(L'/') != std::wstring::npos) {
                SendHttpResponse(s, 400, "Bad Request", "text/plain; charset=utf-8", "Bad file.");
                return;
            }
            path = ProfileGameDir(runtimeRoot_, profileId) + L"\\logs\\" + name;
        } else if (file.rfind("game:", 0) == 0) {
            name = a2w(file.substr(5).c_str());
            if (name.find(L'\\') != std::wstring::npos || name.find(L'/') != std::wstring::npos) {
                SendHttpResponse(s, 400, "Bad Request", "text/plain; charset=utf-8", "Bad file.");
                return;
            }
            path = ProfileGameDir(runtimeRoot_, profileId) + L"\\" + name;
        } else if (file.rfind("crash:", 0) == 0) {
            name = a2w(UrlDecode(file.substr(6)).c_str());
            if (name.find(L'\\') != std::wstring::npos || name.find(L'/') != std::wstring::npos) {
                SendHttpResponse(s, 400, "Bad Request", "text/plain; charset=utf-8", "Bad file.");
                return;
            }
            path = CrashReportsDir(runtimeRoot_) + L"\\" + name;
        } else if (file == "export:profile") {
            EnsureProfilesInitialized(runtimeRoot_);
            const std::wstring active = profileId;
            if (active == kVanillaProfileId) {
                SendHttpResponse(s, 400, "Bad Request", "text/plain; charset=utf-8", "Vanilla cannot be exported.");
                return;
            }
            path = DefaultProfileExportPath(runtimeRoot_, active);
            std::wstring exportError;
            if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES &&
                !ExportProfileMrpack(runtimeRoot_, active, path, exportError)) {
                SendHttpResponse(s, 500, "Internal Server Error", "text/plain; charset=utf-8", w2a(exportError.c_str()));
                return;
            }
            const size_t slash = path.find_last_of(L'\\');
            name = slash == std::wstring::npos ? path : path.substr(slash + 1);
        } else if (file.rfind("export:world:", 0) == 0) {
            const std::wstring worldName = a2w(UrlDecode(file.substr(13)).c_str());
            if (!IsSafeWorldName(worldName)) {
                SendHttpResponse(s, 400, "Bad Request", "text/plain; charset=utf-8", "Bad world.");
                return;
            }
            EnsureProfilesInitialized(runtimeRoot_);
            const std::wstring active = profileId;
            path = DefaultWorldExportPath(runtimeRoot_, worldName);
            std::wstring exportError;
            if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES &&
                !ExportWorldZip(runtimeRoot_, active, worldName, path, exportError)) {
                SendHttpResponse(s, 500, "Internal Server Error", "text/plain; charset=utf-8", w2a(exportError.c_str()));
                return;
            }
            name = SafeFileName(worldName) + L".zip";
        } else {
            SendHttpResponse(s, 400, "Bad Request", "text/plain; charset=utf-8", "Bad file.");
            return;
        }
        SendHttpFile(s, path, w2a(name), GuessDownloadContentType(name));
    }

    bool ExtractMultipartFile(
        const std::map<std::string, std::string>& headers,
        const std::string& body,
        std::wstring& fileName,
        std::vector<unsigned char>& data) {
        auto it = headers.find("content-type");
        if (it == headers.end()) return false;
        const std::string marker = "boundary=";
        const size_t bpos = it->second.find(marker);
        if (bpos == std::string::npos) return false;
        std::string rawBoundary = it->second.substr(bpos + marker.size());
        if (!rawBoundary.empty() && rawBoundary.front() == '"' && rawBoundary.back() == '"') {
            rawBoundary = rawBoundary.substr(1, rawBoundary.size() - 2);
        }
        std::string boundary = "--" + rawBoundary;

        size_t pos = 0;
        while (true) {
            const size_t partStart = body.find(boundary, pos);
            if (partStart == std::string::npos) return false;
            const size_t headerStart = body.find("\r\n", partStart);
            if (headerStart == std::string::npos) return false;
            const size_t headerEnd = body.find("\r\n\r\n", headerStart + 2);
            if (headerEnd == std::string::npos) return false;
            const std::string partHeader = body.substr(headerStart + 2, headerEnd - headerStart - 2);
            if (partHeader.find("name=\"file\"") != std::string::npos) {
                const size_t fn = partHeader.find("filename=\"");
                if (fn == std::string::npos) return false;
                const size_t fnStart = fn + 10;
                const size_t fnEnd = partHeader.find('"', fnStart);
                if (fnEnd == std::string::npos) return false;
                fileName = SafeFileName(a2w(partHeader.substr(fnStart, fnEnd - fnStart).c_str()));
                const size_t dataStart = headerEnd + 4;
                size_t dataEnd = body.find("\r\n" + boundary, dataStart);
                if (dataEnd == std::string::npos || dataEnd < dataStart) return false;
                data.assign(body.begin() + dataStart, body.begin() + dataEnd);
                return !fileName.empty() && !data.empty();
            }
            pos = headerEnd + 4;
        }
    }

    bool ExtractMultipartTextField(
        const std::map<std::string, std::string>& headers,
        const std::string& body,
        const std::string& fieldName,
        std::string& value) {
        auto it = headers.find("content-type");
        if (it == headers.end()) return false;
        const std::string marker = "boundary=";
        const size_t bpos = it->second.find(marker);
        if (bpos == std::string::npos) return false;
        std::string rawBoundary = it->second.substr(bpos + marker.size());
        if (!rawBoundary.empty() && rawBoundary.front() == '"' && rawBoundary.back() == '"') {
            rawBoundary = rawBoundary.substr(1, rawBoundary.size() - 2);
        }
        const std::string boundary = "--" + rawBoundary;
        const std::string nameNeedle = "name=\"" + fieldName + "\"";

        size_t pos = 0;
        while (true) {
            const size_t partStart = body.find(boundary, pos);
            if (partStart == std::string::npos) return false;
            const size_t headerStart = body.find("\r\n", partStart);
            if (headerStart == std::string::npos) return false;
            const size_t headerEnd = body.find("\r\n\r\n", headerStart + 2);
            if (headerEnd == std::string::npos) return false;
            const std::string partHeader = body.substr(headerStart + 2, headerEnd - headerStart - 2);
            const size_t dataStart = headerEnd + 4;
            size_t dataEnd = body.find("\r\n" + boundary, dataStart);
            if (dataEnd == std::string::npos || dataEnd < dataStart) return false;
            if (partHeader.find(nameNeedle) != std::string::npos && partHeader.find("filename=\"") == std::string::npos) {
                value = body.substr(dataStart, dataEnd - dataStart);
                return true;
            }
            pos = dataEnd;
        }
    }

    void HandleUpload(SOCKET s, const std::map<std::string, std::string>& headers, const std::string& body, bool modUpload) {
        std::wstring name;
        std::vector<unsigned char> data;
        if (!ExtractMultipartFile(headers, body, name, data)) {
            SendHttpResponse(s, 400, "Bad Request", "text/html; charset=utf-8", Layout("Upload failed", "<h1>Upload failed</h1><p>No file was received.</p>"));
            return;
        }

        const std::wstring lower = ToLowerW(name);
        const bool allowed = modUpload
            ? (lower.size() >= 4 && lower.substr(lower.size() - 4) == L".jar")
            : (lower.size() >= 4 && lower.substr(lower.size() - 4) == L".zip");
        if (!allowed) {
            SendHttpResponse(s, 400, "Bad Request", "text/html; charset=utf-8", Layout("Upload failed", "<h1>Upload failed</h1><p>Wrong file type.</p>"));
            return;
        }

        std::wstring dir;
        std::string profileText;
        ExtractMultipartTextField(headers, body, "profile", profileText);
        EnsureProfilesInitialized(runtimeRoot_);
        const std::wstring active = NormalizeProfileId(profileText);
        if (modUpload) {
            if (active == kVanillaProfileId) {
                SendHttpResponse(s, 400, "Bad Request", "text/html; charset=utf-8", Layout("Upload failed", "<h1>Upload failed</h1><p>Vanilla is read only. Create or select a profile first.</p>"));
                return;
            }
            dir = ProfileModsDir(runtimeRoot_, active);
        } else {
            EnsureProfileGameDataInitialized(runtimeRoot_, active);
            dir = ProfileGameDir(runtimeRoot_, active) + L"\\resourcepacks";
        }

        EnsureDirectoryTree(dir);
        const std::wstring path = dir + L"\\" + name;
        FILE* f = nullptr;
        if (_wfopen_s(&f, path.c_str(), L"wb") != 0 || !f) {
            SendHttpResponse(s, 500, "Internal Server Error", "text/html; charset=utf-8", Layout("Upload failed", "<h1>Upload failed</h1><p>Could not write the file.</p>"));
            return;
        }
        const bool ok = fwrite(data.data(), 1, data.size(), f) == data.size();
        fclose(f);
        if (!ok) {
            DeleteFileW(path.c_str());
            SendHttpResponse(s, 500, "Internal Server Error", "text/html; charset=utf-8", Layout("Upload failed", "<h1>Upload failed</h1><p>Could not finish writing the file.</p>"));
            return;
        }

        WriteLogF(L"Remote file upload saved: %s bytes=%zu", path.c_str(), data.size());
        SendHttpResponse(s, 200, "OK", "text/html; charset=utf-8",
            Layout("Upload complete", "<div class=\"top\"><h1>Upload complete</h1><a class=\"pill\" href=\"/?pin=" + pin_ + "\">Files home</a></div><p>Saved " + HtmlEscape(name) + ".</p>"));
    }

    void HandleDatapackUpload(SOCKET s, const std::map<std::string, std::string>& headers, const std::string& body) {
        std::wstring name;
        std::vector<unsigned char> data;
        if (!ExtractMultipartFile(headers, body, name, data)) {
            SendHttpResponse(s, 400, "Bad Request", "text/html; charset=utf-8", Layout("Upload failed", "<h1>Upload failed</h1><p>No datapack file was received.</p>"));
            return;
        }

        const std::wstring lower = ToLowerW(name);
        if (lower.size() < 4 || lower.substr(lower.size() - 4) != L".zip") {
            SendHttpResponse(s, 400, "Bad Request", "text/html; charset=utf-8", Layout("Upload failed", "<h1>Upload failed</h1><p>Datapacks must be .zip files.</p>"));
            return;
        }

        std::string saveText;
        if (!ExtractMultipartTextField(headers, body, "save", saveText)) {
            SendHttpResponse(s, 400, "Bad Request", "text/html; charset=utf-8", Layout("Upload failed", "<h1>Upload failed</h1><p>No world was selected.</p>"));
            return;
        }

        const std::wstring saveName = a2w(saveText.c_str());
        if (!IsSafeWorldName(saveName)) {
            SendHttpResponse(s, 400, "Bad Request", "text/html; charset=utf-8", Layout("Upload failed", "<h1>Upload failed</h1><p>Bad world name.</p>"));
            return;
        }

        std::string profileText;
        ExtractMultipartTextField(headers, body, "profile", profileText);
        EnsureProfilesInitialized(runtimeRoot_);
        const std::wstring active = NormalizeProfileId(profileText);
        EnsureProfileGameDataInitialized(runtimeRoot_, active);
        const std::wstring saveDir = ProfileGameDir(runtimeRoot_, active) + L"\\saves\\" + saveName;
        if (!DirectoryExists(saveDir)) {
            SendHttpResponse(s, 404, "Not Found", "text/html; charset=utf-8", Layout("Upload failed", "<h1>Upload failed</h1><p>The selected world was not found.</p>"));
            return;
        }

        const std::wstring dir = saveDir + L"\\datapacks";
        EnsureDirectoryTree(dir);
        const std::wstring path = dir + L"\\" + name;
        FILE* f = nullptr;
        if (_wfopen_s(&f, path.c_str(), L"wb") != 0 || !f) {
            SendHttpResponse(s, 500, "Internal Server Error", "text/html; charset=utf-8", Layout("Upload failed", "<h1>Upload failed</h1><p>Could not write the datapack.</p>"));
            return;
        }
        const bool ok = fwrite(data.data(), 1, data.size(), f) == data.size();
        fclose(f);
        if (!ok) {
            DeleteFileW(path.c_str());
            SendHttpResponse(s, 500, "Internal Server Error", "text/html; charset=utf-8", Layout("Upload failed", "<h1>Upload failed</h1><p>Could not finish writing the datapack.</p>"));
            return;
        }

        WriteLogF(L"Remote datapack upload saved: %s bytes=%zu", path.c_str(), data.size());
        SendHttpResponse(s, 200, "OK", "text/html; charset=utf-8",
            Layout("Upload complete", "<div class=\"top\"><h1>Upload complete</h1><a class=\"pill\" href=\"/?pin=" + pin_ + "\">Files home</a></div><p>Saved " + HtmlEscape(name) + " to " + HtmlEscape(saveName) + ".</p>"));
    }
};

static RemoteFileServer g_remoteFileServer;

void StartRemoteFileServer(const std::wstring& runtimeRoot) {
    g_remoteFileServer.Start(runtimeRoot);
}

void StopRemoteFileServer() {
    g_remoteFileServer.Stop();
}

bool RemoteFileServerRunning() { return g_remoteFileServer.Running(); }
std::wstring RemoteFileServerUrl() { return g_remoteFileServer.Url(); }
std::string RemoteFileServerPin() { return g_remoteFileServer.Pin(); }
