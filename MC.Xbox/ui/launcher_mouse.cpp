#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "launcher_mouse.h"
#include "mouse_support_api.h"

namespace {
typedef int (*PollFrameFn)(MouseSupportFrame*);
typedef unsigned int (*LastActivityFn)(void);
typedef void (*SetHostStateFn)(const MouseSupportHostState*);
constexpr unsigned int kLauncherMouseTimeoutMs = 3000;
constexpr int kCursorModeNormal = 0x00034001;
constexpr float kProtocolWidth = 1920.0f;
constexpr float kProtocolHeight = 1080.0f;

// Written from CoreWindow pointer handlers, read in Update. Both run on the thread that pumps the
// window's dispatcher, but the lock keeps this correct if that ever changes.
SRWLOCK g_nativeLock = SRWLOCK_INIT;
bool g_nativeSeen = false;
float g_nativeX = 0.0f;
float g_nativeY = 0.0f;
float g_nativeWindowW = 0.0f;
float g_nativeWindowH = 0.0f;
bool g_nativeLeftDown = false;
bool g_nativeClickPending = false;
int g_nativeWheelDelta = 0;
constexpr float kWheelDeltaPerNotch = 120.0f;
}

void LauncherMouseNativeMove(float dipX, float dipY, float windowWidthDip, float windowHeightDip) {
    AcquireSRWLockExclusive(&g_nativeLock);
    g_nativeSeen = true;
    g_nativeX = dipX;
    g_nativeY = dipY;
    g_nativeWindowW = windowWidthDip;
    g_nativeWindowH = windowHeightDip;
    ReleaseSRWLockExclusive(&g_nativeLock);
}

void LauncherMouseNativeLeftButton(bool down) {
    AcquireSRWLockExclusive(&g_nativeLock);
    g_nativeSeen = true;
    // A click is the press edge; latched here so a press and release inside one frame still counts.
    if (down && !g_nativeLeftDown) {
        g_nativeClickPending = true;
    }
    g_nativeLeftDown = down;
    ReleaseSRWLockExclusive(&g_nativeLock);
}

void LauncherMouseNativeWheel(int wheelDelta) {
    AcquireSRWLockExclusive(&g_nativeLock);
    g_nativeSeen = true;
    g_nativeWheelDelta += wheelDelta;
    ReleaseSRWLockExclusive(&g_nativeLock);
}

bool LauncherMouse::UpdateNative(float renderWidth, float renderHeight) {
    AcquireSRWLockExclusive(&g_nativeLock);
    const bool seen = g_nativeSeen;
    const float x = g_nativeX;
    const float y = g_nativeY;
    const float w = g_nativeWindowW;
    const float h = g_nativeWindowH;
    const bool click = g_nativeClickPending;
    const int wheel = g_nativeWheelDelta;
    g_nativeClickPending = false;
    g_nativeWheelDelta = 0;
    ReleaseSRWLockExclusive(&g_nativeLock);

    if (!seen || w <= 1.0f || h <= 1.0f) {
        return false;
    }
    nativeActive_ = true;
    connected_ = true;
    seeded_ = true;
    // The window and the launcher's render target cover the same area, so the pointer maps across
    // proportionally whatever the DPI scale is.
    x_ = x * (renderWidth / w);
    y_ = y * (renderHeight / h);
    if (x_ < 0.0f) x_ = 0.0f;
    if (y_ < 0.0f) y_ = 0.0f;
    if (x_ > renderWidth) x_ = renderWidth;
    if (y_ > renderHeight) y_ = renderHeight;
    if (click) {
        clickLatched_ = true;
    }
    wheel_ += static_cast<float>(wheel) / kWheelDeltaPerNotch;
    return true;
}

LauncherMouse::LauncherMouse() {
    HMODULE handle = LoadPackagedLibrary(L"mouse_support.dll", 0);
    if (handle) {
        module_ = handle;
        pollProc_ = reinterpret_cast<void*>(GetProcAddress(handle, "MouseSupport_PollFrame"));
        activityProc_ = reinterpret_cast<void*>(GetProcAddress(handle, "MouseSupport_LastActivityTickMs"));
        setHostProc_ = reinterpret_cast<void*>(GetProcAddress(handle, "MouseSupport_SetHostState"));
        available_ = pollProc_ != nullptr;
    }
}

