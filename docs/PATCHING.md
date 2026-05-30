# Patching Notes

Minecraft, Fabric, LWJGL, and Java expect desktop Windows behavior. Xbox Developer Mode UWP runs inside a packaged sandbox, so a few targeted patches are needed.

## Fabric Loader patch

`scripts\patch-fabric.ps1` compiles Java sources from `patch\` and overlays the resulting classes into the local Fabric Loader JAR:

```text
staging\cache\gameDir\libraries\net\fabricmc\fabric-loader\0.19.2\fabric-loader-0.19.2.jar
```

Patched classes:

- `LoaderUtil`
- `FileSystemUtil`
- `FileSystemReference`
- `OutputConsumerPath`
- `FabricLauncherBase`

The patch script copies every compiled `.class` file from the patch output into
the Fabric Loader JAR. This is intentional: Java sources can generate synthetic
inner classes such as `OutputConsumerPath$1.class`, and leaving an old inner
class in the JAR can break Fabric at remap time.

The main goals are:

- Avoid `Path.toRealPath()` in places where the Xbox sandbox blocks or breaks the underlying Windows path query.
- Keep Fabric remapping from using file system calls that fail in packaged app paths.
- Make loader launch behavior tolerate the UWP runtime layout.

Run it directly with:

```powershell
.\scripts\patch-fabric.ps1
```

The top level build also runs it automatically.

## Compatibility mod

`compat_mod` is a Fabric client mod with mixins for Minecraft code paths that need sandbox aware behavior.

Current mixins:

- `MinecraftClientProbeMixin`
- `PathUtilBypassMixin`
- `SystemDetailsOshiBypassMixin`
- `WorldLoadProgressTrackerMixin`
- `ZipFsBypassMixin`

Build it directly with:

```powershell
.\compat_mod\build_compat_mod.ps1
```

The build copies the mod JAR into the local ignored `gameDir\mods` folder. The top level package step then places bundled launcher-owned mods under `runtime\bundled-mods`, and the UWP host copies them into writable `LocalState\game\mods` on launch.

## GLFW shim

`glfw_shim\glfw_uwp.cpp` builds a replacement `glfw.dll` for LWJGL GLFW.

It handles:

- `CoreWindow` based window setup.
- EGL surface creation for Mesa.
- Keyboard and text input callbacks.
- Basic monitor, cursor, timing, and window API responses expected by LWJGL.
- Xbox controller state through GameInput and the GLFW joystick and gamepad APIs.

Build it directly with:

```powershell
.\glfw_shim\build_glfw.ps1
```

The top level build copies the DLL into package natives. The JVM launch forces LWJGL to use that DLL with `-Dorg.lwjgl.glfw.libname`, so the package no longer rewrites downloaded LWJGL native jars.

## Runtime layout

The packaged app keeps launcher-owned runtime files under the package folder. At launch, `MC.Xbox.exe` prepares writable state in `LocalState`:

```text
LocalState\game
LocalState\assets
LocalState\natives
```

The game uses `LocalState\game` for saves, config, logs, mods, downloaded libraries, downloaded client jars, and other writable files. Bundled compatibility mods are copied there during launch. Mojang libraries, Minecraft client jars, version JSON files, asset indexes, and asset objects are verified from `download_manifest.tsv` and downloaded into `LocalState` after Minecraft ownership verification.

`MC.Xbox.exe` writes a `LocalState\.download_manifest` marker containing the selected Minecraft/Fabric versions and packaged manifest hash. If that marker changes, the launcher removes downloaded official runtime folders before validating the new manifest. The signed-in menu's `Repair downloads` action forces this cleanup for the current manifest.

## Version bumps

When changing Minecraft, Fabric Loader, or key libraries:

1. Update `scripts/config.ps1`, or pass build overrides while testing.
2. Update `compat_mod/src/main/resources/fabric.mod.json`.
3. Recreate `staging\cache\gameDir`.
4. Recreate `staging\cache\natives-1.21` if native versions changed.
5. Run the local Fabric client once so remapped jars are generated.
6. Run `.\build.ps1`; the build regenerates `download_manifest.tsv` for the selected version.

Do not commit generated game files, downloaded assets, natives, certificates, app packages, logs, or saves.
