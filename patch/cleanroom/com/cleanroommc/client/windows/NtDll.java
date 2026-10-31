package com.cleanroommc.client.windows;

// a zero windows build makes checkCompatibility turn dwm styling off without touching jna
public interface NtDll {
    static void initializeWindowsInformation(long windowHandle) {
        WindowsProperties.handle = windowHandle;
    }
}
