package banditvault.fabriccontroller;

import banditvault.controllercore.ControllerAxis;
import banditvault.controllercore.ControllerAction;
import banditvault.controllercore.ControllerButton;
import banditvault.controllercore.ControllerInput;
import banditvault.controllercore.GridNavigation;
import banditvault.controllercore.ControllerRuntime;
import banditvault.controllercore.ControllerState;
import banditvault.fabriccontroller.mixin.BanditControllerContainerAccessor;
import net.minecraft.client.KeyMapping;
import net.minecraft.client.Minecraft;
import net.minecraft.client.Options;
import net.minecraft.client.gui.GuiGraphicsExtractor;
import net.minecraft.client.gui.screens.inventory.AbstractContainerScreen;
import com.mojang.blaze3d.platform.Window;
import net.minecraft.client.input.KeyEvent;
import net.minecraft.world.inventory.ContainerInput;
import net.minecraft.world.inventory.Slot;
import net.minecraft.world.phys.HitResult;
import net.minecraft.network.chat.Component;
import com.mojang.blaze3d.platform.InputConstants;
import net.minecraft.world.MenuProvider;
import net.minecraft.world.phys.BlockHitResult;
import net.minecraft.client.gui.screens.Screen;
import net.minecraft.client.gui.components.Button;
import net.minecraft.client.player.LocalPlayer;
import com.mojang.blaze3d.platform.FramerateLimitTracker;
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
    private static Object lastMouseCursorScreen;
    private static double lastMouseCursorX = Double.NaN;
    private static double lastMouseCursorY = Double.NaN;
    private static boolean mouseOwnsCursor;
    private static boolean snapStickLatched;
    private static KeyMapping radialPressedKey;
    private static boolean radialPressedAsKeyboard;
    private static final java.util.Map<String, KeyMapping> JAVA_KEYS_DOWN = new java.util.HashMap<String, KeyMapping>();
    private static final java.util.Set<String> RAW_JAVA_KEYS_DOWN = new java.util.HashSet<String>();
    private static CursorMode cursorMode = CursorMode.SNAP;

    private static final double MOUSE_CURSOR_MOVE_EPSILON = 0.5;
    private static final double CONTROLLER_CURSOR_TAKEOVER_THRESHOLD = 0.20;

    private enum CursorMode {
        SNAP,
        FREE
    }

    private BanditControllerCompat() {
    }

    public static void tick(Minecraft client) {
        if (client == null) {
            releaseJavaKeyMappings();
            BanditControllerKeyboard.close();
            return;
        }

        releaseRadialKey();
        if (!poll()) {
            releaseJavaKeyMappings();
            BanditControllerKeyboard.close();
            if (FabricClientApi.screen(client) instanceof BanditControllerRadialScreen) {
                FabricClientApi.setScreen(client, null);
            }
            if (active) {
                releaseGameplayKeys(client, FabricClientApi.screen(client) == null);
                crouchToggled = false;
                sprintToggled = false;
                mouseOwnsCursor = false;
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
            FabricClientApi.screen(client) == null && client.player != null && client.isWindowActive());

        if (FabricClientApi.screen(client) != null) {
            lastLookNanos = 0L;
            releaseGameplayKeys(client, false);
            tickScreen(client, FabricClientApi.screen(client));
        } else {
            BanditControllerKeyboard.close();
            mouseOwnsCursor = false;
            tickGameplay(client);
        }

        finishFrame();
    }

    public static void renderFrame(Minecraft client) {
        if (client == null || FabricClientApi.screen(client) != null || client.player == null || !poll()) {
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
        applyLook(client.player, axis(GLFW.GLFW_GAMEPAD_AXIS_RIGHT_X), y, seconds, settings);
    }

    public static void renderCursor(Screen screen, GuiGraphicsExtractor context) {
        Minecraft client = Minecraft.getInstance();
        if (screen instanceof BanditControllerSettingsScreen || screen instanceof BanditControllerRadialScreen) {
            return;
        }
        if (!active || screen == null || context == null || client == null || FabricClientApi.screen(client) != screen) {
            return;
        }
        if (!mouseOwnsCursor) renderControllerGuide(screen, context, client);
        if (mouseOwnsCursor || cursorX < 0.0 || cursorY < 0.0) return;

        int x = (int)Math.round(cursorX);
        int y = (int)Math.round(cursorY);
        FabricScreenApi.drawCursor(context, x, y);
    }

    private static void renderControllerGuide(Screen screen, GuiGraphicsExtractor context, Minecraft client) {
        BanditControllerSettings settings = BanditControllerSettings.get();
        boolean container = screen instanceof AbstractContainerScreen;
        BanditControllerGuide.drawVerticalLeft(context, client.font, 8, 8,
            new ControllerInput[] {
                settings.binding(ControllerAction.MENU_ACCEPT),
                settings.binding(ControllerAction.MENU_CANCEL)
            },
            new String[] { "Select", "Back" });
        BanditControllerGuide.drawVerticalRight(context, client.font, screen.width - 8, 8,
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

    public static boolean shouldRenderCursorInBaseScreen(Screen screen) {
        return !(screen instanceof AbstractContainerScreen);
    }

    public static void updateScreenCursorBeforeRender(Screen screen, int mouseX, int mouseY) {
        Minecraft client = Minecraft.getInstance();
        ensureMenuCursorMode(client);
        if (screen instanceof BanditControllerSettingsScreen || screen instanceof BanditControllerRadialScreen) {
            return;
        }
        if (!active || screen == null || client == null || FabricClientApi.screen(client) != screen) {
            return;
        }
        observeMouseCursor(screen);
        if (mouseOwnsCursor) {
            screen.mouseMoved(lastMouseCursorX, lastMouseCursorY);
            return;
        }
        if (cursorMode == CursorMode.FREE) {
            updateScreenCursor(client, screen, true);
        } else {
            screen.mouseMoved(cursorX, cursorY);
        }
    }

    public static void renderGameplayGuide(GuiGraphicsExtractor context) {
        Minecraft client = Minecraft.getInstance();
        if (!active || context == null || client == null || FabricClientApi.screen(client) != null || client.player == null || FabricClientApi.isHudHidden(client)) return;
        BanditControllerSettings settings = BanditControllerSettings.get();
        BlockHitResult blockHit = client.hitResult instanceof BlockHitResult
            && client.hitResult.getType() == HitResult.Type.BLOCK
                ? (BlockHitResult)client.hitResult
                : null;
        boolean openable = blockHit != null && client.level != null && client.level.getBlockEntity(blockHit.getBlockPos()) instanceof MenuProvider;
        boolean hasTarget = client.hitResult != null && client.hitResult.getType() != HitResult.Type.MISS;
        boolean mainHandItem = !client.player.getMainHandItem().isEmpty();
        boolean heldItem = mainHandItem || !client.player.getOffhandItem().isEmpty();
        BanditControllerGuide.drawVerticalLeft(context, client.font, 8, 8,
            new ControllerInput[] {
                settings.binding(ControllerAction.JUMP),
                settings.binding(ControllerAction.SNEAK)
            },
            new String[] { "Jump", "Sneak" });
        BanditControllerGuide.drawVerticalRight(context, client.font, context.guiWidth() - 8, 8,
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

    public static int screenMouseX(Screen screen, int fallback) {
        Minecraft client = Minecraft.getInstance();
        if (!active || mouseOwnsCursor || cursorX < 0.0 || client == null || FabricClientApi.screen(client) != screen) {
            return fallback;
        }
        return (int)Math.round(cursorX);
    }

    public static int screenMouseY(Screen screen, int fallback) {
        Minecraft client = Minecraft.getInstance();
        if (!active || mouseOwnsCursor || cursorY < 0.0 || client == null || FabricClientApi.screen(client) != screen) {
            return fallback;
        }
        return (int)Math.round(cursorY);
    }

    public static float[] analogMovement() {
        Minecraft client = Minecraft.getInstance();
        if (client == null || FabricClientApi.screen(client) != null || !poll()) {
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

    private static void ensureMenuCursorMode(Minecraft client) {
        if (client == null || FabricClientApi.screen(client) == null || client.getWindow() == null) {
            return;
        }
        long window = client.getWindow().handle();
        if (window != 0L && GLFW.glfwGetInputMode(window, GLFW.GLFW_CURSOR) != GLFW.GLFW_CURSOR_NORMAL) {
            GLFW.glfwSetInputMode(window, GLFW.GLFW_CURSOR, GLFW.GLFW_CURSOR_NORMAL);
        }
    }

    private static void tickGameplay(Minecraft client) {
        BanditControllerSettings settings = BanditControllerSettings.get();
        Options options = client.options;

        if (pressed(settings, ControllerAction.RADIAL_MENU)) {
            releaseGameplayKeys(client, true);
            FabricClientApi.setScreen(client, new BanditControllerRadialScreen());
            return;
        }

        if (pressed(GLFW.GLFW_GAMEPAD_BUTTON_BACK)) {
            FabricClientApi.setScreen(client, new BanditControllerSettingsScreen(null));
            return;
        }

        setHeld(options.keyJump, button(settings, ControllerAction.JUMP));
        if (settings.toggleCrouch) {
            if (pressed(settings, ControllerAction.SNEAK)) {
                crouchToggled = !crouchToggled;
            }
            setHeld(options.keyShift, crouchToggled);
        } else {
            crouchToggled = false;
            setHeld(options.keyShift, button(settings, ControllerAction.SNEAK));
        }
        setHeld(options.keyAttack, button(settings, ControllerAction.ATTACK));
        setHeld(options.keyUse, button(settings, ControllerAction.USE));
        if (settings.toggleSprint) {
            if (pressed(settings, ControllerAction.SPRINT)) {
                sprintToggled = !sprintToggled;
            }
            setHeld(options.keySprint, sprintToggled);
        } else {
            sprintToggled = false;
            setHeld(options.keySprint, button(settings, ControllerAction.SPRINT));
        }

        if (pressed(settings, ControllerAction.ATTACK)) {
            pressKey(options.keyAttack);
        }
        if (pressed(settings, ControllerAction.USE)) {
            pressKey(options.keyUse);
        }
        if (pressed(settings, ControllerAction.INVENTORY)) {
            pressKey(options.keyInventory);
        }
        if (pressed(settings, ControllerAction.DROP)) {
            pressKey(options.keyDrop);
        }
        if (pressed(settings, ControllerAction.SWAP_HANDS)) {
            KeyMapping swapHands = KeyMapping.get("key.swapOffhand");
            if (swapHands != null) pressKey(swapHands);
        }
        if (pressed(settings, ControllerAction.PICK_BLOCK)) {
            pressKey(options.keyPickItem);
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
    }

    private static void tickScreen(Minecraft client, Screen screen) {
        BanditControllerSettings settings = BanditControllerSettings.get();

        if (screen instanceof BanditControllerRadialScreen) {
            BanditControllerRadialScreen radial = (BanditControllerRadialScreen)screen;
            if (client.player == null || !client.isWindowActive()) {
                FabricClientApi.setScreen(client, null);
                return;
            }
            radial.setSelectedSlot(ControllerRuntime.radialSlot(
                axis(GLFW.GLFW_GAMEPAD_AXIS_RIGHT_X),
                axis(GLFW.GLFW_GAMEPAD_AXIS_RIGHT_Y),
                settings.lookDeadzone));
            if (pressed(settings, ControllerAction.MENU_CANCEL)) {
                FabricClientApi.setScreen(client, null);
                return;
            }
            if (released(settings, ControllerAction.RADIAL_MENU)) {
                int slot = radial.selectedSlot();
                FabricClientApi.setScreen(client, null);
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
                FabricClientApi.setScreen(client, null);
            }
            return;
        }
        if (pressed(settings, ControllerAction.QUICK_MOVE)) {
            takeControllerCursor();
            quickMoveFocusedSlot(screen);
        }

        if (FabricClientApi.screen(client) != screen) {
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

    private static void ensureScreenCursor(Screen screen) {
        if (screen != lastCursorScreen) {
            lastCursorScreen = screen;
            lastScreenCursorNanos = 0L;
            cursorX = Math.max(1, screen.width / 2);
            cursorY = Math.max(1, screen.height / 2);
            snapStickLatched = false;
            resetMouseCursorBaseline();
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

    private static void applySnapTarget(Screen screen, Fabric12111MenuNavigation.Position position) {
        if (screen == null || position == null) {
            return;
        }
        cursorX = clamp(position.x, 0.0, Math.max(1, screen.width - 1));
        cursorY = clamp(position.y, 0.0, Math.max(1, screen.height - 1));
        screen.mouseMoved(cursorX, cursorY);
    }

    private static void takeControllerCursor() {
        mouseOwnsCursor = false;
        resetMouseCursorBaseline();
    }

    static void resumeMenuAfterKeyboard(Screen screen) {
        Minecraft client = Minecraft.getInstance();
        if (screen == null || client == null || FabricClientApi.screen(client) != screen) return;
        takeControllerCursor();
        snapStickLatched = false;
        ensureScreenCursor(screen);
        MENU_NAVIGATION.reset(screen);
        if (cursorMode == CursorMode.SNAP) {
            applySnapTarget(screen, MENU_NAVIGATION.discover(screen, cursorX, cursorY));
        }
    }

    private static void resetMouseCursorBaseline() {
        lastMouseCursorScreen = null;
        lastMouseCursorX = Double.NaN;
        lastMouseCursorY = Double.NaN;
    }

    private static void updateScreenCursor(Minecraft client, Screen screen, boolean frameTimed) {
        if (client == null || screen == null || !poll()) {
            return;
        }
        ensureScreenCursor(screen);
        BanditControllerSettings settings = BanditControllerSettings.get();
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
            screen.mouseMoved(cursorX, cursorY);
        }
    }

    private static void observeMouseCursor(Screen screen) {
        Minecraft client = Minecraft.getInstance();
        if (client == null || client.mouseHandler == null) {
            return;
        }
        double mouseX = client.mouseHandler.xpos();
        double mouseY = client.mouseHandler.ypos();
        Window window = client.getWindow();
        if (window != null) {
            mouseX = mouseX * screen.width / Math.max(1.0, window.getScreenWidth());
            mouseY = mouseY * screen.height / Math.max(1.0, window.getScreenHeight());
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

    private static void applyLook(LocalPlayer player, float rx, float ry, float seconds, BanditControllerSettings settings) {
        float lookX = ControllerRuntime.shapedLookAxis(rx, settings.lookDeadzone);
        float lookY = ControllerRuntime.shapedLookAxis(ry, settings.lookDeadzone);
        if (lookX == 0.0f && lookY == 0.0f) {
            return;
        }

        try {
            float yaw = getEntityFloat(player, "yRot");
            float pitch = getEntityFloat(player, "xRot");
            float scale = settings.lookSpeed * seconds;
            setEntityFloat(player, "yRot", yaw + lookX * scale);
            setEntityFloat(player, "xRot", (float)clamp(pitch + lookY * scale, -90.0, 90.0));
        } catch (Throwable t) {
            if (!loggedLookReflectionFailure) {
                loggedLookReflectionFailure = true;
                FabricControllerLog.logException("Bandit controller compat failed to apply look", t);
            }
        }
    }

    private static void releaseGameplayKeys(Minecraft client, boolean preservePhysicalInput) {
        Options options = client.options;
        setHeld(options.keyUp, false, preservePhysicalInput);
        setHeld(options.keyDown, false, preservePhysicalInput);
        setHeld(options.keyLeft, false, preservePhysicalInput);
        setHeld(options.keyRight, false, preservePhysicalInput);
        setHeld(options.keyJump, false, preservePhysicalInput);
        setHeld(options.keyShift, false, preservePhysicalInput);
        setHeld(options.keySprint, false, preservePhysicalInput);
        setHeld(options.keyAttack, false, preservePhysicalInput);
        setHeld(options.keyUse, false, preservePhysicalInput);
    }

    private static void setHeld(KeyMapping key, boolean held) {
        setHeld(key, held, true);
    }

    private static void setHeld(KeyMapping key, boolean held, boolean preservePhysicalInput) {
        if (key == null) {
            return;
        }
        boolean effectiveHeld = ControllerRuntime.shouldHoldKey(
            held,
            preservePhysicalInput && isBoundInputHeld(key),
            preservePhysicalInput);
        key.setDown(effectiveHeld);
        KeyMapping.set(key.getDefaultKey(), effectiveHeld);
    }

    private static boolean isBoundInputHeld(KeyMapping key) {
        Minecraft client = Minecraft.getInstance();
        if (client == null || client.getWindow() == null) {
            return false;
        }
        long window = client.getWindow().handle();
        int code = key.getDefaultKey().getValue();
        if (code < 0) return false;
        return GLFW.glfwGetKey(window, code) == GLFW.GLFW_PRESS
            || (code >= 0 && code <= GLFW.GLFW_MOUSE_BUTTON_LAST
                && GLFW.glfwGetMouseButton(window, code) == GLFW.GLFW_PRESS);
    }

    private static void pressKey(KeyMapping key) {
        if (key == null) {
            return;
        }
        KeyMapping.click(key.getDefaultKey());
    }

    public static String radialKeyLabel(String keyId) {
        if (keyId == null || keyId.isEmpty()) {
            return "Empty";
        }
        KeyMapping key = KeyMapping.get(keyId);
        return key == null
            ? "Missing: " + keyId
            : key.getTranslatedKeyMessage().getString() + " - " + Component.translatable(key.getName()).getString();
    }

    public static String radialKeyGlyph(String keyId) {
        if (keyId == null || keyId.isEmpty()) {
            return "+";
        }
        KeyMapping key = KeyMapping.get(keyId);
        if (key == null) {
            return "?";
        }
        String glyph = key.getTranslatedKeyMessage().getString();
        return glyph.toLowerCase(java.util.Locale.ROOT).contains("not bound") ? "?" : glyph;
    }

    public static String[] radialKeyIds() {
        Minecraft client = Minecraft.getInstance();
        KeyMapping[] keys = client == null || client.options == null ? null : client.options.keyMappings;
        if (keys == null || keys.length == 0) {
            return new String[] { "" };
        }
        int count = 1;
        for (KeyMapping key : keys) {
            if (key != null) count++;
        }
        String[] ids = new String[count];
        ids[0] = "";
        int index = 1;
        for (KeyMapping key : keys) {
            if (key != null) ids[index++] = key.getName();
        }
        return ids;
    }

    private static void activateRadialSlot(int slot) {
        if (slot < 0 || slot >= 8) {
            return;
        }
        KeyMapping key = KeyMapping.get(BanditControllerSettings.get().radialSlot(slot));
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
        radialPressedKey.setDown(isBoundInputHeld(radialPressedKey));
        radialPressedKey = null;
        radialPressedAsKeyboard = false;
    }

    private static boolean sendKeyboardKey(KeyMapping key, int action) {
        InputConstants.Key input = key.getDefaultKey();
        InputConstants.Type type = input.getType();
        if (type != InputConstants.Type.KEYSYM && type != InputConstants.Type.SCANCODE) return false;
        Minecraft client = Minecraft.getInstance();
        if (client == null || client.getWindow() == null) return false;
        try {
            int code = input.getValue();
            if (code < 0) return false;
            KeyEvent event = new KeyEvent(
                type == InputConstants.Type.KEYSYM ? code : GLFW.GLFW_KEY_UNKNOWN,
                type == InputConstants.Type.SCANCODE ? code : 0,
                0);
            java.lang.reflect.Method handler = findMethod(
                client.keyboardHandler.getClass(), "keyPress", long.class, int.class, KeyEvent.class);
            handler.setAccessible(true);
            handler.invoke(client.keyboardHandler, client.getWindow().handle(), action, event);
            return true;
        } catch (Throwable t) {
            if (!loggedKeyReflectionFailure) {
                loggedKeyReflectionFailure = true;
                FabricControllerLog.logException("Bandit controller raw keyboard activation unavailable", t);
            }
            return false;
        }
    }

    private static void pressMappedKey(KeyMapping key) {
        try {
            java.lang.reflect.Field presses = findField(key.getClass(), "clickCount");
            presses.setAccessible(true);
            presses.setInt(key, presses.getInt(key) + 1);
        } catch (Throwable t) {
            if (!loggedKeyReflectionFailure) {
                loggedKeyReflectionFailure = true;
                FabricControllerLog.logException("Bandit controller fell back to physical-key activation", t);
            }
            pressKey(key);
        }
        key.setDown(true);
    }

    private static void tickJavaKeyMappings(Minecraft client, BanditControllerSettings settings, boolean enabled) {
        java.util.Iterator<java.util.Map.Entry<String, KeyMapping>> active = JAVA_KEYS_DOWN.entrySet().iterator();
        while (active.hasNext()) {
            java.util.Map.Entry<String, KeyMapping> entry = active.next();
            ControllerInput input = settings.javaBinding(entry.getKey());
            if (enabled && input.held(CONTROLLER_STATE, settings.triggerDeadzone)) continue;
            if (RAW_JAVA_KEYS_DOWN.remove(entry.getKey())) sendKeyboardKey(entry.getValue(), GLFW.GLFW_RELEASE);
            entry.getValue().setDown(isBoundInputHeld(entry.getValue()));
            active.remove();
        }
        if (!enabled || client.options == null || client.options.keyMappings == null) return;
        for (KeyMapping key : client.options.keyMappings) {
            if (key == null) continue;
            String keyId = key.getName();
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
        for (java.util.Map.Entry<String, KeyMapping> entry : JAVA_KEYS_DOWN.entrySet()) {
            if (RAW_JAVA_KEYS_DOWN.contains(entry.getKey())) sendKeyboardKey(entry.getValue(), GLFW.GLFW_RELEASE);
            entry.getValue().setDown(isBoundInputHeld(entry.getValue()));
        }
        JAVA_KEYS_DOWN.clear();
        RAW_JAVA_KEYS_DOWN.clear();
    }

    private static void changeHotbarSlot(LocalPlayer player, int direction) {
        if (player == null) {
            return;
        }

        try {
            Object inventory = getInventory(player);
            java.lang.reflect.Field selectedSlot = findField(inventory.getClass(), "selected");
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

    private static void quickMoveFocusedSlot(Screen screen) {
        if (!(screen instanceof AbstractContainerScreen)) {
            return;
        }

        try {
            Slot slot = cursorMode == CursorMode.SNAP
                ? MENU_NAVIGATION.selectedSlot(screen)
                : ((BanditControllerContainerAccessor) screen).banditvault$getSlotUnderMouse();
            if (slot == null || !slot.hasItem()) {
                return;
            }
            ((BanditControllerContainerAccessor) screen).banditvault$slotClicked(slot, slot.index, 0, ContainerInput.QUICK_MOVE);
        } catch (Throwable t) {
            if (!loggedQuickMoveReflectionFailure) {
                loggedQuickMoveReflectionFailure = true;
                FabricControllerLog.logException("Bandit controller compat failed to quick-move focused slot", t);
            }
        }
    }

    private static Object getInventory(LocalPlayer player) throws ReflectiveOperationException {
        try {
            java.lang.reflect.Field field = findField(player.getClass(), "inventory");
            field.setAccessible(true);
            return field.get(player);
        } catch (NoSuchFieldException ignored) {
            java.lang.reflect.Method method = findMethod(player.getClass(), "getInventory");
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
            resetInactivityTimer(Minecraft.getInstance());
        }
    }

    private static void finishFrame() {
        CONTROLLER_STATE.finishFrame(BanditControllerSettings.get().triggerDeadzone);
    }

    private static void resetInactivityTimer(Minecraft client) {
        if (client == null) {
            return;
        }
        // controller keys bypass Minecraft input so keep the AFK timer awake
        FramerateLimitTracker tracker = client.getFramerateLimitTracker();
        if (tracker != null) {
            tracker.onInputReceived();
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

    public static Button createButton(int x, int y, int w, int h, String label, Button.OnPress onPress) {
        return Button.builder(Component.nullToEmpty(label), onPress)
            .bounds(x, y, w, h)
            .build();
    }
}
