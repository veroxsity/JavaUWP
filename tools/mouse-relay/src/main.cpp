#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include <timeapi.h>
using SocketHandle = SOCKET;
static constexpr SocketHandle kInvalidSocket = INVALID_SOCKET;
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
using SocketHandle = int;
static constexpr SocketHandle kInvalidSocket = -1;
#endif

namespace {

constexpr uint16_t kInputPort = 7331;
constexpr uint16_t kStatusPort = 7332;
constexpr float kTargetWidth = 1920.0f;
constexpr float kTargetHeight = 1080.0f;
constexpr double kSendIntervalSeconds = 1.0 / 240.0;
constexpr double kHeldButtonRefreshSeconds = 1.0 / 20.0;
constexpr double kConnectProbeSeconds = 0.25;
constexpr double kConnectTimeoutSeconds = 3.0;
constexpr double kRenderIntervalSeconds = 1.0 / 30.0;
constexpr double kButtonHoldSeconds = 1.0;
constexpr double kTouchScrollRepeatSeconds = 1.0 / 12.0;
constexpr float kMenuCoordinateScale = 0.5f;
constexpr float kDebugGlyphWidth = 8.0f;
constexpr float kDebugGlyphHeight = 8.0f;
constexpr float kTouchScrollStep = 1.0f;

#ifdef BANDIT_MOUSE_RELAY_TOUCH
constexpr bool kTouchLayout = true;
#else
constexpr bool kTouchLayout = false;
#endif

enum class RelayMode {
    Gameplay,
    Menu,
};

enum class UiAction {
    None,
    Connect,
    CancelIp,
    ChangeIp,
    ToggleMode,
    ReleaseButtons,
    Resume,
    Quit,
};

struct Button {
    SDL_FRect rect{};
    std::string label;
    UiAction action = UiAction::None;
    int mouseButtonIndex = -1;
    float scrollStep = 0.0f;
    std::string inputText;
    bool backspace = false;
};

static float Clamp(float value, float low, float high) {
    return std::max(low, std::min(value, high));
}

static double NowSeconds() {
    using Clock = std::chrono::steady_clock;
    static const Clock::time_point start = Clock::now();
    return std::chrono::duration<double>(Clock::now() - start).count();
}

static void SleepUntil(double deadline) {
    for (;;) {
        const double remaining = deadline - NowSeconds();
        if (remaining <= 0.0) {
            return;
        }
        // sleep the bulk, spin the last ~1ms since SDL_Delay only resolves to the timer tick
        if (remaining > 0.002) {
            SDL_Delay(static_cast<Uint32>((remaining - 0.001) * 1000.0));
        }
    }
}

class PacketLog {
public:
    void Toggle() {
        if (enabled_) {
            Disable();
        } else {
            Enable();
        }
    }

    bool Enabled() const { return enabled_; }
    const std::string& Path() const { return path_; }

    void Enable() {
        if (enabled_) {
            return;
        }
        std::string dir;
        char* base = SDL_GetPrefPath("BanditVault", "BanditMouseRelay");
        if (base) {
            dir = base;
            SDL_free(base);
        }
        char stamp[32];
        std::time_t tt = std::time(nullptr);
        std::tm tmv{};
#ifdef _WIN32
        localtime_s(&tmv, &tt);
#else
        localtime_r(&tt, &tmv);
#endif
        std::strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", &tmv);
        path_ = dir + "relay-packets-" + stamp + ".log";
        out_.open(path_, std::ios::out | std::ios::trunc);
        if (!out_.is_open()) {
            path_.clear();
            return;
        }
        seqTx_ = 0;
        seqRx_ = 0;
        lastTx_ = -1.0;
        lastRx_ = -1.0;
        lastFlush_ = NowSeconds();
        enabled_ = true;
        out_ << "# Bandit Mouse Relay packet log\n";
        out_ << "# format: wall_clock mono_seconds dir seq dt_ms payload\n";
        out_ << "# dir TX = sent to Xbox, RX = status from Xbox, EV = event marker\n";
        out_.flush();
    }

    void Disable() {
        if (!enabled_) {
            return;
        }
        enabled_ = false;
        out_.flush();
        out_.close();
    }

    void Mark(const std::string& note) {
        if (!enabled_) {
            return;
        }
        char head[112];
        FormatHead(head, sizeof(head), "EV", 0, -1.0);
        out_ << head << note << '\n';
    }

    void LogTx(const std::string& packet, bool ok) {
        if (!enabled_) {
            return;
        }
        const double now = NowSeconds();
        const double dt = lastTx_ >= 0.0 ? (now - lastTx_) * 1000.0 : -1.0;
        lastTx_ = now;
        char head[112];
        FormatHead(head, sizeof(head), "TX", ++seqTx_, dt);
        out_ << head << (ok ? "" : "SENDFAIL ") << packet << '\n';
    }

    void LogRx(const std::string& packet) {
        if (!enabled_) {
            return;
        }
        const double now = NowSeconds();
        const double dt = lastRx_ >= 0.0 ? (now - lastRx_) * 1000.0 : -1.0;
        lastRx_ = now;
        char head[112];
        FormatHead(head, sizeof(head), "RX", ++seqRx_, dt);
        out_ << head << packet << '\n';
    }

    void MaybeFlush() {
        if (!enabled_) {
            return;
        }
        const double now = NowSeconds();
        if (now - lastFlush_ >= 0.25) {
            out_.flush();
            lastFlush_ = now;
        }
    }

private:
    void FormatHead(char* buffer, size_t size, const char* dir, long long seq, double dtMs) {
        const auto sysNow = std::chrono::system_clock::now();
        const std::time_t tt = std::chrono::system_clock::to_time_t(sysNow);
        const int millis = static_cast<int>(
            std::chrono::duration_cast<std::chrono::milliseconds>(sysNow.time_since_epoch()).count() % 1000);
        std::tm tmv{};
#ifdef _WIN32
        localtime_s(&tmv, &tt);
#else
        localtime_r(&tt, &tmv);
#endif
        char wall[16];
        std::strftime(wall, sizeof(wall), "%H:%M:%S", &tmv);
        if (dtMs >= 0.0) {
            std::snprintf(buffer, size, "%s.%03d %.6f %s seq=%lld dt=%.2fms ",
                wall, millis, NowSeconds(), dir, seq, dtMs);
        } else {
            std::snprintf(buffer, size, "%s.%03d %.6f %s seq=%lld ",
                wall, millis, NowSeconds(), dir, seq);
        }
    }

