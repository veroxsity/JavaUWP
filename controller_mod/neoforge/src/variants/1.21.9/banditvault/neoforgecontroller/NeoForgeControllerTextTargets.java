package banditvault.neoforgecontroller;

import banditvault.neoforgecontroller.mixin.NeoForgeControllerBookAccessor;
import net.minecraft.client.gui.screens.Screen;
import net.minecraft.client.gui.screens.inventory.BookEditScreen;

final class NeoForgeControllerTextTargets {
    static void keyPress(net.minecraft.client.KeyboardHandler keyboard, long window, int key, int scan, int action, int modifiers) {
        ((banditvault.neoforgecontroller.mixin.NeoForgeControllerKeyboardAccessor) keyboard).banditvault$keyEvent(window, action, new net.minecraft.client.input.KeyEvent(key, scan, modifiers));
    }

    static Object bookTarget(Screen screen) {
        return screen instanceof BookEditScreen ? ((NeoForgeControllerBookAccessor) screen).banditvault$page() : null;
    }

    static boolean creativeSearchAllowed() {
        return true;
    }

    static boolean multiline(Screen screen) {
        return screen instanceof BookEditScreen;
    }
}
