# Staráč for Babric — Minecraft Beta 1.7.3

This integration targets **Babric Beta 1.7.3**, not Ornithe.

## Exact upstream release

Selected upstream release: **Staráč 1.3.2+babric** (Modrinth version ID
`fBYyB56e`). It is published for Minecraft `b1.7.3` and loader `babric`,
and requires Java 21 or newer.

- Project: https://modrinth.com/mod/starac
- Exact Babric release: https://modrinth.com/mod/starac/version/1.3.2%2Bbabric
- Upstream source: https://github.com/matthewperiut/starac

## GitHub Actions

Run **Actions → Prepare Staráč for Babric Beta 1.7.3 → Run workflow**.
The workflow downloads the exact upstream Babric release, checks that its
Modrinth metadata declares both `b1.7.3` and `babric`, verifies its SHA-256,
and uploads the JAR plus build metadata as
`starac-babric-b1.7.3-java21`.

**This workflow prepares the official Babric JAR; it does not recompile the
Staráč source.** The upstream source checkout currently uses Ornithe mappings
and configuration, so compiling that checkout and labelling it Babric would
be incorrect. A genuine source build needs a Babric-specific source revision
or configuration.

## Xbox UWP status

The official JAR targets Babric on desktop Java. It is not yet an Xbox-ready
APPX component. Staráč uses an LWJGL 3.4.0 / GLFW compatibility facade and
desktop native classifiers. The Xbox package must provide compatible UWP
native implementations and validate GLFW window lifecycle, OpenGL, audio,
keyboard and mouse input on console. Do not package desktop Windows native
DLLs as UWP binaries.

Staráč is not an Xbox gamepad mod. Gamepad mapping remains a separate task.
Java 21 metadata and a successful artifact download do not prove Xbox launch
or controller support.