    bool enabled_ = false;
    std::ofstream out_;
    std::string path_;
    long long seqTx_ = 0;
    long long seqRx_ = 0;
    double lastTx_ = -1.0;
    double lastRx_ = -1.0;
    double lastFlush_ = 0.0;
};

PacketLog g_packetLog;

static std::string ModeName(RelayMode mode) {
    return mode == RelayMode::Gameplay ? "GAMEPLAY" : "MENU";
}

static bool IsValidIpv4(const std::string& text) {
    int octets = 0;
    size_t start = 0;

    while (start <= text.size()) {
        const size_t end = text.find('.', start);
        const size_t stop = (end == std::string::npos) ? text.size() : end;
        if (stop == start || stop - start > 3) {
            return false;
        }

        int value = 0;
        for (size_t i = start; i < stop; ++i) {
            if (text[i] < '0' || text[i] > '9') {
                return false;
            }
            value = (value * 10) + (text[i] - '0');
        }
        if (value > 255) {
            return false;
        }

        ++octets;
        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }

    return octets == 4;
}

static std::string PrefIpPath() {
    char* base = SDL_GetPrefPath("BanditVault", "BanditMouseRelay");
    if (!base) {
        return std::string();
    }
    std::string path = std::string(base) + "last_ip.txt";
    SDL_free(base);
    return path;
}

static std::string LoadSavedIp() {
    const std::string path = PrefIpPath();
    if (path.empty()) {
        return std::string();
    }
    std::ifstream in(path);
    if (!in) {
        return std::string();
    }
    std::string ip;
    std::getline(in, ip);
    while (!ip.empty() && (ip.back() == '\r' || ip.back() == '\n' || ip.back() == ' ')) {
        ip.pop_back();
    }
    return IsValidIpv4(ip) ? ip : std::string();
}

static void SaveIp(const std::string& ip) {
    if (!IsValidIpv4(ip)) {
        return;
    }
    const std::string path = PrefIpPath();
    if (path.empty()) {
        return;
    }
    std::ofstream out(path, std::ios::trunc);
    if (out) {
        out << ip << "\n";
    }
}

static int ButtonIndex(uint8_t button) {
    switch (button) {
    case SDL_BUTTON_LEFT:
        return 0;
    case SDL_BUTTON_RIGHT:
        return 1;
    case SDL_BUTTON_MIDDLE:
        return 2;
    case SDL_BUTTON_X1:
        return 3;
    case SDL_BUTTON_X2:
        return 4;
    default:
        return -1;
    }
}

static bool ParseModeStatus(const std::string& text, RelayMode& mode) {
    if (text.rfind("MODE:GAMEPLAY", 0) == 0) {
        mode = RelayMode::Gameplay;
        return true;
    }
    if (text.rfind("MODE:MENU", 0) == 0) {
        mode = RelayMode::Menu;
        return true;
    }
    return false;
}

static std::optional<std::pair<float, float>> ParseSyncStatus(const std::string& text) {
    size_t prefixLength = 0;
    if (text.rfind("SYNCW:", 0) == 0) {
        prefixLength = 6;
    } else if (text.rfind("SYNC:", 0) == 0) {
        prefixLength = 5;
    } else {
        return std::nullopt;
    }

    const size_t comma = text.find(',', prefixLength);
    if (comma == std::string::npos) {
        return std::nullopt;
    }

    char* end = nullptr;
    const float x = std::strtof(text.c_str() + prefixLength, &end);
    if (!end || static_cast<size_t>(end - text.c_str()) != comma) {
        return std::nullopt;
    }

    end = nullptr;
    const float y = std::strtof(text.c_str() + comma + 1, &end);
    if (!end || *end != '\0') {
        return std::nullopt;
    }

    return std::make_pair(x, y);
}

static bool IsStatusTerminator(char ch) {
    return ch == '\0' || ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
}

static std::optional<std::pair<float, float>> ParsePairStatus(const std::string& text, const char* marker) {
    const size_t markerPos = text.find(marker);
    if (markerPos == std::string::npos) {
        return std::nullopt;
    }

    const size_t prefixLength = markerPos + std::strlen(marker);
    const size_t comma = text.find(',', prefixLength);
    if (comma == std::string::npos) {
        return std::nullopt;
    }

    char* end = nullptr;
    const float x = std::strtof(text.c_str() + prefixLength, &end);
    if (!end || static_cast<size_t>(end - text.c_str()) != comma) {
        return std::nullopt;
    }

    end = nullptr;
    const float y = std::strtof(text.c_str() + comma + 1, &end);
    if (!end || !IsStatusTerminator(*end)) {
        return std::nullopt;
    }

    return std::make_pair(x, y);
}

static std::optional<std::pair<float, float>> ParseDimensionStatus(const std::string& text, const char* markerText) {
    const size_t marker = text.find(markerText);
    if (marker == std::string::npos) {
        return std::nullopt;
    }

    const char* start = text.c_str() + marker + std::strlen(markerText);
    char* end = nullptr;
    const float width = std::strtof(start, &end);
    if (!end || *end != 'x') {
        return std::nullopt;
    }

    start = end + 1;
    end = nullptr;
    const float height = std::strtof(start, &end);
    if (width < 1.0f || height < 1.0f) {
        return std::nullopt;
    }
    if (!end || !IsStatusTerminator(*end)) {
        return std::nullopt;
    }

    return std::make_pair(width, height);
}

static std::optional<std::pair<float, float>> ParseSizeStatus(const std::string& text) {
    return ParseDimensionStatus(text, "size=");
}

#ifdef _WIN32
struct WinsockRuntime {
    WinsockRuntime() {
        WSADATA data{};
        ok = WSAStartup(MAKEWORD(2, 2), &data) == 0;
    }

    ~WinsockRuntime() {
        if (ok) {
            WSACleanup();
        }
    }

    bool ok = false;
};

struct TimerResolution {
    // windows scheduler ticks at ~15.6ms by default, capping SDL_Delay and the loop
    // at ~64hz; request 1ms so the 240hz send pacing is actually reachable
    bool raised = (timeBeginPeriod(1) == TIMERR_NOERROR);
    ~TimerResolution() {
        if (raised) {
            timeEndPeriod(1);
        }
    }
};

static bool SocketWouldBlock() {
    const int err = WSAGetLastError();
    return err == WSAEWOULDBLOCK || err == WSAEINTR;
}

static void CloseSocket(SocketHandle sock) {
    if (sock != kInvalidSocket) {
        closesocket(sock);
    }
}

static bool SetNonblocking(SocketHandle sock) {
    u_long mode = 1;
    return ioctlsocket(sock, FIONBIO, &mode) == 0;
}
#else
struct WinsockRuntime {
    bool ok = true;
};

struct TimerResolution {};

static bool SocketWouldBlock() {
    return errno == EWOULDBLOCK || errno == EAGAIN || errno == EINTR;
}

static void CloseSocket(SocketHandle sock) {
    if (sock != kInvalidSocket) {
        close(sock);
    }
}

static bool SetNonblocking(SocketHandle sock) {
    const int flags = fcntl(sock, F_GETFL, 0);
    if (flags < 0) {
        return false;
    }
    return fcntl(sock, F_SETFL, flags | O_NONBLOCK) == 0;
}
#endif

class UdpTransport {
public:
    UdpTransport() = default;

    ~UdpTransport() {
        Close();
    }

    bool Open(const std::string& host, std::string& error) {
        Close();

        inputSock_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (inputSock_ == kInvalidSocket) {
            error = "Could not create input UDP socket";
            return false;
        }

        statusSock_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (statusSock_ == kInvalidSocket) {
            error = "Could not create status UDP socket";
            Close();
            return false;
        }

        int reuse = 1;
        setsockopt(statusSock_, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));

        sockaddr_in statusAddr{};
        statusAddr.sin_family = AF_INET;
        statusAddr.sin_port = htons(kStatusPort);
        statusAddr.sin_addr.s_addr = htonl(INADDR_ANY);
        if (bind(statusSock_, reinterpret_cast<sockaddr*>(&statusAddr), sizeof(statusAddr)) != 0) {
            error = "Could not bind status UDP port 7332";
            Close();
            return false;
        }

        if (!SetNonblocking(statusSock_)) {
            error = "Could not make status socket nonblocking";
            Close();
            return false;
        }

        destination_ = {};
        destination_.sin_family = AF_INET;
        destination_.sin_port = htons(kInputPort);
        if (inet_pton(AF_INET, host.c_str(), &destination_.sin_addr) != 1) {
            error = "Invalid target IPv4 address";
            Close();
            return false;
        }

        host_ = host;
        return true;
    }

    void Close() {
        CloseSocket(inputSock_);
        CloseSocket(statusSock_);
        inputSock_ = kInvalidSocket;
        statusSock_ = kInvalidSocket;
    }

    bool IsOpen() const {
        return inputSock_ != kInvalidSocket && statusSock_ != kInvalidSocket;
    }

    bool Send(const std::string& packet) {
        if (inputSock_ == kInvalidSocket) {
            return false;
        }

        const int sent = sendto(
            inputSock_,
            packet.data(),
            static_cast<int>(packet.size()),
            0,
            reinterpret_cast<const sockaddr*>(&destination_),
            sizeof(destination_));
        const bool ok = sent == static_cast<int>(packet.size());
        g_packetLog.LogTx(packet, ok);
        return ok;
    }

    std::vector<std::string> ReceiveStatus() {
        std::vector<std::string> messages;
        if (statusSock_ == kInvalidSocket) {
            return messages;
        }

        for (;;) {
            char buffer[256]{};
            sockaddr_in from{};
#ifdef _WIN32
            int fromLen = sizeof(from);
#else
            socklen_t fromLen = sizeof(from);
#endif
            const int received = recvfrom(
                statusSock_,
                buffer,
                static_cast<int>(sizeof(buffer) - 1),
                0,
                reinterpret_cast<sockaddr*>(&from),
                &fromLen);

            if (received < 0) {
                if (!SocketWouldBlock()) {
                    messages.emplace_back("STATUS_SOCKET_ERROR");
                }
                break;
            }

            if (from.sin_addr.s_addr == destination_.sin_addr.s_addr) {
                buffer[received] = '\0';
                messages.emplace_back(buffer);
                g_packetLog.LogRx(buffer);
            }
        }

        return messages;
    }

private:
    SocketHandle inputSock_ = kInvalidSocket;
    SocketHandle statusSock_ = kInvalidSocket;
    sockaddr_in destination_{};
    std::string host_;
};

class RelayApp {
public:
    RelayApp(SDL_Window* window, SDL_Renderer* renderer)
        : window_(window), renderer_(renderer) {
        RefreshWindowSize();
        ipBuffer_ = LoadSavedIp();
        SDL_StartTextInput(window_);
        netRunning_ = true;
        netThread_ = std::thread(&RelayApp::NetLoop, this);
    }

    ~RelayApp() {
        netRunning_ = false;
        if (netThread_.joinable()) {
            netThread_.join();
        }
        ReleaseAllButtons();
        SDL_StopTextInput(window_);
        SetMouseCapture(false);
    }