LauncherMouse::~LauncherMouse() {
}

void LauncherMouse::PushHostState(float renderWidth, float renderHeight) {
    if (!setHostProc_) {
        return;
    }
    // launcher ui always wants a visible cursor, so report menu mode; the dll
    // otherwise keeps its gameplay default and the relay never shows the cursor
    MouseSupportHostState state;
    state.windowWidth = static_cast<int>(renderWidth);
    state.windowHeight = static_cast<int>(renderHeight);
    state.menuWidth = static_cast<int>(renderWidth);
    state.menuHeight = static_cast<int>(renderHeight);
    state.cursorMode = kCursorModeNormal;
    state.menuCursorX = x_;
    state.menuCursorY = y_;
    reinterpret_cast<SetHostStateFn>(setHostProc_)(&state);
}

void LauncherMouse::Update(float renderWidth, float renderHeight) {
    if (renderWidth <= 1.0f || renderHeight <= 1.0f) {
        return;
    }

    // The relay wins while it is actively streaming (someone is driving from a PC); otherwise the
    // console's own mouse, once it has been seen at all.
    bool relayLive = false;
    if (available_ && activityProc_) {
        const unsigned int last = reinterpret_cast<LastActivityFn>(activityProc_)();
        relayLive = last != 0 && (DWORD)(GetTickCount() - (DWORD)last) <= kLauncherMouseTimeoutMs;
    }
    if (!relayLive && UpdateNative(renderWidth, renderHeight)) {
        return;
    }
    nativeActive_ = false;
    if (!available_) {
        return;
    }

    connected_ = false;
    if (activityProc_) {
        LastActivityFn activity = reinterpret_cast<LastActivityFn>(activityProc_);
        const unsigned int last = activity();
        if (last != 0 && (DWORD)(GetTickCount() - (DWORD)last) <= kLauncherMouseTimeoutMs) {
            connected_ = true;
        }
    }

    if (!connected_) {
        seeded_ = false;
        clickLatched_ = false;
        prevLeftDown_ = false;
        wheel_ = 0.0f;
        return;
    }

    if (!seeded_) {
        x_ = renderWidth * 0.5f;
        y_ = renderHeight * 0.5f;
        seeded_ = true;
    }

    MouseSupportFrame frame;
    PollFrameFn poll = reinterpret_cast<PollFrameFn>(pollProc_);
    if (poll(&frame)) {
        if (frame.hasAbsolute) {
            if (frame.absoluteWindow) {
                x_ = static_cast<float>(frame.absX);
                y_ = static_cast<float>(frame.absY);
            } else {
                x_ = static_cast<float>(frame.absX) * (renderWidth / kProtocolWidth);
                y_ = static_cast<float>(frame.absY) * (renderHeight / kProtocolHeight);
            }
        } else {
            x_ += static_cast<float>(frame.dx) * (renderWidth / kProtocolWidth);
            y_ += static_cast<float>(frame.dy) * (renderHeight / kProtocolHeight);
        }
        if (x_ < 0.0f) x_ = 0.0f;
        if (y_ < 0.0f) y_ = 0.0f;
        if (x_ > renderWidth) x_ = renderWidth;
        if (y_ > renderHeight) y_ = renderHeight;

        wheel_ += static_cast<float>(frame.wheel);

        for (int i = 0; i < frame.buttonCount; ++i) {
            if (frame.buttons[i].button != 0) {
                continue;
            }
            if (frame.buttons[i].action != 0) {
                if (!prevLeftDown_) {
                    clickLatched_ = true;
                }
                prevLeftDown_ = true;
            } else {
                prevLeftDown_ = false;
            }
        }
    }

    PushHostState(renderWidth, renderHeight);
}

bool LauncherMouse::TakeClick() {
    if (clickLatched_) {
        clickLatched_ = false;
        return true;
    }
    return false;
}

float LauncherMouse::TakeWheel() {
    float value = wheel_;
    wheel_ = 0.0f;
    return value;
}
