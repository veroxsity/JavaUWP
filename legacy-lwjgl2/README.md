# Legacy LWJGL 2 compatibility area

This directory isolates the experimental Minecraft Beta 1.7.3 work from the modern GLFW/LWJGL 3 launch path.

## Runtime selection

- Beta 1.7.3 is configured to request the project's **Java 21 UWP JVM** from `runtime/jre21`.
- Modern Minecraft profiles keep their existing Java runtime selection (including Java 25 where required).
- The Java 21 UWP runtime must contain a JVM built for the Xbox UWP target architecture and be loadable inside the app process. A desktop JDK/JRE is not interchangeable with it.
- The profile metadata does not itself install or create the Java 21 runtime.

## Planned package layout

```text
legacy-lwjgl2/
  profile.json
  README.md
  runtime/
    jre21/
  lwjgl2/
    lwjgl.jar
    lwjgl_util.jar
    natives/
  adapters/
    java21-uwp/
    corewindow/
    input/
    opengl/
```

The runtime and LWJGL binaries are not checked into this repository. They must be built or obtained from permitted sources and packaged for the Xbox UWP architecture.

## Required implementation work

1. Connect the `b1.7.3` profile to an exact Java 21 UWP runtime resolver in the native launcher.
2. Verify the packaged JVM path, architecture, dependencies and `JNI_CreateJavaVM` loading inside the UWP process.
3. Build an LWJGL 2 JNI compatibility layer for UWP. The original desktop `lwjgl.dll` is not an Xbox UWP implementation.
4. Implement the old `org.lwjgl.opengl.Display` lifecycle using CoreWindow and the project's graphics path.
5. Translate keyboard, mouse and controller input into the LWJGL 2 input APIs.
6. Validate OpenGL context creation, buffer swaps, timing and shutdown on Xbox.
7. Add packaging checks and an actual Beta 1.7.3 launch test.

## Current status

This is still a preparation scaffold. The Java 21 selection is declared in profile metadata, but the native launcher does not yet consume this profile, the Java 21 UWP runtime is not bundled here, and the LWJGL 2/CoreWindow JNI adapter is not implemented. Beta 1.7.3 is not yet confirmed launchable.
