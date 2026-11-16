# Legacy LWJGL 2 compatibility area

This directory isolates the experimental Minecraft Beta 1.7.3 work from the modern GLFW/LWJGL 3 launch path.

## Important Java runtime distinction

- Minecraft 26.3/26.4 uses the project's Java 25 target.
- Beta 1.7.3 is a Java 6-era client. Java 25 is **not** assumed to be compatible with its old client and LWJGL 2 stack.
- The legacy profile therefore records Java 8 as the initial compatibility baseline to validate. A Java 25 experiment must be a separate opt-in test, not the default runtime for Beta 1.7.3.
- Do not replace the modern Java 25 runtime or the current GLFW shim with the legacy files.

## Planned package layout

When implemented, the installed package should keep these files under a dedicated folder:

```text
legacy-lwjgl2/
  profile.json
  README.md
  runtime/
    bin/
    lib/
  lwjgl2/
    lwjgl.jar
    lwjgl_util.jar
    natives/
  adapters/
    corewindow/
    input/
    opengl/
```

The runtime and LWJGL binaries are not checked into this repository. They must be built or obtained from permitted sources and packaged for the Xbox UWP architecture.

## Required implementation work

1. Add a legacy launch profile in the native launcher and select it only for `b1.7.3`.
2. Resolve a dedicated Java home for the legacy profile instead of using the modern Java 25 selection.
3. Build an LWJGL 2 native compatibility layer for UWP. The original desktop `lwjgl.dll` is not an Xbox UWP implementation.
4. Implement the old `org.lwjgl.opengl.Display` lifecycle using CoreWindow and the project's graphics path.
5. Translate keyboard, mouse and controller input into the LWJGL 2 input APIs.
6. Validate OpenGL context creation, buffer swaps, timing and shutdown on Xbox.
7. Add automated packaging checks and an actual Beta 1.7.3 launch test.

## Current status

This is a preparation scaffold only. The native LWJGL 2 adapter, Java runtime packaging, legacy launch selection and Xbox tests are not implemented. Creating this folder does not make Beta 1.7.3 launchable.
