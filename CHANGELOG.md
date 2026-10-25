# Changelog

Notable changes to Bandit Launcher. Nightly packages are numbered by build revision rather than by release, so entries here are dated.

## 2026-10-25

### Added

**Latin American Spanish is picked automatically.** A Spanish console set to Mexico, Argentina, Colombia or another Latin American country uses a language file named `es-419` if there is one, and plain `es` otherwise.

**Translation files are checked when they are sent in.** A file that is not valid, has no name, or changes a `{0}` is flagged on the pull request. The same check runs locally with `scripts/update-translations.ps1 -Check`.

## 2026-10-24

### Added

**The launcher can be translated.** Settings has a new Language option, and each language is a plain text file in the `lang` folder. Anything a file does not translate stays in English. Every screen and message is covered, from sign in errors to pack imports, and the Remote Files page follows the chosen language too. The first time it runs, the launcher picks a language that matches the console's setting if one is available. No translations ship yet. See `docs/TRANSLATING.md` for how to add one, including how to try a file from Remote Files, which has a new Languages folder.

## 2026-10-23

### Added

**The launcher tells you when a new version is out.** When it opens, it checks GitHub for the newest version on the channel you installed. If there is a newer one, a notice shows what changed and a QR code for the release page. You can skip that version, or be reminded next time. Updating is still done from a PC, so nothing installs by itself. The check can be turned off in Settings.

**A What's new page on the main menu.** It shows the changelog without leaving the launcher. It reads the latest copy from GitHub, and falls back to the one that came with your version when you are offline.

## 2026-09-04

### Security

**The remote file server printed its access PIN into the log.** The file browser generates a six digit PIN per session, and that PIN was being written to the launch log, which is collected into crash zips. Anyone holding a crash zip who could also reach the console on the network while that same session was still running could have used it. The PIN is no longer logged, there is now a retry limit and a lockout, and it no longer falls back to a predictable value when the system random source is unavailable. The PIN is generated fresh each session, so there is nothing to change.

**Downloaded files were not always verified.** When a hash lookup failed, the error was swallowed and a blank hash was written into the download manifest, and a blank hash matched anything. Hash failures now stop the build instead.

**JAR signature checking was switched off in every shipped build.** A development setting disabling all signature algorithms had been left in the packaged Java configuration. Signature verification is back on.

**Authentication responses were parsed by scanning for text rather than by reading JSON.** The old scanner had no understanding of nesting or scope, and the entitlement check was a plain substring search. Both now parse properly.

**Raw server responses no longer end up in error messages.** Authentication failures were concatenating whole HTTP bodies into text that reached the log and then the crash zip, which could carry session material.

**The signing certificate password is no longer stored in plaintext**, and the build no longer picks an arbitrary code signing certificate from your store when the expected one is missing.

### Fixed

**1.21.4 could not launch at all.** It failed on startup with a duplicate ASM error, before a single mod loaded. Minecraft declares its own copy of ASM on 1.21.2 through 1.21.10, Fabric Loader declares a different one, and the launcher put both on the classpath. 1.21.4 was the only version in that range the launcher offered, so it was the only one anyone could run into. It now starts. The fix covers the whole range, for whenever the other versions are added.

**The version catalogue no longer lists targets with no controller support.** Sixteen entries were removed. They downloaded their full runtime during a build and had no controller variant behind them. Each will return as it gains real support.

**1.21.4 dropped to the idle framerate while playing with a controller.** Minecraft 1.21.2 added a framerate limiter that watches for keyboard and mouse activity. Controller input never reached it, so the game throttled itself mid-play. Already fixed on 1.21.11, now fixed on 1.21.4.

**The mod browser's target list ran off the bottom of the screen** once the catalogue grew past a handful of versions.

**Version numbers sorted incorrectly.** A prerelease such as `1.0-alpha` ranked above `1.0`, which could pick the wrong Forge artifact.

