# Controlify compatibility on Xbox UWP

## Goal

Investigate and implement a compatibility path for the upstream Fabric mod
[Controlify](https://modrinth.com/mod/controlify) in Bandit Launcher on Xbox
Series S|X. This is separate from the bundled Bandit Controller mod. Do not
enable both controller providers for the same profile at the same time.

This document is an implementation plan, not a claim that Controlify already
works on Xbox.

## Current launcher boundary

- Minecraft currently uses the launcher's GLFW CoreWindow shim
  (`glfw_shim/glfw_uwp.cpp`) as its LWJGL window and input path.
- SDL3 is an optional packaged UWP runtime. Packaging SDL3 does not route
  Minecraft or mods through SDL3, and does not satisfy mods that load GLFW
  native symbols or expect LWJGL's GLFW API.
- The compatibility target is broader than Controlify: multiple mods may
  depend on GLFW window, keyboard, mouse, joystick, or gamepad entry points.
  A missing native symbol/library or incomplete shim can therefore affect
  many mods, not just controller configuration.
- Evaluate a GLFW-to-SDL3 backend as a shared compatibility layer: preserve
  the GLFW/LWJGL-facing API expected by Minecraft and mods, while translating
  supported operations to SDL3 internally. This is not simply renaming
  `SDL3.dll` to a GLFW library; ABI, exported symbols, callbacks, handles,
  event pumping, timing, cursor behavior, and controller semantics must match.
- First inventory the exact missing library/symbols from logs and the current
  `glfw_shim`. Keep the existing CoreWindow GLFW implementation as fallback
  until the SDL3-backed path passes end-to-end tests.
- The bundled Bandit Controller implementation lives under
  `controller_mod/`; it is not the upstream Controlify mod.

## Work plan

1. **Inventory and baseline**
   - Record the Minecraft version, Fabric Loader, Java runtime, Controlify
     version, controller model, and launcher build.
   - Capture `latest.log` from a clean launch with only Controlify and its
     required dependencies enabled.
   - Confirm that the same Controlify build works in a desktop launcher with
     the same Minecraft version and mod set.

2. **Shared GLFW compatibility for mods**
   - Inventory all GLFW/LWJGL entry points used by the targeted Minecraft
     version and common mods, not only joystick functions.
   - Distinguish a missing native library, missing exported symbol, failed
     native initialization, and an unsupported GLFW operation. Record the
     exact library name, symbol, and calling mod from logs.
   - Design a single GLFW-compatible facade that can route to the current
     CoreWindow implementation or an SDL3-backed implementation. Keep the
     Java/LWJGL-facing signatures and native ABI stable; do not require users
     to install a separate adapter for every mod.
   - Cover window/context lifecycle, event polling, keyboard, mouse, cursor,
     clipboard, timing, callbacks, joystick, and gamepad APIs as required by
     the actual mod inventory. Mark unsupported calls explicitly rather than
     returning fabricated success.
   - Add a capability report at startup so users can see which GLFW functions
     are available and which are missing on the current backend.

3. **GLFW gamepad compatibility**
   - Verify `glfwJoystickPresent`, `glfwGetJoystickAxes`,
     `glfwGetJoystickButtons`, `glfwGetJoystickHats`,
     `glfwGetGamepadState`, `glfwJoystickIsGamepad`, and
     `glfwGetGamepadName` in the UWP shim.
   - Preserve stable joystick IDs and report connect/disconnect changes.
   - Map Xbox controller buttons, triggers, sticks, and D-pad consistently
     with GLFW's standard gamepad mapping.
   - Return safe neutral states for unavailable devices; never return stale
     pointers or out-of-range axis/button data.
   - Add native diagnostics for device name, mapping, button count, axis
     count, and connection events.

4. **Controlify integration**
   - Test Controlify's controller discovery and mapping screen against the
     GLFW shim before adding mod-specific patches.
   - If the failure is in a Controlify desktop/native assumption, isolate the
     workaround in an optional UWP compatibility module or a narrowly scoped
     upstream patch. Do not silently replace Controlify's keybinding system.
   - Keep the existing Bandit Controller and Controlify mutually exclusive
     per profile to prevent duplicate input handling.

5. **Voice-chat push-to-talk**
   - After normal controller input works, test the Simple Voice Chat
     push-to-talk binding as an ordinary Minecraft keybinding.
   - Verify press, hold, and release transitions, including focus loss and
     controller disconnect. A lost release event must not leave PTT stuck.
   - Do not couple microphone capture or audio playback changes to this
     controller-input work; those are separate audio-backend issues.

6. **Minecraft asset resolution and launch failure**
   - Track the separate launcher problem where Minecraft cannot locate the
     asset files required to start the game. This is a launcher download,
     index, path-resolution, or packaging issue until logs identify the
     failing stage; it is not automatically a Controlify bug.
   - Validate that the selected Minecraft version's asset index is downloaded
     and parsed, and that every referenced object is resolved by its SHA-1
     under the expected assets object directory.
   - Check the launcher's effective game directory and assets directory,
     including UWP LocalState paths, path separators, case handling, and
     whether the process has permission to read the files.
   - Verify downloads for missing or truncated asset objects, HTTP failures,
     hash mismatches, and interrupted downloads. Re-download only invalid or
     missing objects, then verify their hashes before launch.
   - Ensure the generated launch arguments point to the same asset index and
     asset root that the launcher populated; do not mix paths from another
     profile, version, or install location.
   - Log the exact missing asset path, asset index ID, expected hash, resolved
     path, and download/verification result. Avoid logging credentials.
   - Test a clean install, a second launch using the cache, and recovery after
     deleting one cached asset object.

7. **SDL3-backed GLFW implementation**
   - Implement SDL3 behind the GLFW-compatible facade, not as a separate
     API that individual mods must know about.
   - Integrate the LWJGL native binding, window integration, event pump,
     keyboard/mouse input, controller mapping, and callback translation.
   - Keep the CoreWindow GLFW backend as fallback until the SDL3-backed
     facade passes the same test matrix across multiple mods.

## Acceptance tests

- Xbox controller is detected on launch and after reconnect.
- Sticks, triggers, face buttons, bumpers, D-pad, and View/Menu buttons report
  correct values without duplicate or stuck events.
- Controlify opens its controller configuration UI and accepts a remapped
  button.
- Minecraft keyboard and mouse input continue to work.
- Simple Voice Chat PTT responds to the configured controller binding on
  press and release.
- A clean install downloads and verifies all required assets and starts.
- A second launch reuses valid cached assets without false missing-file errors.
- Removing one cached asset causes only that asset to be downloaded again.
- A representative set of GLFW-dependent mods loads without missing-library
  or missing-symbol errors, including when Controlify is absent.
- Backend capability diagnostics identify unsupported GLFW calls clearly.
- No regression occurs when Controlify is absent or disabled.
- Test results include launcher build, Minecraft version, Fabric Loader,
  Java version, mod versions, asset index ID, and relevant log excerpts.

## Current status

**Not implemented or verified.** This plan intentionally does not add a
placeholder native library, claim SDL3 is active, or patch Controlify without
a reproducible failure and matching source-level diagnosis. The asset issue
is now tracked as a separate launcher workstream; its root cause still needs
the exact launch error or log.
