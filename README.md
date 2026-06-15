# Bandit Launcher

![Pre release](https://img.shields.io/badge/status-pre%20release-2ea44f)
![Xbox Developer Mode](https://img.shields.io/badge/platform-Xbox%20Developer%20Mode-107c10)
![UWP](https://img.shields.io/badge/app-UWP-0078d4)
![Minecraft Java](https://img.shields.io/badge/Minecraft-Java%20Edition-62b47a)
![Fabric](https://img.shields.io/badge/loader-Fabric-f6c344)
![NeoForge](https://img.shields.io/badge/loader-NeoForge-f16436)
![Forge](https://img.shields.io/badge/loader-Forge-e04e39)

Bandit Launcher brings **Minecraft Java Edition** to **Xbox Developer Mode**. It is a UWP app that signs you in with your Microsoft account, verifies that you own Java Edition, downloads the official game files you need, and launches Minecraft with Fabric, Forge, or NeoForge inside the Xbox app sandbox

This is a real pre release build. The launcher is playable, supports multiple Minecraft versions and loaders, can install compatible mods and modpacks from Modrinth, and includes active fixes for Xbox input, graphics, Java, and filesystem behavior.

## Who This Is For

Bandit Launcher is aimed at people who already use **Xbox Developer Mode** and want to run **Minecraft Java Edition** on their console with legitimate Microsoft sign in.

You should be comfortable with:

- Installing and sideloading UWP apps in Developer Mode.
- Waiting through a first launch download of official Minecraft files.
- Early software that may change between builds.

**Xbox Series** consoles are the primary target. **Xbox One** support is still experimental.

## Requirements

- An Xbox console in **Developer Mode**.
- A **Microsoft account that owns Minecraft Java Edition**.
- A network connection for sign in and for downloading official game files on first launch (and when adding new versions).
- Enough free storage on the console for Minecraft libraries, assets, profiles, mods, and saves.

Bandit Launcher does not provide offline play, cracked accounts, or alternate launch paths that skip Microsoft authentication or ownership checks.

## Getting Bandit Launcher

Pre release packages are built from this repository. If you are building or testing with the project, see [docs/BUILDING.md](docs/BUILDING.md) for the full setup guide.

Automated **nightly** packages are published to the [nightly release](https://github.com/veroxsity/JavaUWP/releases/tag/nightly) when relevant source changes land on `main`. These are testing builds, not a final public release.

**Important:** Redistribution of generated APPX packages, including nightly builds, is not permitted without prior written permission. Videos, streams, screenshots, reviews, benchmarks, and tutorials are allowed under the creator rules in [LICENSE](LICENSE) and [docs/LEGAL.md](docs/LEGAL.md).

## First Launch

1. Install the signed APPX on your Xbox in Developer Mode.
2. Open **Bandit Launcher**.
3. If you are not signed in yet, the launcher shows a **Microsoft device code** screen. On another device, open [microsoft.com/link](https://www.microsoft.com/link), enter the code, and sign in with the account that owns Minecraft Java Edition.
4. After sign in, the launcher verifies Xbox and Minecraft Services ownership.
5. Choose or create a **profile** and pick a **launch target** (Minecraft version + loader).
6. On first play for that target, the launcher downloads official Minecraft, loader, and asset files into the app's storage. This can take a while.
7. Press **Play**. Minecraft starts inside the same UWP process using the packaged Java runtime and graphics stack.

From the main menu you can also browse mods, repair downloads, start **Remote Files**, view logs, and sign out.

## Pre Release Status

Bandit Launcher is built for testers who are comfortable with Xbox Developer Mode and early software. Current builds are meant to be used, tested, and improved, but they are not the final public release yet.

## Supported Targets

A launch target is a playable combination of:

```text
minecraft version + loader + loader version
```

| Target | State | Notes |
| --- | --- | --- |
| `1.21.11 + Fabric 0.19.2` | Supported | Current default target. Base game and Controlify have been tested. |
| `1.21.1 + Fabric 0.19.2` | Testing | Base game, Controlify, and Cobblemon have been verified. Uses Java 21 for mods that require it. |
| `1.21.1 + NeoForge 21.1.233` | Experimental | Base game, Controlify, Sodium, JEI, and Modrinth modpack installs have been tested. Uses Java 21. |
| `1.20.4 + Fabric 0.19.2` | Testing | Base game and Controlify have been tested. Uses Java 21. |
| `1.20.1 + Fabric 0.19.2` | Testing | Bundled Bandit controller layer. Uses Java 21. Still needs broader testing. |
| `1.20.1 + Forge 47.4.20` | Experimental | Initial Forge provider with bundled controller mod. Uses Java 21. |
| `1.19.2 + Fabric 0.14.25` | Testing | Base game and controller support have been tested. Uses Java 21. |
| `1.16.5 + Fabric 0.14.25` | Testing | Legacy target under active validation. Uses Java 21 and the built in controller layer. |

Other catalog entries (additional Forge versions, older vanilla targets, and future loaders) may appear in the launcher before their launch providers are finished. Fabric is the most mature path today; NeoForge and Forge 1.20.1 are experimental.

## Features

**Account and launch**

- Microsoft sign in with device code flow.
- Minecraft Java ownership verification before runtime downloads.
- Multiple Minecraft launch targets without rebuilding the APPX.
- Profile targets, so each profile can carry its own Minecraft version and loader.
- Launcher owned repair flow for runtime downloads.

**Downloads and storage**

- Dynamic official Minecraft, asset, library, Fabric, Forge, and NeoForge downloads into UWP `LocalState`.
- Persistent isolated profile storage under UWP `LocalState`.
- Packaged Java 25 runtime for the current default target.
- Java 21 runtime support for mods and targets that require it.

**Mods and content**

- Modrinth browsing and install support for the selected target.
- Browser based mod, resource pack, and datapack uploads to the active profile.
- Per version Fabric loader support.
- Initial NeoForge 1.21.1 launch provider support.
- Experimental Forge 1.20.1 launch provider support.
- Built in Xbox compatibility mod with Minecraft and mod compatibility fixes.
- Bundled Xbox controller mods for legacy Fabric targets and Forge 1.20.1.

**Xbox integration**

- Custom GLFW shim for UWP windowing, input, gamepad state, and EGL.
- Mesa based graphics path for Xbox Series consoles.
- Separate Xbox One graphics runtime path when packaged.
- GameInput based controller support through the GLFW shim.
- Bundled Bandit controller layer for `1.16.5`, `1.19.2`, and `1.20.1` Fabric, plus `1.20.1` Forge, where modern controller mods such as Controlify are not the default path.
- Shared controller settings at `config/bandit-controller.properties` (deadzones, look speed, toggle crouch/sprint).

**Diagnostics and file access**

- Remote file manager over your local network for uploads and log downloads.
- Current and previous run logs, plus packaged crash report zips.
- UWP tile assets and packaged menu panorama assets.

## Profiles

Each profile keeps its own game folder, mods, resource packs, saves, configs, and logs. Shared downloaded Minecraft libraries and assets are stored once and reused across profiles that need the same versions.

When you add a new profile, pick the Minecraft version and loader you want that profile to use. Switching profiles lets you keep separate mod setups or worlds without mixing files.

## Recommended Mods

For the best current experience, install recommended mods from the Mods page after signing in.

For modern Fabric targets:

- Sodium
- Controlify

For `1.21.1 + NeoForge 21.1.233`:

- Sodium
- Controlify
- JEI

For bundled-controller targets (`1.16.5`, `1.19.2`, and `1.20.1` Fabric, plus `1.20.1` Forge), Bandit Launcher includes its own controller support layer instead of Controlify. Press **View/Back** in-game to open Bandit Controller settings. Sodium can still help performance on Fabric, but older Sodium versions may need launcher compatibility settings that are seeded automatically.

Cobblemon has been verified on `1.21.1 + Fabric 0.19.2` using the Java 21 runtime.

## Remote Files

The launcher includes a remote file manager for devices on the same local network. Start it from the launcher, open the shown URL and PIN in a browser, then manage files without using Xbox Device Portal.

Remote Files can:

- Export and import full world saves as `.zip` files for the active profile.
- Upload `.jar` mods to the active profile.
- Upload `.zip` resource packs to the active profile.
- Upload `.zip` datapacks into a selected world under the active profile.
- Import Modrinth `.mrpack` files into the active profile.
- Export the active profile as a Modrinth-compatible `.mrpack` for use on PC.
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

## How It Works

At a high level, Bandit Launcher keeps the game inside one UWP process and adapts desktop Minecraft expectations to the Xbox sandbox:

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
12. The selected loader launches Minecraft.
13. LWJGL loads the custom `glfw.dll`.
14. Mesa translates OpenGL calls to D3D12 on the Xbox graphics path.
15. Remote Files can be started from the launcher to upload profile files or download diagnostics from another device.

## Known Issues

These are the main areas still being worked on for the pre release:

- Xbox One support is experimental.
- Forge support is experimental and currently focused on `1.20.1 + Forge 47.4.20`.
- Additional Forge versions and older vanilla targets are cataloged but not launchable yet.
- NeoForge support is new and currently focused on `1.21.1 + NeoForge 21.1.233`.
- Mod compatibility depends on each mod working inside the Xbox UWP sandbox.
- First launch can take a while because official files need to download.
- Chunk loading can still be choppy on some older versions.
- Some Java diagnostics may warn because the sandbox does not look like desktop Windows.
- Legacy graphics and controller support are still being tuned.

## Videos And Streaming

You may make public videos, streams, screenshots, reviews, benchmarks, tutorials, and other creator content about Bandit Launcher.

Creator content must:

- Credit veroxsity / BanditVault and include a visible link to the official project page or another official BanditVault page.
- Use legitimate Microsoft/Xbox sign in and Minecraft Java ownership verification.
- Avoid sharing generated APPX packages, private builds, modified builds, source archives, mirrors, or re uploads.
- Avoid instructions, patches, scripts, configuration, or links intended to bypass Microsoft/Xbox authentication, Minecraft ownership checks, token validation, account checks, or entitlement enforcement.
- Avoid implying endorsement by veroxsity / BanditVault, Microsoft, Mojang, Xbox, Minecraft, Fabric, Forge, NeoForge, LWJGL, Mesa, or other third parties unless permission has been given separately.

## For Developers

Detailed build, patching, architecture, and legal notes live in `docs/`.

- [Building](docs/BUILDING.md) — requirements, cache setup, packaging, nightly workflow.
- [Architecture](docs/ARCHITECTURE.md) — UWP host layout, launch flow, and loader modules.
- [Patching notes](docs/PATCHING.md) — why Fabric, GLFW, and sandbox patches exist.
- [Legal notes](docs/LEGAL.md) — licensing, redistribution, and nightly package rules.
- [Contributing](CONTRIBUTING.md) — auth policy and contribution expectations.

Quick local build from the repo root:

```powershell
.\scripts\download-libs.ps1
java -jar .\staging\cache\tools\fabric-installer.jar client -dir .\staging\cache\gameDir -mcversion 1.21.11 -loader 0.19.2 -launcher win32 -noprofile
.\build.ps1
```

Generated build output goes to `staging` and `output`. These folders are ignored by git.

To preview or apply cleanup:

```powershell
.\scripts\clean.ps1
.\scripts\clean.ps1 -Apply
```

## Repo Layout

| Path | Purpose |
| --- | --- |
| `MC.Xbox/` | UWP host app: sign in, launcher UI, downloads, profiles, mods, and JVM launch. |
| `glfw_shim/` | Replacement `glfw.dll` for UWP windowing, input, gamepad state, and EGL. |
| `compat_mod/` | Fabric compatibility mod for Minecraft, mod, controller, filesystem, and graphics fixes. |
| `forge_controller_mod/` | Forge 1.20.1 Xbox controller mod (built per Forge target under `runtime/version-mods/`). |
| `patch/` | Patched Fabric Loader and securejarhandler classes used by the build. |
| `scripts/` | Setup, cleanup, asset, patch, manifest, and build helpers. |
| `config/` | Launch target catalog used by the launcher and build. |
| `mesa-runtime/` | Mesa UWP runtime DLLs used by local builds. |
| `build.ps1` | Main APPX build script. |
| `docs/` | Build, architecture, patching, and legal notes. |

This repo does not include Minecraft game files, signed APPX packages, or local signing certificates. Runtime game files are downloaded by the installed app after ownership verification.

## License And Ownership

Original project code in this repository is available under the custom terms in [LICENSE](LICENSE). Private use is allowed with credit. Public forks, redistribution, mirrors, modified public copies, and packaged builds require prior written permission from veroxsity / BanditVault.

Videos, streams, screenshots, reviews, benchmarks, tutorials, and other creator content are allowed when they follow the attribution, redistribution, endorsement, and authentication rules in [LICENSE](LICENSE).

Redistribution of generated APPX packages, including nightly and pre release packages, is not permitted without prior written permission.

Removing, bypassing, disabling, stubbing, faking, or making optional Microsoft/Xbox authentication or Minecraft entitlement checks is not permitted.

Minecraft, Fabric, Mojang assets, Mesa, LWJGL, Java, and other external components remain under their own licenses and terms.
