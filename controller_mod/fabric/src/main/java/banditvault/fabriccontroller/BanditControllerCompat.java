package banditvault.fabriccontroller;

import banditvault.controllercore.ControllerAxis;
import banditvault.controllercore.ControllerButton;
import banditvault.controllercore.ControllerRuntime;
import banditvault.controllercore.ControllerState;
import com.mojang.blaze3d.systems.RenderSystem;
import net.minecraft.class_304;
import net.minecraft.class_310;
import net.minecraft.class_315;
import net.minecraft.class_332;
import net.minecraft.class_465;
import net.minecraft.class_1041;
import net.minecraft.class_1713;
import net.minecraft.class_1735;
import net.minecraft.class_437;
import net.minecraft.class_4587;
import net.minecraft.class_746;
import org.lwjgl.glfw.GLFW;
import org.lwjgl.glfw.GLFWGamepadState;

public final class BanditControllerCompat {
    private static final int GAMEPAD_ID = GLFW.GLFW_JOYSTICK_1;
    private static final int LEFT_CLICK = 0;
    private static final int RIGHT_CLICK = 1;

    private static final GLFWGamepadState GLFW_STATE = GLFWGamepadState.create();
    private static final ControllerState CONTROLLER_STATE = new ControllerState();
    private static boolean loggedReady;
    private static boolean loggedLookReflectionFailure;
    private static boolean loggedHotbarReflectionFailure;
    private static boolean loggedQuickMoveReflectionFailure;
    private static boolean active;
    private static double cursorX = -1.0;
    private static double cursorY = -1.0;
    private static long lastLookNanos;
    private static int scrollCooldown;
    private static boolean crouchToggled;
    private static boolean sprintToggled;
    private static boolean followingCompanion;
    private static Object lastCompanionScreen;
    private static double lastCompanionMouseX;
    private static double lastCompanionMouseY;
    private static long companionActiveUntilNanos;

    private static final long COMPANION_IDLE_NANOS = 500_000_000L;
    private static final double COMPANION_MOVE_EPSILON = 0.5;

    private BanditControllerCompat() {
    }

    public static void tick(class_310 client) {
        if (client == null) {
            return;
        }

        if (!poll()) {
            if (active) {
                releaseGameplayKeys(client, client.field_1755 == null);
                crouchToggled = false;
                sprintToggled = false;
                followingCompanion = false;
                active = false;
            }
            return;
        }

        active = true;
        if (!loggedReady) {
            loggedReady = true;
            FabricControllerLog.log("Bandit controller compat active");
        }

        if (client.field_1755 != null) {
            lastLookNanos = 0L;
            releaseGameplayKeys(client, false);
            tickScreen(client, client.field_1755);
        } else {
            followingCompanion = false;
            tickGameplay(client);
        }

        finishFrame();
    }

    public static void renderFrame(class_310 client) {
        if (client == null || client.field_1755 != null || client.field_1724 == null || !poll()) {
            lastLookNanos = 0L;
            return;
        }

        long now = System.nanoTime();
        float seconds = 1.0f / 60.0f;
        if (lastLookNanos != 0L) {
            seconds = (now - lastLookNanos) / 1000000000.0f;
            if (seconds < 1.0f / 240.0f) {
                seconds = 1.0f / 240.0f;
            } else if (seconds > 1.0f / 20.0f) {
                seconds = 1.0f / 20.0f;
            }
        }
        lastLookNanos = now;

        BanditControllerSettings settings = BanditControllerSettings.get();
        float y = axis(GLFW.GLFW_GAMEPAD_AXIS_RIGHT_Y);
        if (settings.invertY) {
            y = -y;
        }
        applyLook(client.field_1724, axis(GLFW.GLFW_GAMEPAD_AXIS_RIGHT_X), y, seconds, settings);
    }

    public static void renderCursor(class_437 screen, class_4587 matrices) {
        if (!active || followingCompanion || screen == null || cursorX < 0.0 || cursorY < 0.0) {
            return;
        }

        int x = (int)Math.round(cursorX);
        int y = (int)Math.round(cursorY);
        RenderSystem.disableDepthTest();
        RenderSystem.enableBlend();
        matrices.method_22903();
        matrices.method_22904(0.0, 0.0, 400.0);
        class_332.method_25294(matrices, x - 3, y - 3, x + 4, y + 4, 0x66000000);
        class_332.method_25294(matrices, x - 5, y, x + 6, y + 1, 0xFFFFFFFF);
        class_332.method_25294(matrices, x, y - 5, x + 1, y + 6, 0xFFFFFFFF);
        matrices.method_22909();
        RenderSystem.enableDepthTest();
    }

