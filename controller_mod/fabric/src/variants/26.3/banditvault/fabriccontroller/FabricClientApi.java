package banditvault.fabriccontroller;

import net.minecraft.client.Minecraft;
import net.minecraft.client.gui.screens.Screen;

public final class FabricClientApi {
    private FabricClientApi() {
    }

    public static Screen screen(Minecraft client) {
        return client.gui.screen();
    }

    public static void setScreen(Minecraft client, Screen screen) {
        client.gui.setScreen(screen);
    }

    public static boolean isHudHidden(Minecraft client) {
        return client.gui.hud.isHidden();
    }
}
