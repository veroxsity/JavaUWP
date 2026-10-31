package banditvault.cleanroomcontroller;

import banditvault.controllercore.ControllerAction;
import banditvault.controllercore.ControllerAxis;
import banditvault.controllercore.ControllerButton;
import banditvault.controllercore.ControllerInput;
import banditvault.controllercore.ControllerRuntime;
import banditvault.controllercore.ControllerState;
import banditvault.controllercore.GridNavigation;

import net.minecraft.client.Minecraft;
import net.minecraft.client.entity.EntityPlayerSP;
import net.minecraft.client.gui.FontRenderer;
import net.minecraft.client.gui.Gui;
import net.minecraft.client.gui.GuiScreen;
import net.minecraft.client.gui.ScaledResolution;
import net.minecraft.client.gui.inventory.GuiContainer;
import net.minecraft.client.renderer.GlStateManager;
import net.minecraft.client.resources.I18n;
import net.minecraft.client.settings.GameSettings;
import net.minecraft.client.settings.KeyBinding;
import net.minecraft.inventory.ClickType;
import net.minecraft.inventory.IInventory;
import net.minecraft.inventory.Slot;
import net.minecraft.tileentity.TileEntity;
import net.minecraft.util.MovementInput;
import net.minecraft.util.math.RayTraceResult;
import net.minecraft.world.IInteractionObject;
import net.minecraftforge.client.event.GuiScreenEvent;
import net.minecraftforge.client.event.InputUpdateEvent;
import net.minecraftforge.client.event.RenderGameOverlayEvent;
import net.minecraftforge.fml.common.eventhandler.EventPriority;
import net.minecraftforge.fml.common.eventhandler.SubscribeEvent;
import net.minecraftforge.fml.common.gameevent.TickEvent;

import org.lwjgl.glfw.GLFW;
import org.lwjgl.glfw.GLFWGamepadState;
import org.lwjglx.input.Keyboard;
import org.lwjglx.input.Mouse;
import org.lwjglx.opengl.Display;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.HashSet;
import java.util.Iterator;
import java.util.List;
import java.util.Map;
import java.util.Set;

// menu input goes into lwjglxx's event queues so vanilla's own hover, drag and click handling runs
final class CleanroomControllerCompat {
    private static final int GAMEPAD_ID = GLFW.GLFW_JOYSTICK_1;
    private static final int LEFT_CLICK = 0;
    private static final int RIGHT_CLICK = 1;
    private static final int KEY_ESCAPE = 1;
    private static final double RELAY_CURSOR_MOVE_EPSILON = 1.5;
    private static final double CONTROLLER_CURSOR_TAKEOVER_THRESHOLD = 0.20;
    private static final long INJECT_SETTLE_NANOS = 150_000_000L;

    private static final GLFWGamepadState GLFW_STATE = GLFWGamepadState.create();
    private static final ControllerState STATE = new ControllerState();
    private static final CleanroomMenuNavigation NAVIGATION = new CleanroomMenuNavigation();
    private static final float[] AXES = new float[6];
    private static final boolean[] BUTTONS = new boolean[15];

    private static boolean active;
    private static boolean loggedConnect;
    private static long lastLookNanos;
    private static long lastCursorNanos;
    private static long lastInjectNanos;
    private static boolean crouchToggled;
    private static boolean sprintToggled;
    private static double cursorX = -1.0;
    private static double cursorY = -1.0;
    private static double lastInjectedX = Double.NaN;
    private static double lastInjectedY = Double.NaN;
    private static GuiScreen lastCursorScreen;
    private static boolean relayOwnsCursor;
    private static boolean snapStickLatched;
    private static int scrollCooldown;
    private static boolean primaryDown;
    private static boolean secondaryDown;
    private static CursorMode cursorMode = CursorMode.SNAP;

    private static final Set<Integer> HELD_KEY_CODES = new HashSet<Integer>();
    // keys pressed as raw keyboard events for the radial menu, released on the next tick
    private static final List<Integer> PENDING_RAW_RELEASES = new ArrayList<Integer>();
    private static final List<Integer> PENDING_STATE_RELEASES = new ArrayList<Integer>();
    private static final Map<String, Integer> JAVA_KEYS_DOWN = new HashMap<String, Integer>();

    private enum CursorMode {
        SNAP,
        FREE
    }

    @SubscribeEvent
    public void onClientTick(TickEvent.ClientTickEvent event) {
        if (event.phase != TickEvent.Phase.START) return;
        try {
            tick(Minecraft.func_71410_x()); // getMinecraft
        } catch (Throwable t) {
            CleanroomControllerLog.logException("Cleanroom controller tick failed", t);
        }
    }

