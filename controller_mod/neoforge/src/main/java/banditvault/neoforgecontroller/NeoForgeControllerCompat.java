package banditvault.neoforgecontroller;

import banditvault.controllercore.ControllerAxis;
import banditvault.controllercore.ControllerButton;
import banditvault.controllercore.ControllerRuntime;
import banditvault.controllercore.ControllerState;
import banditvault.controllercore.GridNavigation;
import banditvault.neoforgecontroller.mixin.NeoForgeControllerContainerAccessor;
import com.mojang.blaze3d.platform.InputConstants;
import net.minecraft.client.KeyMapping;
import net.minecraft.client.Minecraft;
import net.minecraft.client.Options;
import net.minecraft.client.gui.GuiGraphics;
import net.minecraft.client.gui.components.Button;
import net.minecraft.client.gui.screens.Screen;
import net.minecraft.client.gui.screens.inventory.AbstractContainerScreen;
import net.minecraft.client.player.LocalPlayer;
import net.minecraft.network.chat.Component;
import net.minecraft.world.inventory.ClickType;
import net.minecraft.world.inventory.Slot;
import org.lwjgl.glfw.GLFW;
import org.lwjgl.glfw.GLFWGamepadState;

public final class NeoForgeControllerCompat {
    private static final int GAMEPAD_ID = GLFW.GLFW_JOYSTICK_1;
    private static final int LEFT_CLICK = 0;
    private static final int RIGHT_CLICK = 1;
    private static final double RELAY_CURSOR_MOVE_EPSILON = 0.75;
    private static final double CONTROLLER_CURSOR_TAKEOVER_THRESHOLD = 0.35;

    private static final GLFWGamepadState GLFW_STATE = GLFWGamepadState.create();
    private static final ControllerState CONTROLLER_STATE = new ControllerState();
    private static final NeoForgeMenuNavigation MENU_NAVIGATION = new NeoForgeMenuNavigation();

    private static boolean active;
    private static boolean loggedReady;
    private static boolean loggedNoGamepad;
    private static boolean loggedInit;
    private static long tickCount;
    private static boolean crouchToggled;
    private static boolean sprintToggled;
    private static double cursorX = -1.0;
    private static double cursorY = -1.0;
    private static double renderedCursorX = -1.0;
    private static double renderedCursorY = -1.0;
    private static int scrollCooldown;
    private static long lastLookNanos;
    private static long lastScreenCursorNanos;
    private static long lastRenderedCursorNanos;
    private static long renderFrameActiveNanos;
    private static boolean loggedLookApplied;
    private static Object lastCursorScreen;
    private static Object lastRelayCursorScreen;
    private static double lastRelayCursorX = Double.NaN;
    private static double lastRelayCursorY = Double.NaN;
    private static boolean relayOwnsCursor;
    private static boolean snapStickLatched;
    private static CursorMode cursorMode = CursorMode.SNAP;

    private enum CursorMode {
        SNAP,
        FREE
    }

    private NeoForgeControllerCompat() {
    }

    public static void markRenderFrameActive() {
        renderFrameActiveNanos = System.nanoTime();
    }

    public static void ensureInitialized() {
        if (loggedInit) {
            return;
        }
        loggedInit = true;
        NeoForgeControllerLog.log("NeoForge controller compat initialized");
    }

    public static void tick(Minecraft client) {
        ensureInitialized();
        tickCount++;
        if (client == null) {
            return;
        }
        ensureMenuCursorMode(client);

        if (!poll()) {
            if (!loggedNoGamepad || tickCount <= 5 || tickCount % 600 == 0) {
                loggedNoGamepad = true;
                NeoForgeControllerLog.log("No GLFW gamepad detected on joystick 1 (tick=" + tickCount + ")");
            }
            if (active) {
                releaseGameplayKeys(client, client.screen == null);
                crouchToggled = false;
                sprintToggled = false;
                active = false;
            }
            return;
        }
        loggedNoGamepad = false;

        active = true;
        if (!loggedReady) {
            loggedReady = true;
            NeoForgeControllerLog.log("NeoForge controller compat active");
        }

        if (client.screen != null) {
            lastLookNanos = 0L;
            releaseGameplayKeys(client, false);
            tickScreen(client, client.screen);
        } else {
            tickGameplay(client);
        }

        finishFrame();
    }

