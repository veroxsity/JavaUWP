#define WIN32_LEAN_AND_MEAN
#define MOUSE_SUPPORT_EXPORTS
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "mouse_support_core.h"
#include "mouse_support_api.h"

#pragma comment(lib, "ws2_32.lib")

namespace {

const unsigned short kInputPort = 7331;
const unsigned short kStatusPort = 7332;
const unsigned short kOverlayPort = 7333;
const double kProtocolWidth = 1920.0;
const double kProtocolHeight = 1080.0;
const int kCursorModeNormal = 0x00034001;
const int kCursorModeDisabled = 0x00034003;
const DWORD kModeStabilizeMs = 150;
const double kDefaultFreshnessMs = 50.0;

SRWLOCK g_lock = SRWLOCK_INIT;
mousesupport::MouseMailbox g_mailbox;

MouseSupportHostState g_host = { 1920, 1080, 960, 540, kCursorModeDisabled, 960.0, 540.0 };

SOCKET g_statusSocket = INVALID_SOCKET;
SOCKET g_overlaySocket = INVALID_SOCKET;
sockaddr_in g_statusAddr = {};
bool g_haveStatusAddr = false;

volatile LONG g_started = 0;
volatile LONG g_shutdown = 0;
volatile LONG g_lastActivityTick = 0;
HANDLE g_receiveThread = nullptr;

int g_pendingMode = -1;
int g_reportedMode = kCursorModeDisabled;
ULONGLONG g_modeChangeTime = 0;

long long g_qpcStart = 0;
double g_qpcTicksPerMicro = 1.0;

bool g_diag = false;
long long g_freshnessMicros = (long long)(kDefaultFreshnessMs * 1000.0);
double g_clampMax = 0.0;
double g_smoothAlpha = 0.0;
double g_stallMs = 200.0;
long long g_lastConsumeMicros = 0;
FILE* g_diagFile = nullptr;
bool g_diagFileTried = false;

long long NowMicros() {
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    return (long long)((double)(c.QuadPart - g_qpcStart) / g_qpcTicksPerMicro);
}

void DiagOpenIfNeeded() {
    if (g_diagFile || g_diagFileTried) return;
    g_diagFileTried = true;
    char dir[512] = {};
    const DWORD n = GetEnvironmentVariableA("MC_LOG_DIR", dir, sizeof(dir));
    if (n > 0 && n < sizeof(dir)) {
        char path[640] = {};
        sprintf_s(path, "%s\\mouse_support_diag.log", dir);
        fopen_s(&g_diagFile, path, "a");
    }
}

void DiagLog(const char* fmt, ...) {
    if (!g_diag) return;
    char line[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(line, sizeof(line), fmt, args);
    va_end(args);
    DiagOpenIfNeeded();
    if (g_diagFile) {
        fprintf(g_diagFile, "%s\n", line);
        fflush(g_diagFile);
    } else {
        OutputDebugStringA("[ms-diag] ");
        OutputDebugStringA(line);
        OutputDebugStringA("\n");
    }
}

void Log(const char* fmt, ...) {
    char line[320];
    va_list args;
    va_start(args, fmt);
    vsnprintf(line, sizeof(line), fmt, args);
    va_end(args);
    OutputDebugStringA("[mouse_support] ");
    OutputDebugStringA(line);
    OutputDebugStringA("\n");
}

double WindowToProtocolX(double x, int windowWidth) {
    return windowWidth > 0 ? x * (kProtocolWidth / (double)windowWidth) : x;
}

double WindowToProtocolY(double y, int windowHeight) {
    return windowHeight > 0 ? y * (kProtocolHeight / (double)windowHeight) : y;
}

int StableModeLocked() {
    if (g_pendingMode >= 0) {
        const ULONGLONG elapsed = GetTickCount64() - g_modeChangeTime;
        if (elapsed < kModeStabilizeMs) {
            return g_reportedMode;
        }
        g_reportedMode = g_pendingMode;
        g_pendingMode = -1;
    }
    return g_reportedMode;
}

void EnsureStatusSocket() {
    if (g_statusSocket == INVALID_SOCKET) {
        g_statusSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    }
}

void SendStatusText(const char* text) {
    sockaddr_in target = {};
    bool haveTarget = false;
    AcquireSRWLockShared(&g_lock);
    if (g_haveStatusAddr) {
        target = g_statusAddr;
        haveTarget = true;
    }
    ReleaseSRWLockShared(&g_lock);
    if (!haveTarget) return;

    EnsureStatusSocket();
    if (g_statusSocket == INVALID_SOCKET) return;
    sendto(g_statusSocket, text, (int)strlen(text), 0,
        reinterpret_cast<const sockaddr*>(&target), sizeof(target));
}

void RememberStatusAddress(const sockaddr_in& from) {
    sockaddr_in statusTo = from;
    statusTo.sin_port = htons(kStatusPort);
    AcquireSRWLockExclusive(&g_lock);
    g_statusAddr = statusTo;
    g_haveStatusAddr = true;
    ReleaseSRWLockExclusive(&g_lock);
}

DWORD WINAPI ReceiveThreadProc(LPVOID) {
    Log("receive thread starting on UDP %u", kInputPort);

    SOCKET sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET) {
        Log("socket failed err=%d", WSAGetLastError());
        return 0;
    }

    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(kInputPort);
    addr.sin_addr.s_addr = INADDR_ANY;
    if (bind(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
        Log("bind UDP %u failed err=%d", kInputPort, WSAGetLastError());
        closesocket(sock);
        return 0;
    }

    int rcvBuf = 256 * 1024;
    setsockopt(sock, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char*>(&rcvBuf), sizeof(rcvBuf));
    DWORD rcvTimeoutMs = 50;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&rcvTimeoutMs), sizeof(rcvTimeoutMs));

