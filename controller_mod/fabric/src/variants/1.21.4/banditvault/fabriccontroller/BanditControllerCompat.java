package banditvault.fabriccontroller;

import banditvault.controllercore.ControllerAxis;
import banditvault.controllercore.ControllerButton;
import banditvault.controllercore.GridNavigation;
import banditvault.controllercore.ControllerRuntime;
import banditvault.controllercore.ControllerState;
import banditvault.fabriccontroller.mixin.BanditControllerContainerAccessor;
import net.minecraft.class_304;
import net.minecraft.class_310;
import net.minecraft.class_315;
import net.minecraft.class_332;
import net.minecraft.class_465;
import net.minecraft.class_1041;
import net.minecraft.class_1713;
import net.minecraft.class_1735;
import net.minecraft.class_2561;
import net.minecraft.class_437;
import net.minecraft.class_4185;
import net.minecraft.class_746;
import org.lwjgl.glfw.GLFW;
import org.lwjgl.glfw.GLFWGamepadState;

public final class BanditControllerCompat {
    private static final int GAMEPAD_ID = GLFW.GLFW_JOYSTICK_1;
    private static final int LEFT_CLICK = 0;
    private static final int RIGHT_CLICK = 1;

    private static final GLFWGamepadState GLFW_STATE = GLFWGamepadState.create();
    private static final ControllerState CONTROLLER_STATE = new ControllerState();
    private static final Fabric12111MenuNavigation MENU_NAVIGATION = new Fabric12111MenuNavigation();
    private static boolean loggedReady;
    private static boolean loggedLookReflectionFailure;
    private static boolean loggedHotbarReflectionFailure;
    private static boolean loggedQuickMoveReflectionFailure;
    private static boolean active;
    private static double cursorX = -1.0;
    private static double cursorY = -1.0;
    private static double renderedCursorX = -1.0;
    private static double renderedCursorY = -1.0;
    private static long lastLookNanos;
    private static long lastScreenCursorNanos;
    private static long lastRenderedCursorNanos;
    private static int scrollCooldown;
    private static boolean crouchToggled;
    private static boolean sprintToggled;
    private static Object lastCursorScreen;
    private static Object lastRelayCursorScreen;
    private static double lastRelayCursorX = Double.NaN;
    private static double lastRelayCursorY = Double.NaN;
    private static boolean relayOwnsCursor;
    private static boolean snapStickLatched;
    private static CursorMode cursorMode = CursorMode.SNAP;

    private static final double RELAY_CURSOR_MOVE_EPSILON = 0.5;
    private static final double CONTROLLER_CURSOR_TAKEOVER_THRESHOLD = 0.20;

    private enum CursorMode {
        SNAP,
        FREE
    }

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
                relayOwnsCursor = false;
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
            relayOwnsCursor = false;
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

    public static void renderCursor(class_437 screen, class_332 context) {
        class_310 client = class_310.method_1551();
        if (!active || relayOwnsCursor || screen == null || context == null || client == null || client.field_1755 != screen || renderedCursorX < 0.0 || renderedCursorY < 0.0) {
            return;
        }

        int x = (int)Math.round(renderedCursorX);
        int y = (int)Math.round(renderedCursorY);
        FabricScreenApi.drawCursor(context, x, y);
    }

    public static boolean shouldRenderCursorInBaseScreen(class_437 screen) {
        return !(screen instanceof class_465);
    }

    public static void updateScreenCursorBeforeRender(class_437 screen, int mouseX, int mouseY) {
        class_310 client = class_310.method_1551();
        ensureMenuCursorMode(client);
        if (!active || screen == null || client == null || client.field_1755 != screen) {
            return;
        }
        observeRelayCursor(screen);
        if (relayOwnsCursor) {
            screen.method_16014(lastRelayCursorX, lastRelayCursorY);
            lastRenderedCursorNanos = System.nanoTime();
            return;
        }
        if (cursorMode == CursorMode.FREE) {
            updateScreenCursor(client, screen, true);
            renderedCursorX = cursorX;
            renderedCursorY = cursorY;
        } else {
            advanceRenderedCursor();
            screen.method_16014(cursorX, cursorY);
        }
    }

