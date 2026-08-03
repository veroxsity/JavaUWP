package banditvault.fabriccontroller;

import banditvault.controllercore.ControllerAxis;
import banditvault.controllercore.ControllerAction;
import banditvault.controllercore.ControllerButton;
import banditvault.controllercore.ControllerInput;
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
import net.minecraft.class_11908;
import net.minecraft.class_1713;
import net.minecraft.class_1735;
import net.minecraft.class_239;
import net.minecraft.class_2561;
import net.minecraft.class_3675;
import net.minecraft.class_3908;
import net.minecraft.class_3965;
import net.minecraft.class_437;
import net.minecraft.class_4185;
import net.minecraft.class_746;
import net.minecraft.class_9919;
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
    private static boolean loggedKeyReflectionFailure;
    private static boolean active;
    private static double cursorX = -1.0;
    private static double cursorY = -1.0;
    private static long lastLookNanos;
    private static long lastScreenCursorNanos;
    private static int scrollCooldown;
    private static boolean crouchToggled;
    private static boolean sprintToggled;
    private static Object lastCursorScreen;
    private static Object lastRelayCursorScreen;
    private static double lastRelayCursorX = Double.NaN;
    private static double lastRelayCursorY = Double.NaN;
    private static boolean relayOwnsCursor;
    private static boolean snapStickLatched;
    private static class_304 radialPressedKey;
    private static boolean radialPressedAsKeyboard;
    private static final java.util.Map<String, class_304> JAVA_KEYS_DOWN = new java.util.HashMap<String, class_304>();
    private static final java.util.Set<String> RAW_JAVA_KEYS_DOWN = new java.util.HashSet<String>();
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
            releaseJavaKeyMappings();
            BanditControllerKeyboard.close();
            return;
        }

        releaseRadialKey();
        if (!poll()) {
            releaseJavaKeyMappings();
            BanditControllerKeyboard.close();
            if (client.field_1755 instanceof BanditControllerRadialScreen) {
                client.method_1507(null);
            }
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

        tickJavaKeyMappings(
            client,
            BanditControllerSettings.get(),
            client.field_1755 == null && client.field_1724 != null && client.method_1569());

        if (client.field_1755 != null) {
            lastLookNanos = 0L;
            releaseGameplayKeys(client, false);
            tickScreen(client, client.field_1755);
        } else {
            BanditControllerKeyboard.close();
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
        if (screen instanceof BanditControllerSettingsScreen || screen instanceof BanditControllerRadialScreen) {
            return;
        }
        if (!active || screen == null || context == null || client == null || client.field_1755 != screen) {
            return;
        }
        if (!relayOwnsCursor) renderControllerGuide(screen, context, client);
        if (relayOwnsCursor || cursorX < 0.0 || cursorY < 0.0) return;

        int x = (int)Math.round(cursorX);
        int y = (int)Math.round(cursorY);
        FabricScreenApi.drawCursor(context, x, y);
    }

    private static void renderControllerGuide(class_437 screen, class_332 context, class_310 client) {
        BanditControllerSettings settings = BanditControllerSettings.get();
        boolean container = screen instanceof class_465;
        BanditControllerGuide.drawVerticalLeft(context, client.field_1772, 8, 8,
            new ControllerInput[] {
                settings.binding(ControllerAction.MENU_ACCEPT),
                settings.binding(ControllerAction.MENU_CANCEL)
            },
            new String[] { "Select", "Back" });
        BanditControllerGuide.drawVerticalRight(context, client.field_1772, screen.field_22789 - 8, 8,
            container
                ? new ControllerInput[] {
                    settings.binding(ControllerAction.QUICK_MOVE),
                    settings.binding(ControllerAction.MENU_SECONDARY),
                    settings.binding(ControllerAction.SNAP_FREE_TOGGLE)
                }
                : new ControllerInput[] { settings.binding(ControllerAction.SNAP_FREE_TOGGLE) },
            container
                ? new String[] { "Quick Move", "Secondary", cursorMode == CursorMode.SNAP ? "Free Cursor" : "Snap Cursor" }
                : new String[] { cursorMode == CursorMode.SNAP ? "Free Cursor" : "Snap Cursor" });
    }

    public static boolean shouldRenderCursorInBaseScreen(class_437 screen) {
        return !(screen instanceof class_465);
    }

    public static void updateScreenCursorBeforeRender(class_437 screen, int mouseX, int mouseY) {
        class_310 client = class_310.method_1551();
        ensureMenuCursorMode(client);
        if (screen instanceof BanditControllerSettingsScreen || screen instanceof BanditControllerRadialScreen) {
            return;
        }
        if (!active || screen == null || client == null || client.field_1755 != screen) {
            return;
        }
        observeRelayCursor(screen);
        if (relayOwnsCursor) {
            screen.method_16014(lastRelayCursorX, lastRelayCursorY);
            return;
        }
        if (cursorMode == CursorMode.FREE) {
            updateScreenCursor(client, screen, true);
        } else {
            screen.method_16014(cursorX, cursorY);
        }
    }

    public static void renderCursorOverlay(class_310 client) {
        // 1.20.1 draws the on-screen cursor from Screen.render via DrawContext.
    }

    public static void renderGameplayGuide(class_332 context) {
        class_310 client = class_310.method_1551();
        if (!active || context == null || client == null || client.field_1755 != null || client.field_1724 == null || client.field_1690.field_1842) return;
        BanditControllerSettings settings = BanditControllerSettings.get();
        class_3965 blockHit = client.field_1765 instanceof class_3965
            && client.field_1765.method_17783() == class_239.class_240.field_1332
                ? (class_3965)client.field_1765
                : null;
        boolean openable = blockHit != null && client.field_1687 != null && client.field_1687.method_8321(blockHit.method_17777()) instanceof class_3908;
        boolean hasTarget = client.field_1765 != null && client.field_1765.method_17783() != class_239.class_240.field_1333;
        boolean mainHandItem = !client.field_1724.method_6047().method_7960();
        boolean heldItem = mainHandItem || !client.field_1724.method_6079().method_7960();
        BanditControllerGuide.drawVerticalLeft(context, client.field_1772, 8, 8,
            new ControllerInput[] {
                settings.binding(ControllerAction.JUMP),
                settings.binding(ControllerAction.SNEAK)
            },
            new String[] { "Jump", "Sneak" });
        BanditControllerGuide.drawVerticalRight(context, client.field_1772, context.method_51421() - 8, 8,
            new ControllerInput[] {
                settings.binding(ControllerAction.INVENTORY),
                settings.binding(ControllerAction.RADIAL_MENU),
                blockHit == null ? ControllerInput.UNBOUND : settings.binding(ControllerAction.ATTACK),
                hasTarget || heldItem ? settings.binding(ControllerAction.USE) : ControllerInput.UNBOUND,
                mainHandItem ? settings.binding(ControllerAction.DROP) : ControllerInput.UNBOUND,
                settings.binding(ControllerAction.SWAP_HANDS)
            },
            new String[] { "Open Inventory", "Radial Menu", "Mine", openable ? "Open" : "Use", "Drop Item", "Swap Hands" });
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

    public static float[] analogMovement() {
        class_310 client = class_310.method_1551();
        if (client == null || client.field_1755 != null || !poll()) {
            return null;
        }
        return ControllerRuntime.shapedMovement(
            -axis(GLFW.GLFW_GAMEPAD_AXIS_LEFT_X),
            -axis(GLFW.GLFW_GAMEPAD_AXIS_LEFT_Y),
            BanditControllerSettings.get().moveDeadzone);
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

        if (pressed(settings, ControllerAction.RADIAL_MENU)) {
            releaseGameplayKeys(client, true);
            client.method_1507(new BanditControllerRadialScreen());
            return;
        }

        if (pressed(GLFW.GLFW_GAMEPAD_BUTTON_BACK)) {
            client.method_1507(new BanditControllerSettingsScreen(null));
            return;
        }

        setHeld(options.field_1903, button(settings, ControllerAction.JUMP));
        if (settings.toggleCrouch) {
            if (pressed(settings, ControllerAction.SNEAK)) {
                crouchToggled = !crouchToggled;
            }
            setHeld(options.field_1832, crouchToggled);
        } else {
            crouchToggled = false;
            setHeld(options.field_1832, button(settings, ControllerAction.SNEAK));
        }
        setHeld(options.field_1886, button(settings, ControllerAction.ATTACK));
        setHeld(options.field_1904, button(settings, ControllerAction.USE));
        if (settings.toggleSprint) {
            if (pressed(settings, ControllerAction.SPRINT)) {
                sprintToggled = !sprintToggled;
            }
            setHeld(options.field_1867, sprintToggled);
        } else {
            sprintToggled = false;
            setHeld(options.field_1867, button(settings, ControllerAction.SPRINT));
        }

        if (pressed(settings, ControllerAction.ATTACK)) {
            pressKey(options.field_1886);
        }
        if (pressed(settings, ControllerAction.USE)) {
            pressKey(options.field_1904);
        }
        if (pressed(settings, ControllerAction.INVENTORY)) {
            pressKey(options.field_1822);
        }
        if (pressed(settings, ControllerAction.DROP)) {
            pressKey(options.field_1869);
        }
        if (pressed(settings, ControllerAction.SWAP_HANDS)) {
            class_304 swapHands = class_304.method_65807("key.swapOffhand");
            if (swapHands != null) pressKey(swapHands);
        }
        if (pressed(settings, ControllerAction.PICK_BLOCK)) {
            pressKey(options.field_1871);
        }
        if (pressed(settings, ControllerAction.HOTBAR_PREVIOUS)) {
            changeHotbarSlot(client.field_1724, -1);
        }
        if (pressed(settings, ControllerAction.HOTBAR_NEXT)) {
            changeHotbarSlot(client.field_1724, 1);
        }

        if (pressed(settings, ControllerAction.PAUSE)) {
            client.method_20539(false);
        }
    }

    private static void tickScreen(class_310 client, class_437 screen) {
        BanditControllerSettings settings = BanditControllerSettings.get();

        if (screen instanceof BanditControllerRadialScreen) {
            BanditControllerRadialScreen radial = (BanditControllerRadialScreen)screen;
            if (client.field_1724 == null || !client.method_1569()) {
                client.method_1507(null);
                return;
            }
            radial.setSelectedSlot(ControllerRuntime.radialSlot(
                axis(GLFW.GLFW_GAMEPAD_AXIS_RIGHT_X),
                axis(GLFW.GLFW_GAMEPAD_AXIS_RIGHT_Y),
                settings.lookDeadzone));
            if (pressed(settings, ControllerAction.MENU_CANCEL)) {
                client.method_1507(null);
                return;
            }
            if (released(settings, ControllerAction.RADIAL_MENU)) {
                int slot = radial.selectedSlot();
                client.method_1507(null);
                activateRadialSlot(slot);
            }
            return;
        }

        if (screen instanceof BanditControllerSettingsScreen) {
            ((BanditControllerSettingsScreen)screen).handleControllerInput(CONTROLLER_STATE, settings.triggerDeadzone);
            return;
        }

        if (BanditControllerKeyboard.tick(screen)) {
            return;
        }

        ensureScreenCursor(screen);
        float ry = axis(GLFW.GLFW_GAMEPAD_AXIS_RIGHT_Y);

        if (cursorMode == CursorMode.SNAP && !relayOwnsCursor) {
            applySnapTarget(screen, MENU_NAVIGATION.synchronize(screen, cursorX, cursorY));
        }

        if (pressed(settings, ControllerAction.SNAP_FREE_TOGGLE)) {
            takeControllerCursor();
            cursorMode = cursorMode == CursorMode.SNAP ? CursorMode.FREE : CursorMode.SNAP;
            snapStickLatched = false;
            MENU_NAVIGATION.reset(screen);
            if (cursorMode == CursorMode.SNAP) {
                applySnapTarget(screen, MENU_NAVIGATION.discover(screen, cursorX, cursorY));
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

        if (pressed(settings, ControllerAction.MENU_ACCEPT)) {
            if (BanditControllerKeyboard.activate(screen, cursorX, cursorY)) {
                return;
            }
            takeControllerCursor();
            if (cursorMode == CursorMode.SNAP && MENU_NAVIGATION.usesNativeActivation(screen)) {
                FabricScreenApi.keyPressed(screen, GLFW.GLFW_KEY_ENTER, 0, 0);
            } else {
                FabricScreenApi.mousePressed(screen, cursorX, cursorY, LEFT_CLICK);
            }
        }
        if (released(settings, ControllerAction.MENU_ACCEPT) &&
            (cursorMode == CursorMode.FREE || !MENU_NAVIGATION.usesNativeActivation(screen))) {
            FabricScreenApi.mouseReleased(screen, cursorX, cursorY, LEFT_CLICK);
        }
        if (pressed(settings, ControllerAction.MENU_SECONDARY)) {
            takeControllerCursor();
            FabricScreenApi.mousePressed(screen, cursorX, cursorY, RIGHT_CLICK);
        }
        if (released(settings, ControllerAction.MENU_SECONDARY)) {
            FabricScreenApi.mouseReleased(screen, cursorX, cursorY, RIGHT_CLICK);
        }
        if (pressed(settings, ControllerAction.MENU_CANCEL)) {
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
        if (pressed(settings, ControllerAction.QUICK_MOVE)) {
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
            if (ry < -0.35f || (cursorMode == CursorMode.FREE && button(GLFW.GLFW_GAMEPAD_BUTTON_DPAD_UP))) {
                scroll = settings.scrollAmount;
            } else if (ry > 0.35f || (cursorMode == CursorMode.FREE && button(GLFW.GLFW_GAMEPAD_BUTTON_DPAD_DOWN))) {
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
            cursorX = Math.max(1, screen.field_22789 / 2);
            cursorY = Math.max(1, screen.field_22790 / 2);
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

    private static void takeControllerCursor() {
        relayOwnsCursor = false;
        resetRelayCursorBaseline();
    }

    static void resumeMenuAfterKeyboard(class_437 screen) {
        class_310 client = class_310.method_1551();
        if (screen == null || client == null || client.field_1755 != screen) return;
        takeControllerCursor();
        snapStickLatched = false;
        ensureScreenCursor(screen);
        MENU_NAVIGATION.reset(screen);
        if (cursorMode == CursorMode.SNAP) {
            applySnapTarget(screen, MENU_NAVIGATION.discover(screen, cursorX, cursorY));
        }
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
        if (code < 0) return false;
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

    public static String radialKeyLabel(String keyId) {
        if (keyId == null || keyId.isEmpty()) {
            return "Empty";
        }
        class_304 key = class_304.method_65807(keyId);
        return key == null
            ? "Missing: " + keyId
            : key.method_16007().getString() + " - " + class_2561.method_43471(key.method_1431()).getString();
    }

    public static String radialKeyGlyph(String keyId) {
        if (keyId == null || keyId.isEmpty()) {
            return "+";
        }
        class_304 key = class_304.method_65807(keyId);
        if (key == null) {
            return "?";
        }
        String glyph = key.method_16007().getString();
        return glyph.toLowerCase(java.util.Locale.ROOT).contains("not bound") ? "?" : glyph;
    }

    public static String[] radialKeyIds() {
        class_310 client = class_310.method_1551();
        class_304[] keys = client == null || client.field_1690 == null ? null : client.field_1690.field_1839;
        if (keys == null || keys.length == 0) {
            return new String[] { "" };
        }
        int count = 1;
        for (class_304 key : keys) {
            if (key != null) count++;
        }
        String[] ids = new String[count];
        ids[0] = "";
        int index = 1;
        for (class_304 key : keys) {
            if (key != null) ids[index++] = key.method_1431();
        }
        return ids;
    }

    private static void activateRadialSlot(int slot) {
        if (slot < 0 || slot >= 8) {
            return;
        }
        class_304 key = class_304.method_65807(BanditControllerSettings.get().radialSlot(slot));
        if (key == null) {
            return;
        }
        radialPressedAsKeyboard = sendKeyboardKey(key, GLFW.GLFW_PRESS);
        if (!radialPressedAsKeyboard) {
            pressMappedKey(key);
        }
        radialPressedKey = key;
    }

    private static void releaseRadialKey() {
        if (radialPressedKey == null) {
            return;
        }
        if (radialPressedAsKeyboard) sendKeyboardKey(radialPressedKey, GLFW.GLFW_RELEASE);
        radialPressedKey.method_23481(isBoundInputHeld(radialPressedKey));
        radialPressedKey = null;
        radialPressedAsKeyboard = false;
    }

    private static boolean sendKeyboardKey(class_304 key, int action) {
        class_3675.class_306 input = key.method_1429();
        class_3675.class_307 type = input.method_1442();
        if (type != class_3675.class_307.field_1668 && type != class_3675.class_307.field_1671) return false;
        class_310 client = class_310.method_1551();
        if (client == null || client.method_22683() == null) return false;
        try {
            int code = input.method_1444();
            if (code < 0) return false;
            class_11908 event = new class_11908(
                type == class_3675.class_307.field_1668 ? code : GLFW.GLFW_KEY_UNKNOWN,
                type == class_3675.class_307.field_1671 ? code : 0,
                0);
            java.lang.reflect.Method handler = findMethod(
                client.field_1774.getClass(), "method_1466", long.class, int.class, class_11908.class);
            handler.setAccessible(true);
            handler.invoke(client.field_1774, client.method_22683().method_4490(), action, event);
            return true;
        } catch (Throwable t) {
            if (!loggedKeyReflectionFailure) {
                loggedKeyReflectionFailure = true;
                FabricControllerLog.logException("Bandit controller raw keyboard activation unavailable", t);
            }
            return false;
        }
    }

    private static void pressMappedKey(class_304 key) {
        try {
            java.lang.reflect.Field presses = findField(key.getClass(), "field_1661");
            presses.setAccessible(true);
            presses.setInt(key, presses.getInt(key) + 1);
        } catch (Throwable t) {
            if (!loggedKeyReflectionFailure) {
                loggedKeyReflectionFailure = true;
                FabricControllerLog.logException("Bandit controller fell back to physical-key activation", t);
            }
            pressKey(key);
        }
        key.method_23481(true);
    }

    private static void tickJavaKeyMappings(class_310 client, BanditControllerSettings settings, boolean enabled) {
        java.util.Iterator<java.util.Map.Entry<String, class_304>> active = JAVA_KEYS_DOWN.entrySet().iterator();
        while (active.hasNext()) {
            java.util.Map.Entry<String, class_304> entry = active.next();
            ControllerInput input = settings.javaBinding(entry.getKey());
            if (enabled && input.held(CONTROLLER_STATE, settings.triggerDeadzone)) continue;
            if (RAW_JAVA_KEYS_DOWN.remove(entry.getKey())) sendKeyboardKey(entry.getValue(), GLFW.GLFW_RELEASE);
            entry.getValue().method_23481(isBoundInputHeld(entry.getValue()));
            active.remove();
        }
        if (!enabled || client.field_1690 == null || client.field_1690.field_1839 == null) return;
        for (class_304 key : client.field_1690.field_1839) {
            if (key == null) continue;
            String keyId = key.method_1431();
            ControllerInput input = settings.javaBinding(keyId);
            if (JAVA_KEYS_DOWN.containsKey(keyId) || !input.pressed(CONTROLLER_STATE, settings.triggerDeadzone)) continue;
            boolean raw = sendKeyboardKey(key, GLFW.GLFW_PRESS);
            if (raw) {
                RAW_JAVA_KEYS_DOWN.add(keyId);
            } else {
                pressMappedKey(key);
            }
            JAVA_KEYS_DOWN.put(keyId, key);
        }
    }

    private static void releaseJavaKeyMappings() {
        for (java.util.Map.Entry<String, class_304> entry : JAVA_KEYS_DOWN.entrySet()) {
            if (RAW_JAVA_KEYS_DOWN.contains(entry.getKey())) sendKeyboardKey(entry.getValue(), GLFW.GLFW_RELEASE);
            entry.getValue().method_23481(isBoundInputHeld(entry.getValue()));
        }
        JAVA_KEYS_DOWN.clear();
        RAW_JAVA_KEYS_DOWN.clear();
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

    private static boolean button(BanditControllerSettings settings, ControllerAction action) {
        return binding(settings, action).held(CONTROLLER_STATE, settings.triggerDeadzone);
    }

    private static boolean pressed(BanditControllerSettings settings, ControllerAction action) {
        return binding(settings, action).pressed(CONTROLLER_STATE, settings.triggerDeadzone);
    }

    private static boolean released(BanditControllerSettings settings, ControllerAction action) {
        return binding(settings, action).released(CONTROLLER_STATE, settings.triggerDeadzone);
    }

    private static ControllerInput binding(BanditControllerSettings settings, ControllerAction action) {
        return settings.binding(action);
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

        boolean activity = false;
        for (int i = 0; i < buttons.length; i++) {
            if (buttons[i]) {
                activity = true;
                break;
            }
        }
        if (!activity) {
            for (int i = 0; i < 4; i++) {
                if (Math.abs(axes[i]) > 0.2f) {
                    activity = true;
                    break;
                }
            }
        }
        if (!activity && (axes[4] > 0.0f || axes[5] > 0.0f)) {
            activity = true;
        }
        if (activity) {
            resetInactivityTimer(class_310.method_1551());
        }
    }

    private static void finishFrame() {
        CONTROLLER_STATE.finishFrame(BanditControllerSettings.get().triggerDeadzone);
    }

    private static void resetInactivityTimer(class_310 client) {
        if (client == null) {
            return;
        }
        // controller input goes through KeyMapping.setDown, never MC's kbd/mouse handlers, so the 1.21.2 afk fps limiter never sees us
        // class_9919 = framerate limit tracker, method_61964() its getter, method_61939() stamps last-input time to now
        class_9919 tracker = client.method_61964();
        if (tracker != null) {
            tracker.method_61939();
        }
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
