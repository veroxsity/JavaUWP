package banditvault.cleanroomcontroller;

import net.minecraft.client.gui.GuiChat;
import net.minecraft.client.gui.GuiScreen;
import net.minecraft.client.gui.GuiTextField;
import net.minecraft.client.gui.inventory.GuiContainerCreative;
import net.minecraft.client.gui.inventory.GuiEditSign;
import net.minecraft.tileentity.TileEntitySign;
import net.minecraft.util.text.ITextComponent;
import net.minecraft.util.text.TextComponentString;

import org.lwjglx.input.Keyboard;

// chat and signs open the console keyboard on their own, other text fields open it on accept
final class CleanroomControllerKeyboard {
    private static final int SIGN_MAX_LENGTH = 384;
    private static final int SIGN_LINES = 4;
    private static final int KEY_RETURN = 28;

    private static GuiScreen activeScreen;
    private static Object activeTarget;
    private static Object dismissedTarget;
    private static String mirroredText = "";
    private static int mirroredSelectionStart;
    private static int mirroredSelectionEnd;
    private static int nativeRevision;
    private static boolean loggedUnavailable;

    private CleanroomControllerKeyboard() {
    }

    static boolean tick(GuiScreen screen) {
        Object target = findTarget(screen);
        if (screen != activeScreen) {
            closeSession();
            activeScreen = screen;
            dismissedTarget = null;
        }
        if (activeTarget != null && target != activeTarget) {
            closeSession();
        }
        if (target != dismissedTarget) dismissedTarget = null;
        if (activeTarget == null) {
            boolean automatic = target != null && (screen instanceof GuiEditSign || screen instanceof GuiChat);
            if (!automatic || target == dismissedTarget || !begin(target)) return false;
        }

        if (BanditNativeKeyboard.revision() != nativeRevision) {
            BanditNativeKeyboard.Snapshot snapshot = BanditNativeKeyboard.snapshot();
            apply(activeTarget, snapshot.text, snapshot.selectionStart, snapshot.selectionEnd);
            State applied = state(activeTarget);
            if (applied.matches(snapshot.text, snapshot.selectionStart, snapshot.selectionEnd)) {
                mirror(applied, snapshot.revision);
            } else {
                sync(applied);
            }
        } else {
            State current = state(activeTarget);
            if (!current.matches(mirroredText, mirroredSelectionStart, mirroredSelectionEnd)) {
                sync(current);
            }
        }

        int flags = BanditNativeKeyboard.consumeFlags();
        if ((flags & BanditNativeKeyboard.SUBMIT) != 0) {
            Object submitted = activeTarget;
            if (screen instanceof GuiChat) {
                Keyboard.addKeyEvent(KEY_RETURN, true);
                Keyboard.addKeyEvent(KEY_RETURN, false);
            }
            dismiss(screen, submitted);
            return true;
        }
        if ((flags & BanditNativeKeyboard.CLOSED) != 0) {
            dismiss(screen, activeTarget);
        }
        return true;
    }

    static boolean activate(GuiScreen screen, double cursorX, double cursorY) {
        Object target = textTargetAt(screen, cursorX, cursorY);
        if (target == null) return false;
        if (screen == activeScreen && target == activeTarget) return true;
        if (screen != activeScreen) {
            closeSession();
            activeScreen = screen;
        }
        dismissedTarget = null;
        if (target instanceof GuiTextField) focusOnly(screen, (GuiTextField) target);
        return begin(target);
    }

    static boolean isOpen() {
        return activeTarget != null;
    }

    static void close() {
        closeSession();
        activeScreen = null;
        dismissedTarget = null;
    }

    private static boolean begin(Object target) {
        if (!BanditNativeKeyboard.available()) {
            if (!loggedUnavailable) {
                loggedUnavailable = true;
                CleanroomControllerLog.log("Bandit native keyboard unavailable in the loaded GLFW library");
            }
            dismissedTarget = target;
            return false;
        }
        State initial = state(target);
        int revision = BanditNativeKeyboard.begin(
            initial.text,
            initial.selectionStart,
            initial.selectionEnd,
            initial.maxLength,
            target instanceof GuiEditSign);
        if (revision == 0) {
            CleanroomControllerLog.log("Bandit native keyboard failed to open for " + target.getClass().getName());
            dismissedTarget = target;
            return false;
        }
        activeTarget = target;
        mirror(initial, revision);
        return true;
    }

    private static void closeSession() {
        if (activeTarget != null) BanditNativeKeyboard.end();
        activeTarget = null;
        nativeRevision = 0;
        mirroredText = "";
        mirroredSelectionStart = 0;
        mirroredSelectionEnd = 0;
    }

    private static void dismiss(GuiScreen screen, Object target) {
        dismissedTarget = target;
        closeSession();
        CleanroomControllerCompat.resumeMenuAfterKeyboard(screen);
    }

    private static void sync(State current) {
        int revision = BanditNativeKeyboard.update(
            current.text,
            current.selectionStart,
            current.selectionEnd,
            current.maxLength);
        mirror(current, revision);
    }

