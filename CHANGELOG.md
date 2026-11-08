## V17 - Xbox pointer ownership and 26.3 input-mode HUD

- Hardware report: V16 26.3 runs at ~200 FPS, joins multiplayer, and both physical mouse/keyboard and controller work. Keep V12 Mesa/SDL rendering, V14 WGI and V15 key/button fixes untouched.
- Stop replacing the oversized Xbox/CoreWindow OS pointer with CoreCursor resource 5001. Hide that pointer as the known-good GLFW path does, and synthesize SDL menu absolute coordinates from MouseDevice deltas. Relative gameplay remains raw.
- 26.3 Fabric draws a small GUI/inventory/controller-screen cursor on-screen; no software cursor is drawn in grabbed gameplay.
- Port the 1.21.11 gameplay HUD ownership transition to 26.3: physical mouse or SDL keyboard input hides controller prompts until controller activity resumes.
- Retain all 59 catalog entries and 57 implemented targets. Microsoft sign-in remains intentionally off.

## V16 - Launcher cursor parity

- 26.3 SDL3 menus now use the same Bandit-style white crosshair cursor as the native launcher via WinRT custom cursor resource 5001.
- Gameplay still switches to SDL's transparent cursor resource 5000 so relative mouse movement remains visible to CoreWindow while the pointer is hidden.
- V15 SDL3 keyboard/mouse contract fixes, V14 WGI controller support, V12 graphics contract, and the full multi-version catalog are unchanged.

## V15 - SDL3 WinRT input contract repair

- Preserves the hardware-proven V12 Mesa/WGL rendering contract and V14 full multi-version catalog/WGI controller path.
- Marks 1.21.11 Fabric as hardware-tested after confirming the GLFW/UWP path works with controller plus native mouse/keyboard on Series S.
- Repairs pinned SDL-UWP SDL2-to-SDL3 input API drift: WinRT key down/up now maps to SDL3 `bool down` correctly, mouse buttons use `(button, down)` order, and synthetic Back emits a real press/release pair.
- Uses CoreWindow `KeyDown`/`KeyUp`, matching the hardware-proven GLFW path used by 1.21.x.
- Guards invalid mouse button 0 before SDL's 1-based button bitmask path, removing a native undefined-behavior/crash candidate.
- Resets mouse buttons and keyboard state on capture/focus loss to prevent sticky click/movement/jump state.
- Packages SDL's official transparent WinRT cursor resource (ID 5000) into `MC.Xbox.exe` and logs whether it resolved, so relative gameplay input can hide the OS pointer without suppressing motion.
- Mirrors the working GLFW pointer-capture transition for relative gameplay mode while retaining raw `MouseDevice::MouseMoved` and V14 CoreWindow fallback motion.
- Local package default advances to `1.0.0.15`; Microsoft authentication remains disabled in local builds.

## V14 — Xbox input fix + full launcher catalog by default
- Keep the hardware-proven V12 Mesa/WGL graphics path unchanged.
- Fix the dead 26.3 Xbox controller path at the root: SDL's UWP backend uses Windows.Gaming.Input (WGI), and upstream WGI defaults off. V14 forces `SDL_JOYSTICK_WGI=1` in the launcher and 26.3 Fabric controller, defaults WGI on inside the patched WinRT SDL runtime, and validates that SDL was compiled with `SDL_JOYSTICK_WGI`.
- Add WGI runtime diagnostics for initialization, enumerated controller count, and successful joystick open.
- Add a physical-mouse fallback: when WinRT exposes CoreWindow pointer events but no raw `MouseDevice::MouseMoved` source, SDL converts pointer movement into relative deltas instead of dropping all motion in relative mode.
- Make `scripts/build-local-263.ps1` a compatibility alias for the full multi-version launcher build. The normal package now stages `config/versions.tsv`, not the 26.3-only catalog.
- Add a post-build catalog assertion so an incomplete/one-version `runtime/version_catalog.tsv` fails the build. The old one-version flow is retained only as explicit developer script `scripts/build-regression-263.ps1`.
- Local package default advances to `1.0.0.14`. Microsoft authentication remains disabled in the local build.

## V13 — launcher compatibility profiles + WinRT input bridge

