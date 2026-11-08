package banditvault.neoforgecontroller;

import java.lang.reflect.Field;
import net.minecraft.client.Minecraft;
import net.minecraft.client.gui.GuiGraphicsExtractor;
import net.minecraft.client.gui.screens.Screen;
import net.minecraft.client.gui.screens.recipebook.RecipeBookComponent;
import net.minecraft.client.input.KeyEvent;
import net.minecraft.client.input.MouseButtonEvent;
import net.minecraft.client.input.MouseButtonInfo;
import net.minecraft.client.player.LocalPlayer;
import net.minecraft.world.entity.player.Input;

final class NeoForgeVersionApi {
    private NeoForgeVersionApi() {
    }

    static void setSneakInput(LocalPlayer player, boolean sneak) {
        if (player == null || player.input == null) {
            return;
        }
        Input input = player.input.keyPresses;
        player.input.keyPresses = new Input(
            input.forward(),
            input.backward(),
            input.left(),
            input.right(),
            input.jump(),
            sneak,
            input.sprint());
    }

    static RecipeBookComponent recipeBook(Screen screen) {
        Class<?> type = screen == null ? null : screen.getClass();
        while (type != null) {
            try {
                Field field = type.getDeclaredField("recipeBookComponent");
                field.setAccessible(true);
                return (RecipeBookComponent) field.get(screen);
            } catch (NoSuchFieldException ignored) {
                type = type.getSuperclass();
            } catch (ReflectiveOperationException ignored) {
                return null;
            }
        }
        return null;
    }

    static int selectedHotbarSlot(LocalPlayer player) {
        return player.getInventory().getSelectedSlot();
    }

    static void setSelectedHotbarSlot(LocalPlayer player, int slot) {
        player.getInventory().setSelectedSlot(slot);
    }

    static void renderCursor(Object target, int x, int y) {
        GuiGraphicsExtractor graphics = (GuiGraphicsExtractor) target;
        graphics.fill(x - 3, y - 3, x + 4, y + 4, 0x66000000);
        graphics.fill(x - 5, y, x + 6, y + 1, 0xFFFFFFFF);
        graphics.fill(x, y - 5, x + 1, y + 6, 0xFFFFFFFF);
    }

    static int guiWidth(Object target) {
        return ((GuiGraphicsExtractor)target).guiWidth();
    }

    static long windowHandle(Minecraft client) {
        return client.getWindow().handle();
    }

    static boolean mousePressed(Screen screen, double x, double y, int button) {
        return screen.mouseClicked(mouseEvent(x, y, button), false);
    }

    static boolean mouseReleased(Screen screen, double x, double y, int button) {
        return screen.mouseReleased(mouseEvent(x, y, button));
    }

    static boolean keyPressed(Screen screen, int keyCode, int scanCode, int modifiers) {
        return screen.keyPressed(new KeyEvent(keyCode, scanCode, modifiers));
    }

    private static MouseButtonEvent mouseEvent(double x, double y, int button) {
        return new MouseButtonEvent(x, y, new MouseButtonInfo(button, 0));
    }

    static void quickMove(net.minecraft.client.gui.screens.inventory.AbstractContainerScreen<?> screen, net.minecraft.world.inventory.Slot slot) {
        ((banditvault.neoforgecontroller.mixin.NeoForgeControllerContainerAccessor) screen).banditvault$slotClicked(slot, slot.index, 0, net.minecraft.world.inventory.ContainerInput.QUICK_MOVE);
    }
}
