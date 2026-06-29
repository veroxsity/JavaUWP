#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "launcher_mouse.h"
#include "mouse_support_api.h"

namespace {
typedef int (*PollFrameFn)(MouseSupportFrame*);
}

LauncherMouse::LauncherMouse() {
    HMODULE handle = LoadPackagedLibrary(L"mouse_support.dll", 0);
    if (handle) {
        module_ = handle;
        pollProc_ = reinterpret_cast<void*>(GetProcAddress(handle, "MouseSupport_PollFrame"));
        available_ = pollProc_ != nullptr;
    }
}

LauncherMouse::~LauncherMouse() {
}

void LauncherMouse::Update(float renderWidth, float renderHeight) {
    if (!available_ || renderWidth <= 1.0f || renderHeight <= 1.0f) {
        return;
    }
    if (!seeded_) {
        x_ = renderWidth * 0.5f;
        y_ = renderHeight * 0.5f;
        seeded_ = true;
    }

    MouseSupportFrame frame;
    PollFrameFn poll = reinterpret_cast<PollFrameFn>(pollProc_);
    if (!poll(&frame)) {
        return;
    }

    const float scaleX = renderWidth / 1920.0f;
    const float scaleY = renderHeight / 1080.0f;
    x_ += static_cast<float>(frame.dx) * scaleX;
    y_ += static_cast<float>(frame.dy) * scaleY;
    if (x_ < 0.0f) x_ = 0.0f;
    if (y_ < 0.0f) y_ = 0.0f;
    if (x_ > renderWidth) x_ = renderWidth;
    if (y_ > renderHeight) y_ = renderHeight;

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

bool LauncherMouse::TakeClick() {
    if (clickLatched_) {
        clickLatched_ = false;
        return true;
    }
    return false;
}