    @SubscribeEvent
    public void onRenderTick(TickEvent.RenderTickEvent event) {
        if (event.phase != TickEvent.Phase.START) return;
        try {
            renderFrame(Minecraft.func_71410_x()); // getMinecraft
        } catch (Throwable t) {
            CleanroomControllerLog.logException("Cleanroom controller frame failed", t);
        }
    }

    // after vanilla has read the keys, so the stick's analogue values replace the digital ones
    @SubscribeEvent(priority = EventPriority.LOWEST)
    public void onInputUpdate(InputUpdateEvent event) {
        CleanroomControllerSettings settings = CleanroomControllerSettings.get();
        Minecraft mc = Minecraft.func_71410_x(); // getMinecraft
        if (!active || settings.keyboardMovement || mc.field_71462_r != null) return; // currentScreen
        float[] move = ControllerRuntime.shapedMovement(
            axis(ControllerAxis.LEFT_X), axis(ControllerAxis.LEFT_Y), settings.moveDeadzone);
        if (move[0] == 0.0f && move[1] == 0.0f) return;
        // the event's own getter is typed against cleanroom's notch names, this is the same object
        EntityPlayerSP player = mc.field_71439_g; // player
        if (player == null || player.field_71158_b == null) return; // movementInput
        MovementInput input = player.field_71158_b;
        float scale = input.field_78899_d ? 0.3f : 1.0f; // sneak
        input.field_78902_a = -move[0] * scale; // moveStrafe
        input.field_192832_b = -move[1] * scale; // moveForward
    }

    @SubscribeEvent(priority = EventPriority.LOWEST)
    public void onDrawScreen(GuiScreenEvent.DrawScreenEvent.Post event) {
        try {
            drawMenuOverlay(Minecraft.func_71410_x()); // getMinecraft
        } catch (Throwable t) {
            CleanroomControllerLog.logException("Cleanroom controller menu overlay failed", t);
        }
    }

    @SubscribeEvent
    public void onOverlay(RenderGameOverlayEvent.Post event) {
        if (event.getType() != RenderGameOverlayEvent.ElementType.ALL) return;
        try {
            drawGameplayGuide(Minecraft.func_71410_x()); // getMinecraft
        } catch (Throwable t) {
            CleanroomControllerLog.logException("Cleanroom controller hud guide failed", t);
        }
    }

    private static void tick(Minecraft mc) {
        flushPendingReleases();
        if (!poll()) {
            if (active) {
                releaseGameplayKeys();
                releaseJavaKeys();
                releaseMenuButtons();
                CleanroomControllerKeyboard.close();
                if (mc.field_71462_r instanceof CleanroomControllerRadialScreen) mc.func_147108_a(null); // currentScreen, displayGuiScreen
                crouchToggled = false;
                sprintToggled = false;
                active = false;
            }
            return;
        }
        active = true;
        if (!loggedConnect) {
            loggedConnect = true;
            CleanroomControllerLog.log("Gamepad connected: " + GLFW.glfwGetGamepadName(GAMEPAD_ID));
        }

        CleanroomControllerSettings settings = CleanroomControllerSettings.get();
        GuiScreen screen = mc.field_71462_r; // currentScreen
        tickJavaKeys(mc, settings, screen == null && mc.field_71439_g != null); // player
        if (screen != null) {
            lastLookNanos = 0L;
            releaseGameplayKeys();
            tickScreen(mc, screen, settings);
        } else {
            CleanroomControllerKeyboard.close();
            releaseMenuButtons();
            lastCursorScreen = null;
            if (mc.field_71439_g != null) tickGameplay(mc, mc.field_71439_g, settings); // player
        }
        STATE.finishFrame(settings.triggerDeadzone);
    }

    private static void renderFrame(Minecraft mc) {
        if (!active || !poll()) {
            lastLookNanos = 0L;
            lastCursorNanos = 0L;
            return;
        }
        CleanroomControllerSettings settings = CleanroomControllerSettings.get();
        GuiScreen screen = mc.field_71462_r; // currentScreen
        if (screen == null) {
            lastCursorNanos = 0L;
            if (mc.field_71439_g != null) look(mc.field_71439_g, settings); // player
            return;
        }
        lastLookNanos = 0L;
        if (screen instanceof CleanroomControllerSettingsScreen || screen instanceof CleanroomControllerRadialScreen) return;
        if (CleanroomControllerKeyboard.isOpen()) return;
        ensureScreenCursor(screen);
        observeRelayCursor(mc, screen);
        if (cursorMode == CursorMode.FREE) moveFreeCursor(screen, settings);
    }

