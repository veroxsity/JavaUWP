# Babric 1.7.3 — MCEF / AWT compatibility plan

## Target

- Minecraft: Beta 1.7.3
- Loader: Babric
- Platform: Xbox UWP / CoreWindow
- Goal: provide an AWT-free browser surface if a compatible Chromium/CEF backend can be built.

## Compatibility boundary

Babric Beta 1.7.3 is a separate legacy target. The MCEF 1.12.2 API artifact documented in this directory is not a Babric 1.7.3 implementation and must not be added to a Beta 1.7.3 mod as if it were compatible.

The existing `CoreWindowBrowserSurface` class is only a Java-side transport contract. It does not supply a Babric entrypoint, a version-compatible MCEF API, CEF binaries, or a native UWP host.

## Required work

- [ ] Identify the exact Babric API / loader version and Java runtime expected by the target instance.
- [ ] Create a Babric-specific mod entrypoint and metadata; keep it separate from modern Fabric/Forge/Cleanroom integration.
- [ ] Avoid AWT and Swing in the Xbox runtime path.
- [ ] Adapt the browser surface to the Beta 1.7.3 render and input lifecycle.
- [ ] Select or build a CEF backend whose native binaries and subprocess model are actually UWP-compatible.
- [ ] Connect CEF off-screen rendering to the CoreWindow surface contract.
- [ ] Verify keyboard, pointer, focus, resize, navigation, and shutdown on Xbox Dev Mode.

## Current status

Planning only. No Babric mod, MCEF port, or CEF runtime has been built or tested. Do not mark browser support as working until the full native and in-game validation succeeds.
