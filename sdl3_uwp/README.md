# SDL3 UWP and GLFW backends

This project keeps the existing GLFW CoreWindow compatibility shim and adds an optional SDL3 UWP runtime build.

## GLFW (existing path)

- Built by `glfw_shim/build_glfw.ps1`.
- Packaged as `natives/glfw.dll`.
- Remains the active LWJGL window library in the current launcher. The launcher sets `org.lwjgl.glfw.libname` to this shim.

## SDL3 (experimental runtime)

- Source: [XboxEmulationHub/SDL3-uwp](https://github.com/XboxEmulationHub/SDL3-uwp)
- Branch: `uwp-3.4.20`
- Pinned source commit: `ec2e0e7c77ec9a04437eb3ae884d62ae0e6129b4`
- CMake is configured with `WINDOWS_STORE=ON` and Windows SDK target `10.0.26100.0`.
- Build and package with:

```powershell
.\\build.ps1 -BuildSDL3
```

The optional step builds `SDL3.dll` and copies it to the package root and `natives/`. It does not run on normal builds unless `-BuildSDL3` is supplied.

## Important compatibility boundary

Packaging SDL3 does not replace GLFW or redirect Minecraft's LWJGL calls to SDL3. The current Minecraft launch path still forces the GLFW shim. To make Minecraft 26.3/26.4 actually use SDL3, the project still needs a compatible LWJGL SDL3 binding and a tested window/input/render integration (or a GLFW-to-SDL3 compatibility layer). Until that exists, GLFW remains the working compatibility path and SDL3 is only an optional packaged UWP runtime.

No Xbox build or game launch test has been performed by this repository edit.