    Log("listening on UDP %u (freshness %lldms)", kInputPort, g_freshnessMicros / 1000);

    char buf[64];
    unsigned int packetCount = 0;
    int lastSentStatusMode = -1;
    long long lastRecvLogMicros = NowMicros();
    long long recvSinceLog = 0;

    while (InterlockedCompareExchange(&g_shutdown, 0, 0) == 0) {
        sockaddr_in from = {};
        int fromLen = sizeof(from);
        const int len = recvfrom(sock, buf, sizeof(buf) - 1, 0,
            reinterpret_cast<sockaddr*>(&from), &fromLen);

        int windowWidth, windowHeight, menuWidth, menuHeight, stableMode;
        double menuCursorX, menuCursorY;
        AcquireSRWLockExclusive(&g_lock);
        windowWidth = g_host.windowWidth;
        windowHeight = g_host.windowHeight;
        menuWidth = g_host.menuWidth;
        menuHeight = g_host.menuHeight;
        menuCursorX = g_host.menuCursorX;
        menuCursorY = g_host.menuCursorY;
        stableMode = StableModeLocked();
        ReleaseSRWLockExclusive(&g_lock);

        if (len <= 0) {
            const char* status = (stableMode == kCursorModeDisabled) ? "MODE:GAMEPLAY" : "MODE:MENU";
            if (stableMode != lastSentStatusMode) {
                lastSentStatusMode = stableMode;
                char packet[160] = {};
                sprintf_s(packet, "%s cursorw=%.0f,%.0f size=%dx%d menu=%dx%d",
                    status, menuCursorX, menuCursorY,
                    windowWidth, windowHeight, menuWidth, menuHeight);
                SendStatusText(packet);
            }
            continue;
        }

        buf[len] = 0;
        RememberStatusAddress(from);
        InterlockedExchange(&g_lastActivityTick, (LONG)GetTickCount());

        if (strcmp(buf, "hello") == 0 || strcmp(buf, "ping") == 0) {
            char ack[160] = {};
            sprintf_s(ack, "javauwp_glfw_mouse:ready mode=%d cursor=%.0f,%.0f cursorw=%.0f,%.0f size=%dx%d menu=%dx%d",
                stableMode,
                WindowToProtocolX(menuCursorX, windowWidth), WindowToProtocolY(menuCursorY, windowHeight),
                menuCursorX, menuCursorY,
                windowWidth, windowHeight, menuWidth, menuHeight);
            sendto(sock, ack, (int)strlen(ack), 0, reinterpret_cast<sockaddr*>(&from), fromLen);
            const char* status = (stableMode == kCursorModeDisabled) ? "MODE:GAMEPLAY" : "MODE:MENU";
            lastSentStatusMode = stableMode;
            char packet[160] = {};
            sprintf_s(packet, "%s cursorw=%.0f,%.0f size=%dx%d menu=%dx%d",
                status, menuCursorX, menuCursorY, windowWidth, windowHeight, menuWidth, menuHeight);
            SendStatusText(packet);
            Log("handshake replied");
            continue;
        }

        float dx = 0.0f, dy = 0.0f, wheelY = 0.0f;
        int lb = -1, rb = -1, mb = -1, x1 = -1, x2 = -1;
        bool absolutePacket = false;
        bool absoluteWindowPacket = false;
        int fields = 0;
        if (strncmp(buf, "ABSW:", 5) == 0) {
            absolutePacket = true;
            absoluteWindowPacket = true;
            fields = sscanf_s(buf + 5, "%f,%f,%d,%d,%d,%f,%d,%d", &dx, &dy, &lb, &rb, &mb, &wheelY, &x1, &x2);
        } else if (strncmp(buf, "ABS:", 4) == 0) {
            absolutePacket = true;
            fields = sscanf_s(buf + 4, "%f,%f,%d,%d,%d,%f,%d,%d", &dx, &dy, &lb, &rb, &mb, &wheelY, &x1, &x2);
        } else {
            fields = sscanf_s(buf, "%f,%f,%d,%d,%d,%f,%d,%d", &dx, &dy, &lb, &rb, &mb, &wheelY, &x1, &x2);
        }
        if (fields != 8) {
            const char bad[] = "javauwp_glfw_mouse:bad_packet";
            sendto(sock, bad, (int)sizeof(bad) - 1, 0, reinterpret_cast<sockaddr*>(&from), fromLen);
            continue;
        }

        const long long tMicros = NowMicros();
        AcquireSRWLockExclusive(&g_lock);
        if (absolutePacket) {
            g_mailbox.submitAbsolute(tMicros, (double)dx, (double)dy, absoluteWindowPacket, (double)wheelY);
        } else {
            g_mailbox.submitRelative(tMicros, (double)dx, (double)dy, (double)wheelY);
        }
        g_mailbox.submitButtonValue(1, lb);
        g_mailbox.submitButtonValue(2, rb);
        g_mailbox.submitButtonValue(4, mb);
        g_mailbox.submitButtonValue(8, x1);
        g_mailbox.submitButtonValue(16, x2);
        const long long recvTotal = g_mailbox.receivedCount();
        const long long overwrites = g_mailbox.overwriteCount();
        const int depth = g_mailbox.depth();
        ReleaseSRWLockExclusive(&g_lock);

        ++recvSinceLog;
        if (g_diag) {
            const long long sinceLog = tMicros - lastRecvLogMicros;
            if (sinceLog >= 1000000) {
                const double pps = (double)recvSinceLog * 1000000.0 / (double)sinceLog;
                DiagLog("recv pps=%.0f total=%lld depth=%d overwrites=%lld", pps, recvTotal, depth, overwrites);
                lastRecvLogMicros = tMicros;
                recvSinceLog = 0;
            }
        }

        const char* status = (stableMode == kCursorModeDisabled) ? "MODE:GAMEPLAY" : "MODE:MENU";
        if (stableMode != lastSentStatusMode) {
            lastSentStatusMode = stableMode;
            char packet[160] = {};
            sprintf_s(packet, "%s cursorw=%.0f,%.0f size=%dx%d menu=%dx%d",
                status, menuCursorX, menuCursorY, windowWidth, windowHeight, menuWidth, menuHeight);
            SendStatusText(packet);
        }

        ++packetCount;
        if (packetCount == 1 || (packetCount % 120) == 0) {
            char ack[160] = {};
            sprintf_s(ack, "javauwp_glfw_mouse:receiving mode=%d cursor=%.0f,%.0f cursorw=%.0f,%.0f size=%dx%d menu=%dx%d",
                stableMode,
                WindowToProtocolX(menuCursorX, windowWidth), WindowToProtocolY(menuCursorY, windowHeight),
                menuCursorX, menuCursorY,
                windowWidth, windowHeight, menuWidth, menuHeight);
            sendto(sock, ack, (int)strlen(ack), 0, reinterpret_cast<sockaddr*>(&from), fromLen);
        }
    }