    bool Running() const {
        return running_;
    }

    void HandleEvent(const SDL_Event& event) {
        switch (event.type) {
        case SDL_EVENT_QUIT:
            running_ = false;
            return;
        case SDL_EVENT_WINDOW_RESIZED:
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
            RefreshWindowSize();
            return;
        case SDL_EVENT_WINDOW_FOCUS_GAINED:
            if (hostSet_ && !enteringIp_) {
                SetMouseCapture(true);
            }
            return;
        case SDL_EVENT_WINDOW_FOCUS_LOST:
            SetMouseCapture(false);
            CancelHold();
            ReleaseAllButtons();
            return;
        case SDL_EVENT_WINDOW_MOUSE_LEAVE:
            CancelHold();
            ReleaseAllButtons();
            return;
        case SDL_EVENT_TEXT_INPUT:
            if (enteringIp_) {
                AppendIpText(event.text.text ? event.text.text : "");
            }
            return;
        case SDL_EVENT_KEY_DOWN:
            HandleKey(event.key.key);
            return;
        case SDL_EVENT_MOUSE_MOTION:
            if (!enteringIp_ && !menuOpen_) {
                AddMotion(event.motion.xrel, event.motion.yrel);
            }
            if (holdAction_ != UiAction::None && !PointerOverHoldButton(event.motion.x, event.motion.y)) {
                CancelHold();
            }
            return;
        case SDL_EVENT_MOUSE_WHEEL:
            if (!enteringIp_ && !menuOpen_) {
                const float scroll = event.wheel.integer_y != 0 ? static_cast<float>(event.wheel.integer_y) : event.wheel.y;
                pendingScroll_ += scroll;
            }
            return;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:
            HandleMouseButton(event);
            return;
        case SDL_EVENT_FINGER_DOWN:
        case SDL_EVENT_FINGER_UP:
            HandleFingerButton(event);
            return;
        case SDL_EVENT_FINGER_MOTION:
            if (!enteringIp_ && !menuOpen_ && !IsTouchControlFinger(event.tfinger.fingerID)) {
                AddMotion(event.tfinger.dx * static_cast<float>(windowWidth_), event.tfinger.dy * static_cast<float>(windowHeight_));
            }
            return;
        default:
            return;
        }
    }

    void Tick() {
        PollStatus();
        SendConnectionProbe();
        RepeatHeldTouchScroll();
        UpdateHold();
        g_packetLog.MaybeFlush();
    }

    void Render() {
        const float scale = UiScale();
        const float uiWidth = UiAreaWidth();
        const float uiHeight = UiAreaHeight();

        // otherwise the 8px font draws 1x and ios upscales it
        const float renderScale = scale * pixelDensity_;

        SDL_SetRenderViewport(renderer_, nullptr);
        SDL_SetRenderScale(renderer_, renderScale, renderScale);
        SDL_SetRenderDrawColor(renderer_, 9, 12, 18, 255);
        SDL_RenderClear(renderer_);

        if (safeArea_.x != 0 || safeArea_.y != 0) {
            const SDL_Rect viewport{
                static_cast<int>(std::lround(safeArea_.x / scale)),
                static_cast<int>(std::lround(safeArea_.y / scale)),
                static_cast<int>(std::lround(safeArea_.w / scale)),
                static_cast<int>(std::lround(safeArea_.h / scale))
            };
            SDL_SetRenderViewport(renderer_, &viewport);
        }

        if (enteringIp_) {
            RenderIpScreen(uiWidth, uiHeight);
        } else {
            RenderRelayScreen(uiWidth, uiHeight);
        }

        SDL_SetRenderViewport(renderer_, nullptr);
        SDL_RenderPresent(renderer_);
    }

    void PumpAndTick() {
        SDL_Event event{};
        while (SDL_PollEvent(&event)) {
            std::lock_guard<std::mutex> lock(sendMutex_);
            HandleEvent(event);
        }
        {
            std::lock_guard<std::mutex> lock(sendMutex_);
            Tick();
        }
    }

private:
    void NetLoop() {
#ifdef _WIN32
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
#endif
        double nextTick = NowSeconds();
        while (netRunning_.load(std::memory_order_relaxed)) {
            {
                std::lock_guard<std::mutex> lock(sendMutex_);
                SendPendingPackets();
            }
            nextTick += kSendIntervalSeconds;
            const double now = NowSeconds();
            if (nextTick < now) {
                nextTick = now;
            }
            SleepUntil(nextTick);
        }
    }

    float UiScale() const {
        // under 1 only works because the retina render scale keeps small text sharp
        const float minScale = kTouchLayout ? 0.75f : 1.0f;
        return Clamp(static_cast<float>(safeArea_.h) / 720.0f, minScale, 3.0f);
    }

    float UiAreaWidth() const {
        return static_cast<float>(safeArea_.w) / UiScale();
    }

    float UiAreaHeight() const {
        return static_cast<float>(safeArea_.h) / UiScale();
    }

    std::vector<Button> BuildButtons(float uiWidth, float uiHeight) const {
        std::vector<Button> buttons;

        if (enteringIp_) {
            const float width = kTouchLayout ? 176.0f : 156.0f;
            const float height = kTouchLayout ? 46.0f : 34.0f;
            const float gap = kTouchLayout ? 18.0f : 16.0f;
            const float total = (width * 2.0f) + gap;
            const float x = (uiWidth - total) * 0.5f;
            const float top = IpScreenTop(uiHeight);

            if (kTouchLayout) {
                const float keyWidth = 78.0f;
                const float keyHeight = 44.0f;
                const float keyGap = 8.0f;
                const float keyTotal = (keyWidth * 3.0f) + (keyGap * 2.0f);
                const float keyX = (uiWidth - keyTotal) * 0.5f;
                const float keyY = top + 150.0f;
                const std::array<const char*, 12> labels{
                    "1", "2", "3",
                    "4", "5", "6",
                    "7", "8", "9",
                    ".", "0", "Del"
                };

                for (size_t i = 0; i < labels.size(); ++i) {
                    const float col = static_cast<float>(i % 3);
                    const float row = static_cast<float>(i / 3);
                    const char* label = labels[i];
                    Button key{};
                    key.rect = SDL_FRect{
                        keyX + (col * (keyWidth + keyGap)),
                        keyY + (row * (keyHeight + keyGap)),
                        keyWidth,
                        keyHeight
                    };
                    key.label = label;
                    if (std::strcmp(label, "Del") == 0) {
                        key.backspace = true;
                    } else {
                        key.inputText = label;
                    }
                    buttons.push_back(key);
                }
            }

            const float y = top + (kTouchLayout ? 374.0f : 168.0f);
            buttons.push_back({
                SDL_FRect{ x, y, width, height },
                connecting_ ? "Checking" : "Connect",
                connecting_ ? UiAction::None : UiAction::Connect
            });
            buttons.push_back({ SDL_FRect{ x + width + gap, y, width, height }, hostSet_ || connecting_ ? "Cancel" : "Quit", UiAction::CancelIp });
            return buttons;
        }

        if (kTouchLayout) {
            if (menuOpen_) {
                return BuildMenuButtons(uiWidth, uiHeight);
            }
            const float margin = 16.0f;
            const float utilityWidth = 118.0f;
            const float utilityHeight = 44.0f;
            const float utilityGap = 10.0f;
            const float utilityTotal = (utilityWidth * 4.0f) + (utilityGap * 3.0f);
            float utilityX = std::max(margin, (uiWidth - utilityTotal) * 0.5f);
            const float utilityY = uiHeight - utilityHeight - margin;

            buttons.push_back({ SDL_FRect{ utilityX, utilityY, utilityWidth, utilityHeight }, "Change IP", UiAction::ChangeIp });
            utilityX += utilityWidth + utilityGap;
            buttons.push_back({ SDL_FRect{ utilityX, utilityY, utilityWidth, utilityHeight }, "Toggle", UiAction::ToggleMode });
            utilityX += utilityWidth + utilityGap;
            buttons.push_back({ SDL_FRect{ utilityX, utilityY, utilityWidth, utilityHeight }, "Release", UiAction::ReleaseButtons });
            utilityX += utilityWidth + utilityGap;
            buttons.push_back({ SDL_FRect{ utilityX, utilityY, utilityWidth, utilityHeight }, "Quit", UiAction::Quit });

            const float padWidth = 132.0f;
            const float padHeight = 48.0f;
            const float padGap = 10.0f;
            const float padTop = 172.0f;
            const float columnHeight = (padHeight * 4.0f) + (padGap * 3.0f);

            // column needs 222ui, iphone landscape leaves about 130
            const bool padGrid = (utilityY - padGap - padTop) < columnHeight;

            if (padGrid) {
                const float gridWidth = (padWidth * 2.0f) + padGap;
                const float gridX = uiWidth - gridWidth - margin;
                const float gridY = std::max(padTop, utilityY - padGap - (padHeight * 2.0f) - padGap);
                const float colB = gridX + padWidth + padGap;
                const float rowB = gridY + padHeight + padGap;

                buttons.push_back({ SDL_FRect{ gridX, gridY, padWidth, padHeight }, "Hold L", UiAction::None, 0, 0.0f });
                buttons.push_back({ SDL_FRect{ colB, gridY, padWidth, padHeight }, "Hold R", UiAction::None, 1, 0.0f });
                buttons.push_back({ SDL_FRect{ gridX, rowB, padWidth, padHeight }, "Wheel Up", UiAction::None, -1, kTouchScrollStep });
                buttons.push_back({ SDL_FRect{ colB, rowB, padWidth, padHeight }, "Wheel Down", UiAction::None, -1, -kTouchScrollStep });
                return buttons;
            }

            const float padX = uiWidth - padWidth - margin;
            float padY = std::max(padTop, utilityY - ((padHeight + padGap) * 4.0f) - 10.0f);

            buttons.push_back({ SDL_FRect{ padX, padY, padWidth, padHeight }, "Hold L", UiAction::None, 0, 0.0f });
            padY += padHeight + padGap;
            buttons.push_back({ SDL_FRect{ padX, padY, padWidth, padHeight }, "Hold R", UiAction::None, 1, 0.0f });
            padY += padHeight + padGap;
            buttons.push_back({ SDL_FRect{ padX, padY, padWidth, padHeight }, "Wheel Up", UiAction::None, -1, kTouchScrollStep });
            padY += padHeight + padGap;
            buttons.push_back({ SDL_FRect{ padX, padY, padWidth, padHeight }, "Wheel Down", UiAction::None, -1, -kTouchScrollStep });
            return buttons;
        }

        if (!menuOpen_) {
            return buttons;
        }
        return BuildMenuButtons(uiWidth, uiHeight);
    }

