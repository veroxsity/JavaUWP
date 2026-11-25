package com.onyx.controller.babric;

import java.lang.reflect.Method;
import java.nio.ByteBuffer;
import java.nio.FloatBuffer;

/**
 * Runtime-only GLFW bridge. Reflection keeps this mod compilable against Babric's
 * legacy game classes while Staráč supplies the LWJGL 3 / GLFW implementation.
 */
public final class GamepadBridge {
    private static final int GLFW_JOYSTICK_1 = 0;
    private static final float DEADZONE = 0.18f;

    private static Method joystickPresent;
    private static Method joystickAxes;
    private static Method joystickButtons;
    private static boolean glfwResolved;

    public static final State STATE = new State();

    private GamepadBridge() {}

    public static void poll() {
        if (!resolveGlfw()) {
            STATE.clear();
            return;
        }

        try {
            Object present = joystickPresent.invoke(null, GLFW_JOYSTICK_1);
            if (!(present instanceof Boolean) || !((Boolean) present).booleanValue()) {
                STATE.clear();
                return;
            }

            FloatBuffer axes = (FloatBuffer) joystickAxes.invoke(null, GLFW_JOYSTICK_1);
            ByteBuffer buttons = (ByteBuffer) joystickButtons.invoke(null, GLFW_JOYSTICK_1);
            if (axes == null || buttons == null) {
                STATE.clear();
                return;
            }

            STATE.leftX = axis(axes, 0);
            STATE.leftY = axis(axes, 1);
            STATE.rightX = axis(axes, 2);
            STATE.rightY = axis(axes, 3);

            STATE.a = button(buttons, 0);
            STATE.b = button(buttons, 1);
            STATE.x = button(buttons, 2);
            STATE.y = button(buttons, 3);
            STATE.leftBumper = button(buttons, 4);
            STATE.rightBumper = button(buttons, 5);
            STATE.back = button(buttons, 6);
            STATE.start = button(buttons, 7);
            STATE.leftStick = button(buttons, 8);
            STATE.rightStick = button(buttons, 9);

            // GLFW's standard Xbox mapping exposes triggers as axes 4 and 5.
            STATE.leftTrigger = trigger(axis(axes, 4));
            STATE.rightTrigger = trigger(axis(axes, 5));
        } catch (Throwable error) {
            STATE.clear();
            OnyxXboxController.LOGGER.fine("GLFW gamepad poll failed: " + error.toString());
        }
    }

    private static boolean resolveGlfw() {
        if (glfwResolved) return joystickPresent != null;
        glfwResolved = true;
        try {
            Class<?> glfw = Class.forName("org.lwjgl.glfw.GLFW");
            joystickPresent = glfw.getMethod("glfwJoystickPresent", int.class);
            joystickAxes = glfw.getMethod("glfwGetJoystickAxes", int.class);
            joystickButtons = glfw.getMethod("glfwGetJoystickButtons", int.class);
            return true;
        } catch (Throwable error) {
            OnyxXboxController.LOGGER.warning("GLFW 3 joystick API is unavailable; controller input is disabled.");
            return false;
        }
    }

    private static float axis(FloatBuffer buffer, int index) {
        if (index >= buffer.limit()) return 0.0f;
        float value = buffer.get(index);
        return Math.abs(value) < DEADZONE ? 0.0f : value;
    }

    private static boolean button(ByteBuffer buffer, int index) {
        return index < buffer.limit() && buffer.get(index) == 1;
    }

    private static float trigger(float value) {
        return Math.max(0.0f, Math.min(1.0f, (value + 1.0f) * 0.5f));
    }

    public static final class State {
        public float leftX, leftY, rightX, rightY;
        public float leftTrigger, rightTrigger;
        public boolean a, b, x, y, leftBumper, rightBumper;
        public boolean back, start, leftStick, rightStick;

        private void clear() {
            leftX = leftY = rightX = rightY = 0.0f;
            leftTrigger = rightTrigger = 0.0f;
            a = b = x = y = leftBumper = rightBumper = false;
            back = start = leftStick = rightStick = false;
        }
    }
}
