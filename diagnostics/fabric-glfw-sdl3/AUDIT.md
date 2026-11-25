# Fabric / GLFW / SDL3 integration audit

Repository branch: `feature/sdl3-minecraft-26.3-26.4`

This is an additive audit. It does not change any existing launcher, loader, shim, controller, build, or packaging file.

## Confirmed from the current source

### 1. SDL3 is built and packaged, but Minecraft does not use it

- `build.ps1` exposes the optional `-BuildSDL3` switch.
- That switch invokes `sdl3_uwp/build_sdl3.ps1` and copies `SDL3.dll` into the package root and `natives/`.
- `sdl3_uwp/README.md` explicitly states that the active Minecraft/LWJGL window path still forces the GLFW shim.
- The current SDL3 script only builds the SDL3 native runtime. It does not provide an LWJGL SDL3 binding, nor does it redirect GLFW calls to SDL3.

**Impact:** seeing `SDL3.dll` in the package does not mean Minecraft 26.3/26.4 is running on SDL3. The actual window/input path remains `natives/glfw.dll`.

### 2. GLFW shim build has a strict native dependency

- `glfw_shim/build_glfw.ps1` refuses to build if `mouse_support.lib` is missing.
- It compiles `glfw_uwp.cpp` for x64 with `WINAPI_FAMILY_APP`, links the UWP libraries and exports the GLFW API through `glfw_uwp.def`.

**Impact:** a missing or stale mouse-support import library can stop the GLFW shim build before Minecraft starts. The exact compiler output is needed to tell whether this is the current failure.

### 3. Fabric loader setup is separate from Fabric API

- `MC.Xbox/launch/loaders/fabric.cpp` selects Fabric's KnotClient main class and points to the packaged Fabric Loader JAR.
- It sets Fabric log/game/mod-directory JVM properties and conditionally adds mod directories.
- This file does not itself install or supply the Fabric API mod.

**Impact:** a Fabric API error can be caused by a missing/wrong-version Fabric API JAR, an incompatible mod, or loader/runtime mismatch; it cannot be diagnosed from the loader C++ file alone.

## Safe next implementation plan

Keep the existing project files untouched. Add a separate diagnostic/build area that:

1. Checks whether the packaged `natives/glfw.dll` exports the symbols LWJGL expects.
2. Checks whether the GLFW shim and `mouse_support.lib` were built for x64 UWP and are from the same build.
3. Verifies the actual Minecraft version, Fabric Loader version, Java major version, and Fabric API JAR in the selected profile.
4. Verifies whether SDL3 is merely packaged or whether a real LWJGL SDL3 backend exists.
5. Produces a report without overwriting files or changing the active launch path.

## What is not yet proven

No compiler log, Fabric crash report, or Xbox launch log was supplied with this audit. Therefore the exact failing symbol, missing JAR, or runtime API call is not yet known. Do not replace GLFW with SDL3 or remove Fabric API as a guess.
