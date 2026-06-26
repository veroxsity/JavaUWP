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

## Controls

- Enter Xbox IP on launch. Android shows an in-app numeric keypad so it does
  not depend on the system keyboard.
- Connect waits for the launcher shim to answer before mouse capture starts.
- `Esc` opens the relay menu and releases mouse capture. Resume captures it again.
- `F3` changes the Xbox IP.
- `F8` quits.
- `F9` toggles the local relay mode for troubleshooting.
- On-screen utility buttons are hidden during normal desktop relay use so game
  clicks cannot hit them by accident. Touch devices keep their input buttons.
- Android: the app stays in landscape immersive mode. Drag empty space to move
  the mouse, hold `Hold L` or `Hold R` with one finger, and drag with another
  finger for held-click actions such as breaking blocks. `Wheel Up` and
  `Wheel Down` repeat while held.
