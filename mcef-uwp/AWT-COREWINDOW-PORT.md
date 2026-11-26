# MCEF AWT compatibility investigation (Minecraft 1.12.2–26.x)

## Upstream source located

The original MCEF repository is https://github.com/montoyo/mcef and its Minecraft 1.12.2 release tag is `1.12.2-1.11`.

The release asset `mcef-1.12.2-1.11-api.jar` is explicitly an **API-only JAR** (334,112 bytes), not the complete MCEF mod/runtime. It must not be packaged as the browser implementation.

- Release: https://github.com/montoyo/mcef/releases/tag/1.12.2-1.11
- API JAR: https://github.com/montoyo/mcef/releases/download/1.12.2-1.11/mcef-1.12.2-1.11-api.jar
- Source: https://github.com/montoyo/mcef/tree/1.12.2-1.11

## AWT issue and required fix

The desktop MCEF/JCEF architecture uses Java AWT/Swing components and a native CEF runtime. Setting `java.awt.headless=false` does not create a desktop window in an Xbox UWP AppContainer and is not a valid fix.

The UWP port must:
1. Keep AWT/Swing out of the Xbox runtime path (no `Frame`, `Canvas`, `JFrame`, `SwingUtilities`, or desktop event loop).
2. Provide a browser surface hosted by the app's existing CoreWindow/render loop.
3. Use CEF off-screen rendering (OSR) only if the selected CEF build supports the required UWP packaging, process model, and graphics path.
4. Forward pointer, keyboard, focus, resize, and shutdown events through the app input/lifecycle bridge.
5. Replace or isolate desktop-only native loading and CEF subprocess assumptions.

## Version scope

This is an architecture note for the requested 1.12.x–1.21.x–26.x range, not a claim that one MCEF binary supports every Minecraft version. Each Minecraft/loader line needs its own compatible MCEF artifact and native CEF build.

## Status

- [x] Located the upstream 1.12.2 source and release.
- [x] Identified that the published 1.12.2-1.11 release asset is API-only.
- [ ] Obtain and pin a complete version-matched MCEF/JCEF runtime.
- [ ] Implement CoreWindow-backed OSR renderer and input bridge.
- [ ] Build and validate x64 UWP-compatible CEF binaries and subprocess model.
- [ ] Test browser creation, rendering, input, navigation, and shutdown on Xbox Dev Mode.

**Do not enable or advertise MCEF as working until all validation gates pass.**
