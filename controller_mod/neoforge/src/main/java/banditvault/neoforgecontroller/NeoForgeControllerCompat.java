package banditvault.neoforgecontroller;

import banditvault.controllercore.ControllerAxis;
import banditvault.controllercore.ControllerAction;
import banditvault.controllercore.ControllerButton;
import banditvault.controllercore.ControllerInput;
import banditvault.controllercore.ControllerRuntime;
import banditvault.controllercore.ControllerState;
import banditvault.controllercore.GridNavigation;
import banditvault.neoforgecontroller.mixin.NeoForgeControllerContainerAccessor;
import com.mojang.blaze3d.platform.InputConstants;
import net.minecraft.client.KeyMapping;
import net.minecraft.client.Minecraft;
import net.minecraft.client.Options;
import net.minecraft.client.gui.components.Button;
import net.minecraft.client.gui.screens.Screen;
import net.minecraft.client.gui.screens.inventory.AbstractContainerScreen;
import net.minecraft.client.player.LocalPlayer;
import net.minecraft.network.chat.Component;
import net.minecraft.world.MenuProvider;
import net.minecraft.world.inventory.Slot;
import net.minecraft.world.phys.BlockHitResult;
import net.minecraft.world.phys.HitResult;
import org.lwjgl.glfw.GLFW;
import org.lwjgl.glfw.GLFWGamepadState;

