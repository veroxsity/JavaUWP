# Bandit Launcher

![Pre release](https://img.shields.io/badge/status-pre%20release-2ea44f)
![Xbox Developer Mode](https://img.shields.io/badge/platform-Xbox%20Developer%20Mode-107c10)
![UWP](https://img.shields.io/badge/app-UWP-0078d4)
![Minecraft Java](https://img.shields.io/badge/Minecraft-Java%20Edition-62b47a)
![Fabric](https://img.shields.io/badge/loader-Fabric-f6c344)

Bandit Launcher is a Minecraft Java Edition launcher for Xbox Developer Mode. It packages a UWP host, a custom GLFW shim, a Java runtime, Fabric support, and an Xbox compatibility mod so Java Edition can run inside the Xbox app sandbox.

This is a real pre release. The launcher is playable, supports multiple Minecraft targets, can install compatible Fabric mods from Modrinth, and includes active fixes for Xbox input, graphics, Java, and filesystem behavior.

## Pre Release Status

Bandit Launcher is built for testers who are comfortable with Xbox Developer Mode and early software. Current builds are meant to be used, tested, and improved, but they are not the final public release yet.

Xbox Series consoles are the primary target. Xbox One support is still experimental.

## Supported Targets

The launcher now treats a playable setup as a launch target:

```text
minecraft version + loader + loader version
```

Current Fabric targets:

| Target | State | Notes |
| --- | --- | --- |
| `1.21.11 + Fabric 0.19.2` | Supported | Current default target. Base game and Controlify have been tested. |
| `1.21.1 + Fabric 0.19.2` | Testing | Base game, Controlify, and Cobblemon have been verified. Uses Java 21 for mods that require it. |
| `1.20.4 + Fabric 0.19.2` | Testing | Base game and Controlify have been tested. Uses Java 21. |
| `1.20.1 + Fabric 0.19.2` | Testing | Included for nearby 1.20.x mod support. Still needs broader testing. |
| `1.19.2 + Fabric 0.14.25` | Testing | Base game and controller support have been tested. Uses Java 21. |
| `1.16.5 + Fabric 0.14.25` | Testing | Legacy target under active validation. Uses Java 21 and the built in controller layer. |

Targets for NeoForge, Forge, and older vanilla versions may appear in the catalog, but their launch providers are not finished yet.

## Features

- Microsoft sign in with device code flow.
- Minecraft Java ownership verification before runtime downloads.
- Dynamic official Minecraft, asset, library, and Fabric downloads into UWP `LocalState`.
- Multiple Minecraft launch targets without rebuilding the APPX.
- Profile targets, so each profile can carry its own Minecraft version and loader.
- Persistent isolated profile storage under UWP `LocalState`.
- Modrinth browsing and install support for the selected target.
- Remote file manager over your local network for uploads and log downloads.
- Browser based mod, resource pack, and datapack uploads to the active profile.
- Current and previous run logs, plus packaged crash report zips.
- Per version Fabric loader support.
- Java 21 runtime support for mods that pin or require Java 21.
- Packaged Java 25 runtime for the current default target.
- Custom GLFW shim for UWP windowing, input, gamepad state, and EGL.
- Mesa based graphics path for Xbox Series consoles.
- Separate Xbox One graphics runtime path when packaged.
- Built in Xbox compatibility mod with Minecraft and mod compatibility fixes.
- GameInput based controller support.
- Legacy controller layer for older Fabric targets where modern controller mods are not available.
- Launcher owned repair flow for runtime downloads.
- UWP tile assets and packaged menu panorama assets.

## Recommended Mods

For the best current experience, install recommended mods from the Mods page after signing in.

For modern targets:

- Sodium
- Controlify

For legacy targets such as `1.19.2` and `1.16.5`, Bandit Launcher also includes its own controller support layer. Sodium can still help performance, but older Sodium versions may need launcher compatibility settings that are seeded automatically.

Cobblemon has been verified on `1.21.1 + Fabric 0.19.2` using the Java 21 runtime.

## Remote Files

The launcher includes a remote file manager for devices on the same local network. Start it from the launcher, open the shown URL and PIN in a browser, then manage files without using Xbox Device Portal.

Remote Files can:

- Upload `.jar` mods to the active profile.
- Upload `.zip` resource packs to the active profile.
- Upload `.zip` datapacks into a selected world under the active profile.
- Browse active profile files, saves, mods, resource packs, logs, crash reports, and the shared runtime cache.
- Download current logs, previous run logs, and crash report zips for debugging.

Uploads are scoped to known safe folders. Remote Files does not provide arbitrary write access to all of `LocalState`.

## Persistent Storage

The installed app keeps downloaded runtime files, profiles, saves, configs, mods, resource packs, datapacks, logs, and crash reports in UWP `LocalState`. Updating or reinstalling the APPX should not wipe normal profile data.

Profile data is isolated by profile:

```text
LocalState
  profiles
    <profile-id>
      game
        mods
        resourcepacks
        saves
          <world>
            datapacks
        config
        screenshots
        logs
```

Shared downloaded runtime data is kept separately:

```text
LocalState
  game
    libraries
    versions
  assets
```

Launcher diagnostics are organized separately from game data:

```text
LocalState
  logs
    current
  logs_previous
  crash-reports
  state
```

`logs/current` is the latest launcher run. On the next launcher start, those files move to `logs_previous` before new logs are written. Crash report zips collect launcher logs, previous logs, profile game logs, and Minecraft crash files when possible.

## Known Issues

These are the main areas still being worked on for the pre release:

- Xbox One support is experimental.
- Forge and NeoForge targets are cataloged but not launchable yet.
- Mod compatibility depends on each mod working inside the Xbox UWP sandbox.
- First launch can take a while because official files need to download.
- Chunk loading can still be choppy on some older versions.
- Some Java diagnostics may warn because the sandbox does not look like desktop Windows.
- Legacy graphics and controller support are still being tuned.

## Build From Source

Read [docs/BUILDING.md](docs/BUILDING.md) for full setup notes. The short version is:

```powershell
.\scripts\download-libs.ps1
java -jar .\staging\cache\tools\fabric-installer.jar client -dir .\staging\cache\gameDir -mcversion 1.21.11 -loader 0.19.2 -launcher win32 -noprofile
.\build.ps1
```

The build script compiles the UWP host, builds the GLFW shim, builds the compatibility mod, patches Fabric Loader, generates runtime download manifests, copies launcher owned runtime files, creates UWP assets, packages the APPX, and signs it.

Generated build output goes to `staging` and `output`. These folders are ignored by git.

## Repo Layout

| Path | Purpose |
| --- | --- |
| `MC.Xbox/` | UWP host, launcher UI, Microsoft auth, runtime preparation, JVM startup, and launch logic. |
| `glfw_shim/` | Replacement `glfw.dll` that maps LWJGL window, input, gamepad, and EGL behavior onto UWP. |
| `compat_mod/` | Fabric compatibility mod for Minecraft, mod, controller, filesystem, and graphics fixes. |
| `patch/` | Patched Fabric Loader classes used by the build. |
| `scripts/` | Setup, cleanup, asset, patch, manifest, and build helpers. |
| `mesa-runtime/` | Mesa UWP runtime DLLs used by local builds. |
| `build.ps1` | Main APPX build script. |
| `docs/` | Build, patching, and legal notes. |

## Local Inputs

This repo does not include Minecraft game files or generated app packages. You need to provide your own legal inputs for local builds.

Not included:

- Minecraft client jars, libraries, assets, or asset objects.
- Fabric installer JAR.
- Signed APPX packages.
- Local signing certificates.
- Saves, logs, or local debug output.

Runtime game files are downloaded by the installed app after ownership verification. User profile data, uploaded mods, resource packs, datapacks, saves, configs, logs, and crash reports are written to UWP `LocalState`.

## How It Works

1. `MC.Xbox.exe` starts as a UWP app.
2. The launcher checks for a saved Microsoft refresh token.
3. If needed, it shows a device code sign in screen for `microsoft.com/link`.
4. The launcher verifies Xbox and Minecraft Services ownership.
5. The signed in menu opens with Play, Mods, repair downloads, and sign out.
6. The selected profile chooses the launch target.
7. The app verifies and downloads official files for that target into `LocalState`.
8. The app seeds launcher owned support files and compatibility mods.
9. The app uses the selected profile's isolated game folder for saves, mods, resource packs, config, and logs.
10. The app publishes the live UWP `CoreWindow` for EGL.
11. The app loads `jvm.dll` and starts Java in the same process.
12. Fabric launches Minecraft.
13. LWJGL loads the custom `glfw.dll`.
14. Mesa translates OpenGL calls to D3D12 on the Xbox graphics path.
15. Remote Files can be started from the launcher to upload profile files or download diagnostics from another device.

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

Original project code in this repository is available under the custom terms in [LICENSE](LICENSE). Private use is allowed with credit. Public forks, redistribution, mirrors, modified public copies, and packaged builds require prior written permission from veroxsity / BanditVault.

Redistribution of generated APPX packages, including nightly and pre release packages, is not permitted without prior written permission. Public install tutorials or videos for nightly or pre release builds are not permitted until the full release.

Removing, bypassing, disabling, stubbing, faking, or making optional Microsoft/Xbox authentication or Minecraft entitlement checks is not permitted.

Minecraft, Fabric, Mojang assets, Mesa, LWJGL, Java, and other external components remain under their own licenses and terms.
