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
  Minecraft or Controlify input through SDL3.
- Therefore, first validate and repair the GLFW gamepad API exposed by the
  shim. Treat SDL3 as a future alternative backend only after a compatible
  LWJGL binding and end-to-end Minecraft integration exist.
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

2. **GLFW gamepad compatibility**
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

3. **Controlify integration**
   - Test Controlify's controller discovery and mapping screen against the
     GLFW shim before adding mod-specific patches.
   - If the failure is in a Controlify desktop/native assumption, isolate the
     workaround in an optional UWP compatibility module or a narrowly scoped
     upstream patch. Do not silently replace Controlify's keybinding system.
   - Keep the existing Bandit Controller and Controlify mutually exclusive
     per profile to prevent duplicate input handling.

4. **Voice-chat push-to-talk**
   - After normal controller input works, test the Simple Voice Chat
     push-to-talk binding as an ordinary Minecraft keybinding.
   - Verify press, hold, and release transitions, including focus loss and
     controller disconnect. A lost release event must not leave PTT stuck.
   - Do not couple microphone capture or audio playback changes to this
     controller-input work; those are separate audio-backend issues.

5. **Minecraft asset resolution and launch failure**
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

6. **SDL3 future path**
   - Do not switch Minecraft to SDL3 merely because `SDL3.dll` is packaged.
   - Only add an SDL3 controller backend after the LWJGL binding, window
     integration, event pump, and gamepad mapping are built and tested together.
   - Keep GLFW as the fallback until the SDL3 path passes the same test matrix.

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
- No regression occurs when Controlify is absent or disabled.
- Test results include launcher build, Minecraft version, Fabric Loader,
  Java version, mod versions, asset index ID, and relevant log excerpts.

## Current status

**Not implemented or verified.** This plan intentionally does not add a
placeholder native library, claim SDL3 is active, or patch Controlify without
a reproducible failure and matching source-level diagnosis. The asset issue
is now tracked as a separate launcher workstream; its root cause still needs
the exact launch error or log.