    std::vector<Button> BuildMenuButtons(float uiWidth, float uiHeight) const {
        std::vector<Button> buttons;
        const float buttonWidth = 132.0f;
        const float buttonHeight = kTouchLayout ? 44.0f : 34.0f;
        const float gap = kTouchLayout ? 12.0f : 10.0f;
        const float panelHeight = kTouchLayout ? 360.0f : 300.0f;
        const float x = (uiWidth - buttonWidth) * 0.5f;
        float y = ((uiHeight - panelHeight) * 0.5f) + 58.0f;

        buttons.push_back({ SDL_FRect{ x, y, buttonWidth, buttonHeight }, "Resume", UiAction::Resume });
        y += buttonHeight + gap;
        buttons.push_back({ SDL_FRect{ x, y, buttonWidth, buttonHeight }, "Change IP", UiAction::ChangeIp });
        y += buttonHeight + gap;
        buttons.push_back({ SDL_FRect{ x, y, buttonWidth, buttonHeight }, "Toggle", UiAction::ToggleMode });
        y += buttonHeight + gap;
        buttons.push_back({ SDL_FRect{ x, y, buttonWidth, buttonHeight }, "Release", UiAction::ReleaseButtons });
        y += buttonHeight + gap;
        buttons.push_back({ SDL_FRect{ x, y, buttonWidth, buttonHeight }, "Quit", UiAction::Quit });
        return buttons;
    }

    std::optional<Button> HitButtonAtUi(float uiX, float uiY) const {
        for (const Button& button : BuildButtons(UiAreaWidth(), UiAreaHeight())) {
            if (uiX >= button.rect.x && uiX <= button.rect.x + button.rect.w &&
                uiY >= button.rect.y && uiY <= button.rect.y + button.rect.h) {
                return button;
            }
        }

        return std::nullopt;
    }

    UiAction HitActionAtUi(float uiX, float uiY) const {
        const std::optional<Button> button = HitButtonAtUi(uiX, uiY);
        return button ? button->action : UiAction::None;
    }

    std::optional<Button> HitButton(float windowX, float windowY) const {
        const float scale = UiScale();
        return HitButtonAtUi(
            (windowX - static_cast<float>(safeArea_.x)) / scale,
            (windowY - static_cast<float>(safeArea_.y)) / scale);
    }

    UiAction HitAction(float windowX, float windowY) const {
        const std::optional<Button> button = HitButton(windowX, windowY);
        return button ? button->action : UiAction::None;
    }

    UiAction HitActionAtRelayCursor() const {
        if (enteringIp_ || mode_ != RelayMode::Menu) {
            return UiAction::None;
        }

        const float uiWidth = UiAreaWidth();
        const float uiHeight = UiAreaHeight();
        const float uiX = Clamp((virtualX_ / MenuTargetWidth()) * uiWidth, 0.0f, uiWidth - 1.0f);
        const float uiY = Clamp((virtualY_ / MenuTargetHeight()) * uiHeight, 0.0f, uiHeight - 1.0f);
        return HitActionAtUi(uiX, uiY);
    }

    void ExecuteAction(UiAction action) {
        switch (action) {
        case UiAction::Connect:
            TryConnect();
            break;
        case UiAction::CancelIp:
            if (connecting_) {
                connecting_ = false;
                transport_.Close();
                ipError_.clear();
                SDL_StartTextInput(window_);
                break;
            }
            if (hostSet_) {
                enteringIp_ = false;
                ipError_.clear();
                SDL_StopTextInput(window_);
                SetMouseCapture(true);
            } else {
                running_ = false;
            }
            break;
        case UiAction::ChangeIp:
            ReleaseAllButtons();
            menuOpen_ = false;
            enteringIp_ = true;
            ipBuffer_ = host_;
            ipError_.clear();
            SetMouseCapture(false);
            SDL_StartTextInput(window_);
            break;
        case UiAction::ToggleMode:
            mode_ = mode_ == RelayMode::Gameplay ? RelayMode::Menu : RelayMode::Gameplay;
            if (mode_ == RelayMode::Menu) {
                SyncMenuCenter();
            }
            break;
        case UiAction::ReleaseButtons:
            ReleaseAllButtons();
            break;
        case UiAction::Resume:
            ResumeRelay();
            break;
        case UiAction::Quit:
            running_ = false;
            break;
        case UiAction::None:
            break;
        }
    }

    bool ExecuteButtonRelease(const Button& button) {
        if (button.backspace) {
            if (!ipBuffer_.empty()) {
                ipBuffer_.pop_back();
            }
            return true;
        }

        if (!button.inputText.empty()) {
            AppendIpText(button.inputText.c_str());
            return true;
        }

        if (button.action != UiAction::None) {
            ExecuteAction(button.action);
            return true;
        }

        return false;
    }

    bool ActionNeedsHold(UiAction action) const {
        if (enteringIp_) {
            return false;
        }
        return action != UiAction::None
            && action != UiAction::ToggleMode
            && action != UiAction::Connect
            && action != UiAction::Resume;
    }

    void BeginHold(UiAction action) {
        holdAction_ = action;
        holdStart_ = NowSeconds();
    }

    void CancelHold() {
        holdAction_ = UiAction::None;
    }

    void UpdateHold() {
        if (holdAction_ == UiAction::None) {
            return;
        }
        if (NowSeconds() - holdStart_ >= kButtonHoldSeconds) {
            const UiAction action = holdAction_;
            holdAction_ = UiAction::None;
            ExecuteAction(action);
        }
    }

    float HoldProgress(const Button& button) const {
        if (holdAction_ == UiAction::None || button.action != holdAction_) {
            return 0.0f;
        }
        const float elapsed = static_cast<float>(NowSeconds() - holdStart_);
        return Clamp(elapsed / static_cast<float>(kButtonHoldSeconds), 0.0f, 1.0f);
    }

    bool PointerOverHoldButton(float windowX, float windowY) const {
        if (holdAction_ == UiAction::None) {
            return false;
        }
        const std::optional<Button> hit = HitButton(windowX, windowY);
        if (hit && hit->action == holdAction_) {
            return true;
        }
        const std::optional<Button> relayButton = HitButtonAtRelayCursor();
        if (relayButton && relayButton->action == holdAction_) {
            return true;
        }
        return false;
    }

    void OnButtonPressed(const Button& button) {
        if (button.backspace || !button.inputText.empty()) {
            ExecuteButtonRelease(button);
            return;
        }
        if (button.action == UiAction::None) {
            return;
        }
        if (!ActionNeedsHold(button.action)) {
            ExecuteAction(button.action);
            return;
        }
        BeginHold(button.action);
    }