    public static void renderCursorOverlay(class_310 client) {
        if (client == null || client.field_1755 == null) {
            return;
        }
        renderCursor(client.field_1755, new class_4587());
    }

    public static int screenMouseX(int fallback) {
        if (!active || cursorX < 0.0) {
            return fallback;
        }
        return (int)Math.round(cursorX);
    }

    public static int screenMouseY(int fallback) {
        if (!active || cursorY < 0.0) {
            return fallback;
        }
        return (int)Math.round(cursorY);
    }

    private static boolean poll() {
        try {
            if (!GLFW.glfwJoystickIsGamepad(GAMEPAD_ID) ||
                !GLFW.glfwGetGamepadState(GAMEPAD_ID, GLFW_STATE)) {
                return false;
            }
            captureControllerState();
            return true;
        } catch (Throwable t) {
            if (!loggedReady) {
                loggedReady = true;
                FabricControllerLog.logException("Bandit controller compat failed to poll GLFW gamepad", t);
            }
            return false;
        }
    }

    private static void tickGameplay(class_310 client) {
        BanditControllerSettings settings = BanditControllerSettings.get();
        class_315 options = client.field_1690;
        float lx = axis(GLFW.GLFW_GAMEPAD_AXIS_LEFT_X);
        float ly = axis(GLFW.GLFW_GAMEPAD_AXIS_LEFT_Y);

        if (pressed(GLFW.GLFW_GAMEPAD_BUTTON_BACK)) {
            client.method_1507(new BanditControllerSettingsScreen(null));
            return;
        }

        setHeld(options.field_1894, ly < -settings.moveDeadzone);
        setHeld(options.field_1881, ly > settings.moveDeadzone);
        setHeld(options.field_1913, lx < -settings.moveDeadzone);
        setHeld(options.field_1849, lx > settings.moveDeadzone);
        setHeld(options.field_1903, button(GLFW.GLFW_GAMEPAD_BUTTON_A));
        if (settings.toggleCrouch) {
            if (pressed(GLFW.GLFW_GAMEPAD_BUTTON_B)) {
                crouchToggled = !crouchToggled;
            }
            setHeld(options.field_1832, crouchToggled);
        } else {
            crouchToggled = false;
            setHeld(options.field_1832, button(GLFW.GLFW_GAMEPAD_BUTTON_B));
        }
        setHeld(options.field_1886, trigger(GLFW.GLFW_GAMEPAD_AXIS_RIGHT_TRIGGER));
        setHeld(options.field_1904, trigger(GLFW.GLFW_GAMEPAD_AXIS_LEFT_TRIGGER));
        if (settings.toggleSprint) {
            if (pressed(GLFW.GLFW_GAMEPAD_BUTTON_LEFT_THUMB)) {
                sprintToggled = !sprintToggled;
            }
            setHeld(options.field_1867, sprintToggled);
        } else {
            sprintToggled = false;
            setHeld(options.field_1867, button(GLFW.GLFW_GAMEPAD_BUTTON_LEFT_THUMB));
        }

        if (triggerPressed(GLFW.GLFW_GAMEPAD_AXIS_RIGHT_TRIGGER)) {
            pressKey(options.field_1886);
        }
        if (triggerPressed(GLFW.GLFW_GAMEPAD_AXIS_LEFT_TRIGGER)) {
            pressKey(options.field_1904);
        }
        if (pressed(GLFW.GLFW_GAMEPAD_BUTTON_Y)) {
            pressKey(options.field_1822);
        }
        if (pressed(GLFW.GLFW_GAMEPAD_BUTTON_X)) {
            pressKey(options.field_1869);
        }
        if (pressed(GLFW.GLFW_GAMEPAD_BUTTON_LEFT_BUMPER)) {
            changeHotbarSlot(client.field_1724, -1);
        }
        if (pressed(GLFW.GLFW_GAMEPAD_BUTTON_RIGHT_BUMPER)) {
            changeHotbarSlot(client.field_1724, 1);
        }

        if (pressed(GLFW.GLFW_GAMEPAD_BUTTON_START)) {
            client.method_20539(false);
        }
    }