    closesocket(sock);
    Log("receive thread stopped");
    return 0;
}

const char* GapBucket(double gapMs) {
    if (gapMs > 500.0) return ">500ms";
    if (gapMs > 100.0) return ">100ms";
    if (gapMs > 50.0) return ">50ms";
    if (gapMs > 33.0) return ">33ms";
    if (gapMs > 16.0) return ">16ms";
    return "<=16ms";
}

double ReadEnvDouble(const char* name, double fallback, double lo, double hi) {
    char buf[32] = {};
    const DWORD n = GetEnvironmentVariableA(name, buf, sizeof(buf));
    if (n == 0 || n >= sizeof(buf)) return fallback;
    const double value = atof(buf);
    if (value < lo) return lo;
    if (value > hi) return hi;
    return value;
}

}

extern "C" {

MOUSE_SUPPORT_API void MouseSupport_Init(void) {
    if (InterlockedExchange(&g_started, 1) != 0) return;

    WSADATA wsa = {};
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        Log("WSAStartup failed");
        InterlockedExchange(&g_started, 0);
        return;
    }

    LARGE_INTEGER freq = {};
    QueryPerformanceFrequency(&freq);
    g_qpcTicksPerMicro = freq.QuadPart > 0 ? (double)freq.QuadPart / 1000000.0 : 1.0;
    LARGE_INTEGER start = {};
    QueryPerformanceCounter(&start);
    g_qpcStart = start.QuadPart;

    const double freshnessMs = ReadEnvDouble("BANDIT_MOUSE_FRESHNESS_MS", kDefaultFreshnessMs, 0.0, 1000.0);
    g_freshnessMicros = (long long)(freshnessMs * 1000.0);
    g_clampMax = ReadEnvDouble("BANDIT_MOUSE_CLAMP", 0.0, 0.0, 100000.0);
    g_smoothAlpha = ReadEnvDouble("BANDIT_MOUSE_SMOOTH_ALPHA", 0.0, 0.0, 1.0);
    g_stallMs = ReadEnvDouble("BANDIT_MOUSE_STALL_MS", 200.0, 50.0, 5000.0);
    char diagBuf[16] = {};
    const DWORD dn = GetEnvironmentVariableA("BANDIT_MOUSE_DIAG", diagBuf, sizeof(diagBuf));
    g_diag = (dn > 0 && diagBuf[0] != '0');

    AcquireSRWLockExclusive(&g_lock);
    g_mailbox.reset();
    g_mailbox.setSmoothingAlpha(g_smoothAlpha);
    g_mailbox.setStallThresholdMicros((long long)(g_stallMs * 1000.0));
    ReleaseSRWLockExclusive(&g_lock);
    g_lastConsumeMicros = NowMicros();

    InterlockedExchange(&g_shutdown, 0);
    g_receiveThread = CreateThread(nullptr, 0, ReceiveThreadProc, nullptr, 0, nullptr);
    Log("initialized (freshness %.0fms, stall %.0fms, smoothAlpha %.2f, clamp %.0f, diag %d)", freshnessMs, g_stallMs, g_smoothAlpha, g_clampMax, g_diag ? 1 : 0);
}

