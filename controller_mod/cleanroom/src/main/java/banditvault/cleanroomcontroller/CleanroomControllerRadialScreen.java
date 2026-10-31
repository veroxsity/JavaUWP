package banditvault.cleanroomcontroller;

import banditvault.controllercore.ControllerAction;

import net.minecraft.client.Minecraft;
import net.minecraft.client.gui.Gui;
import net.minecraft.client.gui.GuiScreen;
import net.minecraft.client.renderer.GlStateManager;
import net.minecraft.client.renderer.RenderHelper;
import net.minecraft.item.Item;
import net.minecraft.item.ItemStack;

public final class CleanroomControllerRadialScreen extends GuiScreen {
    private static final int TILE_SIZE = 30;
    private static final int BACKDROP = 0x22000000;
    private static final int SHADOW = 0xAA000000;
    private static final int BORDER = 0xFF080A0D;
    private static final int TILE = 0xED252C33;
    private static final int TILE_INNER = 0xE9181D22;
    private static final int ACTIVE = 0xFF5A9B70;
    private static final int ACTIVE_INNER = 0xF0273B30;
    private static final int MUTED = 0xFF9DA5AC;

    private int selectedSlot = -1;
    private final ItemStack[] icons = new ItemStack[8];

    public CleanroomControllerRadialScreen() {
        CleanroomControllerSettings settings = CleanroomControllerSettings.get();
        for (int slot = 0; slot < icons.length; slot++) {
            icons[slot] = iconFor(CleanroomControllerCompat.legacyKeyId(settings.radialSlot(slot)));
        }
    }

    void setSelectedSlot(int slot) {
        selectedSlot = slot;
    }

    int selectedSlot() {
        return selectedSlot;
    }

    @Override
    public void func_73863_a(int mouseX, int mouseY, float partialTicks) { // drawScreen
        int width = this.field_146294_l; // width
        int height = this.field_146295_m; // height
        Gui.func_73734_a(0, 0, width, height, BACKDROP); // drawRect
        int centerX = width / 2;
        int centerY = height / 2 - 12;
        int availableRadius = Math.min((width - TILE_SIZE) / 2 - 12, (height - TILE_SIZE) / 2 - 38);
        int radius = Math.max(46, Math.min(64, availableRadius));

        CleanroomControllerSettings settings = CleanroomControllerSettings.get();
        for (int slot = 0; slot < 8; slot++) {
            double angle = -Math.PI / 2.0 + slot * Math.PI / 4.0;
            boolean active = slot == selectedSlot;
            int size = active ? TILE_SIZE + 4 : TILE_SIZE;
            int x = centerX + (int) Math.round(Math.cos(angle) * radius) - size / 2;
            int y = centerY + (int) Math.round(Math.sin(angle) * radius) - size / 2;
            drawTile(x, y, size, active, slot, CleanroomControllerCompat.radialKeyGlyph(settings.radialSlot(slot)));
        }

        String selected = selectedSlot < 0
            ? "Point with right stick"
            : CleanroomControllerCompat.radialKeyLabel(settings.radialSlot(selectedSlot));
        String openBinding = settings.binding(ControllerAction.RADIAL_MENU).label;
        String cancelBinding = settings.binding(ControllerAction.MENU_CANCEL).label;
        String hint = "Release " + openBinding + " to use  |  " + cancelBinding + " cancel";
        int captionY = centerY + radius + TILE_SIZE / 2 + 12;
        int captionW = Math.min(width - 24, Math.max(textWidth(selected), textWidth(hint)) + 16);
        Gui.func_73734_a(centerX - captionW / 2, captionY - 5, centerX + captionW / 2, captionY + 22, 0xB812171C);
        CleanroomControllerGuide.centered(this.field_146289_q, shorten(selected, 36), centerX, captionY, 0xFFFFFFFF); // fontRenderer
        CleanroomControllerGuide.centered(this.field_146289_q, hint, centerX, captionY + 12, MUTED);
    }

    @Override
    public boolean func_73868_f() { // doesGuiPauseGame
        return false;
    }

    private void drawTile(int x, int y, int size, boolean active, int slot, String glyph) {
        Gui.func_73734_a(x + 2, y + 3, x + size + 3, y + size + 4, SHADOW); // drawRect
        Gui.func_73734_a(x, y, x + size, y + size, active ? ACTIVE : BORDER);
        Gui.func_73734_a(x + 2, y + 2, x + size - 2, y + size - 2, active ? ACTIVE_INNER : TILE);
        Gui.func_73734_a(x + 4, y + 4, x + size - 4, y + size - 4, active ? ACTIVE_INNER : TILE_INNER);
        Gui.func_73734_a(x + 3, y + 3, x + size - 3, y + 4, active ? 0xFF91C8A2 : 0xFF4A535C);
        if (icons[slot] != null) {
            RenderHelper.func_74520_c(); // enableGUIStandardItemLighting
            Minecraft.func_71410_x().func_175599_af().func_180450_b(icons[slot], x + (size - 16) / 2, y + (size - 16) / 2); // getMinecraft, getRenderItem, renderItemAndEffectIntoGUI
            RenderHelper.func_74518_a(); // disableStandardItemLighting
            GlStateManager.func_179140_f(); // disableLighting
        } else {
            CleanroomControllerGuide.centered(this.field_146289_q, shorten(glyph, 4), x + size / 2, y + size / 2 - 4, active ? 0xFFFFFFFF : 0xFFE0E4E7); // fontRenderer
        }
    }

    private int textWidth(String text) {
        return this.field_146289_q.func_78256_a(text); // fontRenderer.getStringWidth
    }

    // the same pictures the newer radial uses, from items 1.12 has
    private static ItemStack iconFor(String keyId) {
        String item = null;
        int meta = 0;
        if ("key.togglePerspective".equals(keyId)) item = "ender_eye";
        else if ("key.advancements".equals(keyId)) item = "knowledge_book";
        else if ("key.swapHands".equals(keyId)) item = "shield";
        else if ("key.chat".equals(keyId)) item = "writable_book";
        else if ("key.command".equals(keyId)) item = "command_block";
        else if ("key.playerlist".equals(keyId)) { item = "skull"; meta = 3; }
        else if ("key.screenshot".equals(keyId)) item = "painting";
        if (item == null) return null;
        Item found = Item.func_111206_d("minecraft:" + item); // getByNameOrId
        return found == null ? null : new ItemStack(found, 1, meta);
    }

    private static String shorten(String value, int length) {
        return value.length() <= length ? value : value.substring(0, length - 3) + "...";
    }
}
