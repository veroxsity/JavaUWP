#include "remote_file_server.h"

#include "http_client.h"
#include "launcher_common.h"
#include "modpack_io.h"
#include "profiles.h"
#include "world_io.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <map>
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

static std::string JsonEscape(const std::string& s) {
    std::string o;
    for (char c : s) {
        switch (c) {
        case '"': o += "\\\""; break;
        case '\\': o += "\\\\"; break;
        case '\n': o += "\\n"; break;
        case '\r': o += "\\r"; break;
        case '\t': o += "\\t"; break;
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

static std::string GenerateRemotePin() {
    unsigned value = 0;
    if (BCryptGenRandom(nullptr, reinterpret_cast<PUCHAR>(&value), sizeof(value), BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0) {
        value = static_cast<unsigned>(GetTickCount64());
    }
    value = 100000 + (value % 900000);
    char pin[16] = {};
    sprintf_s(pin, "%06u", value);
    return pin;
}

class RemoteFileServer {
public:
    void Start(const std::wstring& runtimeRoot) {
        if (running_.load()) return;
        if (thread_.joinable()) thread_.join();
        runtimeRoot_ = runtimeRoot;
        pin_ = GenerateRemotePin();
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

    std::wstring Url() const {
        return L"http://" + a2w(LocalAddress().c_str()) + L":" + std::to_wstring(port_) + L"/?pin=" + a2w(pin_.c_str());
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
    int port_ = kPort;

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
        WriteLogF(L"Remote file server started url=%s pin=%s", Url().c_str(), a2w(pin_.c_str()).c_str());

        while (!stop_.load()) {
            fd_set readSet;
            FD_ZERO(&readSet);
            FD_SET(s, &readSet);
            timeval tv = {};
            tv.tv_sec = 0;
            tv.tv_usec = 250000;
            const int ready = select(0, &readSet, nullptr, nullptr, &tv);
            if (ready <= 0) continue;
            SOCKET client = accept(s, nullptr, nullptr);
            if (client == INVALID_SOCKET) continue;
            if (stop_.load()) {
                closesocket(client);
                break;
            }
            clientSocket_.store(client);
            HandleClient(client);
            clientSocket_.compare_exchange_strong(client, INVALID_SOCKET);
            closesocket(client);
        }

        SOCKET old = listenSocket_.exchange(INVALID_SOCKET);
        if (old != INVALID_SOCKET) closesocket(old);
        WSACleanup();
        WriteLog(L"Remote file server stopped");
    }

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
        if (contentLength > 512ull * 1024ull * 1024ull) return false;

        body = data.substr(headerEnd + 4);
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
        return body.find("name=\"pin\"\r\n\r\n" + pin_) != std::string::npos;
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

    std::string Layout(const std::string& title, const std::string& body) {
        std::ostringstream html;
        html << "<!doctype html><html><head><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
            << "<title>" << title << "</title><style>"
            << ":root{color-scheme:dark;--bg:#071018;--panel:#101a24;--panel-2:#0c141d;--line:#243444;--muted:#9eb0bf;--text:#edf4f8;--accent:#76c990;--accent-soft:#1a2d24;--danger:#ee867a;}"
            << "*{box-sizing:border-box}body{font:15px/1.5 system-ui,Segoe UI,sans-serif;background:radial-gradient(circle at top,#102030 0,#071018 42%);color:var(--text);margin:0;padding:28px}"
            << "main{max-width:1240px;margin:0 auto}.top{display:flex;justify-content:space-between;gap:18px;align-items:flex-start;margin:0 0 22px}"
            << "h1{font-size:30px;line-height:1.1;margin:0 0 8px}h2{font-size:18px;margin:0 0 10px}h3{font-size:15px;margin:0 0 8px}.muted{color:var(--muted)}a{color:#89dda7;text-decoration:none}a:hover{text-decoration:underline}code{font:13px/1.4 Consolas,ui-monospace,monospace;color:#d7e7f2}"
            << ".shell{display:grid;grid-template-columns:248px minmax(0,1fr);gap:18px;align-items:start}.side{border:1px solid var(--line);background:rgba(16,26,36,.96);border-radius:12px;padding:14px;position:sticky;top:20px}.side-title{font-size:11px;text-transform:uppercase;color:var(--muted);letter-spacing:.1em;margin:10px 0 8px}.side-title:first-child{margin-top:0}.nav{display:grid;gap:4px}.nav a{display:flex;justify-content:space-between;gap:10px;border-radius:8px;padding:9px 11px;color:var(--text);border:1px solid transparent;min-height:38px;align-items:center}.nav a:hover,.nav a.active{background:#13202c;border-color:#2a3d4f;text-decoration:none}.nav small{color:var(--muted)}"
            << ".content{display:grid;gap:16px}.hero{border:1px solid var(--line);background:linear-gradient(180deg,rgba(18,30,40,.98),rgba(12,20,29,.98));border-radius:12px;padding:20px}.hero-row{display:flex;justify-content:space-between;gap:16px;align-items:flex-start}.hero-actions{display:flex;gap:8px;flex-wrap:wrap}.stats{display:flex;gap:8px;flex-wrap:wrap;margin-top:14px}.stat,.pill{display:inline-flex;align-items:center;gap:6px;border:1px solid var(--line);border-radius:999px;padding:7px 12px;min-height:34px;background:#0b141d;color:var(--text);white-space:nowrap}.stat span,.pill span{color:var(--muted)}.pin{font-family:Consolas,ui-monospace,monospace;letter-spacing:.12em}"
            << ".section{border:1px solid var(--line);background:rgba(16,26,36,.96);border-radius:12px;padding:18px}.section-head{display:flex;justify-content:space-between;gap:12px;align-items:flex-start;margin-bottom:14px}.section-note{margin:0;color:var(--muted);font-size:14px}"
            << ".grid{display:grid;grid-template-columns:1fr 1fr;gap:14px}.panel{background:var(--panel-2);border:1px solid var(--line);border-radius:10px;padding:16px}.stack{display:grid;gap:14px}"
            << ".world-list{display:grid;gap:10px}.world-card{display:flex;justify-content:space-between;gap:12px;align-items:center;border:1px solid var(--line);border-radius:10px;padding:12px 14px;background:#0b141d}.world-card strong{display:block;margin-bottom:2px}.world-actions{display:flex;gap:8px;flex-wrap:wrap;justify-content:flex-end}"
            << ".tiles{display:grid;grid-template-columns:repeat(auto-fit,minmax(180px,1fr));gap:10px}.tile{display:block;border:1px solid var(--line);border-radius:10px;padding:14px;background:#0b141d}.tile strong{display:block;margin-bottom:4px}.tile:hover{text-decoration:none;background:#13202c}"
            << ".quick-links{display:grid;grid-template-columns:repeat(auto-fit,minmax(220px,1fr));gap:10px}.quick-link{display:block;border:1px solid var(--line);border-radius:10px;padding:14px;background:#0b141d}.quick-link strong{display:block;margin-bottom:4px}.quick-link:hover{text-decoration:none;background:#13202c}"
            << ".toolbar{display:flex;gap:8px;flex-wrap:wrap;align-items:center}.path{font-family:Consolas,ui-monospace,monospace;color:#d5e2ed;word-break:break-all}.crumbs{display:flex;gap:7px;flex-wrap:wrap;margin-top:10px}.crumbs a{display:inline-flex;align-items:center;border:1px solid var(--line);border-radius:999px;padding:5px 10px;background:#0b141d}.browse-head{display:grid;gap:12px;border:1px solid var(--line);background:rgba(16,26,36,.96);border-radius:12px;padding:14px}.filebox{overflow:auto;border:1px solid var(--line);border-radius:10px;background:#0b141d}"
            << "table{width:100%;border-collapse:collapse;min-width:620px}th,td{text-align:left;padding:11px 13px;border-bottom:1px solid #1c2a38}th{color:var(--muted);font-weight:600;background:#101a24;font-size:13px}.type{width:92px;color:var(--muted)}.size{width:120px;color:var(--muted);font-family:Consolas,ui-monospace,monospace;font-size:13px}.actions{width:170px}"
            << "label{display:block;color:var(--muted);font-size:13px;margin:0 0 7px}.field{display:grid;gap:8px;margin-top:10px}input,select,button{font:inherit;padding:10px 12px;border-radius:8px;border:1px solid #314253;background:#172231;color:#fff;max-width:100%}select{width:100%}button,.button{display:inline-block;background:var(--accent);color:#07110b;border:0;cursor:pointer;border-radius:8px;padding:10px 12px;font-weight:600}.button.secondary{background:#172231;color:var(--text);border:1px solid #314253;font-weight:500}.button.danger{background:#3a1d1d;color:#ffd5d0;border:1px solid #6d3434}.upload{display:grid;grid-template-columns:minmax(0,1fr) auto;gap:10px;align-items:end}.checkline{display:flex;gap:8px;align-items:center;color:var(--muted);font-size:13px;margin-top:8px}.checkline input{width:auto}.danger{color:var(--danger)}.empty{border:1px dashed #314253;border-radius:10px;padding:16px;color:var(--muted);background:#0b141d}.card{max-width:420px;border:1px solid var(--line);border-radius:12px;padding:18px;background:rgba(16,26,36,.96)}"
            << "@media(max-width:900px){body{padding:16px}.shell{grid-template-columns:1fr}.side{position:static}.grid,.quick-links{grid-template-columns:1fr}.top,.hero-row,.world-card{display:block}.world-actions{margin-top:10px;justify-content:flex-start}.upload{grid-template-columns:1fr}}"
            << "</style></head><body><main>"
            << body << "</main></body></html>";
        return html.str();
    }

    void HandleClient(SOCKET s) {
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

        if (!Authorized(query, body)) {
            std::string form = "<div class=\"top\"><div><h1>Bandit Remote Files</h1><p class=\"muted\">Enter the PIN shown on your Xbox to manage files on this device.</p></div></div>"
                "<div class=\"card\"><form method=\"get\">"
                "<div class=\"field\"><label for=\"pin\">PIN</label><input id=\"pin\" name=\"pin\" inputmode=\"numeric\" pattern=\"[0-9]{6}\" maxlength=\"6\" autofocus></div>"
                "<button>Open file manager</button></form></div>";
            SendHttpResponse(s, 401, "Unauthorized", "text/html; charset=utf-8", Layout("Bandit Remote Files", form));
            return;
        }

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
        } else if (method == "POST" && path == "/export-pack") {
            HandleExportPack(s, body);
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

    std::string SidebarHtml(const std::wstring& profileId, const char* activeScope = nullptr) const {
        auto nav = [&](const char* scope, const std::wstring& label, const std::wstring& hint) {
            const bool active = activeScope && scope == std::string(activeScope);
            return std::string("<a") + (active ? " class=\"active\"" : "") + " href=\"" +
                UrlWithPinProfile("/browse?scope=" + std::string(scope), profileId) + "\"><span>" +
                HtmlEscape(label) + "</span><small>" + HtmlEscape(hint) + "</small></a>";
        };
        std::ostringstream out;
        out << "<aside class=\"side\">"
            << "<div class=\"side-title\">Profile</div><nav class=\"nav\">"
            << "<a href=\"" << UrlWithPinProfile("/", profileId) << "\"><span>Dashboard</span><small>home</small></a>"
            << nav("profile", L"Game files", L"folder")
            << nav("saves", L"Worlds", L"saves")
            << nav("mods", L"Mods", L"jars")
            << nav("resourcepacks", L"Resource packs", L"zip")
            << "</nav><div class=\"side-title\">Diagnostics</div><nav class=\"nav\">"
            << nav("logs", L"Current logs", L"now")
            << nav("previous", L"Previous logs", L"last")
            << nav("crash", L"Crash reports", L"zip")
            << nav("runtime", L"Runtime cache", L"read")
            << "</nav></aside>";
        return out.str();
    }

    std::string BrowseLink(const char* scope, const std::wstring& label, const std::wstring& profileId, const std::wstring& rel = L"") const {
        std::string url = "/browse?scope=" + std::string(scope);
        if (!rel.empty()) url += "&path=" + FormUrlEncode(w2a(rel));
        return "<a class=\"tile\" href=\"" + UrlWithPinProfile(url, profileId) + "\"><strong>" + HtmlEscape(label) + "</strong><span class=\"muted\">Open folder</span></a>";
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
        out << "<section class=\"panel\"><h3>Export profile pack</h3>";
        if (profileId == kVanillaProfileId) {
            out << "<div class=\"empty\">Vanilla cannot be exported. Pick a mod profile above first.</div></section>";
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
        out << "</div></section>";
        return out.str();
    }

    std::string ModpackImportHtml(const std::wstring& profileId) {
        std::ostringstream out;
        out << "<section class=\"panel\"><h3>Import Modrinth pack</h3>"
            << "<form method=\"post\" action=\"/upload-modpack\" enctype=\"multipart/form-data\">"
            << "<input type=\"hidden\" name=\"pin\" value=\"" << pin_ << "\">"
            << "<input type=\"hidden\" name=\"profile\" value=\"" << HtmlEscape(profileId) << "\">"
            << "<div class=\"field\"><label for=\"modpackfile\">Modrinth .mrpack</label>"
            << "<div class=\"upload\"><input id=\"modpackfile\" type=\"file\" name=\"file\" accept=\".mrpack\"><button>Import pack</button></div></div>"
            << "</form>"
            << "<p class=\"muted\">Installs into the selected profile. Large packs can take several minutes.</p></section>";
        return out.str();
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

        const std::wstring lower = ToLowerW(name);
        if (lower.size() < 7 || lower.substr(lower.size() - 7) != L".mrpack") {
            SendHttpResponse(s, 400, "Bad Request", "text/html; charset=utf-8",
                Layout("Import failed", "<h1>Import failed</h1><p>Upload a Modrinth .mrpack file.</p>"));
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
        std::wstring installError;
        const bool ok = InstallModpackFromFile(path, runtimeRoot_, active, installError);
        DeleteFileW(path.c_str());
        if (!ok) {
            SendHttpResponse(s, 500, "Internal Server Error", "text/html; charset=utf-8",
                Layout("Import failed", "<h1>Import failed</h1><p>" + HtmlEscape(installError.empty() ? L"Pack install failed" : installError) + "</p>"));
            return;
        }

        const Profile profile = GetProfileById(runtimeRoot_, active);
        SendHttpResponse(s, 200, "OK", "text/html; charset=utf-8",
            Layout("Import complete",
                "<div class=\"top\"><h1>Import complete</h1><a class=\"pill\" href=\"/?pin=" + pin_ + "\">Files home</a></div>"
                "<p>Installed <strong>" + HtmlEscape(name) + "</strong> into profile <strong>" + HtmlEscape(profile.name) + "</strong>.</p>"));
    }

    std::string WorldsSectionHtml(const std::vector<std::wstring>& saves, const std::wstring& profileId) {
        const std::string profEsc = HtmlEscape(profileId);
        const std::string profQ = FormUrlEncode(w2a(profileId));
        std::ostringstream out;
        out << "<section class=\"section\" id=\"worlds\"><div class=\"section-head\"><div><h2>Worlds</h2>"
            << "<p class=\"section-note\">Export full saves as zip files for PC, or import a world zip into the selected profile.</p></div>"
            << "<a class=\"button secondary\" href=\"" << UrlWithPinProfile("/browse?scope=saves", profileId) << "\">Browse saves</a></div>";
        if (saves.empty()) {
            out << "<div class=\"empty\">No worlds yet. Play Minecraft on the console to create one, then refresh this page.</div>";
        } else {
            out << "<div class=\"world-list\">";
            for (const std::wstring& save : saves) {
                const std::wstring exportPath = DefaultWorldExportPath(runtimeRoot_, save);
                const bool hasExport = GetFileAttributesW(exportPath.c_str()) != INVALID_FILE_ATTRIBUTES;
                out << "<div class=\"world-card\"><div><strong>" << HtmlEscape(save) << "</strong>"
                    << "<div class=\"muted\">Full world folder as a PC-friendly zip</div></div><div class=\"world-actions\">"
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
        out << "<div class=\"panel\" style=\"margin-top:14px\"><h3>Import world</h3>"
            << "<form method=\"post\" action=\"/upload-world\" enctype=\"multipart/form-data\">"
            << "<input type=\"hidden\" name=\"pin\" value=\"" << pin_ << "\">"
            << "<input type=\"hidden\" name=\"profile\" value=\"" << profEsc << "\">"
            << "<div class=\"grid\"><div class=\"field\"><label for=\"worldname\">Save as</label>"
            << "<input id=\"worldname\" name=\"save\" placeholder=\"World name\" required></div>"
            << "<div class=\"field\"><label for=\"worldzip\">World .zip</label>"
            << "<div class=\"upload\"><input id=\"worldzip\" type=\"file\" name=\"file\" accept=\".zip\" required><button>Import world</button></div></div></div>"
            << "<label class=\"checkline\"><input type=\"checkbox\" name=\"replace\" value=\"1\"> Replace existing world with the same name</label>"
            << "<p class=\"muted\">Accepts a zip with <code>level.dat</code> at the root or inside one folder. Large worlds may take several minutes.</p>"
            << "</form></div>"
            << "<div class=\"panel\" style=\"margin-top:14px\"><h3>Upload datapack</h3>";
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
                << "<div class=\"upload\"><input id=\"datapack\" type=\"file\" name=\"file\" accept=\".zip\"><button>Upload datapack</button></div></div></div>"
                << "</form>";
        }
        out << "</div></section>";
        return out.str();
    }

    std::string DiagnosticsSectionHtml() {
        std::ostringstream out;
        out << "<section class=\"section\" id=\"diagnostics\"><div class=\"section-head\"><div><h2>Logs and diagnostics</h2>"
            << "<p class=\"section-note\">Download launcher logs, game logs, and crash report bundles.</p></div></div>"
            << "<div class=\"quick-links\">"
            << "<a class=\"quick-link\" href=\"" << UrlWithPin("/browse?scope=logs") << "\"><strong>Current logs</strong><span class=\"muted\">Latest launcher session</span></a>"
            << "<a class=\"quick-link\" href=\"" << UrlWithPin("/browse?scope=previous") << "\"><strong>Previous logs</strong><span class=\"muted\">Last archived session</span></a>"
            << "<a class=\"quick-link\" href=\"" << UrlWithPin("/browse?scope=crash") << "\"><strong>Crash reports</strong><span class=\"muted\">Zip bundles and reports</span></a>"
            << "<a class=\"quick-link\" href=\"" << UrlWithPin("/browse?scope=runtime") << "\"><strong>Runtime cache</strong><span class=\"muted\">Downloaded game files</span></a>"
            << "</div></section>";
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

        std::ostringstream sel;
        sel << "<select onchange=\"location='/?pin=" << pin_ << "&profile='+encodeURIComponent(this.value)\">";
        for (const auto& p : profiles) {
            sel << "<option value=\"" << HtmlEscape(p.id) << "\"" << (p.id == profileId ? " selected" : "") << ">"
                << HtmlEscape(p.name) << " (" << HtmlEscape(p.id) << ")</option>";
        }
        sel << "</select>";

        std::ostringstream out;
        out << "<div class=\"top\"><div><h1>Bandit Remote Files</h1>"
            << "<div class=\"muted\">Manage worlds, mods, packs, and logs for any profile on this Xbox.</div></div>"
            << "<div class=\"hero-actions\"><span class=\"pill\"><span>PIN</span> <span class=\"pin\">" << pin_ << "</span></span>"
            << "<a class=\"pill\" href=\"" << UrlWithPinProfile("/", profileId) << "\">Refresh</a></div></div>"
            << "<div class=\"shell\">" << SidebarHtml(profileId) << "<div class=\"content\">"
            << "<section class=\"hero\"><div class=\"hero-row\"><div><h2>" << HtmlEscape(prof.name) << "</h2><div class=\"muted\">"
            << HtmlEscape(TargetProfileText(target)) << "</div></div><div class=\"hero-actions\">"
            << "<label style=\"display:flex;align-items:center;gap:8px;color:var(--muted);font-size:13px;margin:0\">Viewing " << sel.str() << "</label>"
            << "<a class=\"button secondary\" href=\"" << UrlWithPinProfile("/browse?scope=profile", profileId) << "\">Browse game files</a></div></div>"
            << "<div class=\"stats\"><div class=\"stat\"><span>Profile</span> " << profEsc << "</div>"
            << "<div class=\"stat\"><span>Worlds</span> " << saves.size() << "</div>"
            << "<div class=\"stat\"><span>Port</span> " << port_ << "</div></div></section>"
            << WorldsSectionHtml(saves, profileId)
            << "<section class=\"section\" id=\"mods\"><div class=\"section-head\"><div><h2>Mods and packs</h2>"
            << "<p class=\"section-note\">Upload individual files or move whole mod setups with Modrinth packs.</p></div></div>"
            << "<div class=\"grid\"><section class=\"panel\"><h3>Upload mod</h3>"
            << "<form method=\"post\" action=\"/upload-mod\" enctype=\"multipart/form-data\">"
            << "<input type=\"hidden\" name=\"pin\" value=\"" << pin_ << "\">"
            << "<input type=\"hidden\" name=\"profile\" value=\"" << profEsc << "\">"
            << "<div class=\"field\"><label for=\"modfile\">Mod .jar</label><div class=\"upload\"><input id=\"modfile\" type=\"file\" name=\"file\" accept=\".jar\"><button>Upload mod</button></div></div>"
            << "</form><p class=\"muted\">Saved to the selected profile mods folder.</p></section>"
            << "<section class=\"panel\"><h3>Upload resource pack</h3>"
            << "<form method=\"post\" action=\"/upload-resourcepack\" enctype=\"multipart/form-data\">"
            << "<input type=\"hidden\" name=\"pin\" value=\"" << pin_ << "\">"
            << "<input type=\"hidden\" name=\"profile\" value=\"" << profEsc << "\">"
            << "<div class=\"field\"><label for=\"packfile\">Resource pack .zip</label><div class=\"upload\"><input id=\"packfile\" type=\"file\" name=\"file\" accept=\".zip\"><button>Upload pack</button></div></div>"
            << "</form></section>"
            << ExportPackHtml(profileId)
            << ModpackImportHtml(profileId)
            << "</div></section>"
            << DiagnosticsSectionHtml()
            << "<section class=\"section\"><div class=\"section-head\"><div><h2>Browse folders</h2>"
            << "<p class=\"section-note\">Inspect files directly when you need more than the quick actions above.</p></div></div><div class=\"tiles\">"
            << BrowseLink("saves", L"Worlds", profileId)
            << BrowseLink("mods", L"Mods", profileId)
            << BrowseLink("resourcepacks", L"Resource packs", profileId)
            << BrowseLink("profile", L"Game files", profileId)
            << "</div></section></div></div>";
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
        js << "{\"ok\":true,\"scope\":\"" << JsonEscape(scope) << "\",\"path\":\"" << JsonEscape(w2a(relNorm))
            << "\",\"writable\":" << (writable ? "true" : "false")
            << ",\"world\":" << ((scope == "saves" && IsWorldFolder(full)) ? "true" : "false")
            << ",\"entries\":[";
        bool firstOut = true;
        auto emit = [&](const E& e) {
            if (!firstOut) js << ",";
            firstOut = false;
            js << "{\"name\":\"" << JsonEscape(w2a(e.name)) << "\",\"dir\":" << (e.dir ? "true" : "false")
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

    static const char* ExplorerHead() {
        return R"RFSPA(<!doctype html><html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Bandit Remote Files</title><style>
:root{--bg:#0b0c0e;--bar:#111316;--panel:#17191d;--card:#1a1c20;--line:rgba(255,255,255,.08);--muted:#9ba1a6;--text:#f2f4f5;--accent:#70c486;--danger:#e36a5c;--blue:#6db1e6}
*{box-sizing:border-box}html,body{margin:0;height:100%}
body{background:var(--bg);color:var(--text);font:14px/1.5 system-ui,Segoe UI,sans-serif}
.app{display:flex;flex-direction:column;height:100vh}
.top{display:flex;align-items:center;gap:12px;padding:12px 18px;background:var(--bar);border-bottom:1px solid var(--line)}
.top h1{font-size:16px;margin:0;font-weight:600;white-space:nowrap}
.sp{flex:1}
.profsel{display:flex;align-items:center;gap:8px}.profsel label{color:var(--muted);font-size:12px}
select,.btn,input{font:inherit}
select{font-size:13px;background:#16181c;color:var(--text);border:1px solid var(--line);border-radius:8px;padding:7px 10px}
.pill{font-size:12px;border:1px solid var(--line);border-radius:999px;padding:4px 11px;color:var(--muted);white-space:nowrap}
.pin{font-family:Consolas,ui-monospace,monospace;letter-spacing:.1em;color:var(--text)}
.btn{font-size:13px;border:1px solid var(--line);background:#16181c;color:var(--text);border-radius:8px;padding:7px 12px;cursor:pointer}
.btn:hover{background:#202329}
.btn.primary{background:var(--accent);color:#07130c;border-color:transparent;font-weight:600}
.btn.blue{background:var(--blue);color:#06121c;border-color:transparent;font-weight:600}
.btn.ghost{background:transparent;border-color:transparent;color:var(--muted);padding:5px 9px}
.btn.ghost:hover{background:#202329;color:var(--text)}
.btn.sm{padding:5px 9px;font-size:12.5px}
a.btn{text-decoration:none;display:inline-block}
.body{flex:1;display:grid;grid-template-columns:230px minmax(0,1fr);min-height:0}
.rail{border-right:1px solid var(--line);background:var(--panel);overflow:auto;padding:12px}
.rail .grp{font-size:11px;text-transform:uppercase;letter-spacing:.1em;color:var(--muted);margin:14px 6px 6px}
.rail .grp:first-child{margin-top:0}
.rail a{display:flex;justify-content:space-between;align-items:center;gap:8px;border:1px solid transparent;border-radius:8px;padding:8px 10px;color:var(--text);cursor:pointer;text-decoration:none}
.rail a small{color:var(--muted)}
.rail a:hover{background:var(--card)}
.rail a.active{background:var(--card);border-color:var(--line)}
.main{display:flex;flex-direction:column;min-width:0}
.bar2{display:flex;align-items:center;gap:10px;flex-wrap:wrap;padding:12px 18px;border-bottom:1px solid var(--line)}
.crumbs{display:flex;align-items:center;gap:4px;flex-wrap:wrap;flex:1;min-width:0}
.crumbs a{color:var(--blue);cursor:pointer;text-decoration:none;padding:2px 6px;border-radius:6px}
.crumbs a:hover{background:var(--card)}
.crumbs .sep{color:var(--muted)}
.list{flex:1;overflow:auto}
.row{display:grid;grid-template-columns:1fr 120px 150px auto;align-items:center;gap:10px;padding:9px 18px;border-bottom:1px solid rgba(255,255,255,.05)}
.row:hover{background:var(--panel)}
.nm{display:flex;align-items:center;gap:10px;min-width:0}
.nm .t{white-space:nowrap;overflow:hidden;text-overflow:ellipsis;cursor:pointer}
.nm .t:hover{text-decoration:underline}
.ic{width:18px;height:18px;flex:0 0 18px;color:var(--muted)}.ic.dir{color:#e2b65c}
.size,.mod{color:var(--muted);font-size:12.5px;font-variant-numeric:tabular-nums}
.acts{display:flex;gap:4px;justify-content:flex-end;opacity:0;transition:opacity .1s}
.row:hover .acts{opacity:1}
.empty,.err{padding:42px 18px;color:var(--muted);text-align:center}
.drop{position:fixed;inset:0;background:rgba(11,12,14,.86);border:3px dashed var(--accent);display:none;align-items:center;justify-content:center;font-size:20px;color:var(--accent);z-index:50}
.drop.show{display:flex}
.editor{position:fixed;inset:0;background:var(--bg);display:none;flex-direction:column;z-index:40}
.editor.show{display:flex}
.ehead{display:flex;align-items:center;gap:10px;padding:10px 16px;background:var(--bar);border-bottom:1px solid var(--line)}
.ehead .pa{font-family:Consolas,ui-monospace,monospace;color:var(--muted);font-size:13px;flex:1;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
#ta{flex:1;width:100%;border:0;outline:0;resize:none;background:#070809;color:#dce7ef;font:13px/1.55 Consolas,ui-monospace,monospace;padding:14px;white-space:pre;tab-size:4}
.toast{position:fixed;bottom:20px;left:50%;transform:translateX(-50%);background:#202329;border:1px solid var(--line);color:var(--text);padding:9px 16px;border-radius:10px;opacity:0;transition:opacity .2s;pointer-events:none;z-index:60}
.toast.show{opacity:1}
@media(max-width:760px){.body{grid-template-columns:1fr}.rail{display:none}.row{grid-template-columns:1fr auto}.size,.mod{display:none}.acts{opacity:1}}
</style></head><body><div class="app">
<div class="top"><h1>Bandit Remote Files</h1>
<div class="profsel"><label for="prof">Profile</label><select id="prof" onchange="switchProfile(this.value)"></select></div>
<span class="sp"></span>
<span class="pill">PIN <span class="pin" id="pinv"></span></span>
<button class="btn" id="mkbtn" onclick="mkdir()">New folder</button>
<button class="btn blue" id="upbtn" onclick="document.getElementById('up').click()">Upload</button>
<a class="btn ghost" id="homebtn">Files home</a>
<button class="btn ghost" onclick="reload()" title="Refresh">Refresh</button>
<input id="up" type="file" multiple style="display:none" onchange="upload(this.files)"></div>
<div class="body"><nav class="rail" id="rail"></nav><div class="main">
<div class="bar2"><div class="crumbs" id="crumbs"></div><div id="extra"></div></div>
<div class="list" id="list"></div></div></div>
<div id="drop" class="drop">Drop files to upload</div>
<div id="editor" class="editor"><div class="ehead"><span id="epath" class="pa"></span>
<button class="btn primary" id="savebtn" onclick="saveEd()">Save</button>
<button class="btn" onclick="openEd(edPath)">Reload</button>
<button class="btn ghost" onclick="closeEd()">Close</button></div>
<textarea id="ta" spellcheck="false"></textarea></div>
<div id="toast" class="toast"></div></div>
<script>)RFSPA";
    }

    static const char* ExplorerScript() {
        return R"RFSPA(
var cur="",edPath="",canWrite=false;
var SCOPES=[
 {g:"Profile",items:[{s:"profile",l:"Game files",h:"folder"},{s:"saves",l:"Worlds",h:"saves"},{s:"mods",l:"Mods",h:"jars"},{s:"resourcepacks",l:"Resource packs",h:"zip"}]},
 {g:"Diagnostics",items:[{s:"logs",l:"Current logs",h:"now"},{s:"previous",l:"Previous logs",h:"last"},{s:"crash",l:"Crash reports",h:"zip"},{s:"runtime",l:"Runtime cache",h:"read"}]}
];
var ICON_DIR='<svg class="ic dir" viewBox="0 0 24 24" fill="currentColor"><path d="M10 4H4a2 2 0 0 0-2 2v12a2 2 0 0 0 2 2h16a2 2 0 0 0 2-2V8a2 2 0 0 0-2-2h-8l-2-2z"/></svg>';
var ICON_FILE='<svg class="ic" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M14 2H6a2 2 0 0 0-2 2v16a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V8z"/><path d="M14 2v6h6"/></svg>';
function enc(p){return encodeURIComponent(p)}
function esc(s){return (s+'').replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;').replace(/"/g,'&quot;')}
function join(b,n){return b?b+"/"+n:n}
function q(extra){var s='?pin='+enc(CFG.pin)+'&profile='+enc(CFG.profile)+'&scope='+enc(CFG.scope);if(extra)s+=extra;return s}
function toast(m){var t=document.getElementById('toast');t.textContent=m;t.classList.add('show');clearTimeout(t._t);t._t=setTimeout(function(){t.classList.remove('show')},1800)}
function scopeLabel(sc){for(var i=0;i<SCOPES.length;i++)for(var j=0;j<SCOPES[i].items.length;j++)if(SCOPES[i].items[j].s===sc)return SCOPES[i].items[j].l;return sc}
function homeHref(){return '/?pin='+enc(CFG.pin)+'&profile='+enc(CFG.profile)}
function initChrome(){
 document.getElementById('pinv').textContent=CFG.pin;
 document.getElementById('homebtn').href=homeHref();
 var sel=document.getElementById('prof');sel.innerHTML='';
 CFG.profiles.forEach(function(p){var o=document.createElement('option');o.value=p.id;o.textContent=p.name+' ('+p.id+')';if(p.id===CFG.profile)o.selected=true;sel.appendChild(o)});
 var rail=document.getElementById('rail'),h='';
 SCOPES.forEach(function(grp){h+='<div class="grp">'+esc(grp.g)+'</div>';grp.items.forEach(function(it){h+='<a data-s="'+it.s+'" onclick="go(\''+it.s+'\')"><span>'+esc(it.l)+'</span><small>'+esc(it.h)+'</small></a>'})});
 rail.innerHTML=h;
}
function railActive(){document.querySelectorAll('.rail a').forEach(function(a){a.classList.toggle('active',a.getAttribute('data-s')===CFG.scope)})}
function switchProfile(id){CFG.profile=id;document.getElementById('homebtn').href=homeHref();load('')}
function go(sc){CFG.scope=sc;load('')}
function reload(){load(cur)}
async function load(path){
 cur=path||"";railActive();
 var L=document.getElementById('list');
 var r;try{r=await fetch('/api/list'+q('&path='+enc(cur)))}catch(e){L.innerHTML='<div class="err">Connection lost</div>';return}
 if(!r.ok){L.innerHTML='<div class="err">Could not open folder</div>';return}
 var d=await r.json();
 if(!d.ok){L.innerHTML='<div class="err">Could not open folder</div>';return}
 canWrite=!!d.writable;
 document.getElementById('mkbtn').style.display=canWrite?'':'none';
 document.getElementById('upbtn').style.display=canWrite?'':'none';
 renderCrumbs();renderExtra(d);renderList(d);
}
function renderCrumbs(){
 var c=document.getElementById('crumbs'),html='<a onclick="load(\'\')">'+esc(scopeLabel(CFG.scope))+'</a>',acc="";
 if(cur){cur.split('/').forEach(function(seg){if(!seg)return;acc=join(acc,seg);html+='<span class="sep">/</span><a onclick="load(\''+acc.replace(/'/g,"\\'")+'\')">'+esc(seg)+'</a>'})}
 c.innerHTML=html;
}
function renderExtra(d){
 var e=document.getElementById('extra');
 if(d.world){var wn=cur.split('/')[0]||cur;e.innerHTML='<a class="btn sm" href="/download?file=export:world:'+enc(wn)+'&profile='+enc(CFG.profile)+'&pin='+enc(CFG.pin)+'">Export world zip</a>'}
 else e.innerHTML='';
}
function rowActs(en,p,nm){
 var a='';
 if(en.dir){
  if(en.world)a+='<a class="btn ghost" href="/download?file=export:world:'+enc(en.name)+'&profile='+enc(CFG.profile)+'&pin='+enc(CFG.pin)+'">Zip</a>';
  if(canWrite)a+='<button class="btn ghost" onclick="rename(\''+p+'\',\''+nm+'\')">Rename</button><button class="btn ghost" onclick="del(\''+p+'\',1)">Delete</button>';
 }else{
  if(en.text)a+='<button class="btn ghost" onclick="openEd(\''+p+'\')">Edit</button>';
  a+='<button class="btn ghost" onclick="dl(\''+p+'\')">Download</button>';
  if(canWrite)a+='<button class="btn ghost" onclick="rename(\''+p+'\',\''+nm+'\')">Rename</button><button class="btn ghost" onclick="del(\''+p+'\',0)">Delete</button>';
 }
 return a;
}
function renderList(d){
 var L=document.getElementById('list');
 if(!d.entries.length){L.innerHTML='<div class="empty">This folder is empty</div>';return}
 var h='';
 d.entries.forEach(function(en){
  var path=join(cur,en.name),p=path.replace(/'/g,"\\'"),nm=en.name.replace(/'/g,"\\'");
  var click=en.dir?("load('"+p+"')"):(en.text?("openEd('"+p+"')"):("dl('"+p+"')"));
  h+='<div class="row"><div class="nm">'+(en.dir?ICON_DIR:ICON_FILE)+'<span class="t" onclick="'+click+'">'+esc(en.name)+'</span></div>'
   +'<div class="size">'+(en.dir?'':esc(en.sizeText))+'</div><div class="mod">'+esc(en.modified||'')+'</div>'
   +'<div class="acts">'+rowActs(en,p,nm)+'</div></div>';
 });
 L.innerHTML=h;
}
function dl(p){window.location='/download-path'+q('&path='+enc(p))}
async function del(p,isDir){
 if(!confirm('Delete '+(isDir?'folder':'file')+' "'+p.split('/').pop()+'"?'+(isDir?' Removes everything inside.':'')))return;
 var r=await fetch('/api/delete',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'pin='+enc(CFG.pin)+'&profile='+enc(CFG.profile)+'&scope='+enc(CFG.scope)+'&path='+enc(p)});
 toast(r.ok?'Deleted':'Delete failed');load(cur);
}
async function rename(p,old){
 var nn=prompt('Rename to',old);if(!nn||nn===old)return;
 var r=await fetch('/api/rename',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'pin='+enc(CFG.pin)+'&profile='+enc(CFG.profile)+'&scope='+enc(CFG.scope)+'&path='+enc(p)+'&name='+enc(nn)});
 toast(r.ok?'Renamed':'Rename failed');load(cur);
}
async function mkdir(){
 var nn=prompt('New folder name');if(!nn)return;
 var r=await fetch('/api/mkdir',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'pin='+enc(CFG.pin)+'&profile='+enc(CFG.profile)+'&scope='+enc(CFG.scope)+'&path='+enc(cur)+'&name='+enc(nn)});
 toast(r.ok?'Folder created':'Failed');load(cur);
}
async function upload(files){
 if(!files||!files.length)return;
 for(var i=0;i<files.length;i++){var fd=new FormData();fd.append('file',files[i]);
  await fetch('/api/upload'+q('&path='+enc(cur)),{method:'POST',body:fd});}
 toast(files.length+' file(s) uploaded');document.getElementById('up').value='';load(cur);
}
function openEd(p){
 fetch('/api/raw'+q('&path='+enc(p))).then(function(r){if(!r.ok)throw 0;return r.text()}).then(function(t){
  edPath=p;document.getElementById('epath').textContent=scopeLabel(CFG.scope)+' / '+p;document.getElementById('ta').value=t;
  document.getElementById('savebtn').style.display=canWrite?'':'none';
  document.getElementById('editor').classList.add('show');
 }).catch(function(){toast('Could not open file')});
}
function closeEd(){document.getElementById('editor').classList.remove('show')}
async function saveEd(){
 var r=await fetch('/api/write'+q('&path='+enc(edPath)),{method:'POST',body:document.getElementById('ta').value});
 toast(r.ok?'Saved':'Save failed');
}
document.getElementById('ta').addEventListener('keydown',function(e){
 if(e.key==='Tab'){e.preventDefault();var t=this,s=t.selectionStart;t.value=t.value.slice(0,s)+'  '+t.value.slice(t.selectionEnd);t.selectionStart=t.selectionEnd=s+2}
 if((e.ctrlKey||e.metaKey)&&e.key==='s'){e.preventDefault();saveEd()}
});
var dz=document.getElementById('drop'),dc=0;
window.addEventListener('dragenter',function(e){e.preventDefault();if(!canWrite)return;dc++;dz.classList.add('show')});
window.addEventListener('dragover',function(e){e.preventDefault()});
window.addEventListener('dragleave',function(e){dc--;if(dc<=0)dz.classList.remove('show')});
window.addEventListener('drop',function(e){e.preventDefault();dc=0;dz.classList.remove('show');if(canWrite&&e.dataTransfer.files.length)upload(e.dataTransfer.files)});
initChrome();load('');
)RFSPA";
    }

    std::string ExplorerHtml(const std::string& query) {
        std::string scope = QueryValue(query, "scope");
        if (scope.empty()) scope = "profile";
        const std::wstring profileId = NormalizeProfileId(QueryValue(query, "profile"));

        std::ostringstream cfg;
        cfg << "var CFG={pin:\"" << pin_ << "\",scope:\"" << JsonEscape(scope) << "\",profile:\""
            << JsonEscape(w2a(profileId)) << "\",profiles:[";
        const std::vector<Profile> profiles = LoadProfiles(runtimeRoot_);
        bool firstP = true;
        for (const auto& p : profiles) {
            if (!firstP) cfg << ",";
            firstP = false;
            cfg << "{id:\"" << JsonEscape(w2a(p.id)) << "\",name:\"" << JsonEscape(w2a(p.name)) << "\"}";
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