public final class NeoForgeControllerCompat {
    private static final int GAMEPAD_ID = GLFW.GLFW_JOYSTICK_1;
    private static final int LEFT_CLICK = 0;
    private static final int RIGHT_CLICK = 1;
    private static final double MOUSE_CURSOR_MOVE_EPSILON = 0.5;
    private static final double CONTROLLER_CURSOR_TAKEOVER_THRESHOLD = 0.20;

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
    private static int scrollCooldown;
    private static long lastLookNanos;
    private static long lastScreenCursorNanos;
    private static long renderFrameActiveNanos;
    private static boolean loggedLookApplied;
    private static Object lastCursorScreen;
    private static Object lastMouseCursorScreen;
    private static double lastMouseCursorX = Double.NaN;
    private static double lastMouseCursorY = Double.NaN;
    private static boolean mouseOwnsCursor;
    private static boolean snapStickLatched;
    private static KeyMapping radialPressedKey;
    private static final java.util.Map<String, KeyMapping> JAVA_KEYS_DOWN = new java.util.HashMap<String, KeyMapping>();
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
            releaseJavaKeyMappings();
            return;
        }
        ensureMenuCursorMode(client);
        releaseRadialKey();

        if (!poll()) {
            releaseJavaKeyMappings();
            if (NeoForgeClientApi.screen(client) instanceof NeoForgeControllerRadialScreen) {
                NeoForgeClientApi.setScreen(client, null);
            }
            if (!loggedNoGamepad || tickCount <= 5 || tickCount % 600 == 0) {
                loggedNoGamepad = true;
                NeoForgeControllerLog.log("No GLFW gamepad detected on joystick 1 (tick=" + tickCount + ")");
            }
            if (active) {
                releaseGameplayKeys(client, NeoForgeClientApi.screen(client) == null);
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

        tickJavaKeyMappings(
            client,
            NeoForgeControllerSettings.get(),
            NeoForgeClientApi.screen(client) == null && client.player != null && client.isWindowActive());

        if (NeoForgeClientApi.screen(client) != null) {
            lastLookNanos = 0L;
            releaseGameplayKeys(client, false);
            tickScreen(client, NeoForgeClientApi.screen(client));
        } else {
            tickGameplay(client);
        }

        finishFrame();
    }

    public static void renderFrame(Minecraft client) {
        ensureMenuCursorMode(client);
        if (client == null || NeoForgeClientApi.screen(client) != null || client.player == null || !poll()) {
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

    public static void renderCursor(Screen screen, Object graphics) {
        Minecraft client = Minecraft.getInstance();
        if (screen instanceof NeoForgeControllerSettingsScreen || screen instanceof NeoForgeControllerRadialScreen) {
            return;
        }
        if (!active || screen == null || graphics == null || client == null || NeoForgeClientApi.screen(client) != screen) {
            return;
        }
        if (!mouseOwnsCursor) renderControllerGuide(screen, graphics, client);
        if (mouseOwnsCursor || cursorX < 0.0 || cursorY < 0.0) {
            return;
        }
        int x = (int) Math.round(cursorX);
        int y = (int) Math.round(cursorY);
        NeoForgeVersionApi.renderCursor(graphics, x, y);
    }

    private static void renderControllerGuide(Screen screen, Object graphics, Minecraft client) {
        NeoForgeControllerSettings settings = NeoForgeControllerSettings.get();
        boolean container = screen instanceof AbstractContainerScreen;
        NeoForgeControllerGuide.drawVerticalLeft(graphics, client.font, 8, 8,
            new ControllerInput[] {
                settings.binding(ControllerAction.MENU_ACCEPT),
                settings.binding(ControllerAction.MENU_CANCEL)
            },
            new String[] { "Select", "Back" });
        NeoForgeControllerGuide.drawVerticalRight(graphics, client.font, screen.width - 8, 8,
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

    public static void renderGameplayGuide(Object graphics) {
        Minecraft client = Minecraft.getInstance();
        if (!active || graphics == null || client == null || NeoForgeClientApi.screen(client) != null || client.player == null || NeoForgeClientApi.isHudHidden(client)) return;
        NeoForgeControllerSettings settings = NeoForgeControllerSettings.get();
        BlockHitResult blockHit = client.hitResult instanceof BlockHitResult
            && client.hitResult.getType() == HitResult.Type.BLOCK
                ? (BlockHitResult)client.hitResult
                : null;
        boolean openable = blockHit != null && client.level != null && client.level.getBlockEntity(blockHit.getBlockPos()) instanceof MenuProvider;
        boolean hasTarget = client.hitResult != null && client.hitResult.getType() != HitResult.Type.MISS;
        boolean mainHandItem = !client.player.getMainHandItem().isEmpty();
        boolean heldItem = mainHandItem || !client.player.getOffhandItem().isEmpty();
        NeoForgeControllerGuide.drawVerticalLeft(graphics, client.font, 8, 8,
            new ControllerInput[] {
                settings.binding(ControllerAction.JUMP),
                settings.binding(ControllerAction.SNEAK)
            },
            new String[] { "Jump", "Sneak" });
        NeoForgeControllerGuide.drawVerticalRight(graphics, client.font, NeoForgeVersionApi.guiWidth(graphics) - 8, 8,
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

    public static boolean shouldRenderCursorInBaseScreen(Screen screen) {
        return !(screen instanceof AbstractContainerScreen);
    }

    public static void updateScreenCursorBeforeRender(Screen screen, int mouseX, int mouseY) {
        ensureMenuCursorMode(Minecraft.getInstance());
        if (!active || screen == null || Minecraft.getInstance() == null || NeoForgeClientApi.screen(Minecraft.getInstance()) != screen) {
            return;
        }
        observeMouseCursor(screen);
        if (mouseOwnsCursor) {
            invokeScreenMouseMoved(screen, lastMouseCursorX, lastMouseCursorY);
            return;
        }
        if (cursorMode == CursorMode.FREE) {
            updateScreenCursor(Minecraft.getInstance(), screen, true);
        } else {
            invokeScreenMouseMoved(screen, cursorX, cursorY);
        }
    }

    public static int screenMouseX(Screen screen, int fallback) {
        Minecraft client = Minecraft.getInstance();
        if (!active || mouseOwnsCursor || cursorX < 0.0 || client == null || NeoForgeClientApi.screen(client) != screen) {
            return fallback;
        }
        return (int) Math.round(cursorX);
    }

    public static int screenMouseY(Screen screen, int fallback) {
        Minecraft client = Minecraft.getInstance();
        if (!active || mouseOwnsCursor || cursorY < 0.0 || client == null || NeoForgeClientApi.screen(client) != screen) {
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
        if (client == null || NeoForgeClientApi.screen(client) == null || client.getWindow() == null) {
            return;
        }
        long window = NeoForgeVersionApi.windowHandle(client);
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

        if (pressed(settings, ControllerAction.RADIAL_MENU)) {
            releaseGameplayKeys(client, true);
            NeoForgeClientApi.setScreen(client, new NeoForgeControllerRadialScreen());
            return;
        }

        if (pressed(GLFW.GLFW_GAMEPAD_BUTTON_BACK)) {
            NeoForgeClientApi.setScreen(client, new NeoForgeControllerSettingsScreen(null));
            return;
        }

        float lx = axis(GLFW.GLFW_GAMEPAD_AXIS_LEFT_X);
        float ly = axis(GLFW.GLFW_GAMEPAD_AXIS_LEFT_Y);
        setHeld(keys.forward, ly < -settings.moveDeadzone);
        setHeld(keys.back, ly > settings.moveDeadzone);
        setHeld(keys.left, lx < -settings.moveDeadzone);
        setHeld(keys.right, lx > settings.moveDeadzone);
        setHeld(keys.jump, button(settings, ControllerAction.JUMP));

        boolean sneakHeld;
        if (settings.toggleCrouch) {
            if (pressed(settings, ControllerAction.SNEAK)) {
                crouchToggled = !crouchToggled;
            }
            sneakHeld = crouchToggled;
        } else {
            crouchToggled = false;
            sneakHeld = button(settings, ControllerAction.SNEAK);
        }
        sneakHeld = setHeld(keys.sneak, sneakHeld);
        setSneakInput(client.player, sneakHeld);

        setHeld(keys.attack, button(settings, ControllerAction.ATTACK));
        setHeld(keys.use, button(settings, ControllerAction.USE));

        if (settings.toggleSprint) {
            if (pressed(settings, ControllerAction.SPRINT)) {
                sprintToggled = !sprintToggled;
            }
            setHeld(keys.sprint, sprintToggled);
        } else {
            sprintToggled = false;
            setHeld(keys.sprint, button(settings, ControllerAction.SPRINT));
        }

        if (pressed(settings, ControllerAction.ATTACK)) {
            pressKey(keys.attack);
        }
        if (pressed(settings, ControllerAction.USE)) {
            pressKey(keys.use);
        }
        if (pressed(settings, ControllerAction.INVENTORY)) {
            pressKey(keys.inventory);
        }
        if (pressed(settings, ControllerAction.DROP)) {
            pressKey(keys.drop);
        }
        if (pressed(settings, ControllerAction.SWAP_HANDS)) {
            pressKey(keys.swapOffhand);
        }
        if (pressed(settings, ControllerAction.PICK_BLOCK)) {
            pressKey(keys.pickItem);
        }

        if (pressed(settings, ControllerAction.HOTBAR_PREVIOUS)) {
            changeHotbarSlot(client.player, -1);
        }
        if (pressed(settings, ControllerAction.HOTBAR_NEXT)) {
            changeHotbarSlot(client.player, 1);
        }
        if (pressed(settings, ControllerAction.PAUSE)) {
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

        if (screen instanceof NeoForgeControllerRadialScreen) {
            NeoForgeControllerRadialScreen radial = (NeoForgeControllerRadialScreen)screen;
            if (client.player == null || !client.isWindowActive()) {
                NeoForgeClientApi.setScreen(client, null);
                return;
            }
            radial.setSelectedSlot(ControllerRuntime.radialSlot(
                axis(GLFW.GLFW_GAMEPAD_AXIS_RIGHT_X),
                axis(GLFW.GLFW_GAMEPAD_AXIS_RIGHT_Y),
                settings.lookDeadzone));
            if (pressed(settings, ControllerAction.MENU_CANCEL)) {
                NeoForgeClientApi.setScreen(client, null);
                return;
            }
            if (released(settings, ControllerAction.RADIAL_MENU)) {
                int slot = radial.selectedSlot();
                NeoForgeClientApi.setScreen(client, null);
                activateRadialSlot(slot);
            }
            return;
        }

        if (screen instanceof NeoForgeControllerSettingsScreen) {
            ((NeoForgeControllerSettingsScreen)screen).handleControllerInput(CONTROLLER_STATE, settings.triggerDeadzone);
            return;
        }

        ensureScreenCursor(screen);
        float ry = axis(GLFW.GLFW_GAMEPAD_AXIS_RIGHT_Y);

        if (cursorMode == CursorMode.SNAP && !mouseOwnsCursor) {
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

        if (pressed(settings, ControllerAction.MENU_ACCEPT)) {
            takeControllerCursor();
            if (cursorMode == CursorMode.SNAP && MENU_NAVIGATION.usesNativeActivation(screen)) {
                invokeScreenKeyPressed(screen, GLFW.GLFW_KEY_ENTER, 0, 0);
            } else {
                invokeScreenMousePressed(screen, cursorX, cursorY, LEFT_CLICK);
            }
        }
        if (released(settings, ControllerAction.MENU_ACCEPT) &&
            (cursorMode == CursorMode.FREE || !MENU_NAVIGATION.usesNativeActivation(screen))) {
            invokeScreenMouseReleased(screen, cursorX, cursorY, LEFT_CLICK);
        }
        if (pressed(settings, ControllerAction.MENU_SECONDARY)) {
            takeControllerCursor();
            invokeScreenMousePressed(screen, cursorX, cursorY, RIGHT_CLICK);
        }
        if (released(settings, ControllerAction.MENU_SECONDARY)) {
            invokeScreenMouseReleased(screen, cursorX, cursorY, RIGHT_CLICK);
        }
        if (pressed(settings, ControllerAction.MENU_CANCEL)) {
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
        if (pressed(settings, ControllerAction.QUICK_MOVE)) {
            takeControllerCursor();
            quickMoveFocusedSlot(screen);
        }

        if (NeoForgeClientApi.screen(client) != screen) {
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
            cursorX = Math.max(1, screen.width / 2);
            cursorY = Math.max(1, screen.height / 2);
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

    private static void takeControllerCursor() {
        mouseOwnsCursor = false;
    }

    private static void updateScreenCursor(Minecraft client, Screen screen, boolean frameTimed) {
        if (client == null || screen == null || !poll()) {
            return;
        }

        ensureScreenCursor(screen);
        NeoForgeControllerSettings settings = NeoForgeControllerSettings.get();
        float rawX = axis(GLFW.GLFW_GAMEPAD_AXIS_LEFT_X);
        float rawY = axis(GLFW.GLFW_GAMEPAD_AXIS_LEFT_Y);
        if (mouseOwnsCursor) {
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

    private static void observeMouseCursor(Screen screen) {
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
        if (screen != lastMouseCursorScreen || Double.isNaN(lastMouseCursorX) || Double.isNaN(lastMouseCursorY)) {
            lastMouseCursorScreen = screen;
            lastMouseCursorX = mouseX;
            lastMouseCursorY = mouseY;
            return;
        }

        if (Math.abs(mouseX - lastMouseCursorX) > MOUSE_CURSOR_MOVE_EPSILON ||
            Math.abs(mouseY - lastMouseCursorY) > MOUSE_CURSOR_MOVE_EPSILON) {
            if (!mouseOwnsCursor) {
                screen.clearFocus();
            }
            mouseOwnsCursor = true;
        }
        lastMouseCursorX = mouseX;
        lastMouseCursorY = mouseY;
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
        long window = NeoForgeVersionApi.windowHandle(client);
        int code = inputKey.getValue();
        return GLFW.glfwGetKey(window, code) == GLFW.GLFW_PRESS
            || (code >= 0 && code <= GLFW.GLFW_MOUSE_BUTTON_LAST
                && GLFW.glfwGetMouseButton(window, code) == GLFW.GLFW_PRESS);
    }

    private static void setSneakInput(LocalPlayer player, boolean sneak) {
        NeoForgeVersionApi.setSneakInput(player, sneak);
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

    public static String radialKeyLabel(String keyId) {
        if (keyId == null || keyId.isEmpty()) return "Empty";
        KeyMapping key = findKey(keyId);
        return key == null
            ? "Missing " + keyId
            : key.getTranslatedKeyMessage().getString() + " - " + Component.translatable(key.getName()).getString();
    }

    public static String radialKeyGlyph(String keyId) {
        if (keyId == null || keyId.isEmpty()) return "+";
        KeyMapping key = findKey(keyId);
        if (key == null) return "?";
        String glyph = key.getTranslatedKeyMessage().getString();
        return glyph.toLowerCase(java.util.Locale.ROOT).contains("not bound") ? "?" : glyph;
    }

    public static String[] radialKeyIds() {
        Minecraft client = Minecraft.getInstance();
        KeyMapping[] keys = client == null || client.options == null ? null : client.options.keyMappings;
        if (keys == null || keys.length == 0) return new String[] { "" };
        String[] ids = new String[keys.length + 1];
        ids[0] = "";
        int index = 1;
        for (KeyMapping key : keys) {
            if (key != null) ids[index++] = key.getName();
        }
        if (index == ids.length) return ids;
        return java.util.Arrays.copyOf(ids, index);
    }

    private static KeyMapping findKey(String keyId) {
        Minecraft client = Minecraft.getInstance();
        KeyMapping[] keys = client == null || client.options == null ? null : client.options.keyMappings;
        if (keys == null) return null;
        for (KeyMapping key : keys) {
            if (key != null && key.getName().equals(keyId)) return key;
        }
        return null;
    }

    private static void activateRadialSlot(int slot) {
        if (slot < 0 || slot >= 8) return;
        KeyMapping key = findKey(NeoForgeControllerSettings.get().radialSlot(slot));
        if (key == null) return;
        pressKey(key);
        key.setDown(true);
        radialPressedKey = key;
    }

    private static void releaseRadialKey() {
        if (radialPressedKey == null) return;
        radialPressedKey.setDown(isBoundInputHeld(radialPressedKey.getKey()));
        radialPressedKey = null;
    }

    private static void tickJavaKeyMappings(Minecraft client, NeoForgeControllerSettings settings, boolean enabled) {
        java.util.Iterator<java.util.Map.Entry<String, KeyMapping>> activeKeys = JAVA_KEYS_DOWN.entrySet().iterator();
        while (activeKeys.hasNext()) {
            java.util.Map.Entry<String, KeyMapping> entry = activeKeys.next();
            ControllerInput input = settings.javaBinding(entry.getKey());
            if (enabled && input.held(CONTROLLER_STATE, settings.triggerDeadzone)) continue;
            entry.getValue().setDown(isBoundInputHeld(entry.getValue().getKey()));
            activeKeys.remove();
        }
        if (!enabled || client.options == null || client.options.keyMappings == null) return;
        for (KeyMapping key : client.options.keyMappings) {
            if (key == null) continue;
            String keyId = key.getName();
            ControllerInput input = settings.javaBinding(keyId);
            if (JAVA_KEYS_DOWN.containsKey(keyId) || !input.pressed(CONTROLLER_STATE, settings.triggerDeadzone)) continue;
            pressKey(key);
            key.setDown(true);
            JAVA_KEYS_DOWN.put(keyId, key);
        }
    }

    private static void releaseJavaKeyMappings() {
        for (java.util.Map.Entry<String, KeyMapping> entry : JAVA_KEYS_DOWN.entrySet()) {
            entry.getValue().setDown(isBoundInputHeld(entry.getValue().getKey()));
        }
        JAVA_KEYS_DOWN.clear();
    }

    private static void changeHotbarSlot(LocalPlayer player, int direction) {
        if (player == null) {
            return;
        }
        int slot = (NeoForgeVersionApi.selectedHotbarSlot(player) + direction) % 9;
        if (slot < 0) {
            slot += 9;
        }
        NeoForgeVersionApi.setSelectedHotbarSlot(player, slot);
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
        NeoForgeVersionApi.quickMove(container, slot);
    }

    private static void invokeScreenMouseMoved(Screen screen, double x, double y) {
        screen.mouseMoved(x, y);
    }

    private static void invokeScreenMousePressed(Screen screen, double x, double y, int button) {
        NeoForgeVersionApi.mousePressed(screen, x, y, button);
    }

    private static void invokeScreenMouseReleased(Screen screen, double x, double y, int button) {
        NeoForgeVersionApi.mouseReleased(screen, x, y, button);
    }

    private static void invokeScreenMouseScrolled(Screen screen, double x, double y, double scroll) {
        screen.mouseScrolled(x, y, 0.0, scroll);
    }

    private static boolean invokeScreenKeyPressed(Screen screen, int keyCode, int scanCode, int modifiers) {
        return NeoForgeVersionApi.keyPressed(screen, keyCode, scanCode, modifiers);
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

    private static boolean button(NeoForgeControllerSettings settings, ControllerAction action) {
        return settings.binding(action).held(CONTROLLER_STATE, settings.triggerDeadzone);
    }

    private static boolean pressed(NeoForgeControllerSettings settings, ControllerAction action) {
        return settings.binding(action).pressed(CONTROLLER_STATE, settings.triggerDeadzone);
    }

    private static boolean released(NeoForgeControllerSettings settings, ControllerAction action) {
        return settings.binding(action).released(CONTROLLER_STATE, settings.triggerDeadzone);
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
