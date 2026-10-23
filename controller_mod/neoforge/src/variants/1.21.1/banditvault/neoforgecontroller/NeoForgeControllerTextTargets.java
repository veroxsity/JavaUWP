package banditvault.neoforgecontroller;

import banditvault.neoforgecontroller.mixin.NeoForgeControllerBookAccessor;
import net.minecraft.client.gui.screens.Screen;
import net.minecraft.client.gui.screens.inventory.BookEditScreen;

final class NeoForgeControllerTextTargets {
    static void keyPress(net.minecraft.client.KeyboardHandler keyboard, long window, int key, int scan, int action, int modifiers) {
        ((banditvault.neoforgecontroller.mixin.NeoForgeControllerKeyboardAccessor) keyboard).banditvault$keyPress(window, key, scan, action, modifiers);
    }

    static Object bookTarget(Screen screen) {
        if (!(screen instanceof BookEditScreen)) return null;
        NeoForgeControllerBookAccessor access = (NeoForgeControllerBookAccessor) screen;
        return access.banditvault$signing() ? access.banditvault$title() : access.banditvault$page();
    }

    static boolean creativeSearchAllowed() {
        return false;
    }

    static boolean multiline(Screen screen) {
        return screen instanceof BookEditScreen && !((NeoForgeControllerBookAccessor) screen).banditvault$signing();
    }
}