MOUSE_SUPPORT_API void MouseSupport_Shutdown(void) {
    if (InterlockedCompareExchange(&g_started, 0, 0) == 0) return;
    InterlockedExchange(&g_shutdown, 1);
    if (g_receiveThread) {
        WaitForSingleObject(g_receiveThread, 300);
        CloseHandle(g_receiveThread);
        g_receiveThread = nullptr;
    }
    if (g_statusSocket != INVALID_SOCKET) {
        closesocket(g_statusSocket);
        g_statusSocket = INVALID_SOCKET;
    }
    if (g_overlaySocket != INVALID_SOCKET) {
        closesocket(g_overlaySocket);
        g_overlaySocket = INVALID_SOCKET;
    }
    if (g_diagFile) {
        fclose(g_diagFile);
        g_diagFile = nullptr;
    }
    InterlockedExchange(&g_started, 0);
    Log("shutdown complete");
}

MOUSE_SUPPORT_API int MouseSupport_IsRunning(void) {
    return InterlockedCompareExchange(&g_started, 0, 0) != 0 ? 1 : 0;
}

MOUSE_SUPPORT_API int MouseSupport_PollFrame(MouseSupportFrame* out) {
    if (!out) return 0;

    const long long now = NowMicros();
    mousesupport::ConsumeResult r;
    AcquireSRWLockExclusive(&g_lock);
    g_mailbox.consume(now, g_freshnessMicros, g_clampMax, r);
    ReleaseSRWLockExclusive(&g_lock);

    out->dx = r.dx;
    out->dy = r.dy;
    out->wheel = r.wheel;
    out->hasAbsolute = r.hasAbsolute ? 1 : 0;
    out->absoluteWindow = r.absoluteWindow ? 1 : 0;
    out->absX = r.absX;
    out->absY = r.absY;
    out->buttonCount = r.buttonCount;
    for (int i = 0; i < r.buttonCount && i < MOUSE_SUPPORT_MAX_BUTTONS; ++i) {
        out->buttons[i].button = r.buttons[i].button;
        out->buttons[i].action = r.buttons[i].action;
    }

    if (g_diag) {
        const double gapMs = (double)(now - g_lastConsumeMicros) / 1000.0;
        const double ageMs = r.appliedAgeMicros / 1000.0;
        if (gapMs > 100.0) {
            DiagLog("STALL-RESUME gap=%.1fms appliedAgeOldest=%.1fms emitted=(%.2f,%.2f) fresh=%d droppedStale=%d droppedMotion=(%.1f,%.1f) ring=%d clamp=%d bucket=%s",
                gapMs, ageMs, r.dx, r.dy, r.freshSamples, r.droppedSamples, r.droppedDx, r.droppedDy, r.ringDepthAtConsume, r.clamped ? 1 : 0, GapBucket(gapMs));
        } else {
            DiagLog("consume gap=%.1fms applied=(%.2f,%.2f) ageOldest=%.1fms fresh=%d droppedStale=%d droppedMotion=(%.1f,%.1f) ring=%d clamp=%d bucket=%s",
                gapMs, r.dx, r.dy, ageMs, r.freshSamples, r.droppedSamples, r.droppedDx, r.droppedDy, r.ringDepthAtConsume, r.clamped ? 1 : 0, GapBucket(gapMs));
        }
    }
    g_lastConsumeMicros = now;
    return 1;
}

