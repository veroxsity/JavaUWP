package com.cleanroommc.client.windows;

import com.sun.jna.platform.win32.WinDef;

// keeps the public surface of cleanroom's TaskbarApi, a uwp app has no taskbar to drive
public final class TaskbarApi implements AutoCloseable {
    private static final TaskbarApi INSTANCE = new TaskbarApi();

    public enum TBPFLAG {
        TBPF_NOPROGRESS(0),
        TBPF_INDETERMINATE(1),
        TBPF_NORMAL(2),
        TBPF_ERROR(4),
        TBPF_PAUSED(8);

        public final int value;

        TBPFLAG(int value) {
            this.value = value;
        }
    }

    private TaskbarApi() {
    }

    public static TaskbarApi getInstance() {
        return INSTANCE;
    }

    public static TaskbarApi create() {
        return INSTANCE;
    }

    public static WinDef.HWND hwndFromGlfw(long window) {
        return null;
    }

    public void hrInit() {
    }

    public void setProgressState(WinDef.HWND window, TBPFLAG state) {
    }

    public void setProgressValue(WinDef.HWND window, long completed, long total) {
    }

    public void clearProgress(WinDef.HWND window) {
    }

    public void setOverlayIcon(WinDef.HWND window, WinDef.HICON icon, String description) {
    }

    public void clearOverlayIcon(WinDef.HWND window) {
    }

    public int addRef() {
        return 1;
    }

    public int release() {
        return 1;
    }

    @Override
    public void close() {
    }

    public static boolean flashTaskbar(WinDef.HWND window, int count) {
        return false;
    }

    public static boolean flashTaskbarUntilForeground(long window) {
        return false;
    }

    public static boolean stopFlash(WinDef.HWND window) {
        return false;
    }

    public static void clearInstance() {
    }
}
