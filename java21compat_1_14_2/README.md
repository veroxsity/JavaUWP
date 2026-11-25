# Java21Compat — Minecraft 1.14.2 Fabric

This is an isolated client-side Fabric mod for the JavaUWP Minecraft **1.14.2** profile. It is not a general compatibility mod and must not be installed on other Minecraft versions.

## Current implementation (0.1.0)

- Declares Minecraft 1.14.2 and Fabric Loader 0.14.25 as dependencies.
- Requires Java 21 or newer through Fabric metadata.
- Compiles the mod bytecode for Java 8 (`--release 8`) so the legacy game/loader class-loading environment can read it while it runs on the Java 21 VM.
- Logs the Java runtime, VM, OS/architecture, and detected LWJGL version to help diagnose the JavaUWP launch.

## Important limitations

This first version is a diagnostic/bootstrap mod, not a complete Java 21 port. A Fabric mod is loaded after the JVM starts, so it cannot add JVM startup flags retroactively. It also cannot make desktop Windows GLFW/LWJGL native libraries UWP-compatible. Those parts must be handled by the JavaUWP launcher/runtime and its UWP GLFW 3 native shim.

Do not interpret successful mod loading as proof that rendering, input, or native library loading works on Xbox. Validate the 1.14.2 profile on the target runtime and add narrowly scoped mixins only after a reproducible Java 21 failure is identified.

## Build

Use JDK 21 and Gradle 8.6 or a compatible Gradle 8.x installation:

```powershell
gradle build
```

The output JAR is written to `build/libs/`.