MOUSE_SUPPORT_API void MouseSupport_SetHostState(const MouseSupportHostState* state) {
    if (!state) return;
    AcquireSRWLockExclusive(&g_lock);
    if (state->cursorMode != g_host.cursorMode) {
        g_pendingMode = state->cursorMode;
        g_modeChangeTime = GetTickCount64();
    }
    g_host = *state;
    ReleaseSRWLockExclusive(&g_lock);
}

MOUSE_SUPPORT_API void MouseSupport_SendCursorSync(double x, double y) {
    char packet[64] = {};
    sprintf_s(packet, "SYNC:%.0f,%.0f", x, y);
    SendStatusText(packet);
}

MOUSE_SUPPORT_API void MouseSupport_SendWindowCursorSync(double x, double y) {
    char packet[64] = {};
    sprintf_s(packet, "SYNCW:%.0f,%.0f", x, y);
    SendStatusText(packet);
}

MOUSE_SUPPORT_API void MouseSupport_UpdateOverlay(double menuCursorX, double menuCursorY, int visible) {
    if (g_overlaySocket == INVALID_SOCKET) {
        g_overlaySocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (g_overlaySocket == INVALID_SOCKET) return;
    }
    int windowWidth, windowHeight;
    AcquireSRWLockShared(&g_lock);
    windowWidth = g_host.windowWidth;
    windowHeight = g_host.windowHeight;
    ReleaseSRWLockShared(&g_lock);

    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(kOverlayPort);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    char packet[64] = {};
    sprintf_s(packet, "CURSOR:%.0f,%.0f,%d",
        WindowToProtocolX(menuCursorX, windowWidth), WindowToProtocolY(menuCursorY, windowHeight), visible);
    sendto(g_overlaySocket, packet, (int)strlen(packet), 0,
        reinterpret_cast<const sockaddr*>(&addr), sizeof(addr));
}

MOUSE_SUPPORT_API unsigned int MouseSupport_LastActivityTickMs(void) {
    return (unsigned int)InterlockedCompareExchange(&g_lastActivityTick, 0, 0);
}

MOUSE_SUPPORT_API double MouseSupport_SmoothingMs(void) {
    return (double)g_freshnessMicros / 1000.0;
}

}
