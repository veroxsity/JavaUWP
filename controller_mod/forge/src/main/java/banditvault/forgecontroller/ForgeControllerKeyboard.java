package banditvault.forgecontroller;

import banditvault.forgecontroller.mixin.ForgeControllerEditBoxAccessor;
import banditvault.forgecontroller.mixin.ForgeControllerSignEditScreenAccessor;
import banditvault.forgecontroller.mixin.ForgeControllerTextHelperAccessor;
import net.minecraft.client.gui.screens.inventory.BookEditScreen;
import banditvault.forgecontroller.mixin.ForgeControllerTextFieldAccessor;
import banditvault.forgecontroller.mixin.ForgeControllerTextModelAccessor;
import net.minecraft.client.gui.components.AbstractWidget;
import net.minecraft.client.gui.components.EditBox;
import net.minecraft.client.gui.components.events.GuiEventListener;
import net.minecraft.client.gui.font.TextFieldHelper;
import net.minecraft.client.gui.screens.ChatScreen;
import net.minecraft.client.gui.screens.Screen;
import net.minecraft.client.gui.screens.inventory.CreativeModeInventoryScreen;
import net.minecraft.client.gui.components.MultiLineEditBox;
import net.minecraft.client.gui.components.MultilineTextField;
import net.minecraft.client.gui.components.Whence;
import net.minecraft.client.gui.screens.inventory.AbstractSignEditScreen;
import org.lwjgl.glfw.GLFW;

final class ForgeControllerKeyboard {
    private static final int SIGN_MAX_LENGTH = 384;

    private static Screen activeScreen;
    private static Object activeTarget;
    private static Object dismissedTarget;
    private static String mirroredText = "";
    private static int mirroredSelectionStart;
    private static int mirroredSelectionEnd;
    private static int nativeRevision;
    private static boolean loggedUnavailable;

    private ForgeControllerKeyboard() {
    }

    static boolean tick(Screen screen) {
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
            boolean automatic = target != null && (screen instanceof AbstractSignEditScreen || screen instanceof ChatScreen || screen instanceof BookEditScreen);
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
            if (screen instanceof AbstractSignEditScreen) {
                screen.m_7933_( GLFW.GLFW_KEY_ENTER, 0, 0);
                syncFromGame();
            } else {
                if (screen instanceof ChatScreen) screen.m_7933_( GLFW.GLFW_KEY_ENTER, 0, 0);
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

    static boolean activate(Screen screen, double cursorX, double cursorY) {
        Object target = textTargetAt(screen, cursorX, cursorY);
        if (target == null) return false;
        if (screen == activeScreen && target == activeTarget) return true;
        if (screen != activeScreen) {
            closeSession();
            activeScreen = screen;
        }
        dismissedTarget = null;
        if (target instanceof GuiEventListener && target != findTarget(screen)) screen.m_7522_((GuiEventListener)target);
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
                ForgeControllerLog.log("Bandit native keyboard unavailable in the loaded GLFW library");
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
            target instanceof MultiLineEditBox || (screenIsBook() && ForgeControllerTextTargets.multiline(activeScreen)));
        if (revision == 0) {
            ForgeControllerLog.log("Bandit native keyboard failed to open for " + target.getClass().getName());
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

    private static void dismiss(Screen screen, Object target) {
        dismissedTarget = target;
        closeSession();
        if (target instanceof EditBox || target instanceof MultiLineEditBox) screen.m_7522_(null);
        ForgeControllerCompat.resumeMenuAfterKeyboard(screen);
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

    private static Object findTarget(Screen screen) {
        if (screen == null) return null;
        GuiEventListener focused = ForgeMenuNavigation.deepestFocused(screen);
        if (focused instanceof EditBox || focused instanceof MultiLineEditBox) return focused;
        if (screen instanceof AbstractSignEditScreen) {
            return ((ForgeControllerSignEditScreenAccessor) screen).banditvault$selectionManager();
        }
        return ForgeControllerTextTargets.bookTarget(screen);
    }

    private static Object textTargetAt(Screen screen, double cursorX, double cursorY) {
        if (screen instanceof CreativeModeInventoryScreen) return null;
        Object focused = findTarget(screen);
        if (focused instanceof AbstractWidget && ((AbstractWidget)focused).m_5953_(cursorX, cursorY)) return focused;
        GuiEventListener hovered = screen.m_94729_(cursorX, cursorY).orElse(null);
        if (hovered instanceof EditBox || hovered instanceof MultiLineEditBox) return hovered;
        if (hovered == null && screen instanceof AbstractSignEditScreen) return focused;
        if (hovered == null && screen instanceof BookEditScreen &&
            cursorX >= screen.f_96543_ / 2.0 - 58 && cursorX <= screen.f_96543_ / 2.0 + 58 &&
            cursorY >= 32 && cursorY <= 160) return ForgeControllerTextTargets.bookTarget(screen);
        return null;
    }

    private static State state(Object target) {
        if (target instanceof EditBox) {
            EditBox field = (EditBox)target;
            ForgeControllerTextFieldAccessor access = (ForgeControllerTextFieldAccessor)field;
            return new State(field.m_94155_(), access.banditvault$cursor(), access.banditvault$selectionStart(), access.banditvault$maxLength());
        }
        if (target instanceof MultiLineEditBox) {
            MultiLineEditBox field = (MultiLineEditBox)target;
            MultilineTextField model = ((ForgeControllerEditBoxAccessor)field).banditvault$textModel();
            ForgeControllerTextModelAccessor access = (ForgeControllerTextModelAccessor)model;
            return new State(field.m_239249_(), access.banditvault$cursor(), access.banditvault$selectionStart(), model.m_239390_());
        }
        TextFieldHelper selection = (TextFieldHelper) target;
        String text = ((ForgeControllerTextHelperAccessor) selection).banditvault$text().get();
        return new State(text, selection.m_95194_(), selection.m_95197_(), screenIsBook() ? 1024 : SIGN_MAX_LENGTH);
    }

    private static void apply(Object target, String text, int selectionStart, int selectionEnd) {
        if (target instanceof EditBox) {
            EditBox field = (EditBox)target;
            if (!field.m_94155_().equals(text)) {
                field.m_94144_(text);
            }
            int length = field.m_94155_().length();
            field.m_94196_(clamp(selectionEnd, length));
            field.m_94208_(clamp(selectionStart, length));
            return;
        }
        if (target instanceof MultiLineEditBox) {
            MultiLineEditBox field = (MultiLineEditBox)target;
            if (!field.m_239249_().equals(text)) field.m_240159_(text);
            MultilineTextField model = ((ForgeControllerEditBoxAccessor)field).banditvault$textModel();
            int length = field.m_239249_().length();
            int start = clamp(selectionStart, length);
            int end = clamp(selectionEnd, length);
            model.m_239950_(false);
            model.m_239797_(Whence.ABSOLUTE, start);
            model.m_239950_(true);
            model.m_239797_(Whence.ABSOLUTE, end);
            model.m_239950_(false);
            return;
        }
        TextFieldHelper selection = (TextFieldHelper) target;
        if (!state(target).text.equals(text)) {
            selection.m_95188_();
            selection.m_95158_(text);
        }
        int length = state(target).text.length();
        selection.m_95147_(clamp(selectionEnd, length), clamp(selectionStart, length));
    }

    private static boolean screenIsBook() {
        return activeScreen instanceof BookEditScreen;
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
