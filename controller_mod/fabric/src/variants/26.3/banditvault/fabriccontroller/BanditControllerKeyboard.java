package banditvault.fabriccontroller;

import net.minecraft.client.Minecraft;
import net.minecraft.client.gui.components.AbstractWidget;
import net.minecraft.client.gui.components.EditBox;
import net.minecraft.client.gui.components.MultiLineEditBox;
import net.minecraft.client.gui.components.events.GuiEventListener;
import net.minecraft.client.gui.screens.ChatScreen;
import net.minecraft.client.gui.screens.Screen;
import net.minecraft.client.gui.screens.inventory.AbstractSignEditScreen;
import org.lwjgl.sdl.SDLHints;

/** 26.3 text input bridge. Minecraft now owns SDL text events directly. */
final class BanditControllerKeyboard {
    private static Screen activeScreen;
    private static GuiEventListener activeTarget;
    private static Object dismissedTarget;
    private static boolean screenKeyboardHintEnabled;

    private BanditControllerKeyboard() {
    }

    static boolean tick(Screen screen) {
        GuiEventListener target = findTarget(screen);
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
            boolean automatic = target != null && (target instanceof AbstractSignEditScreen || screen instanceof ChatScreen);
            if (!automatic || target == dismissedTarget || !begin(target)) return false;
        }
        return true;
    }

    static boolean activate(Screen screen, double cursorX, double cursorY) {
        GuiEventListener target = textTargetAt(screen, cursorX, cursorY);
        if (target == null) return false;
        if (screen != activeScreen) {
            closeSession();
            activeScreen = screen;
        }
        dismissedTarget = null;
        if (target != findTarget(screen)) screen.setFocused(target);
        return begin(target);
    }

    static void close() {
        closeSession();
        activeScreen = null;
        dismissedTarget = null;
    }

    private static boolean begin(GuiEventListener target) {
        Minecraft client = Minecraft.getInstance();
        if (client == null || target == null) return false;
        try {
            // Mojang disables automatic screen keyboards globally in 26.3. The controller path
            // opts in only while the player explicitly edits a text field, then restores it.
            SDLHints.SDL_SetHint("SDL_ENABLE_SCREEN_KEYBOARD", "1");
            screenKeyboardHintEnabled = true;
            client.onTextInputFocusChange(target, true);
            activeTarget = target;
            FabricControllerLog.log("SDL3 text input activated for " + target.getClass().getName());
            return true;
        } catch (Throwable t) {
            FabricControllerLog.logException("SDL3 text input activation failed", t);
            SDLHints.SDL_SetHint("SDL_ENABLE_SCREEN_KEYBOARD", "0");
            screenKeyboardHintEnabled = false;
            dismissedTarget = target;
            return false;
        }
    }

    private static void closeSession() {
        Minecraft client = Minecraft.getInstance();
        GuiEventListener target = activeTarget;
        activeTarget = null;
        if (target != null && client != null) {
            try {
                client.onTextInputFocusChange(target, false);
            } catch (Throwable t) {
                FabricControllerLog.logException("SDL3 text input shutdown failed", t);
            }
        }
        if (screenKeyboardHintEnabled) {
            SDLHints.SDL_SetHint("SDL_ENABLE_SCREEN_KEYBOARD", "0");
            screenKeyboardHintEnabled = false;
            // Keep vanilla text input synchronized for a field that remains focused after the
            // controller dismisses the on-screen keyboard; the hint prevents reopening the OSK.
            if (target != null && client != null && target.isFocused()) {
                try {
                    client.onTextInputFocusChange(target, true);
                } catch (Throwable ignored) {
                }
            }
        }
    }

    private static GuiEventListener findTarget(Screen screen) {
        if (screen == null) return null;
        GuiEventListener focused = Fabric12111MenuNavigation.deepestFocused(screen);
        if (focused instanceof EditBox || focused instanceof MultiLineEditBox) return focused;
        return screen instanceof AbstractSignEditScreen ? screen : null;
    }

    private static GuiEventListener textTargetAt(Screen screen, double cursorX, double cursorY) {
        if (screen == null) return null;
        GuiEventListener focused = findTarget(screen);
        if (focused instanceof AbstractWidget && ((AbstractWidget)focused).isMouseOver(cursorX, cursorY)) return focused;
        GuiEventListener hovered = screen.getChildAt(cursorX, cursorY).orElse(null);
        if (hovered == null && screen instanceof AbstractSignEditScreen) return focused;
        return hovered instanceof EditBox || hovered instanceof MultiLineEditBox ? hovered : null;
    }
}