    private static boolean poll() {
        try {
            if (!GLFW.glfwJoystickIsGamepad(GAMEPAD_ID) || !GLFW.glfwGetGamepadState(GAMEPAD_ID, GLFW_STATE)) return false;
        } catch (RuntimeException e) {
            return false;
        }
        for (int i = 0; i < AXES.length; i++) AXES[i] = GLFW_STATE.axes(i);
        for (int i = 0; i < BUTTONS.length; i++) BUTTONS[i] = GLFW_STATE.buttons(i) == GLFW.GLFW_PRESS;
        STATE.capture(AXES, BUTTONS);
        return true;
    }


    private static void tickGameplay(Minecraft mc, EntityPlayerSP player, CleanroomControllerSettings settings) {
        GameSettings options = mc.field_71474_y; // gameSettings

        if (pressed(settings, ControllerAction.RADIAL_MENU)) {
            releaseGameplayKeys();
            mc.func_147108_a(new CleanroomControllerRadialScreen()); // displayGuiScreen
            return;
        }
        if (STATE.pressed(ControllerButton.BACK)) {
            releaseGameplayKeys();
            mc.func_147108_a(new CleanroomControllerSettingsScreen(null)); // displayGuiScreen
            return;
        }

        float lx = axis(ControllerAxis.LEFT_X);
        float ly = axis(ControllerAxis.LEFT_Y);
        boolean keys = settings.keyboardMovement;
        setHeld(options.field_74351_w, keys && ly < -settings.moveDeadzone, false); // keyBindForward
        setHeld(options.field_74368_y, keys && ly > settings.moveDeadzone, false); // keyBindBack
        setHeld(options.field_74370_x, keys && lx < -settings.moveDeadzone, false); // keyBindLeft
        setHeld(options.field_74366_z, keys && lx > settings.moveDeadzone, false); // keyBindRight

        setHeld(options.field_74312_F, held(settings, ControllerAction.ATTACK), pressed(settings, ControllerAction.ATTACK)); // keyBindAttack
        setHeld(options.field_74313_G, held(settings, ControllerAction.USE), pressed(settings, ControllerAction.USE)); // keyBindUseItem
        setHeld(options.field_74314_A, held(settings, ControllerAction.JUMP), pressed(settings, ControllerAction.JUMP)); // keyBindJump

        if (settings.toggleCrouch) {
            if (pressed(settings, ControllerAction.SNEAK)) crouchToggled = !crouchToggled;
        } else {
            crouchToggled = held(settings, ControllerAction.SNEAK);
        }
        setHeld(options.field_74311_E, crouchToggled, false); // keyBindSneak

        if (settings.toggleSprint) {
            if (pressed(settings, ControllerAction.SPRINT)) sprintToggled = !sprintToggled;
        } else {
            sprintToggled = held(settings, ControllerAction.SPRINT);
        }
        setHeld(options.field_151444_V, sprintToggled, false); // keyBindSprint

        tap(options.field_151445_Q, pressed(settings, ControllerAction.INVENTORY)); // keyBindInventory
        tap(options.field_74316_C, pressed(settings, ControllerAction.DROP)); // keyBindDrop
        tap(options.field_186718_X, pressed(settings, ControllerAction.SWAP_HANDS)); // keyBindSwapHands
        tap(options.field_74322_I, pressed(settings, ControllerAction.PICK_BLOCK)); // keyBindPickBlock

        // changeCurrentItem takes a wheel direction, positive moves the selection left
        if (pressed(settings, ControllerAction.HOTBAR_PREVIOUS)) player.field_71071_by.func_70453_c(1); // inventory.changeCurrentItem
        if (pressed(settings, ControllerAction.HOTBAR_NEXT)) player.field_71071_by.func_70453_c(-1);

        if (pressed(settings, ControllerAction.PAUSE)) {
            releaseGameplayKeys();
            mc.func_71385_j(); // displayInGameMenu
        }
    }

    private static void look(EntityPlayerSP player, CleanroomControllerSettings settings) {
        long now = System.nanoTime();
        float seconds = 1.0f / 60.0f;
        if (lastLookNanos != 0L) {
            seconds = Math.max(1.0f / 240.0f, Math.min(1.0f / 20.0f, (now - lastLookNanos) / 1.0e9f));
        }
        lastLookNanos = now;
        float yaw = ControllerRuntime.shapedLookAxis(axis(ControllerAxis.RIGHT_X), settings.lookDeadzone);
        float pitch = ControllerRuntime.shapedLookAxis(axis(ControllerAxis.RIGHT_Y), settings.lookDeadzone);
        if (settings.invertY) pitch = -pitch;
        if (yaw == 0.0f && pitch == 0.0f) return;
        // turn scales by 0.15 and subtracts pitch, so undo both to get degrees per second
        float degrees = settings.lookSpeed * seconds / 0.15f;
        player.func_70082_c(yaw * degrees, -pitch * degrees); // turn
    }