    public static void renderCursorOverlay(class_310 client) {
        // 1.20.1 draws the on-screen cursor from Screen.render via DrawContext.
    }

    public static int screenMouseX(class_437 screen, int fallback) {
        class_310 client = class_310.method_1551();
        if (!active || relayOwnsCursor || cursorX < 0.0 || client == null || client.field_1755 != screen) {
            return fallback;
        }
        return (int)Math.round(cursorX);
    }

    public static int screenMouseY(class_437 screen, int fallback) {
        class_310 client = class_310.method_1551();
        if (!active || relayOwnsCursor || cursorY < 0.0 || client == null || client.field_1755 != screen) {
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

    private static void ensureMenuCursorMode(class_310 client) {
        if (client == null || client.field_1755 == null || client.method_22683() == null) {
            return;
        }
        long window = client.method_22683().method_4490();
        if (window != 0L && GLFW.glfwGetInputMode(window, GLFW.GLFW_CURSOR) != GLFW.GLFW_CURSOR_NORMAL) {
            GLFW.glfwSetInputMode(window, GLFW.GLFW_CURSOR, GLFW.GLFW_CURSOR_NORMAL);
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
        ensureScreenCursor(screen);
        float ry = axis(GLFW.GLFW_GAMEPAD_AXIS_RIGHT_Y);

        if (cursorMode == CursorMode.SNAP && !relayOwnsCursor) {
            applySnapTarget(screen, MENU_NAVIGATION.synchronize(screen, cursorX, cursorY));
        }

        if (pressed(GLFW.GLFW_GAMEPAD_BUTTON_BACK)) {
            takeControllerCursor();
            cursorMode = cursorMode == CursorMode.SNAP ? CursorMode.FREE : CursorMode.SNAP;
            snapStickLatched = false;
            MENU_NAVIGATION.reset(screen);
            if (cursorMode == CursorMode.SNAP) {
                applySnapTarget(screen, MENU_NAVIGATION.discover(screen, cursorX, cursorY));
            } else {
                renderedCursorX = cursorX;
                renderedCursorY = cursorY;
            }
            FabricControllerLog.log("Menu cursor mode changed to " + cursorMode + " screen=" + screen.getClass().getName());
            return;
        }

        if (cursorMode == CursorMode.SNAP) {
            GridNavigation.Direction direction = snapDirection();
            if (direction != null) {
                takeControllerCursor();
                applySnapTarget(screen, MENU_NAVIGATION.move(screen, direction, cursorX, cursorY));
            }
        } else {
            snapStickLatched = false;
        }

        if (pressed(GLFW.GLFW_GAMEPAD_BUTTON_A)) {
            takeControllerCursor();
            if (cursorMode == CursorMode.SNAP && MENU_NAVIGATION.usesNativeActivation(screen)) {
                FabricScreenApi.keyPressed(screen, GLFW.GLFW_KEY_ENTER, 0, 0);
            } else {
                FabricScreenApi.mousePressed(screen, cursorX, cursorY, LEFT_CLICK);
            }
        }
        if (released(GLFW.GLFW_GAMEPAD_BUTTON_A) &&
            (cursorMode == CursorMode.FREE || !MENU_NAVIGATION.usesNativeActivation(screen))) {
            FabricScreenApi.mouseReleased(screen, cursorX, cursorY, LEFT_CLICK);
        }
        if (pressed(GLFW.GLFW_GAMEPAD_BUTTON_X)) {
            takeControllerCursor();
            FabricScreenApi.mousePressed(screen, cursorX, cursorY, RIGHT_CLICK);
        }
        if (released(GLFW.GLFW_GAMEPAD_BUTTON_X)) {
            FabricScreenApi.mouseReleased(screen, cursorX, cursorY, RIGHT_CLICK);
        }
        if (pressed(GLFW.GLFW_GAMEPAD_BUTTON_B)) {
            takeControllerCursor();
            if (MENU_NAVIGATION.handleBack(screen)) {
                if (cursorMode == CursorMode.SNAP) {
                    applySnapTarget(screen, MENU_NAVIGATION.discover(screen, cursorX, cursorY));
                }
            } else if (!FabricScreenApi.keyPressed(screen, GLFW.GLFW_KEY_ESCAPE, 0, 0)) {
                client.method_1507(null);
            }
            return;
        }
        if (pressed(GLFW.GLFW_GAMEPAD_BUTTON_Y)) {
            takeControllerCursor();
            quickMoveFocusedSlot(screen);
        }

        if (client.field_1755 != screen) {
            return;
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
                FabricScreenApi.scroll(screen, cursorX, cursorY, scroll);
                scrollCooldown = 5;
            }
        }
    }

    private static void ensureScreenCursor(class_437 screen) {
        if (screen != lastCursorScreen) {
            lastCursorScreen = screen;
            lastScreenCursorNanos = 0L;
            lastRenderedCursorNanos = 0L;
            cursorX = Math.max(1, screen.field_22789 / 2);
            cursorY = Math.max(1, screen.field_22790 / 2);
            renderedCursorX = cursorX;
            renderedCursorY = cursorY;
            snapStickLatched = false;
            resetRelayCursorBaseline();
            MENU_NAVIGATION.reset(screen);
            if (cursorMode == CursorMode.SNAP) {
                applySnapTarget(screen, MENU_NAVIGATION.discover(screen, cursorX, cursorY));
            }
            return;
        }
        if (cursorX < 0.0 || cursorY < 0.0) {
            cursorX = Math.max(1, screen.field_22789 / 2);
            cursorY = Math.max(1, screen.field_22790 / 2);
            renderedCursorX = cursorX;
            renderedCursorY = cursorY;
        }
    }

    private static GridNavigation.Direction snapDirection() {
        if (pressed(GLFW.GLFW_GAMEPAD_BUTTON_DPAD_UP)) return GridNavigation.Direction.UP;
        if (pressed(GLFW.GLFW_GAMEPAD_BUTTON_DPAD_DOWN)) return GridNavigation.Direction.DOWN;
        if (pressed(GLFW.GLFW_GAMEPAD_BUTTON_DPAD_LEFT)) return GridNavigation.Direction.LEFT;
        if (pressed(GLFW.GLFW_GAMEPAD_BUTTON_DPAD_RIGHT)) return GridNavigation.Direction.RIGHT;

        float x = axis(GLFW.GLFW_GAMEPAD_AXIS_LEFT_X);
        float y = axis(GLFW.GLFW_GAMEPAD_AXIS_LEFT_Y);
        if (Math.max(Math.abs(x), Math.abs(y)) < 0.35f) {
            snapStickLatched = false;
            return null;
        }
        if (snapStickLatched || Math.max(Math.abs(x), Math.abs(y)) < 0.65f) {
            return null;
        }
        snapStickLatched = true;
        if (Math.abs(x) > Math.abs(y)) {
            return x < 0.0f ? GridNavigation.Direction.LEFT : GridNavigation.Direction.RIGHT;
        }
        return y < 0.0f ? GridNavigation.Direction.UP : GridNavigation.Direction.DOWN;
    }

    private static void applySnapTarget(class_437 screen, Fabric12111MenuNavigation.Position position) {
        if (screen == null || position == null) {
            return;
        }
        cursorX = clamp(position.x, 0.0, Math.max(1, screen.field_22789 - 1));
        cursorY = clamp(position.y, 0.0, Math.max(1, screen.field_22790 - 1));
        screen.method_16014(cursorX, cursorY);
    }

    private static void advanceRenderedCursor() {
        long now = System.nanoTime();
        double seconds = 1.0 / 60.0;
        if (lastRenderedCursorNanos != 0L) {
            seconds = (now - lastRenderedCursorNanos) / 1000000000.0;
            seconds = clamp(seconds, 0.0, 1.0 / 20.0);
        }
        lastRenderedCursorNanos = now;
        double blend = 1.0 - Math.exp(-18.0 * seconds);
        renderedCursorX += (cursorX - renderedCursorX) * blend;
        renderedCursorY += (cursorY - renderedCursorY) * blend;
        if (Math.abs(renderedCursorX - cursorX) < 0.05) renderedCursorX = cursorX;
        if (Math.abs(renderedCursorY - cursorY) < 0.05) renderedCursorY = cursorY;
    }

    private static void takeControllerCursor() {
        relayOwnsCursor = false;
        resetRelayCursorBaseline();
    }

    private static void resetRelayCursorBaseline() {
        lastRelayCursorScreen = null;
        lastRelayCursorX = Double.NaN;
        lastRelayCursorY = Double.NaN;
    }

    private static void updateScreenCursor(class_310 client, class_437 screen, boolean frameTimed) {
        if (client == null || screen == null || !poll()) {
            return;
        }
        ensureScreenCursor(screen);
        BanditControllerSettings settings = BanditControllerSettings.get();
        float rawX = axis(GLFW.GLFW_GAMEPAD_AXIS_LEFT_X);
        float rawY = axis(GLFW.GLFW_GAMEPAD_AXIS_LEFT_Y);
        if (relayOwnsCursor) {
            double takeoverMagnitude = Math.max(Math.abs(rawX), Math.abs(rawY));
            if (takeoverMagnitude < CONTROLLER_CURSOR_TAKEOVER_THRESHOLD) {
                lastScreenCursorNanos = System.nanoTime();
                return;
            }
            takeControllerCursor();
        }
        double dx = shapedCursorAxis(rawX, settings.cursorDeadzone);
        double dy = shapedCursorAxis(rawY, settings.cursorDeadzone);

        double scale = settings.cursorSpeed;
        if (frameTimed) {
            long now = System.nanoTime();
            double seconds = 1.0 / 60.0;
            if (lastScreenCursorNanos != 0L) {
                seconds = (now - lastScreenCursorNanos) / 1000000000.0;
                if (seconds < 0.0) {
                    seconds = 0.0;
                } else if (seconds > 1.0 / 20.0) {
                    seconds = 1.0 / 20.0;
                }
            }
            lastScreenCursorNanos = now;
            scale *= seconds * 30.0;
        }

        if (dx != 0.0 || dy != 0.0) {
            cursorX = clamp(cursorX + dx * scale, 0.0, Math.max(1, screen.field_22789 - 1));
            cursorY = clamp(cursorY + dy * scale, 0.0, Math.max(1, screen.field_22790 - 1));
            screen.method_16014(cursorX, cursorY);
        }
    }

    private static void observeRelayCursor(class_437 screen) {
        class_310 client = class_310.method_1551();
        if (client == null || client.field_1729 == null) {
            return;
        }
        double mouseX = client.field_1729.method_1603();
        double mouseY = client.field_1729.method_1604();
        class_1041 window = client.method_22683();
        if (window != null) {
            mouseX = mouseX * screen.field_22789 / Math.max(1.0, window.method_4480());
            mouseY = mouseY * screen.field_22790 / Math.max(1.0, window.method_4507());
        }
        if (screen != lastRelayCursorScreen || Double.isNaN(lastRelayCursorX) || Double.isNaN(lastRelayCursorY)) {
            lastRelayCursorScreen = screen;
            lastRelayCursorX = mouseX;
            lastRelayCursorY = mouseY;
            return;
        }

        if (Math.abs(mouseX - lastRelayCursorX) > RELAY_CURSOR_MOVE_EPSILON ||
            Math.abs(mouseY - lastRelayCursorY) > RELAY_CURSOR_MOVE_EPSILON) {
            if (!relayOwnsCursor) {
                screen.method_48267();
            }
            relayOwnsCursor = true;
        }
        lastRelayCursorX = mouseX;
        lastRelayCursorY = mouseY;
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
            class_1735 slot = cursorMode == CursorMode.SNAP
                ? MENU_NAVIGATION.selectedSlot(screen)
                : ((BanditControllerContainerAccessor) screen).banditvault$getSlotUnderMouse();
            if (slot == null || !slot.method_7681()) {
                return;
            }
            ((BanditControllerContainerAccessor) screen).banditvault$slotClicked(slot, slot.field_7874, 0, class_1713.field_7794);
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

    public static class_4185 createButton(int x, int y, int w, int h, String label, class_4185.class_4241 onPress) {
        return class_4185.method_46430(class_2561.method_30163(label), onPress)
            .method_46434(x, y, w, h)
            .method_46431();
    }
}
