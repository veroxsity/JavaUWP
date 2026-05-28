# Bandit Launcher for Minecraft: Java Edition on Xbox Series X/S

`Bandit Launcher` is an experimental launcher for Minecraft: Java Edition to use on Xbox Dev Mode.

The app embeds a real JDK, starts Fabric inside the UWP process, and presents Minecraft through a custom GLFW layer backed by UWP `CoreWindow` and Mesa EGL on D3D12.

## Current state

The project is usable for active development:

- Minecraft 1.21.11 with Fabric Loader 0.19.2 boots in Xbox Developer Mode.
- Dynamic Auth works with multiplayer for a seamless experience.
- Menus render through Mesa on the Xbox GPU.
- Keyboard and basic text input work.
- Single player reaches gameplay.
- Multiplayer and skin loading have recent fixes.
- Xbox controller input is exposed through GameInput in the GLFW shim.
- The app includes generated UWP tile assets from the checked in icon.
- The host seeds writable game state into `LocalState` on launch.

Expect rough edges. This is still a research port and development tool, not a release launcher.

## Repo layout

- `MC.Xbox/`
  UWP host app. It starts the JVM, publishes the `CoreWindow` for EGL, prepares writable state, and launches Fabric.
- `glfw_shim/`
  Replacement `glfw.dll` for LWJGL. It maps GLFW window, input, gamepad, and EGL calls onto UWP APIs.
- `compat_mod/`
  Fabric compatibility mod with mixins for Minecraft code paths that break inside the Xbox sandbox.
- `patch/`
  Patched Fabric Loader classes used by `scripts/patch-fabric.ps1`.
- `scripts/`
  Setup, cleanup, asset, patch, and shared build helper scripts.
- `mesa-runtime/`
  Mesa UWP runtime DLLs used by local builds.
- `build.ps1`
  Main build and package script.

## Build overview

Read [docs/BUILDING.md](docs/BUILDING.md) for the complete setup. The short version is:

```powershell
.\scripts\download-libs.ps1
.\scripts\download-assets.ps1
java -jar .\staging\cache\tools\fabric-installer.jar client -dir .\staging\cache\gameDir -mcversion 1.21.11 -loader 0.19.2 -launcher win32 -noprofile
.\build.ps1
```

The build script compiles the UWP host, builds the GLFW shim, builds the compatibility mod, patches the local Fabric Loader JAR, copies assets and runtime files, injects the shim into the LWJGL GLFW native JAR, creates UWP tile assets, then signs `output\MC_Java_1.0.0.0.appx`.

Generated files live under `staging` and `output`. They are ignored by git.

## Local inputs

You need to provide your own legal game and runtime inputs. This repo does not include:

- Minecraft game files beyond files downloaded into your local ignored cache.
- Mojang asset objects beyond files downloaded into your local ignored cache.
- Fabric installer JAR.
- A JRE image committed to git.
- Signed app packages.
- Local signing certificates.
- Saves, logs, or local debug output.

The Mesa UWP runtime DLLs needed by the build are tracked in `mesa-runtime/`.

## How it works

1. `MC.Xbox.exe` starts as a UWP app.
2. The app loads a main menu with Dynamic Auth.
3. The app publishes the live `CoreWindow` through app properties.
4. The app seeds `LocalState` with runtime files that need writable paths.
5. The app loads `jvm.dll` and starts Java in the same process.
6. Fabric launches Minecraft from the embedded JVM.
7. LWJGL loads the custom `glfw.dll`.
8. The GLFW shim creates an EGL surface for the UWP window.
9. Mesa translates OpenGL calls to D3D12.

The main compatibility work is around Xbox sandbox paths, packaged app file access, native library loading, GLFW behavior, input, and Fabric remapping.

## Status and limits

Known limits include:

- Xbox Developer Mode is the only supported target.
- Retail mode is not supported.
- Path handling is still the most sensitive area.
- Some Java platform diagnostics can still warn or fail because the sandbox does not look like desktop Windows.
- Controller support exists through GameInput, but game controls still need testing and tuning.
- Version bumps can break Fabric patches, the compatibility mod, or native runtime layout.

## Documentation

- [Building](docs/BUILDING.md)
- [Patching notes](docs/PATCHING.md)
- [Legal notes](docs/LEGAL.md)

For cleanup, preview first:

```powershell
.\scripts\clean.ps1
```

Then apply when the preview looks right:

```powershell
.\scripts\clean.ps1 -Apply
```

## License and ownership

Original project code in this repository is available under the custom terms in [LICENSE](LICENSE). Use is allowed with credit. Redistribution requires prior written permission from veroxsity / BanditVault.

Minecraft, Fabric, Mojang assets, Mesa, LWJGL, Java, and other third-party components remain under their own licenses and terms.
