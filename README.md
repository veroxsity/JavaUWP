# Bandit Launcher

![Pre-release](https://img.shields.io/badge/status-pre%20release-2ea44f)
![Xbox Series X|S](https://img.shields.io/badge/platform-Xbox%20Series%20X%7CS-107c10)
![Minecraft Java Edition](https://img.shields.io/badge/Minecraft-Java%20Edition-62b47a)

Bandit Launcher runs **Minecraft Java Edition directly on Xbox Series X|S in Developer Mode**, with Fabric, Forge and NeoForge. It signs in through Microsoft, verifies Java Edition ownership and downloads the official game files. Website and downloads at [launcher.banditvault.co.uk](https://launcher.banditvault.co.uk/).

> **Xbox One, Xbox One S and Xbox One X are not supported.** Both launcher builds are for Xbox Series S and Xbox Series X only.

[Website](https://launcher.banditvault.co.uk/) ·
[Download](#download) · [Getting started](#getting-started) · [Minecraft versions](#minecraft-versions) · [Discord](https://discord.gg/XTFYUKbG7g) · [Video guide](https://youtu.be/ATHiVaBenY8)

## Requirements

- **Xbox Series S or Xbox Series X** with Developer Mode enabled.
- A Microsoft account with a valid Minecraft Java Edition entitlement.
- An internet connection for sign-in and downloads.
- Free console storage for game files, profiles, mods and worlds.

Java and controller support are included in the launcher package for the targets that use them. No separate Java installation is needed on the console. Bandit Launcher requires Microsoft authentication and ownership verification. It does not support cracked or offline accounts.

## Download

| Build | Download | Mouse input |
| --- | --- | --- |
| Nightly | [Main nightly release](https://github.com/veroxsity/JavaUWP/releases/tag/nightly) | Optional mouse relay from a phone or PC on the same network. |
| Native mouse | [Native mouse nightly release](https://github.com/veroxsity/JavaUWP/releases/tag/nightly-native) | Direct USB mouse input in launcher menus and Minecraft. |

This branch builds the **native mouse package**. Download the `.appx` from the chosen release. For the native mouse build, also use the matching public `.cer` certificate from that release. Both releases include a `.sha256` file for checking the package download.

These are pre-release testing builds. Nightly builds are experimental and do not come with support. Read the release notes before installing.

**The native mouse build installs as a separate app and has its own saves, profiles and settings.** Switching between builds does not transfer data automatically. Export important worlds through Remote Files before changing builds or removing an app.

## Getting started

1. Install the signed APPX through Xbox Device Portal while the console is in Developer Mode.
2. Open **Bandit Launcher**.
3. When the Microsoft device code appears, open [microsoft.com/link](https://www.microsoft.com/link) on another device. Enter the code and sign in with the account that has Java Edition.
4. Wait for the launcher to verify the account and Minecraft entitlement.
5. Choose or create a profile, then select its Minecraft version and loader. The default is **Minecraft 1.21.11 with Fabric**.
6. Press **Play**. The first launch downloads the game, libraries, assets and loader files for that target. Allow time for this to finish.

Use the **Mods** page to manage profiles and install content. Start **Remote Files** to upload worlds, edit configs or download logs from another device.

## Features

- Separate profiles for different Minecraft versions, loaders and mod setups.
- Modrinth and CurseForge browsing for mods, modpacks, resource packs and shaders.
- Recommended mods filtered for the selected launch target.
- Bundled Bandit Controller support for targets that list a controller provider.
- Profile backups and restore options, including undoing the last deleted profile.
- Remote Files for managing profiles and worlds through a browser on the local network.
- Download repair, current and previous run logs, and crash report ZIPs.
- Optional crash and usage reporting, controlled from launcher Settings.

## Minecraft versions

The default target is **1.21.11 + Fabric 0.19.5**, using Java 21. The catalog also includes these targets.

| Minecraft version | Loaders in the catalog | Status |
| --- | --- | --- |
| `26.1`, `26.1.1`, `26.1.2`, `26.2` | Fabric, Forge, NeoForge | Experimental. Uses the Java 25 runtime. |
| `1.21` through `1.21.11` | Fabric and NeoForge. Forge on every listed version except `1.21.2`. | Experimental. Uses Java 21. |
| `1.20.4` | Fabric, NeoForge | Fabric is testing. NeoForge is experimental and has no bundled controller provider. |
| `1.20.1` | Fabric, Forge | Fabric is testing. Forge is experimental. |
| `1.19.2` | Fabric | Testing. |
| `1.18.2` | Forge | Experimental. No bundled controller provider. |
| `1.16.5` | Fabric | Testing. |
| `1.12.2` | Forge | Experimental. Forge runs through Cleanroom, and the controller works the same as on newer versions. Vanilla 1.12.2 is not supported. |

Fabric uses **0.19.5** on listed targets from `1.20.1` onward, and **0.14.25** on `1.19.2` and `1.16.5`. Exact Forge and NeoForge versions, runtime choices and controller providers are listed in [the version catalog](config/versions.tsv).

A catalog entry is not proof that the game or every mod works on Xbox. A target needs its launch files and bundled support files in the installed package. Experimental targets still need testing, and available targets can differ between builds. Check the installed launcher's target list and the release notes.

## Mods, modpacks and other content

Choose the profile and launch target before installing content. Mods must match both the Minecraft version and loader, and must work inside the Xbox UWP sandbox.

The Mods page includes **Popular**, **Latest**, **Recommended**, **Modpacks**, **Resource packs** and **Shaders** tabs. Switch between Modrinth and CurseForge using the source selector or launcher Settings.

**CurseForge needs a personal API key.** Start Remote Files and add it under **Settings**, then select CurseForge in the launcher. Modrinth does not need a key.

Some CurseForge authors block downloads by third-party launchers. If a pack needs those files, Remote Files shows a **Manual downloads** panel with links and an upload option. Finish those downloads before trying to play the pack.

Shader packs need a compatible shader loader such as Iris or Oculus in the same profile. Installing a shader pack does not guarantee it will render correctly or run well on Xbox.

## Controller and mouse

Bandit Controller is included for the catalog targets that have a controller provider. Press **View/Back** in game to open its settings. Controller availability for each target is listed in [the version catalog](config/versions.tsv).

To use another controller mod, turn **Bandit Controller** off for that profile before installing it.

- **Main nightly** uses the optional mouse relay. A browser page is available from the launcher. Companion downloads for Windows, Android and iOS are on the [Mouse Relay release](https://github.com/veroxsity/JavaUWP/releases/tag/mouse-relay-nightly).
- **Native mouse nightly** accepts a USB mouse directly. Launcher Settings has separate menu and game mouse sensitivity controls.

## Remote Files

Start **Remote Files** in the launcher, then open the displayed URL and PIN on a phone or computer on the same network. Pick the profile to manage before uploading or changing files.

Remote Files can:

- Browse, upload, download, rename and delete profile files and create folders.
- Edit text files such as configs in the browser.
- Import and export world saves as `.zip` files.
- Upload mods, resource packs and world datapacks.
- Import Modrinth `.mrpack` and CurseForge `.zip` modpacks.
- Export a mod profile as a Modrinth-compatible `.mrpack` for use on PC.
- Download logs and crash report ZIPs.
- Set the CurseForge API key and complete manual pack downloads.

Remote Files uses plain HTTP and a PIN on the local network. Keep the URL private, use a trusted network and stop the server when finished. Do not expose it to the internet. See [Privacy](docs/PRIVACY.md) for details.

## Saves and updates

Each profile has its own worlds, mods, resource packs, configs and logs. Downloaded game libraries and assets are shared between profiles within the same installed app.

Updating the same app package keeps its normal profile data. **Back up worlds before uninstalling or moving to another build.** Removing the app can remove its local data, and the main and native mouse packages do not share storage.

Use Remote Files to export worlds. The Profiles page also has profile backup and restore options.

## Known limitations and troubleshooting

- **Xbox One consoles are not supported.**
- Forge, NeoForge and modern targets are experimental. Mod and shader compatibility varies by version and build.
- Large modpacks can exceed the console app's available memory. A successful download does not mean the pack will run.
- First launch and large pack imports can take several minutes.
- Some desktop mods depend on native libraries or platform features that the Xbox sandbox does not provide.

If sign-in fails, check that the Microsoft account has Java Edition and a Minecraft Java profile. If required game files are missing, use the launcher's download repair option.

For a game or mod crash, download the crash report ZIP and the profile's `latest.log` through Remote Files. Include the launcher build, Minecraft version, loader and modpack or mod names when describing the problem. Remove private information before sharing files.

Crash and usage reporting is off until enabled. It can be changed in launcher Settings and is not required to use the launcher. See [Privacy](docs/PRIVACY.md) for what is sent and how it is stored.

## Project docs

- [Building](docs/BUILDING.md)
- [Compatible mods](docs/COMPATIBLE-MODS.md)
- [Architecture](docs/ARCHITECTURE.md)
- [Patching notes](docs/PATCHING.md)
- [Privacy](docs/PRIVACY.md)
- [Contributing](CONTRIBUTING.md)
- [Translating](docs/TRANSLATING.md)
- [Third-party components](docs/THIRD-PARTY.md)

## License and creator content

Original project code uses the custom terms in [LICENSE](LICENSE). Private use and private modifications are allowed under those terms. Public forks, mirrors, source redistribution and packaged builds require prior written permission from veroxsity / BanditVault.

Videos, streams, screenshots, reviews, benchmarks and tutorials are allowed when they credit veroxsity / BanditVault and visibly link to the official project or another official BanditVault page. They must follow the license's redistribution, authentication and endorsement rules.

Redistributing APPX packages, including nightly builds, requires prior written permission. Removing or bypassing Microsoft/Xbox authentication or Minecraft ownership checks is not permitted.

Bandit Launcher does not bundle Minecraft game files. Minecraft and all third-party components remain under their own licenses and terms. See [Legal notes](docs/LEGAL.md) for more detail.
