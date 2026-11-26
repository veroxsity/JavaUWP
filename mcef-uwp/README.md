# MCEF 1.12.2 — isolated Xbox UWP integration

This directory is an additive integration area. It does not edit or replace the existing launcher, Cleanroom loader, or Cleanroom controller mod.

## Goal

Provide a separate compatibility layer for evaluating Minecraft Chromium Embedded Framework (MCEF) with Minecraft 1.12.2 / Cleanroom in the Xbox UWP app.

## Current status

- [x] Isolated integration directory and runtime manifest.
- [x] Win32-to-UWP adaptation boundary documented.
- [ ] Pin and vendor a verified MCEF build for Minecraft 1.12.2.
- [ ] Audit the exact CEF/Chromium version, license notices, and native dependencies.
- [ ] Implement the UWP-compatible native bridge.
- [ ] Implement or adapt the browser rendering/input surface for CoreWindow.
- [ ] Package and validate all required x64 UWP-compatible binaries.
- [ ] Test on Xbox Dev Mode.

**This commit does not claim that MCEF is already running.** No MCEF/CEF binaries are included because the exact upstream build, redistribution terms, and UWP compatibility have not yet been verified.

## Babric Beta 1.7.3

Babric is tracked as a separate legacy target. See [BABRIC-1.7.3.md](BABRIC-1.7.3.md); the modern MCEF 1.12.2 API artifact is not a Babric Beta 1.7.3 implementation.

## AWT / CoreWindow port

See [AWT-COREWINDOW-PORT.md](AWT-COREWINDOW-PORT.md) for the located upstream MCEF 1.12.2 source/release, the API-only asset distinction, and the required AWT-to-CoreWindow work across the requested version families. The upstream 1.12.2 release asset is API-only and is not a complete runnable MCEF/CEF package.

## Why a Win32 adapter is needed

MCEF's desktop Windows native layer cannot be assumed to work inside an AppContainer. The adapter must explicitly handle:

1. Native library loading from the app's permitted package/LocalState paths.
2. Process creation and CEF subprocess behavior without assuming unrestricted Win32 process access.
3. Chromium sandbox behavior under UWP restrictions.
4. Window handles and browser surfaces without assuming a normal desktop HWND.
5. Keyboard, mouse, gamepad, focus, and resize events through the app's CoreWindow/input path.
6. GPU/compositor fallback when desktop GPU paths are unavailable.

A compatibility shim cannot make blocked Win32 APIs available. Unsupported CEF behavior must be replaced with a UWP implementation or marked unsupported; it must not silently fall back to desktop-only calls.

## Integration boundary

- Keep the existing Cleanroom controller mod unchanged.
- Keep existing launcher/loader sources unchanged.
- Stage MCEF Java artifacts, CEF resources, and native binaries in this directory's packaging area only after their exact versions and licenses are verified.
- Enable this integration only behind a future opt-in feature flag.
- Do not bundle desktop-only DLLs as if they were UWP-compatible.

## Validation gate

The integration is not ready to enable until a build proves that every native dependency is x64 and UWP-loadable, and an Xbox test verifies browser startup, page rendering, input, shutdown, and repeated launch without leaving CEF subprocesses running.
