# Babric Beta 1.7.3 Xbox launch target

This target is for launching Minecraft Beta 1.7.3 with Babric in the existing Xbox UWP launcher. It is separate from the MCEF/AWT work and does not use the modern Minecraft 1.12+ loader path.

## Target profile

- Minecraft: Beta 1.7.3
- Loader: Babric (legacy Fabric ecosystem)
- Graphics/window API: legacy LWJGL 2 API surface, implemented through the project's UWP/CoreWindow adapter
- Runtime: Java 21 UWP JVM, only if the packaged JVM is confirmed to support this legacy game and its bytecode/API requirements
- Native architecture: Xbox package architecture (x64 where the current package is x64)

## Startup requirements

1. Resolve the official Beta 1.7.3 client JAR and its libraries into this profile's isolated game directory.
2. Resolve a Babric loader build specifically compatible with Beta 1.7.3. Do not substitute Fabric Loader for modern Minecraft.
3. Build the legacy launch classpath and Babric entrypoint/main class from the selected loader metadata.
4. Keep the Beta profile's native libraries separate from modern LWJGL 3/GLFW libraries.
5. Route LWJGL 2 Display creation, event polling, input and buffer swaps through the UWP CoreWindow adapter.
6. Ensure no desktop AWT/Swing window is created. Any AWT use in optional legacy libraries must be identified and handled separately; do not globally fake AWT availability.
7. Launch with the profile-specific Java runtime and capture stdout/stderr plus the first Java exception in the Xbox log.

## Implementation status

- [x] Added a separate Babric Beta 1.7.3 target specification.
- [x] Identified the existing legacy-LWJGL2 isolation area.
- [ ] Add Babric metadata resolution and launch-classpath construction to the native launcher.
- [ ] Implement and connect the LWJGL 2 CoreWindow JNI adapter.
- [ ] Verify the Java 21 UWP JVM and all required class/native dependencies.
- [ ] Build and launch on Xbox Dev Mode.

**This is not yet a working launch profile.** The existing launcher currently has Fabric/Forge/Cleanroom/NeoForge paths, but no Babric loader implementation. The profile must not be advertised as launchable until the pending native launcher and Xbox tests pass.