    private static void tickScreen(Minecraft mc, GuiScreen screen, CleanroomControllerSettings settings) {
        if (screen instanceof CleanroomControllerRadialScreen) {
            CleanroomControllerRadialScreen radial = (CleanroomControllerRadialScreen) screen;
            if (mc.field_71439_g == null) { // player
                mc.func_147108_a(null); // displayGuiScreen
                return;
            }
            radial.setSelectedSlot(ControllerRuntime.radialSlot(
                axis(ControllerAxis.RIGHT_X), axis(ControllerAxis.RIGHT_Y), settings.lookDeadzone));
            if (pressed(settings, ControllerAction.MENU_CANCEL)) {
                mc.func_147108_a(null);
                return;
            }
            if (released(settings, ControllerAction.RADIAL_MENU)) {
                int slot = radial.selectedSlot();
                mc.func_147108_a(null);
                activateRadialSlot(mc, slot);
            }
            return;
        }
        if (screen instanceof CleanroomControllerSettingsScreen) {
            ((CleanroomControllerSettingsScreen) screen).handleControllerInput(STATE, settings.triggerDeadzone);
            return;
        }
        if (CleanroomControllerKeyboard.tick(screen)) return;

        ensureScreenCursor(screen);
        if (cursorMode == CursorMode.SNAP && !relayOwnsCursor) {
            applySnapTarget(screen, NAVIGATION.synchronize(screen, cursorX, cursorY));
        }

        if (pressed(settings, ControllerAction.SNAP_FREE_TOGGLE)) {
            takeControllerCursor();
            cursorMode = cursorMode == CursorMode.SNAP ? CursorMode.FREE : CursorMode.SNAP;
            snapStickLatched = false;
            NAVIGATION.reset(screen);
            if (cursorMode == CursorMode.SNAP) applySnapTarget(screen, NAVIGATION.discover(screen, cursorX, cursorY));
            CleanroomControllerLog.log("Menu cursor mode changed to " + cursorMode + " screen=" + screen.getClass().getName());
            return;
        }

        if (cursorMode == CursorMode.SNAP) {
            GridNavigation.Direction direction = snapDirection();
            if (direction != null) {
                takeControllerCursor();
                applySnapTarget(screen, NAVIGATION.move(screen, direction, cursorX, cursorY));
            }
        } else {
            snapStickLatched = false;
        }

        if (pressed(settings, ControllerAction.MENU_ACCEPT)) {
            takeControllerCursor();
            injectCursor(screen);
            if (CleanroomControllerKeyboard.activate(screen, cursorX, cursorY)) return;
        }
        primaryDown = mouseButton(LEFT_CLICK, held(settings, ControllerAction.MENU_ACCEPT), primaryDown);
        if (pressed(settings, ControllerAction.MENU_SECONDARY)) {
            takeControllerCursor();
            injectCursor(screen);
        }
        secondaryDown = mouseButton(RIGHT_CLICK, held(settings, ControllerAction.MENU_SECONDARY), secondaryDown);

        if (pressed(settings, ControllerAction.MENU_CANCEL)) {
            takeControllerCursor();
            Keyboard.addKeyEvent(KEY_ESCAPE, true);
            Keyboard.addKeyEvent(KEY_ESCAPE, false);
            return;
        }
        if (pressed(settings, ControllerAction.QUICK_MOVE) && screen instanceof GuiContainer) {
            takeControllerCursor();
            quickMove(mc, (GuiContainer) screen);
        }

        if (scrollCooldown > 0) {
            scrollCooldown--;
        } else {
            float ry = axis(ControllerAxis.RIGHT_Y);
            double scroll = 0.0;
            if (ry < -0.35f || (cursorMode == CursorMode.FREE && STATE.button(ControllerButton.DPAD_UP))) {
                scroll = settings.scrollAmount;
            } else if (ry > 0.35f || (cursorMode == CursorMode.FREE && STATE.button(ControllerButton.DPAD_DOWN))) {
                scroll = -settings.scrollAmount;
            }
            if (scroll != 0.0) {
                injectCursor(screen);
                Mouse.addWheelEvent(scroll);
                scrollCooldown = 5;
            }
        }
    }

    static void resumeMenuAfterKeyboard(GuiScreen screen) {
        Minecraft mc = Minecraft.func_71410_x(); // getMinecraft
        if (screen == null || mc.field_71462_r != screen) return; // currentScreen
        takeControllerCursor();
        snapStickLatched = false;
        ensureScreenCursor(screen);
        NAVIGATION.reset(screen);
        if (cursorMode == CursorMode.SNAP) applySnapTarget(screen, NAVIGATION.discover(screen, cursorX, cursorY));
    }

