# Bandit Mouse Relay

Bandit Mouse Relay relays local mouse input to the Xbox UWP GLFW shim over UDP:

- App to Xbox: UDP `7331`.
- Xbox status to app: UDP `7332`.
- Gameplay packet: `dx,dy,l,r,m,scroll,x1,x2`.
- Menu packet: `ABSW:x,y,l,r,m,scroll,x1,x2`.
- Status packets: `MODE:GAMEPLAY ...`, `MODE:MENU ...`, `SYNC:x,y`,
  and `SYNCW:x,y`.

This tool does not launch Minecraft and does not handle account auth or
entitlement checks.

## Windows Build

```powershell
.\tools\mouse-relay\scripts\build-windows.ps1
```

Portable zip:

```powershell
.\tools\mouse-relay\scripts\package-windows.ps1
```

Output:

```text
dist/mouse-relay/BanditMouseRelay-nightly-win-x64.zip
```

## Android Build

The Android project uses the SDL3 Android AAR and Gradle/NDK/CMake. Install
the Android SDK command-line tools, then install the same packages used by CI:

```powershell
sdkmanager "platforms;android-36" "build-tools;36.0.0" "ndk;28.2.13676358" "cmake;3.22.1"
```

```powershell
cd tools\mouse-relay\android
gradle :app:assembleDebug
```

Output:

```text
tools/mouse-relay/android/app/build/outputs/apk/debug/app-debug.apk
```

## iOS Build

iOS builds need Xcode, so they run on the `ios` job in
`.github/workflows/mouse-relay-nightly.yml` (macOS runner) rather than locally on
Windows. Push to a branch the workflow watches, or trigger it by hand, then grab
the `BanditMouseRelay-nightly-ios` artifact.

The job produces an **unsigned** IPA. Signing happens on the PC with Sideloadly
or AltStore using your Apple ID, so no Mac is needed at any point.

Output:

```text
dist/mouse-relay/BanditMouseRelay-nightly-ios.ipa
```

On a Mac the same build is:

```bash
cmake -S tools/mouse-relay -B build/ios -G Xcode \
  -DCMAKE_SYSTEM_NAME=iOS \
  -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=14.0
cmake --build build/ios --config Release
```

Notes:

- SDL3 links statically on iOS. A sideloaded bundle cannot load an unembedded
  dylib, and one Mach-O keeps re-signing to just the app binary.
- `ios/Info.plist` carries `NSLocalNetworkUsageDescription`. Without it iOS 14+
  drops UDP to LAN addresses while `sendto` still reports success, which looks
  exactly like the Xbox not answering. iOS shows a local network permission
  prompt on first connect and the relay will not reach the Xbox until it is
  accepted.
- The bundle has no app icon yet, so it installs with a blank tile.
- Free-cert sideloads expire after 7 days and need re-signing.

## Controls

- Enter Xbox IP on launch. Android and iOS show an in-app numeric keypad so they
  do not depend on the system keyboard.
- Connect waits for the launcher shim to answer before mouse capture starts.
- `Esc` opens the relay menu and releases mouse capture. Resume captures it again.
- `F3` changes the Xbox IP.
- `F8` quits.
- `F9` toggles the local relay mode for troubleshooting.
- On-screen utility buttons are hidden during normal desktop relay use so game
  clicks cannot hit them by accident. Touch devices keep their input buttons.
- Android and iOS: the app stays in landscape immersive mode. Drag empty space to
  move the mouse, hold `Hold L` or `Hold R` with one finger, and drag with another
  finger for held-click actions such as breaking blocks. `Wheel Up` and
  `Wheel Down` repeat while held.
