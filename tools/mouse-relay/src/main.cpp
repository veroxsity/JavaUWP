#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
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
constexpr double kRenderIntervalSeconds = 1.0 / 60.0;
constexpr float kMenuCoordinateScale = 0.5f;
constexpr float kDebugGlyphWidth = 8.0f;
constexpr float kDebugGlyphHeight = 8.0f;

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
    Quit,
};

struct Button {
    SDL_FRect rect{};
    std::string label;
    UiAction action = UiAction::None;
};

static float Clamp(float value, float low, float high) {
    return std::max(low, std::min(value, high));
}

static double NowSeconds() {
    using Clock = std::chrono::steady_clock;
    static const Clock::time_point start = Clock::now();
    return std::chrono::duration<double>(Clock::now() - start).count();
}

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
        return sent == static_cast<int>(packet.size());
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
        SDL_StartTextInput(window_);
    }

    ~RelayApp() {
        ReleaseAllButtons();
        SDL_StopTextInput(window_);
        SDL_SetWindowRelativeMouseMode(window_, false);
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
        case SDL_EVENT_TEXT_INPUT:
            if (enteringIp_) {
                AppendIpText(event.text.text ? event.text.text : "");
            }
            return;
        case SDL_EVENT_KEY_DOWN:
            HandleKey(event.key.key);
            return;
        case SDL_EVENT_MOUSE_MOTION:
            if (!enteringIp_) {
                AddMotion(event.motion.xrel, event.motion.yrel);
            }
            return;
        case SDL_EVENT_MOUSE_WHEEL:
            if (!enteringIp_) {
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
            if (!enteringIp_) {
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
        SendPendingPackets();
    }

    void Render() {
        const float scale = UiScale();
        const float uiWidth = static_cast<float>(windowWidth_) / scale;
        const float uiHeight = static_cast<float>(windowHeight_) / scale;

        SDL_SetRenderScale(renderer_, scale, scale);
        SDL_SetRenderDrawColor(renderer_, 9, 12, 18, 255);
        SDL_RenderClear(renderer_);

        if (enteringIp_) {
            RenderIpScreen(uiWidth, uiHeight);
        } else {
            RenderRelayScreen(uiWidth, uiHeight);
        }

        SDL_RenderPresent(renderer_);
    }

private:
    float UiScale() const {
        return Clamp(static_cast<float>(windowHeight_) / 720.0f, 1.0f, 3.0f);
    }

    std::vector<Button> BuildButtons(float uiWidth, float uiHeight) const {
        std::vector<Button> buttons;

        if (enteringIp_) {
            const float width = 156.0f;
            const float height = 34.0f;
            const float gap = 16.0f;
            const float total = (width * 2.0f) + gap;
            const float x = (uiWidth - total) * 0.5f;
            const float y = IpScreenTop(uiHeight) + 168.0f;
            buttons.push_back({
                SDL_FRect{ x, y, width, height },
                connecting_ ? "Checking" : "Connect",
                connecting_ ? UiAction::None : UiAction::Connect
            });
            buttons.push_back({ SDL_FRect{ x + width + gap, y, width, height }, hostSet_ || connecting_ ? "Cancel" : "Quit", UiAction::CancelIp });
            return buttons;
        }

        const float buttonWidth = 132.0f;
        const float buttonHeight = 30.0f;
        const float gap = 10.0f;
        const float total = (buttonWidth * 4.0f) + (gap * 3.0f);
        float x = (uiWidth - total) * 0.5f;
        const float y = uiHeight - buttonHeight - 18.0f;

        buttons.push_back({ SDL_FRect{ x, y, buttonWidth, buttonHeight }, "Change IP", UiAction::ChangeIp });
        x += buttonWidth + gap;
        buttons.push_back({ SDL_FRect{ x, y, buttonWidth, buttonHeight }, "Toggle", UiAction::ToggleMode });
        x += buttonWidth + gap;
        buttons.push_back({ SDL_FRect{ x, y, buttonWidth, buttonHeight }, "Release", UiAction::ReleaseButtons });
        x += buttonWidth + gap;
        buttons.push_back({ SDL_FRect{ x, y, buttonWidth, buttonHeight }, "Quit", UiAction::Quit });
        return buttons;
    }

    UiAction HitActionAtUi(float uiX, float uiY) const {
        const float scale = UiScale();
        const float uiWidth = static_cast<float>(windowWidth_) / scale;
        const float uiHeight = static_cast<float>(windowHeight_) / scale;

        for (const Button& button : BuildButtons(uiWidth, uiHeight)) {
            if (uiX >= button.rect.x && uiX <= button.rect.x + button.rect.w &&
                uiY >= button.rect.y && uiY <= button.rect.y + button.rect.h) {
                return button.action;
            }
        }

        return UiAction::None;
    }

    UiAction HitAction(float windowX, float windowY) const {
        const float scale = UiScale();
        return HitActionAtUi(windowX / scale, windowY / scale);
    }

    UiAction HitActionAtRelayCursor() const {
        if (enteringIp_ || mode_ != RelayMode::Menu) {
            return UiAction::None;
        }

        const float scale = UiScale();
        const float uiWidth = static_cast<float>(windowWidth_) / scale;
        const float uiHeight = static_cast<float>(windowHeight_) / scale;
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
                SDL_SetWindowRelativeMouseMode(window_, true);
            } else {
                running_ = false;
            }
            break;
        case UiAction::ChangeIp:
            ReleaseAllButtons();
            enteringIp_ = true;
            ipBuffer_ = host_;
            ipError_.clear();
            SDL_SetWindowRelativeMouseMode(window_, false);
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
        case UiAction::Quit:
            running_ = false;
            break;
        case UiAction::None:
            break;
        }
    }

    void HandleKey(SDL_Keycode key) {
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

        if (key == SDLK_F3) {
            ExecuteAction(UiAction::ChangeIp);
        } else if (key == SDLK_F8 || key == SDLK_ESCAPE) {
            ExecuteAction(UiAction::Quit);
        } else if (key == SDLK_F9) {
            ExecuteAction(UiAction::ToggleMode);
        }
    }

    void HandleMouseButton(const SDL_Event& event) {
        UiAction action = UiAction::None;
        if (enteringIp_ || mode_ == RelayMode::Menu) {
            action = HitAction(event.button.x, event.button.y);
            if (action == UiAction::None) {
                action = HitActionAtRelayCursor();
            }
        }

        if (event.type == SDL_EVENT_MOUSE_BUTTON_UP && action != UiAction::None) {
            ExecuteAction(action);
            return;
        }
        if (action != UiAction::None || enteringIp_) {
            return;
        }

        const int index = ButtonIndex(event.button.button);
        if (index >= 0) {
            buttonState_[static_cast<size_t>(index)] = event.type == SDL_EVENT_MOUSE_BUTTON_DOWN ? 1 : 0;
        }
    }

    void HandleFingerButton(const SDL_Event& event) {
        const float x = event.tfinger.x * static_cast<float>(windowWidth_);
        const float y = event.tfinger.y * static_cast<float>(windowHeight_);
        const UiAction action = HitAction(x, y);
        if (event.type == SDL_EVENT_FINGER_UP && action != UiAction::None) {
            ExecuteAction(action);
        }
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
        RefreshMotionScale();
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
            }
            SyncMenuPos(syncX, syncY, true);
            return;
        }

        if (mode_ == RelayMode::Menu) {
            if (windowCursor) {
                SyncMenuPos(windowCursor->first, windowCursor->second, true);
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
        connecting_ = false;
        enteringIp_ = false;
        ipError_.clear();
        SDL_StopTextInput(window_);
        SDL_SetWindowRelativeMouseMode(window_, true);
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
            x,
            y,
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
        SDL_SetRenderDrawColor(renderer_, 32, 43, 58, 255);
        SDL_RenderFillRect(renderer_, &button.rect);
        SDL_SetRenderDrawColor(renderer_, 105, 183, 204, 255);
        SDL_RenderRect(renderer_, &button.rect);

        const float textX = button.rect.x + ((button.rect.w - TextWidth(button.label)) * 0.5f);
        const float textY = button.rect.y + ((button.rect.h - kDebugGlyphHeight) * 0.5f);
        DrawText(textX, textY, button.label, SDL_Color{ 232, 246, 250, 255 });
    }

    float IpScreenTop(float uiHeight) const {
        return (uiHeight - 216.0f) * 0.5f;
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
            : "Keyboard: Enter confirm, Backspace edit, Esc cancel";
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
        DrawText(18.0f, 82.0f, "Packets: sent=" + std::to_string(sentPackets_) + " status=" + std::to_string(statusPackets_), SDL_Color{ 174, 187, 202, 255 });
        DrawText(18.0f, 102.0f, "Window: " + std::to_string(windowWidth_) + "x" + std::to_string(windowHeight_) + " -> menu " + std::to_string((int)MenuTargetWidth()) + "x" + std::to_string((int)MenuTargetHeight()) + " raw " + std::to_string((int)targetWidth_) + "x" + std::to_string((int)targetHeight_), SDL_Color{ 174, 187, 202, 255 });
        DrawText(18.0f, 122.0f, "Keys: F3 change IP, F8 quit, F9 toggle local mode", SDL_Color{ 145, 158, 174, 255 });
        DrawText(18.0f, 142.0f, "Last: " + lastStatus_, SDL_Color{ 145, 158, 174, 255 });

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
    bool connecting_ = false;
    bool hostSet_ = false;
    bool connected_ = false;

    int windowWidth_ = 1280;
    int windowHeight_ = 720;
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

    float virtualX_ = kTargetWidth * kMenuCoordinateScale * 0.5f;
    float virtualY_ = kTargetHeight * kMenuCoordinateScale * 0.5f;
    float accumDx_ = 0.0f;
    float accumDy_ = 0.0f;
    float pendingScroll_ = 0.0f;
    bool motionPending_ = false;
    double connectStart_ = 0.0;
    double lastConnectProbe_ = 0.0;
    double lastMotionSend_ = 0.0;
    double lastHeldButtonRefresh_ = 0.0;
    double lastPing_ = 0.0;
    uint64_t sentPackets_ = 0;
    uint64_t statusPackets_ = 0;
};

} // namespace

int main(int, char**) {
    WinsockRuntime winsock;
    if (!winsock.ok) {
        SDL_Log("Winsock initialization failed");
        return 2;
    }

    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return 2;
    }

    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    if (!SDL_CreateWindowAndRenderer(
            "Bandit Mouse Relay",
            960,
            540,
            SDL_WINDOW_RESIZABLE,
            &window,
            &renderer)) {
        SDL_Log("SDL_CreateWindowAndRenderer failed: %s", SDL_GetError());
        SDL_Quit();
        return 2;
    }

    SDL_SetRenderVSync(renderer, 0);

    {
        RelayApp app(window, renderer);
        double lastRender = -kRenderIntervalSeconds;
        while (app.Running()) {
            SDL_Event event{};
            while (SDL_PollEvent(&event)) {
                app.HandleEvent(event);
            }

            app.Tick();
            const double now = NowSeconds();
            if (now - lastRender >= kRenderIntervalSeconds) {
                app.Render();
                lastRender = now;
            }
            SDL_Delay(1);
        }
    }

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