    private static void ensureScreenCursor(GuiScreen screen) {
        if (screen != lastCursorScreen) {
            lastCursorScreen = screen;
            lastCursorNanos = 0L;
            lastInjectedX = Double.NaN;
            lastInjectedY = Double.NaN;
            cursorX = Math.max(1, screen.field_146294_l / 2); // width
            cursorY = Math.max(1, screen.field_146295_m / 2); // height
            snapStickLatched = false;
            NAVIGATION.reset(screen);
            if (cursorMode == CursorMode.SNAP) applySnapTarget(screen, NAVIGATION.discover(screen, cursorX, cursorY));
            return;
        }
        if (cursorX < 0.0 || cursorY < 0.0) {
            cursorX = Math.max(1, screen.field_146294_l / 2);
            cursorY = Math.max(1, screen.field_146295_m / 2);
        }
    }

    private static GridNavigation.Direction snapDirection() {
        if (STATE.pressed(ControllerButton.DPAD_UP)) return GridNavigation.Direction.UP;
        if (STATE.pressed(ControllerButton.DPAD_DOWN)) return GridNavigation.Direction.DOWN;
        if (STATE.pressed(ControllerButton.DPAD_LEFT)) return GridNavigation.Direction.LEFT;
        if (STATE.pressed(ControllerButton.DPAD_RIGHT)) return GridNavigation.Direction.RIGHT;

        float x = axis(ControllerAxis.LEFT_X);
        float y = axis(ControllerAxis.LEFT_Y);
        if (Math.max(Math.abs(x), Math.abs(y)) < 0.35f) {
            snapStickLatched = false;
            return null;
        }
        if (snapStickLatched || Math.max(Math.abs(x), Math.abs(y)) < 0.65f) return null;
        snapStickLatched = true;
        if (Math.abs(x) > Math.abs(y)) return x < 0.0f ? GridNavigation.Direction.LEFT : GridNavigation.Direction.RIGHT;
        return y < 0.0f ? GridNavigation.Direction.UP : GridNavigation.Direction.DOWN;
    }

    private static void applySnapTarget(GuiScreen screen, CleanroomMenuNavigation.Position position) {
        if (screen == null || position == null) return;
        cursorX = ControllerRuntime.clamp(position.x, 0.0, Math.max(1, screen.field_146294_l - 1)); // width
        cursorY = ControllerRuntime.clamp(position.y, 0.0, Math.max(1, screen.field_146295_m - 1)); // height
        injectCursor(screen);
    }

    private static void moveFreeCursor(GuiScreen screen, CleanroomControllerSettings settings) {
        float rawX = axis(ControllerAxis.LEFT_X);
        float rawY = axis(ControllerAxis.LEFT_Y);
        if (relayOwnsCursor) {
            if (Math.max(Math.abs(rawX), Math.abs(rawY)) < CONTROLLER_CURSOR_TAKEOVER_THRESHOLD) {
                lastCursorNanos = System.nanoTime();
                return;
            }
            takeControllerCursor();
        }
        double dx = ControllerRuntime.shapedCursorAxis(rawX, settings.cursorDeadzone);
        double dy = ControllerRuntime.shapedCursorAxis(rawY, settings.cursorDeadzone);
        long now = System.nanoTime();
        double seconds = lastCursorNanos == 0L ? 1.0 / 60.0 : Math.max(0.0, Math.min(1.0 / 20.0, (now - lastCursorNanos) / 1.0e9));
        lastCursorNanos = now;
        if (dx == 0.0 && dy == 0.0) return;
        double scale = settings.cursorSpeed * seconds * 30.0;
        cursorX = ControllerRuntime.clamp(cursorX + dx * scale, 0.0, Math.max(1, screen.field_146294_l - 1)); // width
        cursorY = ControllerRuntime.clamp(cursorY + dy * scale, 0.0, Math.max(1, screen.field_146295_m - 1)); // height
        injectCursor(screen);
    }

    // through glfw so the shim and relay agree, and into lwjglxx's queue so the move lands before any click
    private static void injectCursor(GuiScreen screen) {
        if (cursorX < 0.0 || cursorY < 0.0 || screen.field_146294_l <= 0 || screen.field_146295_m <= 0) return; // width, height
        long window = Display.getWindow();
        int[] w = new int[1];
        int[] h = new int[1];
        GLFW.glfwGetWindowSize(window, w, h);
        if (w[0] <= 0 || h[0] <= 0) return;
        double wx = cursorX * w[0] / screen.field_146294_l;
        double wy = cursorY * h[0] / screen.field_146295_m;
        GLFW.glfwSetCursorPos(window, wx, wy);
        Mouse.addMoveEvent(wx, wy);
        lastInjectedX = cursorX;
        lastInjectedY = cursorY;
        lastInjectNanos = System.nanoTime();
    }