- Keep the hardware-proven V12 Mesa/RenderPearl graphics path unchanged for SDL3 targets.
- Resolve the game's window backend from its own download manifest (`lwjgl-sdl` vs `lwjgl-glfw`) instead of assuming every version at or above a numeric threshold uses SDL3.
- Pass the resolved backend through native staging and JVM launch so older 1.21.x/1.20.x targets continue using the UWP GLFW shim while 26.3 uses the patched SDL3 runtime.
- Add a full multi-version local build entry point (`scripts/build-local-multiversion.ps1`) while retaining the focused 26.3 regression build.
- Add the `BanditSDL_WinRTInputBridgeV13` SDL marker and harden WinRT mouse input. If SDL's optional transparent cursor resource is unavailable, never fall back to `PointerCursor=nullptr`, because that suppresses CoreWindow pointer-motion events.
- Keep raw `Windows.Devices.Input.MouseDevice::MouseMoved` deltas for relative first-person input, make MouseDevice subscription null-safe, and add one-time absolute/relative input diagnostics.
- When a profile enables Bandit's controller support, verify its exact version-specific controller jar before launch instead of silently entering Minecraft without a usable controller cursor.
- Use Java 17 for the pre-1.20.5 compatibility targets that were previously pointed at Java 21; keep 1.21.x on Java 21 and 26.x on the current runtime profile.
- Surface Testing/Experimental labels in profile target text. 26.3 Fabric moves to Testing after V12 graphics was confirmed on Xbox hardware.
- Local package default advances to `1.0.0.13`.

## V12 — RenderPearl/Mesa framebuffer contract correction

- Remove V10 from the active SDL patch chain. Minecraft 26.3 creates one OpenGL context and rebinds it from its hidden utility window to the final game window; the single-HGLRC wrapper experiment was based on the wrong startup model.
- Mirror the project's Series-S-proven MesaTestApp/GLFW pixel-format contract: `wglChoosePixelFormat` followed by `libgallium_wgl.dll!SetPixelFormat`.
- Never call Mesa `wglGetPixelFormat` before pixel-format installation. In the bundled Mesa WGL runtime that query enters `stw_pixelformat_get` and can instantiate framebuffer state before the requested format is configured.
- Refuse SDL's legacy-context fallback when Minecraft requests OpenGL 3.3 core. Query and log the actual active GL version/profile/renderer instead of trusting SDL's requested attributes.
- Unbind the temporary WGL context with `wglMakeCurrent(NULL, NULL)` before deleting it, matching the known-good Xbox Mesa sequence.
- Preserve Minecraft's logical hidden/utility/high-DPI window flags and implement synchronous Xbox-safe position/size/sync callbacks for RenderPearl's final-window setup.
- Keep the Mesa support stack preloaded before JVM/LWJGL startup and keep `mesa_glthread=false` while the Xbox path is being stabilized.
- Add first/60th-present breadcrumbs and a `BanditSDL_WinRTRenderPearlV12` runtime marker. Package build default is `1.0.0.12`.

# Changelog

## 2026-10-25

### Minecraft 26.3 / SDL3

**V9 hardens the post-provider Mesa/WGL handoff after Xbox hardware reached a black screen and one native hard exit.** Mesa threaded GL is disabled by default during bring-up, `wglGetProcAddress` is now protected against driver SEH faults, the nonessential `wglGetExtensionsStringARB` probe is removed from the first-context path, and the critical context/make-current/swap stages are flushed to `logs/current/sdl_wgl_stage.log`. If the process is killed before Java can write a crash file, the next launcher start includes the last native SDL/WGL stage in the crash message instead of only `unknown_hard_crash`.

**V8 fixes the hardware-proven `glGetError mismatch` regression.** Minecraft 26.3 compares the exact function address returned by LWJGL with `SDL_GL_GetProcAddress("glGetError")`. LWJGL on Windows resolves through `wglGetProcAddress` first and falls back to the OpenGL DLL export; V7 accidentally reversed that order. V8 restores the LWJGL order, adds a dedicated provider-parity marker/log line, and makes the SDL cache/build validation reject any older runtime.

**Minecraft 26.3 now has a direct SDL3 UWP path instead of a GLFW compatibility wrapper.** The launcher points LWJGL's SDL binding at a patched x64 SDL3-UWP runtime, forces the WinRT video driver and packaged Mesa OpenGL runtime, and keeps the GLFW shim only for 26.2 and older.

**WinRT now forces Mesa through SDL's WGL backend.** The previous SDL3-UWP build enabled both WGL and EGL/OpenGL ES, and the WinRT driver prefers EGL when both are present. On Xbox this caused Minecraft 26.3 to stop at backend discovery with `Could not load EGL library`. V5 disables SDL OpenGL ES/EGL, includes the WinRT WGL implementation in the WindowsStore CMake build, and makes WGL win if both backends are ever enabled accidentally.

