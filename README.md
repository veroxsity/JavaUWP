# Bandit Launcher

`Bandit Launcher` is an experimental Minecraft Java Edition launcher for Xbox Developer Mode UWP. Current builds are pre-release quality: playable, but not a finished main release.

The app signs in with Microsoft, verifies Minecraft Java ownership, prepares the local runtime, then starts Fabric/Minecraft inside the UWP process. Rendering and input still use a custom GLFW layer backed by UWP `CoreWindow`; Series consoles use the Mesa EGL/D3D12 runtime, while Xbox One can use a separate MobileGlues/ANGLE-style runtime when packaged.

## Current State

The project is usable for active launcher development and pre-release testing:

- Minecraft 1.21.11 with Fabric Loader 0.19.2 boots in Xbox Developer Mode.
- Dynamic Microsoft device-code auth works and persists the session in the UWP Credential Locker.
- The launcher shows a signed-in menu with Play, Mods, Repair downloads, and Sign out.
- The Mods page can show installed user mods, browse popular/latest Fabric mods from Modrinth, and install matching Modrinth jars into `LocalState\game\user-mods`.
- Sign out clears the saved Microsoft refresh token and returns to the device-code sign-in flow.
- The host skips the LocalState runtime copy on warm launches when the installed package has not changed.
- The APPX no longer bundles Mojang libraries, Minecraft client jars, asset indexes, or asset objects. It verifies Java ownership, then downloads official files into `LocalState` from the generated `download_manifest.tsv`.
- Runtime downloads use a manifest marker in `LocalState`; changing the packaged manifest cleans old downloaded official files before re-downloading.
- Menus render through Mesa on the Xbox GPU.
- Keyboard and basic text input work.
- Single player reaches gameplay.
- Multiplayer and skin loading have recent fixes.
- Xbox controller input is exposed through GameInput in the GLFW shim.
- The app includes generated UWP tile assets from the checked in icon.
- The launcher menu can display packaged panorama assets from `MC.Xbox\Assets\panorama`.
- The host seeds launcher-owned support files into `LocalState` when needed.

Expect rough edges. This is still a development launcher, not a finished release product.

## Nightly And Pre-Release Builds

Nightly builds are highly experimental snapshots of current development work. They are not full game releases, and support is not provided for nightly builds.

The project is currently pre-released because it is in a playable state, but it is not ready for a main release. Xbox Series consoles are the primary target right now. Xbox One support is spotty at best.

After installing a pre-release or nightly build, open the Mods page, go to the Recommended section, and install at least:

- Sodium
- Controlify

These are strongly recommended for performance and controller support.

Redistribution of the APPX package is not permitted without prior written permission, as described in [LICENSE](LICENSE). Video tutorials or other public install guides for nightly or pre-release builds are not permitted until the full release.

## Repo Layout

- `MC.Xbox/`
  UWP host and launcher app. It handles Microsoft auth, the launcher menu, runtime preparation, JVM startup, `CoreWindow` publishing for EGL, and Fabric launch.
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

## Build Overview

Read [docs/BUILDING.md](docs/BUILDING.md) for the complete setup. The short version is:

```powershell
.\scripts\download-libs.ps1
java -jar .\staging\cache\tools\fabric-installer.jar client -dir .\staging\cache\gameDir -mcversion 1.21.11 -loader 0.19.2 -launcher win32 -noprofile
.\build.ps1
```

The build script compiles the UWP host, builds the GLFW shim, builds the compatibility mod, patches the local Fabric Loader JAR, generates an official download manifest, copies only launcher-owned runtime files, creates UWP tile assets, then signs `output\BanditLauncher_1.0.0.0.appx`.

Generated files live under `staging` and `output`. They are ignored by git.

## Local Inputs

You need to provide your own legal game and runtime inputs. This repo does not include:

- Minecraft game files. Build-time cache files are local only; runtime game files are downloaded by the installed app after ownership verification.
- Mojang asset objects.
- Fabric installer JAR.
- A JRE image committed to git.
- Signed app packages.
- Local signing certificates.
- Saves, logs, or local debug output.

The Mesa UWP runtime DLLs needed by the build are tracked in `mesa-runtime/`.

## How It Works

1. `MC.Xbox.exe` starts as a UWP app.
2. The launcher checks for a saved Microsoft refresh token.
3. If needed, it shows a device-code sign-in screen with a QR code for `microsoft.com/link`.
4. The launcher verifies Xbox/Minecraft Services ownership and loads the Minecraft profile.
5. The signed-in menu opens with Play, Mods, Repair downloads, and Sign out.
6. Play prepares launcher-owned files in `LocalState` only when the packaged runtime has changed.
7. The app verifies all files from `download_manifest.tsv`, downloading missing or stale official Minecraft/Fabric files into `LocalState`.
8. The app publishes the live `CoreWindow` through app properties.
9. The app loads `jvm.dll` and starts Java in the same process.
10. Fabric launches Minecraft from the embedded JVM.
11. LWJGL loads the custom `glfw.dll`.
12. The GLFW shim creates an EGL surface for the UWP window.
13. Mesa translates OpenGL calls to D3D12.

The main launcher work is around Microsoft auth, ownership verification, UWP package identity, Xbox sandbox paths, packaged app file access, native library loading, GLFW behavior, input, and Fabric remapping.

## Status And Limits

Known limits include:

- Xbox Developer Mode is the only supported target.
- Retail mode is not supported.
- Xbox Series consoles are the primary target. Xbox One support is experimental and currently unreliable.
- The Mods page is an early Modrinth integration. It installs Fabric mods for the configured Minecraft version, but broad mod compatibility still depends on each mod working inside the Xbox/UWP runtime.
- Sodium and Controlify should be installed from the Recommended section for the best current pre-release experience.
- First launch can take a while because official game libraries and asset objects are downloaded after sign-in. Missing/stale files are downloaded with limited parallelism.
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

## License And Ownership

Original project code in this repository is available under the custom terms in [LICENSE](LICENSE). Use is allowed with credit. Redistribution requires prior written permission from veroxsity / BanditVault.

Redistribution of generated APPX packages is not permitted without prior written permission. Public install tutorials or videos for nightly or pre-release builds are not permitted until the full release.

Minecraft, Fabric, Mojang assets, Mesa, LWJGL, Java, and other third-party components remain under their own licenses and terms.
