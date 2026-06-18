package banditvault.xboxcompat;

import net.minecraft.class_332;
import net.minecraft.class_437;

public final class BanditMouseCursorOverlay {
    private BanditMouseCursorOverlay() {
    }

    public static void render(class_437 screen, class_332 context, int mouseX, int mouseY) {
        if (screen == null || context == null) {
            return;
        }

        context.method_25294(mouseX - 3, mouseY - 3, mouseX + 4, mouseY + 4, 0x66000000);
        context.method_25294(mouseX - 5, mouseY, mouseX + 6, mouseY + 1, 0xFFFFFFFF);
        context.method_25294(mouseX, mouseY - 5, mouseX + 1, mouseY + 6, 0xFFFFFFFF);
    }
}