**Minecraft's absolute OpenGL provider path is normalized inside SDL on WinRT.** Minecraft 26.3 passes `((SharedLibrary) GL.getFunctionProvider()).getPath()` directly to `SDL_GL_LoadLibrary()`, which overrides `SDL_OPENGL_LIBRARY`. On Xbox that is an absolute `S:\Program Files\WindowsApps\...\graphics\mesa\opengl32.dll` path, and `LoadPackagedLibrary` rejects absolute paths with `ERROR_INVALID_PARAMETER`. V6 converts Bandit's packaged Mesa path back to `graphics\mesa\opengl32.dll` before calling `LoadPackagedLibrary`, while LWJGL continues using the absolute path it needs.

**The WinRT/WGL lifecycle was rebuilt for Minecraft 26.3 after the first native Mesa crash.** V7 no longer creates SDL's temporary WGL extension-probe context during `SDL_GL_LoadLibrary`, before any SDL window exists. It sets the pixel format on the real CoreWindow with `WIN_GL_SetupWindow`, initializes WGL capabilities only after the real OpenGL context is current, originally preferred the DLL export for core GL symbols; hardware testing proved that ordering does not match LWJGL and V8 corrects it. V7 also converts MSVC structured exceptions from critical WGL calls into SDL diagnostics instead of opaque `EXCEPTION_ACCESS_VIOLATION` crashes. The fork's stale `int WIN_SetError` declaration is corrected to `bool`, so failures cannot be misread as success.

**Minecraft 26.3's multiple startup windows now work on one Xbox CoreWindow.** RenderPearl creates an OpenGL utility window, a second hidden test window, and then the real game window. The upstream WinRT backend rejected the second SDL window outright. V7 virtualizes these logical SDL windows over the single physical CoreWindow, reuses the one WGL pixel format, restores the previous logical event target when a temporary window is destroyed, and keeps one CoreWindow event sink.

**The SDL build cache is content-addressed.** The pinned SDL commit, external-CoreWindow patch contents, build script, architecture/configuration, and CMake options are part of the cache key, so editing the patch cannot silently reuse a stale DLL. Developer-supplied SDL overrides must be x64 and carry the Bandit patch marker plus the required 26.3 SDL exports.

**SDL can now actually load Mesa in a WindowsStore build.** The upstream CMake path otherwise selected SDL's dummy shared-object loader for UWP even though the WinRT WGL backend calls `SDL_LoadObject`. The V4 patch compiles SDL's WinRT-safe Windows loader (`LoadPackagedLibrary`) and passes SDL the package-relative `graphics\mesa\opengl32.dll` path; absolute paths are not valid for `LoadPackagedLibrary`. Older CoreWindow-only patched SDL binaries are rejected by the V4 marker export.

**The embedded UWP host now satisfies SDL's `SDL_main` readiness gate.** Minecraft 26.3 reached `SDL_Init` through LWJGL, but Bandit's already-running `IFrameworkView` means SDL's own WinRT bootstrap never calls `SDL_SetMainReady()`. The V4 runtime primes that state on the first WinRT subsystem initialization, eliminating the early `Application didn't initialize properly, did you include SDL_main.h...` crash without reintroducing a GLFW wrapper.

**Fabric controller support was ported to SDL3 for 26.3.** Gamepads and mouse state use LWJGL SDL, key and mouse codes use 26.3 `InputConstants`, and controller text entry uses Minecraft's SDL text-input focus path. The 26.3 render injection and recipe-book accessors were updated for the new client API. Forge and NeoForge controller providers remain disabled until their mappings are separately validated.

**26.3 compatibility code has its own overlay.** It no longer silently reuses the 26.2 source directory. The vanilla mixin targets used by the overlay were checked against 26.3.

**Build/package verification is stricter.** `scripts\verify-26.3-build.ps1` builds and validates SDL, compiles the 26.3 Fabric compatibility/controller mods, generates loader manifests, and can drive the signed APPX build. Package validation checks 26.3's LWJGL 3.4.3 native set and SDL runtime, and the build verifies the APPX signature after signing.

**Launcher input is gated while Minecraft runs.** Persistent launcher pointer/mouse handlers no longer process gameplay input at the same time as SDL, reducing duplicate events when SDL attaches to the existing CoreWindow.

The 26.3 target remains experimental until the Windows/UWP acceptance build and Xbox runtime checklist complete successfully.

Notable changes to Bandit Launcher. Nightly packages are numbered by build revision rather than by release, so entries here are dated.

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