    // the relay mouse moves the same pointer, the controller hands the cursor over until it is used again
    private static void observeRelayCursor(Minecraft mc, GuiScreen screen) {
        if (mc.field_71443_c <= 0 || mc.field_71440_d <= 0) return; // displayWidth, displayHeight
        double mouseX = Mouse.getX() * (double) screen.field_146294_l / mc.field_71443_c; // width
        double mouseY = screen.field_146295_m - Mouse.getY() * (double) screen.field_146295_m / mc.field_71440_d - 1; // height
        if (Double.isNaN(lastInjectedX) || System.nanoTime() - lastInjectNanos < INJECT_SETTLE_NANOS) return;
        if (Math.abs(mouseX - lastInjectedX) > RELAY_CURSOR_MOVE_EPSILON || Math.abs(mouseY - lastInjectedY) > RELAY_CURSOR_MOVE_EPSILON) {
            relayOwnsCursor = true;
            cursorX = mouseX;
            cursorY = mouseY;
            lastInjectedX = mouseX;
            lastInjectedY = mouseY;
        }
    }

    private static void takeControllerCursor() {
        relayOwnsCursor = false;
    }

    private static void quickMove(Minecraft mc, GuiContainer screen) {
        Slot slot = cursorMode == CursorMode.SNAP ? NAVIGATION.selectedSlot(screen) : slotUnderMouse(screen);
        if (slot == null || !slot.func_75216_d() || mc.field_71439_g == null) return; // getHasStack, player
        int windowId = screen.field_147002_h.field_75152_c; // inventorySlots.windowId
        mc.field_71442_b.func_187098_a(windowId, slot.field_75222_d, 0, ClickType.QUICK_MOVE, mc.field_71439_g); // playerController, windowClick, slotNumber
    }

    // forge adds getSlotUnderMouse in its binpatch, so it is missing from the vanilla jar this compiles against
    private static Slot slotUnderMouse(GuiContainer screen) {
        try {
            return (Slot) GuiContainer.class.getMethod("getSlotUnderMouse").invoke(screen);
        } catch (ReflectiveOperationException | ClassCastException e) {
            return null;
        }
    }

    private static boolean mouseButton(int button, boolean held, boolean wasDown) {
        if (held != wasDown) Mouse.addButtonEvent(button, held);
        return held;
    }

    private static void releaseMenuButtons() {
        primaryDown = mouseButton(LEFT_CLICK, false, primaryDown);
        secondaryDown = mouseButton(RIGHT_CLICK, false, secondaryDown);
    }


    private static void drawMenuOverlay(Minecraft mc) {
        GuiScreen screen = mc.field_71462_r; // currentScreen
        if (!active || screen == null || relayOwnsCursor) return;
        if (screen instanceof CleanroomControllerSettingsScreen || screen instanceof CleanroomControllerRadialScreen) return;
        beginOverlay();
        CleanroomControllerSettings settings = CleanroomControllerSettings.get();
        if (settings.showGuides) {
            FontRenderer font = mc.field_71466_p; // fontRenderer
            boolean container = screen instanceof GuiContainer;
            String toggle = cursorMode == CursorMode.SNAP ? "Free Cursor" : "Snap Cursor";
            CleanroomControllerGuide.drawVerticalLeft(font, 8, 8,
                new ControllerInput[] { settings.binding(ControllerAction.MENU_ACCEPT), settings.binding(ControllerAction.MENU_CANCEL) },
                new String[] { "Select", "Back" });
            CleanroomControllerGuide.drawVerticalRight(font, screen.field_146294_l - 8, 8, // width
                container
                    ? new ControllerInput[] {
                        settings.binding(ControllerAction.QUICK_MOVE),
                        settings.binding(ControllerAction.MENU_SECONDARY),
                        settings.binding(ControllerAction.SNAP_FREE_TOGGLE) }
                    : new ControllerInput[] { settings.binding(ControllerAction.SNAP_FREE_TOGGLE) },
                container ? new String[] { "Quick Move", "Secondary", toggle } : new String[] { toggle });
        }
        if (cursorX >= 0.0 && cursorY >= 0.0 && !CleanroomControllerKeyboard.isOpen()) {
            int x = (int) Math.round(cursorX);
            int y = (int) Math.round(cursorY);
            Gui.func_73734_a(x - 3, y - 3, x + 4, y + 4, 0x66000000); // drawRect
            Gui.func_73734_a(x - 5, y, x + 6, y + 1, 0xFFFFFFFF);
            Gui.func_73734_a(x, y - 5, x + 1, y + 6, 0xFFFFFFFF);
        }
        endOverlay();
    }