    private static void tickScreen(class_310 client, class_437 screen) {
        BanditControllerSettings settings = BanditControllerSettings.get();
        if (cursorX < 0.0 || cursorY < 0.0) {
            cursorX = Math.max(1, screen.field_22789 / 2);
            cursorY = Math.max(1, screen.field_22790 / 2);
        }

        float lx = axis(GLFW.GLFW_GAMEPAD_AXIS_LEFT_X);
        float ly = axis(GLFW.GLFW_GAMEPAD_AXIS_LEFT_Y);
        float ry = axis(GLFW.GLFW_GAMEPAD_AXIS_RIGHT_Y);

        if (updateCompanion(client, screen)) {
            followCompanionCursor(client, screen);
        } else {
            double dx = shapedCursorAxis(lx, settings.cursorDeadzone);
            double dy = shapedCursorAxis(ly, settings.cursorDeadzone);
            cursorX = clamp(cursorX + dx * settings.cursorSpeed, 0.0, Math.max(1, screen.field_22789 - 1));
            cursorY = clamp(cursorY + dy * settings.cursorSpeed, 0.0, Math.max(1, screen.field_22790 - 1));
            screen.method_16014(cursorX, cursorY);
        }

        if (pressed(GLFW.GLFW_GAMEPAD_BUTTON_BACK)) {
            if (screen instanceof BanditControllerSettingsScreen) {
                ((BanditControllerSettingsScreen)screen).close();
            } else {
                client.method_1507(new BanditControllerSettingsScreen(screen));
            }
            return;
        }

        if (pressed(GLFW.GLFW_GAMEPAD_BUTTON_A)) {
            screen.method_25402(cursorX, cursorY, LEFT_CLICK);
        }
        if (released(GLFW.GLFW_GAMEPAD_BUTTON_A)) {
            screen.method_25406(cursorX, cursorY, LEFT_CLICK);
        }
        if (pressed(GLFW.GLFW_GAMEPAD_BUTTON_X)) {
            screen.method_25402(cursorX, cursorY, RIGHT_CLICK);
        }
        if (released(GLFW.GLFW_GAMEPAD_BUTTON_X)) {
            screen.method_25406(cursorX, cursorY, RIGHT_CLICK);
        }
        if (pressed(GLFW.GLFW_GAMEPAD_BUTTON_B)) {
            if (!screen.method_25404(GLFW.GLFW_KEY_ESCAPE, 0, 0)) {
                client.method_1507(null);
            }
        }
        if (pressed(GLFW.GLFW_GAMEPAD_BUTTON_Y)) {
            quickMoveFocusedSlot(screen);
        }

        if (scrollCooldown > 0) {
            scrollCooldown--;
        }
        if (scrollCooldown == 0) {
            double scroll = 0.0;
            if (ry < -0.35f || button(GLFW.GLFW_GAMEPAD_BUTTON_DPAD_UP)) {
                scroll = settings.scrollAmount;
            } else if (ry > 0.35f || button(GLFW.GLFW_GAMEPAD_BUTTON_DPAD_DOWN)) {
                scroll = -settings.scrollAmount;
            }
            if (scroll != 0.0) {
                screen.method_25401(cursorX, cursorY, scroll);
                scrollCooldown = 5;
            }
        }
    }

    private static boolean updateCompanion(class_310 client, class_437 screen) {
        double mouseX;
        double mouseY;
        try {
            mouseX = client.field_1729.method_1603();
            mouseY = client.field_1729.method_1604();
        } catch (Throwable t) {
            followingCompanion = false;
            return false;
        }

        long now = System.nanoTime();
        if (screen != lastCompanionScreen) {
            lastCompanionScreen = screen;
            lastCompanionMouseX = mouseX;
            lastCompanionMouseY = mouseY;
        } else if (Math.abs(mouseX - lastCompanionMouseX) > COMPANION_MOVE_EPSILON ||
            Math.abs(mouseY - lastCompanionMouseY) > COMPANION_MOVE_EPSILON) {
            lastCompanionMouseX = mouseX;
            lastCompanionMouseY = mouseY;
            companionActiveUntilNanos = now + COMPANION_IDLE_NANOS;
        }

        boolean companion = now <= companionActiveUntilNanos;
        followingCompanion = companion;
        return companion;
    }

