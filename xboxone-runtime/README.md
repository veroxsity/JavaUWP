# Xbox One Graphics Runtime

This folder contains the Xbox One ANGLE runtime files extracted from the local
RetroArch Xbox One package. It is the repo-level input used by `build.ps1`,
matching how `mesa-runtime/` is used for Series consoles.

Current contents:

- `libEGL.dll`
- `libGLESv2.dll`
- `libGLESv1_CM.dll`
- `glu32.dll`

This is not complete by itself. Xbox One still needs a UWP-compatible
MobileGlues `opengl32.dll` in this folder so Minecraft's desktop OpenGL calls
can run on top of ANGLE/GLES.