    private static void drawGameplayGuide(Minecraft mc) {
        CleanroomControllerSettings settings = CleanroomControllerSettings.get();
        EntityPlayerSP player = mc.field_71439_g; // player
        if (!active || !settings.showGuides || player == null || mc.field_71462_r != null || mc.field_71474_y.field_74319_N) return; // currentScreen, gameSettings.hideGUI
        RayTraceResult hit = mc.field_71476_x; // objectMouseOver
        boolean block = hit != null && hit.field_72313_a == RayTraceResult.Type.BLOCK; // typeOfHit
        boolean hasTarget = hit != null && hit.field_72313_a != RayTraceResult.Type.MISS;
        boolean openable = false;
        if (block && mc.field_71441_e != null) { // world
            TileEntity tile = mc.field_71441_e.func_175625_s(hit.func_178782_a()); // getTileEntity, getBlockPos
            openable = tile instanceof IInteractionObject || tile instanceof IInventory;
        }
        boolean mainHandItem = !player.func_184614_ca().func_190926_b(); // getHeldItemMainhand, isEmpty
        boolean heldItem = mainHandItem || !player.func_184592_cb().func_190926_b(); // getHeldItemOffhand
        FontRenderer font = mc.field_71466_p; // fontRenderer
        int width = new ScaledResolution(mc).func_78326_a(); // getScaledWidth
        beginOverlay();
        CleanroomControllerGuide.drawVerticalLeft(font, 8, 8,
            new ControllerInput[] { settings.binding(ControllerAction.JUMP), settings.binding(ControllerAction.SNEAK) },
            new String[] { "Jump", "Sneak" });
        CleanroomControllerGuide.drawVerticalRight(font, width - 8, 8,
            new ControllerInput[] {
                settings.binding(ControllerAction.INVENTORY),
                settings.binding(ControllerAction.RADIAL_MENU),
                block ? settings.binding(ControllerAction.ATTACK) : ControllerInput.UNBOUND,
                hasTarget || heldItem ? settings.binding(ControllerAction.USE) : ControllerInput.UNBOUND,
                mainHandItem ? settings.binding(ControllerAction.DROP) : ControllerInput.UNBOUND,
                settings.binding(ControllerAction.SWAP_HANDS) },
            new String[] { "Open Inventory", "Radial Menu", "Mine", openable ? "Open" : "Use", "Drop Item", "Swap Hands" });
        endOverlay();
    }

    // above items in container slots, which vanilla draws with depth at z 100 and up
    private static void beginOverlay() {
        GlStateManager.func_179094_E(); // pushMatrix
        GlStateManager.func_179109_b(0.0f, 0.0f, 500.0f); // translate
        GlStateManager.func_179140_f(); // disableLighting
        GlStateManager.func_179131_c(1.0f, 1.0f, 1.0f, 1.0f); // color
    }

    private static void endOverlay() {
        GlStateManager.func_179121_F(); // popMatrix
        GlStateManager.func_179131_c(1.0f, 1.0f, 1.0f, 1.0f); // color
    }


    // only ever releases keys the controller pressed, so a relay keyboard holding the same key wins
    private static void setHeld(KeyBinding binding, boolean held, boolean pressed) {
        int code = binding.func_151463_i(); // getKeyCode
        if (code == 0) return;
        if (held) {
            KeyBinding.func_74510_a(code, true); // setKeyBindState
            HELD_KEY_CODES.add(code);
        } else if (HELD_KEY_CODES.remove(code)) {
            KeyBinding.func_74510_a(code, false);
        }
        if (pressed) KeyBinding.func_74507_a(code); // onTick
    }

    private static void tap(KeyBinding binding, boolean pressed) {
        int code = binding.func_151463_i(); // getKeyCode
        if (pressed && code != 0) KeyBinding.func_74507_a(code); // onTick
    }

    private static void releaseGameplayKeys() {
        for (Integer code : HELD_KEY_CODES) KeyBinding.func_74510_a(code, false); // setKeyBindState
        HELD_KEY_CODES.clear();
    }

    // vanilla reads screenshot and chat from the event stream, so keyboard keys go in as raw events
    private static void pressBinding(KeyBinding binding) {
        int code = binding.func_151463_i(); // getKeyCode
        if (code > 0) {
            Keyboard.addKeyEvent(code, true);
        } else if (code < 0) {
            KeyBinding.func_74510_a(code, true); // setKeyBindState
            KeyBinding.func_74507_a(code); // onTick
        }
    }

    private static void releaseBinding(int code) {
        if (code > 0) {
            Keyboard.addKeyEvent(code, false);
        } else if (code < 0) {
            KeyBinding.func_74510_a(code, false); // setKeyBindState
        }
    }

    private static void flushPendingReleases() {
        for (Integer code : PENDING_RAW_RELEASES) releaseBinding(code);
        PENDING_RAW_RELEASES.clear();
        for (Integer code : PENDING_STATE_RELEASES) KeyBinding.func_74510_a(code, false); // setKeyBindState
        PENDING_STATE_RELEASES.clear();
    }

