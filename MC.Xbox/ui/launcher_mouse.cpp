#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "launcher_mouse.h"

namespace {
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
    if (down && !g_nativeLeftDown) g_nativeClickPending = true;
    g_nativeLeftDown = down;
    ReleaseSRWLockExclusive(&g_nativeLock);
}

void LauncherMouseNativeWheel(int wheelDelta) {
    AcquireSRWLockExclusive(&g_nativeLock);
    g_nativeSeen = true;
    g_nativeWheelDelta += wheelDelta;
    ReleaseSRWLockExclusive(&g_nativeLock);
}

void LauncherMouse::Update(float renderWidth, float renderHeight) {
    if (renderWidth <= 1.0f || renderHeight <= 1.0f) return;

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

    if (!seen || w <= 1.0f || h <= 1.0f) return;

    visible_ = true;
    x_ = x * (renderWidth / w);
    y_ = y * (renderHeight / h);
    if (x_ < 0.0f) x_ = 0.0f;
    if (y_ < 0.0f) y_ = 0.0f;
    if (x_ > renderWidth) x_ = renderWidth;
    if (y_ > renderHeight) y_ = renderHeight;
    if (click) clickLatched_ = true;
    wheel_ += static_cast<float>(wheel) / kWheelDeltaPerNotch;
}

bool LauncherMouse::TakeClick() {
    if (!clickLatched_) return false;
    clickLatched_ = false;
    return true;
}

float LauncherMouse::TakeWheel() {
    const float value = wheel_;
    wheel_ = 0.0f;
    return value;
}