    std::optional<Button> HitButtonAtRelayCursor() const {
        if (enteringIp_ || mode_ != RelayMode::Menu) {
            return std::nullopt;
        }
        const float uiWidth = UiAreaWidth();
        const float uiHeight = UiAreaHeight();
        const float uiX = Clamp((virtualX_ / MenuTargetWidth()) * uiWidth, 0.0f, uiWidth - 1.0f);
        const float uiY = Clamp((virtualY_ / MenuTargetHeight()) * uiHeight, 0.0f, uiHeight - 1.0f);
        return HitButtonAtUi(uiX, uiY);
    }

    void HandleKey(SDL_Keycode key) {
        if (key == SDLK_F11) {
            const bool isFullscreen = (SDL_GetWindowFlags(window_) & SDL_WINDOW_FULLSCREEN) != 0;
            SDL_SetWindowFullscreen(window_, !isFullscreen);
            return;
        }
        if (enteringIp_) {
            if (connecting_) {
                if (key == SDLK_ESCAPE) {
                    ExecuteAction(UiAction::CancelIp);
                }
                return;
            }

            if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
                TryConnect();
            } else if (key == SDLK_BACKSPACE && !ipBuffer_.empty()) {
                ipBuffer_.pop_back();
            } else if (key == SDLK_ESCAPE) {
                ExecuteAction(UiAction::CancelIp);
            }
            return;
        }

