# Version compatibility model

Bandit does not force one graphics/input/runtime stack onto every Minecraft version. A launch target is resolved into independent capabilities.

## Window backend

The launcher's download manifest is authoritative:

- a target containing `org/lwjgl/lwjgl-sdl` uses **SDL3 / WinRT / Mesa**;
- a target containing `org/lwjgl/lwjgl-glfw` uses **GLFW UWP shim / Mesa**;
- only an incomplete legacy manifest falls back to the historical version threshold.

This keeps the hardware-proven Minecraft 26.3 V12 graphics sequence isolated from older releases. V13 adds input hardening after V12; it does not change V12's Mesa pixel-format or WGL context sequence.

## Java runtime

The version catalog selects the packaged runtime independently of the window backend:

| Family | Runtime profile | Notes |
| --- | --- | --- |
| 26.x | `current` | Uses the current runtime selected by the build from Mojang's metadata. |
| 1.21.x | `java21` | Java 21 family. |
| 1.18.2–1.20.4 | `java17` | Matches the older modern Java generation more closely than Java 21. |
| 1.16.5 | `java17` compatibility | This is the oldest presently implemented runtime profile; treat as experimental. |
| 1.12.2 | `legacy` / Java 8 | Catalog-only until a UWP Java 8 provider and patches are implemented. |

## Controller/input provider

Controller support is version-specific. `controllerProvider` in `config/versions.tsv` chooses the bundled mod provider. If a profile enables Bandit Controller and the exact provider jar is absent, V13 refuses to launch with a clear error instead of silently starting an uncontrollable game.

SDL3 targets use SDL gamepad and SDL mouse APIs. GLFW targets keep the older UWP GLFW input facade. Physical USB mouse input is handled by the native backend, not by the controller mod.

## Current confidence tiers

- **Testing:** a path has meaningful Xbox validation but still needs broader version/mod testing. Minecraft 26.3 Fabric is in this tier after the V12 graphics path reached the menu smoothly on Series S.
- **Experimental:** the build/provider/compatibility path exists, but it has not received equivalent Xbox validation.
- **Catalog only:** metadata may be present, but a required launcher provider is still missing. 1.12.2 is currently here.

Do not infer support only from a version number. The installed target must have a complete manifest, correct Java runtime, correct native window backend, loader provider, and any requested version-specific controller/compatibility jars.

## Build workflows

- `scripts/build-local-263.ps1`: historical alias for the normal full-catalog launcher build (V14+).
- `scripts/build-regression-263.ps1`: explicit developer-only 26.3 SDL3 regression package.
- `scripts/build-local-multiversion.ps1`: full `config/versions.tsv` launcher package with strict per-target validation.
