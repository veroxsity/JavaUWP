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

    static void beginCursorLayer(GuiGraphicsExtractor context) {
        context.nextStratum();
    }

    static void endCursorLayer(GuiGraphicsExtractor context) {
    }

    static boolean mousePressed(Screen screen, double mouseX, double mouseY, int button) {
        return screen.mouseClicked(mouseEvent(mouseX, mouseY, button), false);
    }

    static boolean mouseReleased(Screen screen, double mouseX, double mouseY, int button) {
        return screen.mouseReleased(mouseEvent(mouseX, mouseY, button));
    }

    static boolean keyPressed(Screen screen, int keyCode, int scanCode, int modifiers) {
        try {
            for (java.lang.reflect.Constructor<?> constructor : KeyEvent.class.getDeclaredConstructors()) {
                Class<?>[] types = constructor.getParameterTypes();
                Object[] args = new Object[types.length];
                int integerIndex = 0;
                boolean supported = true;
                for (int i = 0; i < types.length; i++) {
                    if (types[i] == int.class || types[i] == Integer.class) {
                        args[i] = integerIndex == 0 ? keyCode : integerIndex == 1 ? scanCode : integerIndex == 2 ? modifiers : 0;
                        integerIndex++;
                    } else if (types[i] == boolean.class || types[i] == Boolean.class) {
                        args[i] = false;
                    } else {
                        supported = false;
                        break;
                    }
                }
                if (supported && integerIndex >= 3) {
                    constructor.setAccessible(true);
                    return screen.keyPressed((KeyEvent) constructor.newInstance(args));
                }
            }
            throw new NoSuchMethodException("No compatible Minecraft KeyEvent constructor");
        } catch (ReflectiveOperationException error) {
            throw new IllegalStateException("Unable to create Minecraft KeyEvent", error);
        }
    }

    static void scroll(Screen screen, double mouseX, double mouseY, double amount) {
        screen.mouseScrolled(mouseX, mouseY, 0.0, amount);
    }

    private static MouseButtonEvent mouseEvent(double mouseX, double mouseY, int button) {
        return new MouseButtonEvent(mouseX, mouseY, new MouseButtonInfo(button, 0));
    }
}