        if (key == SDLK_ESCAPE) {
            if (menuOpen_) {
                ResumeRelay();
            } else {
                OpenRelayMenu();
            }
        } else if (key == SDLK_F3) {
            ExecuteAction(UiAction::ChangeIp);
        } else if (key == SDLK_F8) {
            ExecuteAction(UiAction::Quit);
        } else if (key == SDLK_F9) {
            ExecuteAction(UiAction::ToggleMode);
        } else if (key == SDLK_F10) {
            g_packetLog.Toggle();
            if (g_packetLog.Enabled()) {
                g_packetLog.Mark("logging started host=" + host_ + " mode=" + ModeName(mode_));
            }
        }
    }

    void HandleMouseButton(const SDL_Event& event) {
        std::optional<Button> hit;
        std::optional<Button> relayCursorButton;
        if (enteringIp_ || menuOpen_ || mode_ == RelayMode::Menu) {
            hit = HitButton(event.button.x, event.button.y);
            if (!hit && !menuOpen_) {
                relayCursorButton = HitButtonAtRelayCursor();
            }
        }

        if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
            if (hit) {
                OnButtonPressed(*hit);
                return;
            }
            if (relayCursorButton) {
                OnButtonPressed(*relayCursorButton);
                return;
            }
            if (enteringIp_ || menuOpen_) {
                return;
            }
            const int index = ButtonIndex(event.button.button);
            if (index >= 0) {
                buttonState_[static_cast<size_t>(index)] = 1;
            }
            return;
        }

        CancelHold();
        if (menuOpen_) {
            return;
        }
        const int index = ButtonIndex(event.button.button);
        if (index >= 0) {
            buttonState_[static_cast<size_t>(index)] = 0;
        }
    }

    void HandleFingerButton(const SDL_Event& event) {
        const float x = event.tfinger.x * static_cast<float>(windowWidth_);
        const float y = event.tfinger.y * static_cast<float>(windowHeight_);
        const std::optional<Button> hit = HitButton(x, y);
        const SDL_FingerID finger = event.tfinger.fingerID;

        if (event.type == SDL_EVENT_FINGER_DOWN) {
            if (!hit) {
                return;
            }

            if (hit->backspace || !hit->inputText.empty()) {
                ExecuteButtonRelease(*hit);
                return;
            }

            if (hit->mouseButtonIndex >= 0 && hit->mouseButtonIndex < static_cast<int>(buttonState_.size())) {
                const size_t index = static_cast<size_t>(hit->mouseButtonIndex);
                buttonState_[index] = 1;
                touchButtonFingers_[index] = finger;
                return;
            }

            if (hit->scrollStep != 0.0f) {
                pendingScroll_ += hit->scrollStep;
                touchScrollFinger_ = finger;
                touchScrollStep_ = hit->scrollStep;
                lastTouchScrollRepeat_ = NowSeconds();
                return;
            }

            if (hit->action != UiAction::None) {
                if (ActionNeedsHold(hit->action)) {
                    touchActionFinger_ = finger;
                    touchAction_ = hit->action;
                    BeginHold(hit->action);
                } else {
                    ExecuteAction(hit->action);
                }
            }
            return;
        }

        if (event.type == SDL_EVENT_FINGER_UP) {
            for (size_t i = 0; i < touchButtonFingers_.size(); ++i) {
                if (touchButtonFingers_[i] && *touchButtonFingers_[i] == finger) {
                    buttonState_[i] = 0;
                    touchButtonFingers_[i].reset();
                    return;
                }
            }

            if (touchScrollFinger_ && *touchScrollFinger_ == finger) {
                touchScrollFinger_.reset();
                touchScrollStep_ = 0.0f;
                return;
            }

            if (touchActionFinger_ && *touchActionFinger_ == finger) {
                touchActionFinger_.reset();
                touchAction_ = UiAction::None;
                CancelHold();
            }
        }
    }

    bool IsTouchControlFinger(SDL_FingerID finger) const {
        if (touchActionFinger_ && *touchActionFinger_ == finger) {
            return true;
        }
        if (touchScrollFinger_ && *touchScrollFinger_ == finger) {
            return true;
        }
        for (const std::optional<SDL_FingerID>& buttonFinger : touchButtonFingers_) {
            if (buttonFinger && *buttonFinger == finger) {
                return true;
            }
        }
        return false;
    }

    void AppendIpText(const char* text) {
        for (const char* p = text; p && *p; ++p) {
            if ((*p >= '0' && *p <= '9') || *p == '.') {
                if (ipBuffer_.size() < 15) {
                    ipBuffer_.push_back(*p);
                }
            }
        }
    }

    void TryConnect() {
        std::string candidate = ipBuffer_;
        candidate.erase(std::remove_if(candidate.begin(), candidate.end(), [](unsigned char ch) {
            return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
        }), candidate.end());

        if (!IsValidIpv4(candidate)) {
            ipError_ = "Not a valid IPv4 address";
            return;
        }

        std::string error;
        if (!transport_.Open(candidate, error)) {
            hostSet_ = false;
            host_.clear();
            ipError_ = error;
            return;
        }

        hostSet_ = false;
        host_.clear();
        pendingHost_ = candidate;
        connecting_ = true;
        enteringIp_ = true;
        connected_ = false;
        lastStatus_ = "<none>";
        statusPackets_ = 0;
        sentPackets_ = 0;
        pendingScroll_ = 0.0f;
        accumDx_ = 0.0f;
        accumDy_ = 0.0f;
        motionPending_ = false;
        lastSentButtons_.fill(-1);
        ipError_ = "Checking for Bandit launcher at " + candidate + "...";
        connectStart_ = NowSeconds();
        lastConnectProbe_ = 0.0;

        SDL_StopTextInput(window_);
    }

    void RefreshWindowSize() {
        int width = 0;
        int height = 0;
        if (!SDL_GetWindowSize(window_, &width, &height) || width <= 0 || height <= 0) {
            width = 1280;
            height = 720;
        }

        windowWidth_ = width;
        windowHeight_ = height;

        safeArea_ = SDL_Rect{ 0, 0, width, height };
        if (kTouchLayout) {
            SDL_Rect safe{};
            // notch and home indicator sit over the ui otherwise
            if (SDL_GetWindowSafeArea(window_, &safe) && safe.w > 0 && safe.h > 0) {
                safeArea_ = safe;
            }
        }

        pixelDensity_ = SDL_GetWindowPixelDensity(window_);
        if (!(pixelDensity_ > 0.0f)) {
            pixelDensity_ = 1.0f;
        }

        RefreshMotionScale();
    }

    void SetMouseCapture(bool on) {
        SDL_SetWindowRelativeMouseMode(window_, on);
        SDL_SetWindowMouseGrab(window_, on);
    }

    void OpenRelayMenu() {
        ReleaseAllButtons();
        CancelHold();
        pendingScroll_ = 0.0f;
        accumDx_ = 0.0f;
        accumDy_ = 0.0f;
        motionPending_ = false;
        menuOpen_ = true;
        SetMouseCapture(false);
    }

    void ResumeRelay() {
        if (enteringIp_) {
            return;
        }
        menuOpen_ = false;
        SetMouseCapture(true);
    }

    void AddMotion(float rawDx, float rawDy) {
        if (mode_ == RelayMode::Menu) {
            const float dx = rawDx * MenuScaleX();
            const float dy = rawDy * MenuScaleY();
            if (dx != 0.0f || dy != 0.0f) {
                virtualX_ = Clamp(virtualX_ + dx, 0.0f, MenuTargetWidth() - 1.0f);
                virtualY_ = Clamp(virtualY_ + dy, 0.0f, MenuTargetHeight() - 1.0f);
                motionPending_ = true;
            }
        } else if (rawDx != 0.0f || rawDy != 0.0f) {
            accumDx_ += rawDx * scaleX_;
            accumDy_ += rawDy * scaleY_;
            motionPending_ = true;
        }
    }

    void PollStatus() {
        if ((!connecting_ && enteringIp_) || !transport_.IsOpen()) {
            return;
        }

        for (const std::string& status : transport_.ReceiveStatus()) {
            ProcessStatus(status);
        }
    }

    void ProcessStatus(const std::string& status) {
        const auto sync = ParseSyncStatus(status);
        const auto windowCursor = ParsePairStatus(status, "cursorw=");
        const auto protocolCursor = ParsePairStatus(status, "cursor=");
        const auto size = ParseSizeStatus(status);
        const auto menuSize = ParseDimensionStatus(status, "menu=");
        RelayMode parsedMode{};
        const bool hasMode = ParseModeStatus(status, parsedMode);
        const bool isReady = status.rfind("javauwp_glfw_mouse:ready", 0) == 0 ||
            status.rfind("javauwp_glfw_mouse:receiving", 0) == 0;
        if (!sync && !windowCursor && !protocolCursor && !hasMode && !size && !menuSize && !isReady) {
            return;
        }

        if (connecting_) {
            CompleteConnection(status);
        }

        ++statusPackets_;
        connected_ = true;
        lastStatus_ = status;

        if (size) {
            SetTargetSize(size->first, size->second);
        }
        if (menuSize) {
            SetMenuTargetSize(menuSize->first, menuSize->second);
        }

        if (sync) {
            float syncX = sync->first;
            float syncY = sync->second;
            if (status.rfind("SYNC:", 0) == 0) {
                syncX = syncX * (MenuTargetWidth() / kTargetWidth);
                syncY = syncY * (MenuTargetHeight() / kTargetHeight);
            } else if (status.rfind("SYNCW:", 0) == 0) {
                syncX = WindowToMenuX(syncX);
                syncY = WindowToMenuY(syncY);
            }
            SyncMenuPos(syncX, syncY, true);
            return;
        }

        if (mode_ == RelayMode::Menu) {
            if (windowCursor) {
                SyncMenuPos(
                    WindowToMenuX(windowCursor->first),
                    WindowToMenuY(windowCursor->second),
                    true);
            } else if (protocolCursor) {
                SyncMenuPos(
                    protocolCursor->first * (MenuTargetWidth() / kTargetWidth),
                    protocolCursor->second * (MenuTargetHeight() / kTargetHeight),
                    true);
            }
        }

        if (hasMode) {
            const RelayMode previous = mode_;
            appMode_ = parsedMode;
            mode_ = parsedMode;
            if (previous == RelayMode::Gameplay && mode_ == RelayMode::Menu) {
                SyncMenuCenter();
            }
        }
    }

    void CompleteConnection(const std::string&) {
        host_ = pendingHost_;
        pendingHost_.clear();
        hostSet_ = true;
        SaveIp(host_);
        connecting_ = false;
        enteringIp_ = false;
        menuOpen_ = false;
        ipError_.clear();
        SDL_StopTextInput(window_);
        SetMouseCapture(true);
    }

    void SetTargetSize(float width, float height) {
        targetWidth_ = Clamp(width, 1.0f, 16384.0f);
        targetHeight_ = Clamp(height, 1.0f, 16384.0f);
        if (haveMenuTargetSize_) {
            ResizeMenuTarget(menuTargetWidth_, menuTargetHeight_, true);
        } else {
            ResizeMenuTarget(targetWidth_ * kMenuCoordinateScale, targetHeight_ * kMenuCoordinateScale, false);
        }
        RefreshMotionScale();
    }

    void SetMenuTargetSize(float width, float height) {
        ResizeMenuTarget(width, height, true);
    }

    void ResizeMenuTarget(float width, float height, bool explicitSize) {
        const float oldMenuWidth = MenuTargetWidth();
        const float oldMenuHeight = MenuTargetHeight();
        const float relativeX = oldMenuWidth > 0.0f ? virtualX_ / oldMenuWidth : 0.5f;
        const float relativeY = oldMenuHeight > 0.0f ? virtualY_ / oldMenuHeight : 0.5f;

        menuTargetWidth_ = Clamp(width, 1.0f, targetWidth_);
        menuTargetHeight_ = Clamp(height, 1.0f, targetHeight_);
        haveMenuTargetSize_ = haveMenuTargetSize_ || explicitSize;

        virtualX_ = Clamp(relativeX * MenuTargetWidth(), 0.0f, MenuTargetWidth() - 1.0f);
        virtualY_ = Clamp(relativeY * MenuTargetHeight(), 0.0f, MenuTargetHeight() - 1.0f);
    }

    void RefreshMotionScale() {
        scaleX_ = targetWidth_ / static_cast<float>(std::max(1, windowWidth_));
        scaleY_ = targetHeight_ / static_cast<float>(std::max(1, windowHeight_));
    }

    float MenuTargetWidth() const {
        return Clamp(menuTargetWidth_, 1.0f, targetWidth_);
    }

    float MenuTargetHeight() const {
        return Clamp(menuTargetHeight_, 1.0f, targetHeight_);
    }

    float MenuScaleX() const {
        return MenuTargetWidth() / static_cast<float>(std::max(1, windowWidth_));
    }

    float MenuScaleY() const {
        return MenuTargetHeight() / static_cast<float>(std::max(1, windowHeight_));
    }

    static float MapCoordinate(float value, float sourceExtent, float targetExtent) {
        if (sourceExtent <= 1.0f || targetExtent <= 1.0f) {
            return 0.0f;
        }
        const float clamped = Clamp(value, 0.0f, sourceExtent - 1.0f);
        return clamped * ((targetExtent - 1.0f) / (sourceExtent - 1.0f));
    }

    float MenuToWindowX(float x) const {
        return MapCoordinate(x, MenuTargetWidth(), targetWidth_);
    }

    float MenuToWindowY(float y) const {
        return MapCoordinate(y, MenuTargetHeight(), targetHeight_);
    }

    float WindowToMenuX(float x) const {
        return MapCoordinate(x, targetWidth_, MenuTargetWidth());
    }

    float WindowToMenuY(float y) const {
        return MapCoordinate(y, targetHeight_, MenuTargetHeight());
    }

    void SendConnectionProbe() {
        if (!connecting_ || !transport_.IsOpen()) {
            return;
        }

        const double now = NowSeconds();
        if (now - connectStart_ >= kConnectTimeoutSeconds) {
            const std::string failedHost = pendingHost_;
            connecting_ = false;
            pendingHost_.clear();
            transport_.Close();
            ipError_ = "No Bandit launcher response from " + failedHost;
            SDL_StartTextInput(window_);
            return;
        }

        if (lastConnectProbe_ == 0.0 || now - lastConnectProbe_ >= kConnectProbeSeconds) {
            if (transport_.Send("ping")) {
                ++sentPackets_;
            }
            lastConnectProbe_ = now;
        }
    }

    void RepeatHeldTouchScroll() {
        if (!touchScrollFinger_ || touchScrollStep_ == 0.0f || enteringIp_) {
            return;
        }

        const double now = NowSeconds();
        if (now - lastTouchScrollRepeat_ >= kTouchScrollRepeatSeconds) {
            pendingScroll_ += touchScrollStep_;
            lastTouchScrollRepeat_ = now;
        }
    }

    std::string FormatPacket(float dx, float dy, const std::array<int, 5>& buttons, float scroll) const {
        char buffer[160]{};
        std::snprintf(
            buffer,
            sizeof(buffer),
            "%.4f,%.4f,%d,%d,%d,%.4f,%d,%d",
            dx,
            dy,
            buttons[0],
            buttons[1],
            buttons[2],
            scroll,
            buttons[3],
            buttons[4]);
        return buffer;
    }

    std::string FormatAbsPacket(float x, float y, const std::array<int, 5>& buttons, float scroll) const {
        char buffer[180]{};
        std::snprintf(
            buffer,
            sizeof(buffer),
            "ABSW:%.4f,%.4f,%d,%d,%d,%.4f,%d,%d",
            MenuToWindowX(x),
            MenuToWindowY(y),
            buttons[0],
            buttons[1],
            buttons[2],
            scroll,
            buttons[3],
            buttons[4]);
        return buffer;
    }

    void SendPendingPackets() {
        if (enteringIp_ || !transport_.IsOpen()) {
            return;
        }

        const double now = NowSeconds();
        std::array<int, 5> packetButtons{ -1, -1, -1, -1, -1 };
        bool haveButtonChange = false;

        for (size_t i = 0; i < buttonState_.size(); ++i) {
            if (buttonState_[i] != lastSentButtons_[i]) {
                packetButtons[i] = buttonState_[i];
                haveButtonChange = true;
            }
        }

        const bool motionDue = motionPending_ && (now - lastMotionSend_) >= kSendIntervalSeconds;
        const bool heldRefreshDue = AnyLocalMouseButtonDown() && (now - lastHeldButtonRefresh_) >= kHeldButtonRefreshSeconds;
        if (heldRefreshDue) {
            packetButtons = buttonState_;
            haveButtonChange = true;
        }

        if (haveButtonChange || pendingScroll_ != 0.0f || motionDue) {
            const float scroll = pendingScroll_;
            pendingScroll_ = 0.0f;

            const std::string packet = mode_ == RelayMode::Menu
                ? FormatAbsPacket(virtualX_, virtualY_, packetButtons, scroll)
                : FormatPacket(accumDx_, accumDy_, packetButtons, scroll);

            if (transport_.Send(packet)) {
                ++sentPackets_;
            }

            accumDx_ = 0.0f;
            accumDy_ = 0.0f;
            motionPending_ = false;
            lastMotionSend_ = now;
            if (lastSendStamp_ > 0.0) {
                const double dtMs = (now - lastSendStamp_) * 1000.0;
                sendDtMinMs_ = std::min(sendDtMinMs_, dtMs);
                sendDtMaxMs_ = std::max(sendDtMaxMs_, dtMs);
            }
            lastSendStamp_ = now;
            if (heldRefreshDue) {
                lastHeldButtonRefresh_ = now;
            }

            for (size_t i = 0; i < packetButtons.size(); ++i) {
                if (packetButtons[i] != -1) {
                    lastSentButtons_[i] = packetButtons[i];
                }
            }
        }

        if (now - lastPing_ >= 1.0) {
            if (transport_.Send("ping")) {
                ++sentPackets_;
            }
            lastPing_ = now;
        }
    }

    bool AnyLocalMouseButtonDown() const {
        for (int state : buttonState_) {
            if (state) {
                return true;
            }
        }
        return false;
    }

    void SyncMenuCenter() {
        SyncMenuPos(MenuTargetWidth() * 0.5f, MenuTargetHeight() * 0.5f, false);
    }

    void SyncMenuPos(float x, float y, bool fromStatus) {
        virtualX_ = Clamp(x, 0.0f, MenuTargetWidth() - 1.0f);
        virtualY_ = Clamp(y, 0.0f, MenuTargetHeight() - 1.0f);
        accumDx_ = 0.0f;
        accumDy_ = 0.0f;
        motionPending_ = false;

        if (!fromStatus && transport_.IsOpen()) {
            const std::array<int, 5> unchanged{ -1, -1, -1, -1, -1 };
            if (transport_.Send(FormatAbsPacket(virtualX_, virtualY_, unchanged, 0.0f))) {
                ++sentPackets_;
            }
        }
    }

    void ReleaseAllButtons() {
        buttonState_.fill(0);
        for (std::optional<SDL_FingerID>& buttonFinger : touchButtonFingers_) {
            buttonFinger.reset();
        }
        touchScrollFinger_.reset();
        touchScrollStep_ = 0.0f;
        if (transport_.IsOpen()) {
            const std::array<int, 5> released{ 0, 0, 0, 0, 0 };
            if (transport_.Send(FormatPacket(0.0f, 0.0f, released, 0.0f))) {
                ++sentPackets_;
            }
            lastSentButtons_ = released;
        }
    }

    void DrawText(float x, float y, const std::string& text, SDL_Color color) {
        SDL_SetRenderDrawColor(renderer_, color.r, color.g, color.b, color.a);
        SDL_RenderDebugText(renderer_, x, y, text.c_str());
    }

    float TextWidth(const std::string& text) const {
        return static_cast<float>(text.size()) * kDebugGlyphWidth;
    }

    void DrawTextCentered(float centerX, float y, const std::string& text, SDL_Color color) {
        DrawText(centerX - (TextWidth(text) * 0.5f), y, text, color);
    }

    void DrawButton(const Button& button) {
        const bool active = (button.mouseButtonIndex >= 0 &&
            button.mouseButtonIndex < static_cast<int>(buttonState_.size()) &&
            buttonState_[static_cast<size_t>(button.mouseButtonIndex)] != 0) ||
            (button.scrollStep != 0.0f && touchScrollStep_ == button.scrollStep) ||
            (button.action != UiAction::None && touchAction_ == button.action);

        if (active) {
            SDL_SetRenderDrawColor(renderer_, 58, 92, 112, 255);
        } else {
            SDL_SetRenderDrawColor(renderer_, 32, 43, 58, 255);
        }
        SDL_RenderFillRect(renderer_, &button.rect);

        const float holdProgress = HoldProgress(button);
        if (holdProgress > 0.0f) {
            SDL_FRect fill = button.rect;
            fill.w = button.rect.w * holdProgress;
            SDL_SetRenderDrawColor(renderer_, 80, 140, 165, 255);
            SDL_RenderFillRect(renderer_, &fill);
        }

        SDL_SetRenderDrawColor(renderer_, active ? 147 : 105, active ? 221 : 183, active ? 232 : 204, 255);
        SDL_RenderRect(renderer_, &button.rect);

        const float textX = button.rect.x + ((button.rect.w - TextWidth(button.label)) * 0.5f);
        const float textY = button.rect.y + ((button.rect.h - kDebugGlyphHeight) * 0.5f);
        DrawText(textX, textY, button.label, SDL_Color{ 232, 246, 250, 255 });
    }

    float IpScreenTop(float uiHeight) const {
        return (uiHeight - (kTouchLayout ? 236.0f : 216.0f)) * 0.5f;
    }

    void RenderIpScreen(float uiWidth, float uiHeight) {
        const float centerX = uiWidth * 0.5f;
        const float top = IpScreenTop(uiHeight);
        const float inputWidth = std::min(380.0f, std::max(300.0f, uiWidth - 64.0f));

        DrawTextCentered(centerX, top, "Bandit Mouse Relay", SDL_Color{ 246, 249, 252, 255 });
        DrawTextCentered(centerX, top + 32.0f, "Enter the Xbox Developer Mode IP address", SDL_Color{ 174, 187, 202, 255 });

        const SDL_FRect box{ centerX - (inputWidth * 0.5f), top + 78.0f, inputWidth, 34.0f };
        SDL_SetRenderDrawColor(renderer_, 16, 22, 31, 255);
        SDL_RenderFillRect(renderer_, &box);
        SDL_SetRenderDrawColor(renderer_, 105, 183, 204, 255);
        SDL_RenderRect(renderer_, &box);

        const bool showCaret = (SDL_GetTicks() / 500) % 2 == 0;
        DrawText(box.x + 12.0f, box.y + 13.0f, ipBuffer_ + (showCaret ? "_" : ""), SDL_Color{ 222, 245, 250, 255 });

        if (!ipError_.empty()) {
            const SDL_Color color = connecting_ ? SDL_Color{ 252, 213, 128, 255 } : SDL_Color{ 255, 118, 118, 255 };
            DrawTextCentered(centerX, box.y - 24.0f, ipError_, color);
        }

        const std::string hint = connecting_
            ? "Waiting for MODE/SYNC reply on UDP 7332. Esc cancels."
            : (kTouchLayout ? "Use the keypad below, then tap Connect" : "Keyboard: Enter confirm, Backspace edit, Esc cancel");
        DrawTextCentered(centerX, box.y + 50.0f, hint, SDL_Color{ 145, 158, 174, 255 });

        for (const Button& button : BuildButtons(uiWidth, uiHeight)) {
            DrawButton(button);
        }
    }

    void RenderRelayScreen(float uiWidth, float uiHeight) {
        const char* network = connected_ ? "CONNECTED" : "WAITING";
        const std::string appMode = appMode_ ? ModeName(*appMode_) : "UNKNOWN";

        DrawText(18.0f, 18.0f, "Bandit Mouse Relay", SDL_Color{ 246, 249, 252, 255 });
        DrawText(18.0f, 42.0f, "Xbox: " + host_, SDL_Color{ 218, 229, 241, 255 });
        DrawText(18.0f, 62.0f, "Mouse: " + ModeName(mode_) + "  App: " + appMode + "  UDP: " + network, SDL_Color{ 218, 229, 241, 255 });
        DrawText(18.0f, 82.0f, "Packets: sent=" + std::to_string(sentPackets_.load()) + " status=" + std::to_string(statusPackets_), SDL_Color{ 174, 187, 202, 255 });
        DrawText(18.0f, 102.0f, "Window: " + std::to_string(windowWidth_) + "x" + std::to_string(windowHeight_) + " -> menu " + std::to_string((int)MenuTargetWidth()) + "x" + std::to_string((int)MenuTargetHeight()) + " raw " + std::to_string((int)targetWidth_) + "x" + std::to_string((int)targetHeight_), SDL_Color{ 174, 187, 202, 255 });
        DrawText(18.0f, 122.0f, kTouchLayout ? "Touch: drag empty space, hold L/R with another finger, wheel pads scroll" : "Keys: Esc menu, F3 change IP, F8 quit, F9 toggle local mode, F10 packet log", SDL_Color{ 145, 158, 174, 255 });
        DrawText(18.0f, 142.0f, "Last: " + lastStatus_, SDL_Color{ 145, 158, 174, 255 });

        int diagPixelW = 0;
        int diagPixelH = 0;
        SDL_GetWindowSizeInPixels(window_, &diagPixelW, &diagPixelH);
        const bool diagRelative = SDL_GetWindowRelativeMouseMode(window_);
        const bool diagGrab = SDL_GetWindowMouseGrab(window_);
        const bool diagFocus = (SDL_GetWindowFlags(window_) & SDL_WINDOW_INPUT_FOCUS) != 0;
        char diagLine[240];
        std::snprintf(diagLine, sizeof(diagLine),
            "Capture: rel=%s grab=%s focus=%s  pixels=%dx%d  dpr=%.2f  safe=%dx%d+%d+%d  ui=%.0fx%.0f @%.2f",
            diagRelative ? "on" : "off", diagGrab ? "on" : "off", diagFocus ? "yes" : "no",
            diagPixelW, diagPixelH, SDL_GetWindowDisplayScale(window_),
            safeArea_.w, safeArea_.h, safeArea_.x, safeArea_.y,
            UiAreaWidth(), UiAreaHeight(), UiScale());
        DrawText(18.0f, 162.0f, diagLine, SDL_Color{ 150, 205, 165, 255 });

        char sendLine[120];
        std::snprintf(sendLine, sizeof(sendLine), "Send dt(ms): min=%.1f max=%.1f (even ~4.2 = no render starvation)", sendDtMinMs_, sendDtMaxMs_);
        DrawText(18.0f, 182.0f, sendLine, SDL_Color{ 205, 195, 150, 255 });
        sendDtMinMs_ = 1000.0;
        sendDtMaxMs_ = 0.0;

        char logLine[256];
        if (g_packetLog.Enabled()) {
            std::snprintf(logLine, sizeof(logLine), "Packet log: ON -> %s", g_packetLog.Path().c_str());
        } else {
            std::snprintf(logLine, sizeof(logLine), "Packet log: off (press F10 to record sent/received packets)");
        }
        DrawText(18.0f, 202.0f, logLine, g_packetLog.Enabled() ? SDL_Color{ 235, 180, 90, 255 } : SDL_Color{ 145, 158, 174, 255 });

        if (menuOpen_) {
            const float panelHeight = kTouchLayout ? 360.0f : 300.0f;
            const SDL_FRect panel{ (uiWidth - 220.0f) * 0.5f, (uiHeight - panelHeight) * 0.5f, 220.0f, panelHeight };
            SDL_SetRenderDrawColor(renderer_, 13, 18, 26, 235);
            SDL_RenderFillRect(renderer_, &panel);
            SDL_SetRenderDrawColor(renderer_, 105, 183, 204, 255);
            SDL_RenderRect(renderer_, &panel);
            DrawTextCentered(uiWidth * 0.5f, panel.y + 24.0f, "Relay Menu", SDL_Color{ 246, 249, 252, 255 });
        }

        for (const Button& button : BuildButtons(uiWidth, uiHeight)) {
            DrawButton(button);
        }

        if (mode_ == RelayMode::Menu) {
            const float x = Clamp((virtualX_ / MenuTargetWidth()) * uiWidth, 0.0f, uiWidth - 1.0f);
            const float y = Clamp((virtualY_ / MenuTargetHeight()) * uiHeight, 0.0f, uiHeight - 1.0f);
            SDL_SetRenderDrawColor(renderer_, 245, 247, 250, 255);
            SDL_RenderLine(renderer_, x - 9.0f, y, x + 9.0f, y);
            SDL_RenderLine(renderer_, x, y - 9.0f, x, y + 9.0f);
            SDL_SetRenderDrawColor(renderer_, 9, 12, 18, 255);
            SDL_RenderPoint(renderer_, x, y);
        }
    }

    SDL_Window* window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    UdpTransport transport_;

    bool running_ = true;
    bool enteringIp_ = true;
    bool menuOpen_ = false;
    bool connecting_ = false;
    bool hostSet_ = false;
    bool connected_ = false;

    int windowWidth_ = 1280;
    int windowHeight_ = 720;
    SDL_Rect safeArea_{ 0, 0, 1280, 720 };
    float pixelDensity_ = 1.0f;
    float scaleX_ = 1.0f;
    float scaleY_ = 1.0f;
    float targetWidth_ = kTargetWidth;
    float targetHeight_ = kTargetHeight;
    float menuTargetWidth_ = kTargetWidth * kMenuCoordinateScale;
    float menuTargetHeight_ = kTargetHeight * kMenuCoordinateScale;
    bool haveMenuTargetSize_ = false;

    std::string ipBuffer_;
    std::string ipError_;
    std::string host_;
    std::string pendingHost_;
    std::string lastStatus_ = "<none>";

    RelayMode mode_ = RelayMode::Menu;
    std::optional<RelayMode> appMode_;
    std::array<int, 5> buttonState_{ 0, 0, 0, 0, 0 };
    std::array<int, 5> lastSentButtons_{ -1, -1, -1, -1, -1 };
    std::array<std::optional<SDL_FingerID>, 5> touchButtonFingers_{};
    std::optional<SDL_FingerID> touchActionFinger_;
    std::optional<SDL_FingerID> touchScrollFinger_;
    UiAction touchAction_ = UiAction::None;
    UiAction holdAction_ = UiAction::None;
    double holdStart_ = 0.0;

    float virtualX_ = kTargetWidth * kMenuCoordinateScale * 0.5f;
    float virtualY_ = kTargetHeight * kMenuCoordinateScale * 0.5f;
    float accumDx_ = 0.0f;
    float accumDy_ = 0.0f;
    float pendingScroll_ = 0.0f;
    float touchScrollStep_ = 0.0f;
    bool motionPending_ = false;
    double connectStart_ = 0.0;
    double lastConnectProbe_ = 0.0;
    double lastMotionSend_ = 0.0;
    double lastHeldButtonRefresh_ = 0.0;
    double lastTouchScrollRepeat_ = 0.0;
    double lastPing_ = 0.0;
    double lastSendStamp_ = 0.0;
    double sendDtMinMs_ = 1000.0;
    double sendDtMaxMs_ = 0.0;
    std::atomic<uint64_t> sentPackets_{ 0 };
    uint64_t statusPackets_ = 0;

    std::mutex sendMutex_;
    std::thread netThread_;
    std::atomic<bool> netRunning_{ false };
};

} // namespace