    private static void tickJavaKeys(Minecraft mc, CleanroomControllerSettings settings, boolean enabled) {
        Iterator<Map.Entry<String, Integer>> down = JAVA_KEYS_DOWN.entrySet().iterator();
        while (down.hasNext()) {
            Map.Entry<String, Integer> entry = down.next();
            if (enabled && settings.javaBinding(entry.getKey()).held(STATE, settings.triggerDeadzone)) continue;
            releaseBinding(entry.getValue());
            down.remove();
        }
        if (!enabled) return;
        for (KeyBinding binding : mc.field_71474_y.field_74324_K) { // gameSettings.keyBindings
            if (binding == null) continue;
            String keyId = binding.func_151464_g(); // getKeyDescription
            if (JAVA_KEYS_DOWN.containsKey(keyId)) continue;
            if (!settings.javaBinding(keyId).pressed(STATE, settings.triggerDeadzone)) continue;
            pressBinding(binding);
            JAVA_KEYS_DOWN.put(keyId, binding.func_151463_i()); // getKeyCode
        }
    }

    private static void releaseJavaKeys() {
        for (Integer code : JAVA_KEYS_DOWN.values()) releaseBinding(code);
        JAVA_KEYS_DOWN.clear();
    }


    private static void activateRadialSlot(Minecraft mc, int slot) {
        if (slot < 0 || slot >= 8) return;
        KeyBinding binding = findKey(CleanroomControllerSettings.get().radialSlot(slot));
        if (binding == null) return;
        int code = binding.func_151463_i(); // getKeyCode
        pressBinding(binding);
        if (code > 0) PENDING_RAW_RELEASES.add(code);
        else if (code < 0) PENDING_STATE_RELEASES.add(code);
    }

    // the shared settings file uses modern key ids, 1.12 names a few of them differently
    static String legacyKeyId(String keyId) {
        if ("key.swapOffhand".equals(keyId)) return "key.swapHands";
        if ("key.socialInteractions".equals(keyId)) return "key.playerlist";
        return keyId;
    }

    static ControllerAction actionForKey(String keyId) {
        if ("key.swapHands".equals(keyId)) return ControllerAction.SWAP_HANDS;
        return CleanroomControllerSettings.controllerActionForJavaKey(keyId);
    }

    static String radialKeyLabel(String keyId) {
        if (keyId == null || keyId.isEmpty()) return "Empty";
        KeyBinding binding = findKey(keyId);
        return binding == null
            ? "Missing " + keyId
            : GameSettings.func_74298_c(binding.func_151463_i()) + " - " + I18n.func_135052_a(binding.func_151464_g()); // getKeyDisplayString, format, getKeyDescription
    }

    static String radialKeyGlyph(String keyId) {
        if (keyId == null || keyId.isEmpty()) return "+";
        KeyBinding binding = findKey(keyId);
        if (binding == null || binding.func_151463_i() == 0) return "?"; // getKeyCode
        return GameSettings.func_74298_c(binding.func_151463_i()); // getKeyDisplayString
    }

    static String[] radialKeyIds() {
        KeyBinding[] keys = Minecraft.func_71410_x().field_71474_y.field_74324_K; // getMinecraft, gameSettings, keyBindings
        List<String> ids = new ArrayList<String>();
        ids.add("");
        for (KeyBinding binding : keys) {
            if (binding != null) ids.add(binding.func_151464_g()); // getKeyDescription
        }
        return ids.toArray(new String[ids.size()]);
    }

    private static KeyBinding findKey(String keyId) {
        String wanted = legacyKeyId(keyId);
        for (KeyBinding binding : Minecraft.func_71410_x().field_71474_y.field_74324_K) { // getMinecraft, gameSettings, keyBindings
            if (binding != null && binding.func_151464_g().equals(wanted)) return binding; // getKeyDescription
        }
        return null;
    }

    // a sign line holds 90 pixels of text, anything past that is lost when the sign is saved
    static String fitSignLine(String line) {
        FontRenderer font = Minecraft.func_71410_x().field_71466_p; // getMinecraft, fontRenderer
        return font.func_78256_a(line) <= 90 ? line : font.func_78269_a(line, 90); // getStringWidth, trimStringToWidth
    }


    private static float axis(ControllerAxis axis) {
        return STATE.axis(axis);
    }

    private static boolean held(CleanroomControllerSettings settings, ControllerAction action) {
        return settings.binding(action).held(STATE, settings.triggerDeadzone);
    }

    private static boolean pressed(CleanroomControllerSettings settings, ControllerAction action) {
        return settings.binding(action).pressed(STATE, settings.triggerDeadzone);
    }

    private static boolean released(CleanroomControllerSettings settings, ControllerAction action) {
        return settings.binding(action).released(STATE, settings.triggerDeadzone);
    }
}
