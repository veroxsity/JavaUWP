package banditvault.forgecontroller;

import banditvault.forgecontroller.mixin.ForgeControllerBookAccessor;
import net.minecraft.client.gui.screens.Screen;
import net.minecraft.client.gui.screens.inventory.BookEditScreen;

final class ForgeControllerTextTargets {
    static Object bookTarget(Screen screen) {
        if (!(screen instanceof BookEditScreen)) return null;
        ForgeControllerBookAccessor access = (ForgeControllerBookAccessor) screen;
        return access.banditvault$signing() ? access.banditvault$title() : access.banditvault$page();
    }

    static boolean multiline(Screen screen) {
        return screen instanceof BookEditScreen && !((ForgeControllerBookAccessor) screen).banditvault$signing();
    }
}