#ifdef _WIN32
static void RaiseRelaySchedulingPriority() {
    SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
    PROCESS_POWER_THROTTLING_STATE throttling{};
    throttling.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
    throttling.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
    throttling.StateMask = 0;
    SetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling, &throttling, sizeof(throttling));
}
#endif

int main(int, char**) {
    WinsockRuntime winsock;
    if (!winsock.ok) {
        SDL_Log("Winsock initialization failed");
        return 2;
    }

    TimerResolution timerResolution;

    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
    if (kTouchLayout) {
        // android gets this from the activity instead
        SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
    }

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return 2;
    }

    if (kTouchLayout) {
        // relay takes no touches mid-game so the idle timer would lock the phone
        SDL_DisableScreenSaver();
    }

    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    if (!SDL_CreateWindowAndRenderer(
            "Bandit Mouse Relay",
            960,
            540,
            SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY,
            &window,
            &renderer)) {
        SDL_Log("SDL_CreateWindowAndRenderer failed: %s", SDL_GetError());
        SDL_Quit();
        return 2;
    }

    SDL_SetRenderVSync(renderer, 0);

#ifdef _WIN32
    RaiseRelaySchedulingPriority();
#endif

    {
        RelayApp app(window, renderer);
        double lastRender = -kRenderIntervalSeconds;
        double nextTick = NowSeconds();
        while (app.Running()) {
            app.PumpAndTick();
            const double now = NowSeconds();
            if (now - lastRender >= kRenderIntervalSeconds) {
                app.Render();
                lastRender = now;
            }

            nextTick += kSendIntervalSeconds;
            if (nextTick < now) {
                nextTick = now;
            }
            SleepUntil(nextTick);
        }
    }

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