    private static void mirror(State state, int revision) {
        mirroredText = state.text;
        mirroredSelectionStart = state.selectionStart;
        mirroredSelectionEnd = state.selectionEnd;
        nativeRevision = revision;
    }

    private static Object findTarget(GuiScreen screen) {
        if (screen == null) return null;
        if (screen instanceof GuiEditSign) return screen;
        for (GuiTextField field : CleanroomMenuNavigation.textFields(screen)) {
            if (field.func_146176_q() && field.func_146206_l()) return field; // getVisible, isFocused
        }
        return null;
    }

    private static Object textTargetAt(GuiScreen screen, double x, double y) {
        for (GuiTextField field : CleanroomMenuNavigation.textFields(screen)) {
            if (!field.func_146176_q()) continue; // getVisible
            int height = CleanroomReflect.getInt(field, GuiTextField.class, "field_146219_i", 20); // height
            if (x >= field.field_146209_f && x <= field.field_146209_f + field.func_146200_o() // x, getWidth
                && y >= field.field_146210_g && y <= field.field_146210_g + height) { // y
                return field;
            }
        }
        // anywhere above the done button is the sign text
        if (screen instanceof GuiEditSign && y < screen.field_146295_m / 4.0 + 100) return screen; // height
        return null;
    }

    private static void focusOnly(GuiScreen screen, GuiTextField target) {
        for (GuiTextField field : CleanroomMenuNavigation.textFields(screen)) {
            field.func_146195_b(field == target); // setFocused
        }
    }

    private static State state(Object target) {
        if (target instanceof GuiTextField) {
            GuiTextField field = (GuiTextField) target;
            return new State(field.func_146179_b(), field.func_146198_h(), field.func_146186_n(), field.func_146208_g()); // getText, getCursorPosition, getSelectionEnd, getMaxStringLength
        }
        String text = signText((GuiEditSign) target);
        return new State(text, text.length(), text.length(), SIGN_MAX_LENGTH);
    }

    private static void apply(Object target, String text, int selectionStart, int selectionEnd) {
        if (target instanceof GuiTextField) {
            GuiTextField field = (GuiTextField) target;
            if (!field.func_146179_b().equals(text)) { // getText
                field.func_146180_a(text); // setText
                refreshCreativeSearch();
            }
            int length = field.func_146179_b().length();
            field.func_146190_e(clamp(selectionEnd, length)); // setCursorPosition
            field.func_146199_i(clamp(selectionStart, length)); // setSelectionPos
            return;
        }
        setSignText((GuiEditSign) target, text);
    }

    // typing into the creative search does this from keyTyped, setText alone does not
    private static void refreshCreativeSearch() {
        if (activeScreen instanceof GuiContainerCreative) {
            CleanroomReflect.call(activeScreen, GuiContainerCreative.class, "func_147053_i", new Class<?>[0]); // updateCreativeSearch
        }
    }

    private static String signText(GuiEditSign screen) {
        TileEntitySign sign = (TileEntitySign) CleanroomReflect.get(screen, GuiEditSign.class, "field_146848_f"); // tileSign
        if (sign == null) return "";
        ITextComponent[] lines = sign.field_145915_a; // signText
        int last = -1;
        for (int i = 0; i < lines.length; i++) {
            if (lines[i] != null && !lines[i].func_150260_c().isEmpty()) last = i; // getUnformattedText
        }
        StringBuilder text = new StringBuilder();
        for (int i = 0; i <= last; i++) {
            if (i > 0) text.append('\n');
            if (lines[i] != null) text.append(lines[i].func_150260_c());
        }
        return text.toString();
    }

    private static void setSignText(GuiEditSign screen, String text) {
        TileEntitySign sign = (TileEntitySign) CleanroomReflect.get(screen, GuiEditSign.class, "field_146848_f"); // tileSign
        if (sign == null) return;
        String[] parts = text.split("\n", -1);
        for (int i = 0; i < SIGN_LINES; i++) {
            String line = i < parts.length ? parts[i] : "";
            // vanilla stops a line at 90 pixels, longer text would be cut when the sign is saved
            line = CleanroomControllerCompat.fitSignLine(line);
            sign.field_145915_a[i] = new TextComponentString(line); // signText
        }
    }

    private static int clamp(int value, int length) {
        return Math.max(0, Math.min(value, length));
    }

    private static final class State {
        final String text;
        final int selectionStart;
        final int selectionEnd;
        final int maxLength;

        State(String text, int selectionStart, int selectionEnd, int maxLength) {
            this.text = text;
            this.selectionStart = Math.min(selectionStart, selectionEnd);
            this.selectionEnd = Math.max(selectionStart, selectionEnd);
            this.maxLength = maxLength;
        }

        boolean matches(String otherText, int otherStart, int otherEnd) {
            return text.equals(otherText) && selectionStart == otherStart && selectionEnd == otherEnd;
        }
    }
}
