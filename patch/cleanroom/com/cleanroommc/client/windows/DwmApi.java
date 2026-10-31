package com.cleanroommc.client.windows;

import com.sun.jna.platform.win32.WinDef;

// jna cannot load dwmapi in the app container and the console has no title bar to style
public interface DwmApi {
    static void updateDwm(boolean darkMode, long windowHandle) {
    }

    static void disableWindowEffect(WinDef.HWND window) {
    }
}