    private static void followCompanionCursor(class_310 client, class_437 screen) {
        class_1041 window = client.method_22683();
        double rawX = client.field_1729.method_1603();
        double rawY = client.field_1729.method_1604();
        double scaledX = rawX * screen.field_22789 / Math.max(1.0, window.method_4480());
        double scaledY = rawY * screen.field_22790 / Math.max(1.0, window.method_4507());
        cursorX = clamp(scaledX, 0.0, Math.max(1, screen.field_22789 - 1));
        cursorY = clamp(scaledY, 0.0, Math.max(1, screen.field_22790 - 1));
        screen.method_16014(cursorX, cursorY);
    }

    private static void applyLook(class_746 player, float rx, float ry, float seconds, BanditControllerSettings settings) {
        float lookX = ControllerRuntime.shapedLookAxis(rx, settings.lookDeadzone);
        float lookY = ControllerRuntime.shapedLookAxis(ry, settings.lookDeadzone);
        if (lookX == 0.0f && lookY == 0.0f) {
            return;
        }

        try {
            float yaw = getEntityFloat(player, "field_6031");
            float pitch = getEntityFloat(player, "field_5965");
            float scale = settings.lookSpeed * seconds;
            setEntityFloat(player, "field_6031", yaw + lookX * scale);
            setEntityFloat(player, "field_5965", (float)clamp(pitch + lookY * scale, -90.0, 90.0));
        } catch (Throwable t) {
            if (!loggedLookReflectionFailure) {
                loggedLookReflectionFailure = true;
                FabricControllerLog.logException("Bandit controller compat failed to apply look", t);
            }
        }
    }

    private static void releaseGameplayKeys(class_310 client, boolean preservePhysicalInput) {
        class_315 options = client.field_1690;
        setHeld(options.field_1894, false, preservePhysicalInput);
        setHeld(options.field_1881, false, preservePhysicalInput);
        setHeld(options.field_1913, false, preservePhysicalInput);
        setHeld(options.field_1849, false, preservePhysicalInput);
        setHeld(options.field_1903, false, preservePhysicalInput);
        setHeld(options.field_1832, false, preservePhysicalInput);
        setHeld(options.field_1867, false, preservePhysicalInput);
        setHeld(options.field_1886, false, preservePhysicalInput);
        setHeld(options.field_1904, false, preservePhysicalInput);
    }

    private static void setHeld(class_304 key, boolean held) {
        setHeld(key, held, true);
    }

    private static void setHeld(class_304 key, boolean held, boolean preservePhysicalInput) {
        if (key == null) {
            return;
        }
        boolean effectiveHeld = ControllerRuntime.shouldHoldKey(
            held,
            preservePhysicalInput && isBoundInputHeld(key),
            preservePhysicalInput);
        key.method_23481(effectiveHeld);
        class_304.method_1416(key.method_1429(), effectiveHeld);
    }

    private static boolean isBoundInputHeld(class_304 key) {
        class_310 client = class_310.method_1551();
        if (client == null || client.method_22683() == null) {
            return false;
        }
        long window = client.method_22683().method_4490();
        int code = key.method_1429().method_1444();
        return GLFW.glfwGetKey(window, code) == GLFW.GLFW_PRESS
            || (code >= 0 && code <= GLFW.GLFW_MOUSE_BUTTON_LAST
                && GLFW.glfwGetMouseButton(window, code) == GLFW.GLFW_PRESS);
    }

    private static void pressKey(class_304 key) {
        if (key == null) {
            return;
        }
        class_304.method_1420(key.method_1429());
    }

    private static void changeHotbarSlot(class_746 player, int direction) {
        if (player == null) {
            return;
        }

        try {
            Object inventory = getInventory(player);
            java.lang.reflect.Field selectedSlot = findField(inventory.getClass(), "field_7545");
            selectedSlot.setAccessible(true);
            int slot = selectedSlot.getInt(inventory);
            slot = (slot + direction) % 9;
            if (slot < 0) {
                slot += 9;
            }
            selectedSlot.setInt(inventory, slot);
        } catch (Throwable t) {
            if (!loggedHotbarReflectionFailure) {
                loggedHotbarReflectionFailure = true;
                FabricControllerLog.logException("Bandit controller compat failed to change hotbar slot", t);
            }
        }
    }