    public static void renderFrame(Minecraft client) {
        ensureMenuCursorMode(client);
        if (client == null || client.screen != null || client.player == null || !poll()) {
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

        NeoForgeControllerSettings settings = NeoForgeControllerSettings.get();
        float y = axis(GLFW.GLFW_GAMEPAD_AXIS_RIGHT_Y);
        if (settings.invertY) {
            y = -y;
        }
        applyLook(client.player, axis(GLFW.GLFW_GAMEPAD_AXIS_RIGHT_X), y, seconds, settings);
    }

    public static void renderCursor(Screen screen, GuiGraphics graphics) {
        Minecraft client = Minecraft.getInstance();
        if (!active || screen == null || graphics == null || client == null || client.screen != screen) {
            return;
        }
        if (relayOwnsCursor || renderedCursorX < 0.0 || renderedCursorY < 0.0) {
            return;
        }
        int x = (int) Math.round(renderedCursorX);
        int y = (int) Math.round(renderedCursorY);
        graphics.pose().pushPose();
        graphics.pose().translate(0.0, 0.0, 1000.0);
        graphics.fill(x - 3, y - 3, x + 4, y + 4, 0x66000000);
        graphics.fill(x - 5, y, x + 6, y + 1, 0xFFFFFFFF);
        graphics.fill(x, y - 5, x + 1, y + 6, 0xFFFFFFFF);
        graphics.pose().popPose();
    }

    public static boolean shouldRenderCursorInBaseScreen(Screen screen) {
        return !(screen instanceof AbstractContainerScreen);
    }

    public static void updateScreenCursorBeforeRender(Screen screen, int mouseX, int mouseY) {
        ensureMenuCursorMode(Minecraft.getInstance());
        if (!active || screen == null || Minecraft.getInstance() == null || Minecraft.getInstance().screen != screen) {
            return;
        }
        observeRelayCursor(screen);
        if (relayOwnsCursor) {
            invokeScreenMouseMoved(screen, lastRelayCursorX, lastRelayCursorY);
            lastRenderedCursorNanos = System.nanoTime();
            return;
        }
        if (cursorMode == CursorMode.FREE) {
            updateScreenCursor(Minecraft.getInstance(), screen, true);
            renderedCursorX = cursorX;
            renderedCursorY = cursorY;
        } else {
            advanceRenderedCursor();
            invokeScreenMouseMoved(screen, cursorX, cursorY);
        }
    }

    public static int screenMouseX(Screen screen, int fallback) {
        Minecraft client = Minecraft.getInstance();
        if (!active || relayOwnsCursor || cursorX < 0.0 || client == null || client.screen != screen) {
            return fallback;
        }
        return (int) Math.round(cursorX);
    }

    public static int screenMouseY(Screen screen, int fallback) {
        Minecraft client = Minecraft.getInstance();
        if (!active || relayOwnsCursor || cursorY < 0.0 || client == null || client.screen != screen) {
            return fallback;
        }
        return (int) Math.round(cursorY);
    }

    private static boolean poll() {
        try {
            if (!GLFW.glfwJoystickIsGamepad(GAMEPAD_ID) ||
                !GLFW.glfwGetGamepadState(GAMEPAD_ID, GLFW_STATE)) {
                return false;
            }
            captureControllerState();
            return true;
        } catch (RuntimeException t) {
            if (!loggedReady) {
                loggedReady = true;
                NeoForgeControllerLog.logException("NeoForge controller compat failed to poll GLFW gamepad", t);
            }
            return false;
        }
    }

    private static void ensureMenuCursorMode(Minecraft client) {
        if (client == null || client.screen == null || client.getWindow() == null) {
            return;
        }
        long window = client.getWindow().getWindow();
        if (window != 0L && GLFW.glfwGetInputMode(window, GLFW.GLFW_CURSOR) != GLFW.GLFW_CURSOR_NORMAL) {
            GLFW.glfwSetInputMode(window, GLFW.GLFW_CURSOR, GLFW.GLFW_CURSOR_NORMAL);
        }
    }

    private static void tickGameplay(Minecraft client) {
        NeoForgeControllerSettings settings = NeoForgeControllerSettings.get();
        Options options = client.options;
        if (options == null) {
            return;
        }
        NeoForgeControllerKeys keys = new NeoForgeControllerKeys(options);

        float lx = axis(GLFW.GLFW_GAMEPAD_AXIS_LEFT_X);
        float ly = axis(GLFW.GLFW_GAMEPAD_AXIS_LEFT_Y);
        setHeld(keys.forward, ly < -settings.moveDeadzone);
        setHeld(keys.back, ly > settings.moveDeadzone);
        setHeld(keys.left, lx < -settings.moveDeadzone);
        setHeld(keys.right, lx > settings.moveDeadzone);
        setHeld(keys.jump, button(GLFW.GLFW_GAMEPAD_BUTTON_A));

        boolean sneakHeld;
        if (settings.toggleCrouch) {
            if (pressed(GLFW.GLFW_GAMEPAD_BUTTON_B)) {
                crouchToggled = !crouchToggled;
            }
            sneakHeld = crouchToggled;
        } else {
            crouchToggled = false;
            sneakHeld = button(GLFW.GLFW_GAMEPAD_BUTTON_B);
        }
        sneakHeld = setHeld(keys.sneak, sneakHeld);
        setSneakInput(client.player, sneakHeld);

        setHeld(keys.attack, trigger(GLFW.GLFW_GAMEPAD_AXIS_RIGHT_TRIGGER));
        setHeld(keys.use, trigger(GLFW.GLFW_GAMEPAD_AXIS_LEFT_TRIGGER));

        if (settings.toggleSprint) {
            if (pressed(GLFW.GLFW_GAMEPAD_BUTTON_LEFT_THUMB)) {
                sprintToggled = !sprintToggled;
            }
            setHeld(keys.sprint, sprintToggled);
        } else {
            sprintToggled = false;
            setHeld(keys.sprint, button(GLFW.GLFW_GAMEPAD_BUTTON_LEFT_THUMB));
        }

        if (triggerPressed(GLFW.GLFW_GAMEPAD_AXIS_RIGHT_TRIGGER)) {
            pressKey(keys.attack);
        }
        if (triggerPressed(GLFW.GLFW_GAMEPAD_AXIS_LEFT_TRIGGER)) {
            pressKey(keys.use);
        }
        if (pressed(GLFW.GLFW_GAMEPAD_BUTTON_Y)) {
            pressKey(keys.inventory);
        }
        if (pressed(GLFW.GLFW_GAMEPAD_BUTTON_X)) {
            pressKey(keys.swapOffhand);
        }

        if (pressed(GLFW.GLFW_GAMEPAD_BUTTON_LEFT_BUMPER)) {
            changeHotbarSlot(client.player, -1);
        }
        if (pressed(GLFW.GLFW_GAMEPAD_BUTTON_RIGHT_BUMPER)) {
            changeHotbarSlot(client.player, 1);
        }
        if (pressed(GLFW.GLFW_GAMEPAD_BUTTON_START)) {
            client.pauseGame(false);
        }

        if (!renderFramePathActive()) {
            float y = axis(GLFW.GLFW_GAMEPAD_AXIS_RIGHT_Y);
            if (settings.invertY) {
                y = -y;
            }
            applyLook(client.player, axis(GLFW.GLFW_GAMEPAD_AXIS_RIGHT_X), y, 1.0f / 20.0f, settings);
        }
    }

    private static void tickScreen(Minecraft client, Screen screen) {
        NeoForgeControllerSettings settings = NeoForgeControllerSettings.get();
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
            NeoForgeControllerLog.log("Menu cursor mode changed to " + cursorMode + " screen=" + screen.getClass().getName());
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
                invokeScreenKeyPressed(screen, GLFW.GLFW_KEY_ENTER, 0, 0);
            } else {
                invokeScreenMousePressed(screen, cursorX, cursorY, LEFT_CLICK);
            }
        }
        if (released(GLFW.GLFW_GAMEPAD_BUTTON_A) &&
            (cursorMode == CursorMode.FREE || !MENU_NAVIGATION.usesNativeActivation(screen))) {
            invokeScreenMouseReleased(screen, cursorX, cursorY, LEFT_CLICK);
        }
        if (pressed(GLFW.GLFW_GAMEPAD_BUTTON_X)) {
            takeControllerCursor();
            invokeScreenMousePressed(screen, cursorX, cursorY, RIGHT_CLICK);
        }
        if (released(GLFW.GLFW_GAMEPAD_BUTTON_X)) {
            invokeScreenMouseReleased(screen, cursorX, cursorY, RIGHT_CLICK);
        }
        if (pressed(GLFW.GLFW_GAMEPAD_BUTTON_B)) {
            takeControllerCursor();
            if (MENU_NAVIGATION.handleBack(screen)) {
                if (cursorMode == CursorMode.SNAP) {
                    applySnapTarget(screen, MENU_NAVIGATION.discover(screen, cursorX, cursorY));
                }
            } else {
                invokeScreenKeyPressed(screen, GLFW.GLFW_KEY_ESCAPE, 0, 0);
            }
            return;
        }
        if (pressed(GLFW.GLFW_GAMEPAD_BUTTON_Y)) {
            takeControllerCursor();
            quickMoveFocusedSlot(screen);
        }

