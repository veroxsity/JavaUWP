# Win32 compatibility adapter — UWP design boundary

This folder is reserved for a dedicated adapter. It is intentionally separate from `MC.Xbox/launch` and the existing Cleanroom controller source.

## Planned modules

| Module | Responsibility | Current state |
|---|---|---|
| LibraryLoader | Load approved native modules from package/LocalState paths | Not implemented |
| ProcessModel | Replace or adapt CEF desktop subprocess assumptions | Not implemented |
| WindowSurface | Connect browser output to a UWP/CoreWindow-compatible surface | Not implemented |
| InputBridge | Forward keyboard, pointer, focus, resize, and controller events | Not implemented |
| GpuBridge | Select a supported rendering path or software fallback | Not implemented |
| Lifecycle | Close browser instances and release native resources safely | Not implemented |

## Non-negotiable constraints

- Do not call desktop-only Win32 APIs without checking their UWP availability.
- Do not assume unrestricted `CreateProcess`, arbitrary DLL loading, HWND ownership, or desktop window message loops.
- Do not disable Chromium security features as a substitute for UWP support.
- Keep all native code x64 and build it against the appropriate UWP SDK.
- Keep the feature disabled until the full CEF process and rendering model has been validated on device.

This file is a design boundary, not a working Win32 implementation. The actual adapter must be implemented and tested before MCEF can be enabled.
