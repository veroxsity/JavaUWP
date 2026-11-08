package banditvault.fabriccontroller;

import net.minecraft.class_332;
import net.minecraft.class_437;

final class FabricScreenApi {
    private FabricScreenApi() {
    }

    static void renderBackground(class_437 screen, class_332 context, int mouseX, int mouseY, float delta) {
    }

    static void drawCursor(class_332 context, int x, int y) {
        context.method_25294(x - 3, y - 3, x + 4, y + 4, 0x66000000);
        context.method_25294(x - 5, y, x + 6, y + 1, 0xFFFFFFFF);
        context.method_25294(x, y - 5, x + 1, y + 6, 0xFFFFFFFF);
    }

    static void beginOverlay(class_332 context) {
    }

    static void endOverlay(class_332 context) {
    }

    static boolean mousePressed(class_437 screen, double mouseX, double mouseY, int button) {
        return screen.method_25402(mouseX, mouseY, button);
    }

    static boolean mouseReleased(class_437 screen, double mouseX, double mouseY, int button) {
        return screen.method_25406(mouseX, mouseY, button);
    }

    static boolean keyPressed(class_437 screen, int keyCode, int scanCode, int modifiers) {
        return screen.method_25404(keyCode, scanCode, modifiers);
    }

    static void scroll(class_437 screen, double mouseX, double mouseY, double amount) {
        screen.method_25401(mouseX, mouseY, 0.0, amount);
    }
}
