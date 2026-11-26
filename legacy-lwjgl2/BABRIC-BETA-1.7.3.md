# Babric Beta 1.7.3 Xbox launch target

This target is for launching Minecraft Beta 1.7.3 with Babric in the existing Xbox UWP launcher. It is separate from the MCEF/AWT work and does not use the modern Minecraft 1.12+ loader path.

## Target profile

- Minecraft: Beta 1.7.3
- Loader: Babric (legacy Fabric ecosystem)
- Window/input compatibility: Starac 1.3.2+babric replaces the legacy LWJGL 2 API surface with a compatibility facade over LWJGL 3.4.0 / GLFW; Xbox still needs a UWP-native GLFW/CoreWindow backend
- Runtime: Java 21 UWP JVM, only if the packaged JVM is confirmed to support this legacy game and its bytecode/API requirements
- Native architecture: Xbox package architecture (x64 where the current package is x64)

## Startup requirements

1. Resolve the official Beta 1.7.3 client JAR and its libraries into this profile's isolated game directory.
2. Resolve a Babric loader build specifically compatible with Beta 1.7.3. Do not substitute Fabric Loader for modern Minecraft.
3. Resolve Starac 1.3.2+babric from Modrinth version `fBYyB56e` (`maven.modrinth:n9UZ6gzC:fBYyB56e`) and include it in the client mod set.
4. Include Starac's required LWJGL 3.4.0 / GLFW classes and platform natives, checking the actual Modrinth file metadata rather than assuming desktop natives work on UWP.
5. Build the legacy launch classpath and Babric entrypoint/main class from the selected loader metadata; keep Beta profile libraries isolated from modern profiles.
6. Replace Starac's desktop GLFW native/window integration with the project's UWP-safe GLFW/CoreWindow bridge, covering window lifecycle, event polling, input and buffer swaps.
7. Ensure no desktop AWT/Swing window or desktop GLFW native is loaded. Launch with the profile-specific Java runtime and capture stdout/stderr plus the first Java exception in the Xbox log.

## Starac dependency

- Project: [Starac](https://modrinth.com/mod/starac) by Slainlight and DanyGames2014.
- Exact pinned Babric release: `1.3.2+babric` (Modrinth version ID `fBYyB56e`).
- Modrinth Maven coordinate: `maven.modrinth:n9UZ6gzC:fBYyB56e`.
- Source: [matthewperiut/starac](https://github.com/matthewperiut/starac).
- License: LGPL-2.1-only for the LWJGL3 compatibility implementation; preserve its license and notices when packaging.
- Starac de-AWTs the game and maps the old LWJGL2 API to LWJGL3.4.0/GLFW. It does **not** provide an Xbox UWP GLFW implementation; desktop GLFW binaries cannot be assumed to load in the Xbox sandbox.

## Implementation status

- [x] Added a separate Babric Beta 1.7.3 target specification.
- [x] Pinned Starac `1.3.2+babric` in the version catalog and documented its Modrinth Maven coordinate.
- [x] Identified the existing legacy-LWJGL2 isolation area.
- [ ] Add Babric metadata resolution and launch-classpath construction to the native launcher.
- [ ] Integrate Starac's LWJGL3 classes and replace desktop GLFW natives with a UWP-safe CoreWindow bridge.
- [ ] Verify the Java 21 UWP JVM and all required class/native dependencies.
- [ ] Build and launch on Xbox Dev Mode.

**This is not yet a working launch profile.** The catalog now identifies Babric and pins Starac, but the native launcher still has no Babric loader implementation and Starac's desktop GLFW backend is not Xbox UWP-compatible. The UWP CoreWindow bridge and Xbox tests are still pending.