    private static void quickMoveFocusedSlot(class_437 screen) {
        if (!(screen instanceof class_465)) {
            return;
        }

        try {
            java.lang.reflect.Field focusedSlotField = findField(screen.getClass(), "field_2787");
            focusedSlotField.setAccessible(true);
            class_1735 slot = (class_1735)focusedSlotField.get(screen);
            if (slot == null || !slot.method_7681()) {
                return;
            }

            java.lang.reflect.Method clickSlot = findMethod(
                screen.getClass(),
                "method_2383",
                class_1735.class,
                int.class,
                int.class,
                class_1713.class);
            clickSlot.setAccessible(true);
            clickSlot.invoke(screen, slot, slot.field_7874, 0, class_1713.field_7794);
        } catch (Throwable t) {
            if (!loggedQuickMoveReflectionFailure) {
                loggedQuickMoveReflectionFailure = true;
                FabricControllerLog.logException("Bandit controller compat failed to quick-move focused slot", t);
            }
        }
    }

    private static Object getInventory(class_746 player) throws ReflectiveOperationException {
        try {
            java.lang.reflect.Field field = findField(player.getClass(), "field_7514");
            field.setAccessible(true);
            return field.get(player);
        } catch (NoSuchFieldException ignored) {
            java.lang.reflect.Method method = findMethod(player.getClass(), "method_31548");
            method.setAccessible(true);
            return method.invoke(player);
        }
    }

    private static float axis(int index) {
        return CONTROLLER_STATE.axis(axisFor(index));
    }

    private static boolean trigger(int index) {
        return CONTROLLER_STATE.trigger(axisFor(index), BanditControllerSettings.get().triggerDeadzone);
    }

    private static boolean triggerPressed(int index) {
        return CONTROLLER_STATE.triggerPressed(axisFor(index), BanditControllerSettings.get().triggerDeadzone);
    }

    private static boolean button(int index) {
        return CONTROLLER_STATE.button(buttonFor(index));
    }

    private static boolean pressed(int index) {
        return CONTROLLER_STATE.pressed(buttonFor(index));
    }

    private static boolean released(int index) {
        return CONTROLLER_STATE.released(buttonFor(index));
    }

    private static void copyButtons() {
        finishFrame();
    }

    private static float shapedLookAxis(float value, float deadzone) {
        return ControllerRuntime.shapedLookAxis(value, deadzone);
    }

    private static double shapedCursorAxis(float value, float deadzone) {
        return ControllerRuntime.shapedCursorAxis(value, deadzone);
    }

    private static float clampAxis(float value) {
        return ControllerState.clampAxis(value);
    }

    private static float getEntityFloat(Object value, String fieldName) throws ReflectiveOperationException {
        java.lang.reflect.Field field = findField(value.getClass(), fieldName);
        field.setAccessible(true);
        return field.getFloat(value);
    }

    private static void setEntityFloat(Object value, String fieldName, float fieldValue) throws ReflectiveOperationException {
        java.lang.reflect.Field field = findField(value.getClass(), fieldName);
        field.setAccessible(true);
        field.setFloat(value, fieldValue);
    }

    private static java.lang.reflect.Field findField(Class<?> type, String fieldName) throws NoSuchFieldException {
        Class<?> current = type;
        while (current != null) {
            try {
                return current.getDeclaredField(fieldName);
            } catch (NoSuchFieldException ignored) {
                current = current.getSuperclass();
            }
        }
        throw new NoSuchFieldException(fieldName);
    }

    private static java.lang.reflect.Method findMethod(Class<?> type, String methodName) throws NoSuchMethodException {
        Class<?> current = type;
        while (current != null) {
            try {
                return current.getDeclaredMethod(methodName);
            } catch (NoSuchMethodException ignored) {
                current = current.getSuperclass();
            }
        }
        throw new NoSuchMethodException(methodName);
    }

    private static java.lang.reflect.Method findMethod(Class<?> type, String methodName, Class<?>... parameterTypes) throws NoSuchMethodException {
        Class<?> current = type;
        while (current != null) {
            try {
                return current.getDeclaredMethod(methodName, parameterTypes);
            } catch (NoSuchMethodException ignored) {
                current = current.getSuperclass();
            }
        }
        throw new NoSuchMethodException(methodName);
    }

