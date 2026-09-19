#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cwchar>
#include <string>

#include "launcher_mouse.h"
#include "launcher_common.h"

namespace {
SRWLOCK g_nativeLock = SRWLOCK_INIT;
bool g_nativeSeen = false;
bool g_nativeAbsoluteSeen = false;
bool g_nativeRawSeen = false;
float g_nativeX = 0.0f;
float g_nativeY = 0.0f;
float g_nativeRawX = 0.0f;
float g_nativeRawY = 0.0f;
float g_nativeWindowW = 0.0f;
float g_nativeWindowH = 0.0f;
bool g_nativeLeftDown = false;
bool g_nativeClickPending = false;
int g_nativeWheelDelta = 0;
ULONGLONG g_lastUiUpdate = 0;
constexpr float kWheelDeltaPerNotch = 120.0f;
}

MouseSensitivity LoadMouseSensitivity() {
    MouseSensitivity value;
    const std::wstring dir = GetLocalStateDir();
    if (dir.empty()) return value;
    std::wstring text;
    if (!ReadTextFile(dir + L"\\mouse_sensitivity.txt", text)) return value;
    int menu = 0;
    int game = 0;
    if (swscanf_s(text.c_str(), L"%d %d", &menu, &game) == 2 &&
        menu >= 25 && menu <= 300 && game >= 25 && game <= 300) {
        value.menu = menu;
        value.game = game;
    }
    return value;
}

bool SaveMouseSensitivity(const MouseSensitivity& value) {
    if (value.menu < 25 || value.menu > 300 || value.game < 25 || value.game > 300) return false;
    const std::wstring dir = GetLocalStateDir();
    if (dir.empty()) return false;
    return WriteTextFile(dir + L"\\mouse_sensitivity.txt",
        std::to_wstring(value.menu) + L" " + std::to_wstring(value.game) + L"\n");
}

void ApplyMouseSensitivity(const MouseSensitivity& value) {
    LauncherMouseInstance().SetSensitivity(value.menu);
    SetEnvironmentVariableW(L"MC_MOUSE_MENU_SPEED", std::to_wstring(value.menu).c_str());
    SetEnvironmentVariableW(L"MC_MOUSE_GAME_SPEED", std::to_wstring(value.game).c_str());
}

void LauncherMouseNativeMove(float dipX, float dipY, float windowWidthDip, float windowHeightDip) {
    AcquireSRWLockExclusive(&g_nativeLock);
    g_nativeSeen = true;
    g_nativeAbsoluteSeen = true;
    g_nativeX = dipX;
    g_nativeY = dipY;
    g_nativeWindowW = windowWidthDip;
    g_nativeWindowH = windowHeightDip;
    ReleaseSRWLockExclusive(&g_nativeLock);
}

void LauncherMouseNativeDelta(int dx, int dy, float windowWidthDip, float windowHeightDip) {
    AcquireSRWLockExclusive(&g_nativeLock);
    g_nativeSeen = true;
    g_nativeRawSeen = true;
    g_nativeWindowW = windowWidthDip;
    g_nativeWindowH = windowHeightDip;
    if (GetTickCount64() - g_lastUiUpdate < 250) {
        g_nativeRawX += static_cast<float>(dx);
        g_nativeRawY += static_cast<float>(dy);
    }
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
    g_lastUiUpdate = GetTickCount64();
    const bool seen = g_nativeSeen;
    const bool absoluteSeen = g_nativeAbsoluteSeen;
    const bool rawSeen = g_nativeRawSeen;
    const float x = g_nativeX;
    const float y = g_nativeY;
    const float dx = g_nativeRawX;
    const float dy = g_nativeRawY;
    const float w = g_nativeWindowW;
    const float h = g_nativeWindowH;
    const bool click = g_nativeClickPending;
    const int wheel = g_nativeWheelDelta;
    g_nativeClickPending = false;
    g_nativeWheelDelta = 0;
    g_nativeRawX = 0.0f;
    g_nativeRawY = 0.0f;
    ReleaseSRWLockExclusive(&g_nativeLock);

    if (!seen || w <= 1.0f || h <= 1.0f) return;

    visible_ = true;
    if (!seeded_) {
        x_ = absoluteSeen ? x * (renderWidth / w) : renderWidth * 0.5f;
        y_ = absoluteSeen ? y * (renderHeight / h) : renderHeight * 0.5f;
        seeded_ = true;
    } else {
        const float speed = static_cast<float>(sensitivity_) / 100.0f;
        x_ += (rawSeen ? dx : x - lastNativeX_) * (renderWidth / w) * speed;
        y_ += (rawSeen ? dy : y - lastNativeY_) * (renderHeight / h) * speed;
    }
    lastNativeX_ = x;
    lastNativeY_ = y;
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
