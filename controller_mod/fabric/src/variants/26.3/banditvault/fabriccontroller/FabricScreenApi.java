package banditvault.fabriccontroller;

import net.minecraft.client.gui.GuiGraphicsExtractor;
import net.minecraft.client.gui.screens.Screen;
import net.minecraft.client.input.KeyEvent;
import net.minecraft.client.input.MouseButtonEvent;
import net.minecraft.client.input.MouseButtonInfo;

final class FabricScreenApi {
    private FabricScreenApi() {
    }

    static void renderBackground(Screen screen, GuiGraphicsExtractor context, int mouseX, int mouseY, float delta) {
    }

    static void drawCursor(GuiGraphicsExtractor context, int x, int y) {
        context.fill(x - 3, y - 3, x + 4, y + 4, 0x66000000);
        context.fill(x - 5, y, x + 6, y + 1, 0xFFFFFFFF);
        context.fill(x, y - 5, x + 1, y + 6, 0xFFFFFFFF);
    }

    static boolean mousePressed(Screen screen, double mouseX, double mouseY, int button) {
        return screen.mouseClicked(mouseEvent(mouseX, mouseY, button), false);
    }

    static boolean mouseReleased(Screen screen, double mouseX, double mouseY, int button) {
        return screen.mouseReleased(mouseEvent(mouseX, mouseY, button));
    }

    static boolean keyPressed(Screen screen, int keyCode, int scanCode, int modifiers) {
        return screen.keyPressed(new KeyEvent(keyCode, scanCode, modifiers));
    }

    static void scroll(Screen screen, double mouseX, double mouseY, double amount) {
        screen.mouseScrolled(mouseX, mouseY, 0.0, amount);
    }

    private static MouseButtonEvent mouseEvent(double mouseX, double mouseY, int button) {
        return new MouseButtonEvent(mouseX, mouseY, new MouseButtonInfo(button, 0));
    }
}
