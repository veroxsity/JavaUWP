# Onyx Xbox Controller — Babric / Beta 1.7.3

Client-side controller mod for Minecraft Beta 1.7.3 on Babric. It is a separate mod from Staráč.

## Runtime design

- The Xbox controller is read by the JavaUWP host through the native Windows gamepad path.
- Staráč provides the LWJGL 3 / GLFW compatibility layer.
- This mod polls GLFW's standard joystick API and maps the gamepad to Beta 1.7.3 key bindings.
- No desktop XInput DLL or external controller application is required.

## Build

Use JDK 17 and run:

```powershell
.gradlew.bat build
```

The Gradle project uses the Babric Loom plugin and the Babric Beta 1.7.3 mappings. The resulting mod is in `build/libs/`.

## Initial button map

| Xbox input | Beta action |
|---|---|
| Left stick | Move |
| A | Jump |
| B / left stick click | Sneak |
| X | Drop item |
| Y | Inventory |
| RT / RB | Attack |
| LT / LB | Use item |
| Right stick click | Sprint |

## Known implementation limits

This first integration layer is deliberately conservative. Right-stick camera movement, D-pad hotbar selection, menu navigation, trigger calibration and reconnect handling still need runtime validation against the exact GLFW build packaged by JavaUWP/Staráč. GLFW must expose a standard gamepad/joystick mapping for the Xbox controller. The Beta 1.7.3 tick and key-binding names also need a Babric build and in-game verification before this mod is called complete.

## Compatibility

- Minecraft: Beta 1.7.3
- Loader: Babric
- Java: 17
- Window/input bridge: Staráč GLFW 3 compatibility layer hosted by JavaUWP
