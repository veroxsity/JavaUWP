package banditvault.fabriccontroller;

import banditvault.fabriccontroller.mixin.BanditControllerCreativeInventoryAccessor;
import banditvault.fabriccontroller.mixin.BanditControllerEditBoxAccessor;
import banditvault.fabriccontroller.mixin.BanditControllerSignEditScreenAccessor;
import banditvault.fabriccontroller.mixin.BanditControllerTextFieldAccessor;
import banditvault.fabriccontroller.mixin.BanditControllerTextModelAccessor;
import net.minecraft.class_339;
import net.minecraft.class_342;
import net.minecraft.class_364;
import net.minecraft.class_3728;
import net.minecraft.class_408;
import net.minecraft.class_437;
import net.minecraft.class_481;
import net.minecraft.class_7529;
import net.minecraft.class_7530;
import net.minecraft.class_7533;
import net.minecraft.class_7743;
import org.lwjgl.glfw.GLFW;

final class BanditControllerKeyboard {
    private static final int SIGN_MAX_LENGTH = 384;

    private static class_437 activeScreen;
    private static Object activeTarget;
    private static Object dismissedTarget;
    private static String mirroredText = "";
    private static int mirroredSelectionStart;
    private static int mirroredSelectionEnd;
    private static int nativeRevision;
    private static boolean loggedUnavailable;

    private BanditControllerKeyboard() {
    }

    static boolean tick(class_437 screen) {
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
            boolean automatic = target != null && (target instanceof class_7743 || screen instanceof class_408);
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
            if (submitted instanceof class_7743) {
                FabricScreenApi.keyPressed(screen, GLFW.GLFW_KEY_ENTER, 0, 0);
                syncFromGame();
            } else {
                if (screen instanceof class_408) FabricScreenApi.keyPressed(screen, GLFW.GLFW_KEY_ENTER, 0, 0);
                dismiss(screen, submitted);
            }
            return true;
        }
        if ((flags & BanditNativeKeyboard.CLOSED) != 0) {
            dismiss(screen, activeTarget);
            return true;
        }
        return true;
    }

    static boolean activate(class_437 screen, double cursorX, double cursorY) {
        Object target = textTargetAt(screen, cursorX, cursorY);
        if (target == null) return false;
        if (screen != activeScreen) {
            closeSession();
            activeScreen = screen;
        }
        dismissedTarget = null;
        if (target != findTarget(screen)) screen.method_25395((class_364)target);
        return begin(target);
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
                FabricControllerLog.log("Bandit native keyboard unavailable in the loaded GLFW library");
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
            target instanceof class_7529);
        if (revision == 0) {
            FabricControllerLog.log("Bandit native keyboard failed to open for " + target.getClass().getName());
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

    private static void dismiss(class_437 screen, Object target) {
        dismissedTarget = target;
        closeSession();
        BanditControllerCompat.resumeMenuAfterKeyboard(screen);
    }

    private static void syncFromGame() {
        sync(state(activeTarget));
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

    private static Object findTarget(class_437 screen) {
        if (screen == null) return null;
        class_364 focused = Fabric12111MenuNavigation.deepestFocused(screen);
        if (focused instanceof class_342 || focused instanceof class_7529) return focused;
        return screen instanceof class_7743 ? screen : null;
    }

    private static Object textTargetAt(class_437 screen, double cursorX, double cursorY) {
        Object focused = findTarget(screen);
        if (focused instanceof class_339 && ((class_339)focused).method_25405(cursorX, cursorY)) return focused;
        class_364 hovered = screen.method_19355(cursorX, cursorY).orElse(null);
        return hovered instanceof class_342 || hovered instanceof class_7529 ? hovered : null;
    }

    private static State state(Object target) {
        if (target instanceof class_342) {
            class_342 field = (class_342)target;
            BanditControllerTextFieldAccessor access = (BanditControllerTextFieldAccessor)field;
            return new State(field.method_1882(), access.banditvault$cursor(), access.banditvault$selectionStart(), access.banditvault$maxLength());
        }
        if (target instanceof class_7529) {
            class_7529 field = (class_7529)target;
            class_7530 model = ((BanditControllerEditBoxAccessor)field).banditvault$textModel();
            BanditControllerTextModelAccessor access = (BanditControllerTextModelAccessor)model;
            return new State(field.method_44405(), access.banditvault$cursor(), access.banditvault$selectionStart(), model.method_44409());
        }
        BanditControllerSignEditScreenAccessor sign = (BanditControllerSignEditScreenAccessor)target;
        String text = sign.banditvault$lines()[sign.banditvault$currentLine()];
        class_3728 selection = sign.banditvault$selectionManager();
        return new State(text, selection.method_16201(), selection.method_16203(), SIGN_MAX_LENGTH);
    }

    private static void apply(Object target, String text, int selectionStart, int selectionEnd) {
        if (target instanceof class_342) {
            class_342 field = (class_342)target;
            if (!field.method_1882().equals(text)) {
                field.method_1852(text);
                if (activeScreen instanceof class_481) {
                    ((BanditControllerCreativeInventoryAccessor)activeScreen).banditvault$refreshSearchResults();
                }
            }
            int length = field.method_1882().length();
            field.method_1875(clamp(selectionEnd, length));
            field.method_1884(clamp(selectionStart, length));
            return;
        }
        if (target instanceof class_7529) {
            class_7529 field = (class_7529)target;
            if (!field.method_44405().equals(text)) field.method_44400(text);
            class_7530 model = ((BanditControllerEditBoxAccessor)field).banditvault$textModel();
            int length = field.method_44405().length();
            int start = clamp(selectionStart, length);
            int end = clamp(selectionEnd, length);
            model.method_44417(false);
            model.method_44412(class_7533.field_39535, start);
            model.method_44417(true);
            model.method_44412(class_7533.field_39535, end);
            model.method_44417(false);
            return;
        }
        BanditControllerSignEditScreenAccessor sign = (BanditControllerSignEditScreenAccessor)target;
        if (sign.banditvault$acceptsLine(text)) sign.banditvault$setCurrentLine(text);
        int length = sign.banditvault$lines()[sign.banditvault$currentLine()].length();
        sign.banditvault$selectionManager().method_27548(clamp(selectionEnd, length), clamp(selectionStart, length));
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