        if (client.screen != screen) {
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
                invokeScreenMouseScrolled(screen, cursorX, cursorY, scroll);
                scrollCooldown = 5;
            }
        }
    }

    private static void ensureScreenCursor(Screen screen) {
        if (screen != lastCursorScreen) {
            lastCursorScreen = screen;
            lastScreenCursorNanos = 0L;
            lastRenderedCursorNanos = 0L;
            cursorX = Math.max(1, screen.width / 2);
            cursorY = Math.max(1, screen.height / 2);
            renderedCursorX = cursorX;
            renderedCursorY = cursorY;
            snapStickLatched = false;
            MENU_NAVIGATION.reset(screen);
            if (cursorMode == CursorMode.SNAP) {
                applySnapTarget(screen, MENU_NAVIGATION.discover(screen, cursorX, cursorY));
            }
            return;
        }
        if (cursorX < 0.0 || cursorY < 0.0) {
            cursorX = Math.max(1, screen.width / 2);
            cursorY = Math.max(1, screen.height / 2);
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

    private static void applySnapTarget(Screen screen, NeoForgeMenuNavigation.Position position) {
        if (screen == null || position == null) {
            return;
        }
        cursorX = clamp(position.x, 0.0, Math.max(1, screen.width - 1));
        cursorY = clamp(position.y, 0.0, Math.max(1, screen.height - 1));
        invokeScreenMouseMoved(screen, cursorX, cursorY);
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
    }

    private static void updateScreenCursor(Minecraft client, Screen screen, boolean frameTimed) {
        if (client == null || screen == null || !poll()) {
            return;
        }
        ensureScreenCursor(screen);
        NeoForgeControllerSettings settings = NeoForgeControllerSettings.get();
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
            cursorX = clamp(cursorX + dx * scale, 0.0, Math.max(1, screen.width - 1));
            cursorY = clamp(cursorY + dy * scale, 0.0, Math.max(1, screen.height - 1));
            invokeScreenMouseMoved(screen, cursorX, cursorY);
        }
    }

    private static void observeRelayCursor(Screen screen) {
        Minecraft client = Minecraft.getInstance();
        if (client == null || client.mouseHandler == null) {
            return;
        }
        double mouseX = client.mouseHandler.xpos();
        double mouseY = client.mouseHandler.ypos();
        if (client.getWindow() != null) {
            mouseX = client.mouseHandler.xpos() * screen.width / Math.max(1.0, client.getWindow().getScreenWidth());
            mouseY = client.mouseHandler.ypos() * screen.height / Math.max(1.0, client.getWindow().getScreenHeight());
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
                screen.clearFocus();
            }
            relayOwnsCursor = true;
        }
        lastRelayCursorX = mouseX;
        lastRelayCursorY = mouseY;
    }

    private static void applyLook(LocalPlayer player, float rx, float ry, float seconds, NeoForgeControllerSettings settings) {
        float lookX = ControllerRuntime.shapedLookAxis(rx, settings.lookDeadzone);
        float lookY = ControllerRuntime.shapedLookAxis(ry, settings.lookDeadzone);
        if (lookX == 0.0f && lookY == 0.0f) {
            return;
        }
        float scale = settings.lookSpeed * seconds;
        player.setYRot(player.getYRot() + lookX * scale);
        player.setXRot((float) clamp(player.getXRot() + lookY * scale, -90.0, 90.0));
        if (!loggedLookApplied) {
            loggedLookApplied = true;
            NeoForgeControllerLog.log(String.format(
                "Applied controller look rx=%.2f ry=%.2f scale=%.2f",
                lookX,
                lookY,
                scale));
        }
    }

    private static boolean renderFramePathActive() {
        return renderFrameActiveNanos != 0L
            && System.nanoTime() - renderFrameActiveNanos < 100_000_000L;
    }

    private static void releaseGameplayKeys(Minecraft client, boolean preservePhysicalInput) {
        Options options = client.options;
        if (options == null) {
            return;
        }
        NeoForgeControllerKeys keys = new NeoForgeControllerKeys(options);
        setHeld(keys.forward, false, preservePhysicalInput);
        setHeld(keys.back, false, preservePhysicalInput);
        setHeld(keys.left, false, preservePhysicalInput);
        setHeld(keys.right, false, preservePhysicalInput);
        setHeld(keys.jump, false, preservePhysicalInput);
        boolean sneakHeld = setHeld(keys.sneak, false, preservePhysicalInput);
        setSneakInput(client.player, sneakHeld);
        setHeld(keys.sprint, false, preservePhysicalInput);
        setHeld(keys.attack, false, preservePhysicalInput);
        setHeld(keys.use, false, preservePhysicalInput);
    }

    private static boolean setHeld(KeyMapping key, boolean held) {
        return setHeld(key, held, true);
    }

    private static boolean setHeld(KeyMapping key, boolean held, boolean preservePhysicalInput) {
        if (key == null) {
            return false;
        }
        InputConstants.Key inputKey = key.getKey();
        boolean effectiveHeld = ControllerRuntime.shouldHoldKey(
            held,
            preservePhysicalInput && isBoundInputHeld(inputKey),
            preservePhysicalInput);
        key.setDown(effectiveHeld);
        if (inputKey != null) {
            KeyMapping.set(inputKey, effectiveHeld);
        }
        return effectiveHeld;
    }

    private static boolean isBoundInputHeld(InputConstants.Key inputKey) {
        Minecraft client = Minecraft.getInstance();
        if (inputKey == null || client == null || client.getWindow() == null) {
            return false;
        }
        long window = client.getWindow().getWindow();
        int code = inputKey.getValue();
        return GLFW.glfwGetKey(window, code) == GLFW.GLFW_PRESS
            || (code >= 0 && code <= GLFW.GLFW_MOUSE_BUTTON_LAST
                && GLFW.glfwGetMouseButton(window, code) == GLFW.GLFW_PRESS);
    }

    private static void setSneakInput(LocalPlayer player, boolean sneak) {
        if (player == null || player.input == null) {
            return;
        }
        player.input.shiftKeyDown = sneak;
    }

    private static void pressKey(KeyMapping key) {
        if (key == null) {
            return;
        }
        InputConstants.Key inputKey = key.getKey();
        if (inputKey != null) {
            KeyMapping.click(inputKey);
        }
    }

    private static void changeHotbarSlot(LocalPlayer player, int direction) {
        if (player == null) {
            return;
        }
        int slot = (player.getInventory().selected + direction) % 9;
        if (slot < 0) {
            slot += 9;
        }
        player.getInventory().selected = slot;
    }

    private static void quickMoveFocusedSlot(Screen screen) {
        if (!(screen instanceof AbstractContainerScreen)) {
            return;
        }
        AbstractContainerScreen<?> container = (AbstractContainerScreen<?>) screen;
        Slot slot = cursorMode == CursorMode.SNAP
            ? MENU_NAVIGATION.selectedSlot(screen)
            : container.getSlotUnderMouse();
        if (slot == null || !slot.hasItem()) {
            return;
        }
        ((NeoForgeControllerContainerAccessor) container).banditvault$slotClicked(slot, slot.index, 0, ClickType.QUICK_MOVE);
    }

    private static void invokeScreenMouseMoved(Screen screen, double x, double y) {
        screen.mouseMoved(x, y);
    }

    private static void invokeScreenMousePressed(Screen screen, double x, double y, int button) {
        screen.mouseClicked(x, y, button);
    }

    private static void invokeScreenMouseReleased(Screen screen, double x, double y, int button) {
        screen.mouseReleased(x, y, button);
    }

    private static void invokeScreenMouseScrolled(Screen screen, double x, double y, double scroll) {
        screen.mouseScrolled(x, y, 0.0, scroll);
    }

    private static boolean invokeScreenKeyPressed(Screen screen, int keyCode, int scanCode, int modifiers) {
        return screen.keyPressed(keyCode, scanCode, modifiers);
    }

    private static float axis(int index) {
        return CONTROLLER_STATE.axis(axisFor(index));
    }

    private static boolean trigger(int index) {
        return CONTROLLER_STATE.trigger(axisFor(index), NeoForgeControllerSettings.get().triggerDeadzone);
    }

    private static boolean triggerPressed(int index) {
        return CONTROLLER_STATE.triggerPressed(axisFor(index), NeoForgeControllerSettings.get().triggerDeadzone);
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

    private static double shapedCursorAxis(float value, float deadzone) {
        return ControllerRuntime.shapedCursorAxis(value, deadzone);
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
        CONTROLLER_STATE.finishFrame(NeoForgeControllerSettings.get().triggerDeadzone);
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

    public static Component textLiteral(String text) {
        return Component.literal(text);
    }

    public static Button createButton(int x, int y, int w, int h, String label, Button.OnPress onPress) {
        return Button.builder(textLiteral(label), onPress).bounds(x, y, w, h).build();
    }

    public static Screen createSettingsScreen(Screen parent) {
        return new NeoForgeControllerSettingsScreen(parent);
    }
}
