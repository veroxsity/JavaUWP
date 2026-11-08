package banditvault.neoforgecontroller;

import net.minecraft.client.gui.GuiGraphics;
import net.minecraft.client.Minecraft;
import net.minecraft.client.gui.screens.Screen;
import net.minecraft.client.gui.screens.recipebook.RecipeBookComponent;
import net.minecraft.client.gui.screens.recipebook.RecipeUpdateListener;
import net.minecraft.client.player.LocalPlayer;

final class NeoForgeVersionApi {
    private NeoForgeVersionApi() {
    }

    static void setSneakInput(LocalPlayer player, boolean sneak) {
        if (player != null && player.input != null) {
            player.input.shiftKeyDown = sneak;
        }
    }

    static RecipeBookComponent recipeBook(Screen screen) {
        return screen instanceof RecipeUpdateListener
            ? ((RecipeUpdateListener) screen).getRecipeBookComponent()
            : null;
    }

    static int selectedHotbarSlot(LocalPlayer player) {
        return player.getInventory().selected;
    }

    static void setSelectedHotbarSlot(LocalPlayer player, int slot) {
        player.getInventory().selected = slot;
    }

    static void renderCursor(Object target, int x, int y) {
        GuiGraphics graphics = (GuiGraphics) target;
        graphics.pose().pushPose();
        graphics.pose().translate(0.0, 0.0, 1000.0);
        graphics.fill(x - 3, y - 3, x + 4, y + 4, 0x66000000);
        graphics.fill(x - 5, y, x + 6, y + 1, 0xFFFFFFFF);
        graphics.fill(x, y - 5, x + 1, y + 6, 0xFFFFFFFF);
        graphics.pose().popPose();
    }

    static int guiWidth(Object target) {
        return ((GuiGraphics)target).guiWidth();
    }

    static void beginOverlay(GuiGraphics graphics) {
        graphics.pose().pushPose();
        graphics.pose().translate(0.0, 0.0, 400.0);
    }

    static void endOverlay(GuiGraphics graphics) {
        graphics.pose().popPose();
    }

    static long windowHandle(Minecraft client) {
        return client.getWindow().getWindow();
    }

    static boolean mousePressed(Screen screen, double x, double y, int button) {
        return screen.mouseClicked(x, y, button);
    }

    static boolean mouseReleased(Screen screen, double x, double y, int button) {
        return screen.mouseReleased(x, y, button);
    }

    static boolean keyPressed(Screen screen, int keyCode, int scanCode, int modifiers) {
        return screen.keyPressed(keyCode, scanCode, modifiers);
    }

    static void quickMove(net.minecraft.client.gui.screens.inventory.AbstractContainerScreen<?> screen, net.minecraft.world.inventory.Slot slot) {
        ((banditvault.neoforgecontroller.mixin.NeoForgeControllerContainerAccessor) screen).banditvault$slotClicked(slot, slot.index, 0, net.minecraft.world.inventory.ClickType.QUICK_MOVE);
    }
}
