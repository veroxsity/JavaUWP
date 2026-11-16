# LWJGL 2 UWP / CoreWindow adapter

The desktop LWJGL 2 native library cannot be reused as-is in Xbox UWP. Beta 1.7.3 needs a UWP-native JNI implementation for the LWJGL 2 APIs it calls.

## Required surface

- `org.lwjgl.opengl.Display`: create/destroy the window surface, process events, update and swap buffers.
- `org.lwjgl.input.Keyboard`: translate CoreWindow keyboard events and preserve LWJGL 2 key codes.
- `org.lwjgl.input.Mouse`: translate pointer position, buttons, wheel and capture/visibility behavior.
- OpenGL: create a context through the supported UWP graphics path and expose only the GL capabilities actually available.

## Constraints

- Keep this backend isolated from the modern GLFW/LWJGL 3 shim.
- Implement the JNI symbols expected by the selected LWJGL 2 Java API; matching DLL names alone is insufficient.
- Do not claim compatibility until the client reaches its title screen and input/rendering are verified on Xbox.

Status: design scaffold only; no native implementation or Xbox test is included yet.