    private static double clamp(double value, double min, double max) {
        return ControllerRuntime.clamp(value, min, max);
    }

    private static void captureControllerState() {
        float[] axes = new float[] {
            GLFW_STATE.axes(GLFW.GLFW_GAMEPAD_AXIS_LEFT_X),
            GLFW_STATE.axes(GLFW.GLFW_GAMEPAD_AXIS_LEFT_Y),
            GLFW_STATE.axes(GLFW.GLFW_GAMEPAD_AXIS_RIGHT_X),
            GLFW_STATE.axes(GLFW.GLFW_GAMEPAD_AXIS_RIGHT_Y),
            GLFW_STATE.axes(GLFW.GLFW_GAMEPAD_AXIS_LEFT_TRIGGER),
            GLFW_STATE.axes(GLFW.GLFW_GAMEPAD_AXIS_RIGHT_TRIGGER)
        };
        boolean[] buttons = new boolean[15];
        for (int i = 0; i < buttons.length; i++) {
            buttons[i] = GLFW_STATE.buttons(i) == GLFW.GLFW_PRESS;
        }
        CONTROLLER_STATE.capture(axes, buttons);
    }

    private static void finishFrame() {
        CONTROLLER_STATE.finishFrame(BanditControllerSettings.get().triggerDeadzone);
    }

    private static ControllerAxis axisFor(int index) {
        switch (index) {
            case GLFW.GLFW_GAMEPAD_AXIS_LEFT_X: return ControllerAxis.LEFT_X;
            case GLFW.GLFW_GAMEPAD_AXIS_LEFT_Y: return ControllerAxis.LEFT_Y;
            case GLFW.GLFW_GAMEPAD_AXIS_RIGHT_X: return ControllerAxis.RIGHT_X;
            case GLFW.GLFW_GAMEPAD_AXIS_RIGHT_Y: return ControllerAxis.RIGHT_Y;
            case GLFW.GLFW_GAMEPAD_AXIS_LEFT_TRIGGER: return ControllerAxis.LEFT_TRIGGER;
            case GLFW.GLFW_GAMEPAD_AXIS_RIGHT_TRIGGER: return ControllerAxis.RIGHT_TRIGGER;
            default: return ControllerAxis.LEFT_X;
        }
    }

    private static ControllerButton buttonFor(int index) {
        switch (index) {
            case GLFW.GLFW_GAMEPAD_BUTTON_A: return ControllerButton.A;
            case GLFW.GLFW_GAMEPAD_BUTTON_B: return ControllerButton.B;
            case GLFW.GLFW_GAMEPAD_BUTTON_X: return ControllerButton.X;
            case GLFW.GLFW_GAMEPAD_BUTTON_Y: return ControllerButton.Y;
            case GLFW.GLFW_GAMEPAD_BUTTON_LEFT_BUMPER: return ControllerButton.LEFT_BUMPER;
            case GLFW.GLFW_GAMEPAD_BUTTON_RIGHT_BUMPER: return ControllerButton.RIGHT_BUMPER;
            case GLFW.GLFW_GAMEPAD_BUTTON_BACK: return ControllerButton.BACK;
            case GLFW.GLFW_GAMEPAD_BUTTON_START: return ControllerButton.START;
            case GLFW.GLFW_GAMEPAD_BUTTON_GUIDE: return ControllerButton.GUIDE;
            case GLFW.GLFW_GAMEPAD_BUTTON_LEFT_THUMB: return ControllerButton.LEFT_THUMB;
            case GLFW.GLFW_GAMEPAD_BUTTON_RIGHT_THUMB: return ControllerButton.RIGHT_THUMB;
            case GLFW.GLFW_GAMEPAD_BUTTON_DPAD_UP: return ControllerButton.DPAD_UP;
            case GLFW.GLFW_GAMEPAD_BUTTON_DPAD_RIGHT: return ControllerButton.DPAD_RIGHT;
            case GLFW.GLFW_GAMEPAD_BUTTON_DPAD_DOWN: return ControllerButton.DPAD_DOWN;
            case GLFW.GLFW_GAMEPAD_BUTTON_DPAD_LEFT: return ControllerButton.DPAD_LEFT;
            default: return ControllerButton.A;
        }
    }
}