**A failed sign in and a first run were indistinguishable**, because a real credential store failure was being reported as an empty token.

**Bundled mods could be skipped** for a non default target when no version specific folder existed.

### Controller support on 1.20.1, 1.20.4, 1.21.1 and 1.21.4

These four shipped a reduced version of the controller mod. Everything below already worked on 1.21.11 and now works on all of them.

- **Radial menu.** Hold D-Pad Up, point with the right stick, release to run the bound action. Eight slots, each assignable to any Minecraft key binding.
- **On-screen button guide.** Context hints in menus and in the world, showing what each button does for whatever you are looking at.
- **Creative inventory.** The cursor now works on the creative screen, which it did not before.
- **Controller text entry.** The Xbox keyboard opens by itself for signs, chat, world names and server addresses, and typing updates the field as you go.
- **Full settings screen.** A Radial tab for assigning slots, and the Controls tab now runs past the controller actions into Minecraft's own key bindings, so any key can be placed on a controller input.
- **Swap Hands and the menu secondary action** were implemented but hidden. They can now be seen and rebound.
- **Analog movement.** The left stick was on or off. It is now proportional on every target.
- **Menu navigation** uses Minecraft's own navigation rather than simulated arrow keys, so focus moves correctly and no longer sticks inside a text box.
- Closing the on-screen keyboard no longer takes two button presses, and menu overlays no longer draw underneath the list behind them.

### Performance

**Logging was costing frame time.** Every log line was opening and closing its file, across four separate logger implementations. Those are now one shared logger that keeps the handle open.

**Garbage collection pauses and log churn during play have been cut**, and the watchdog no longer dumps every thread on a routine tick.

**The Mesa runtime folder was being looked up repeatedly at startup** and falling back to a legacy path each time.

### Known issue

Typing into the creative inventory search box with the controller keyboard closes the game on 1.20.1, 1.20.4 and 1.21.1. The on-screen keyboard is disabled for that one box on those three versions until the cause is found. Every other text field works, and 1.21.4 and newer are unaffected.

### Building from source

**Setup is one command.** `scripts\setup.ps1` now fetches the Mojang and Fabric metadata, client libraries, native DLLs, the Fabric installer and the asset index, then installs and patches the loader. Of the twelve prerequisites the docs used to list, only the Forge installer still has to be placed by hand, and only if you are building Forge.

**Builds are incremental.** The compatibility mod, the controller mod and the loader patches no longer wipe their output and rebuild from scratch every run. Mojang's version manifest was being fetched twenty nine times per build and is now fetched once and cached.

**Three duplicated logger classes, two duplicated settings classes and several copy pasted helpers** were replaced with shared modules.

**The GLFW shim compiles at a real warning level** rather than the lowest MSVC offers, and at the C++ standard it was written for.

**The Python asset generation step is gone.** It produced five static images from an icon that does not change, and needed a Python install with a hardcoded path.

**The controller mod build now fails if a target falls behind the newest one**, so features cannot silently go missing from a version again. An incremental build fault that skipped recompiling edited controller sources has also been fixed.

### Documentation

- `BUILDING.md` rewritten around the setup script, with the manual steps kept as an appendix for machines that cannot reach Mojang and Fabric.
- Minecraft `26.2` is now documented. It needs JDK 25, ships unobfuscated with no Fabric intermediary, and its mixins target Mojang names rather than intermediary ones.
- `THIRD-PARTY.md` added, listing every third party component with its upstream, version and licence. Vendored code now carries attribution.
- `COMPATIBLE MODS.md` renamed to `COMPATIBLE-MODS.md` so the filename has no space in it.
- Em-dashes, BOMs and trailing whitespace cleaned out, and `@author` tags corrected on three shipped mixins.

### Removed

Around 4 MB of unreferenced code and 10 MB of unused assets, including a stale patch file that no longer applied, duplicate splash screens, and several functions with no callers.
