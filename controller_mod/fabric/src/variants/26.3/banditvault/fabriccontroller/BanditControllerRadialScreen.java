package banditvault.fabriccontroller;

import banditvault.controllercore.ControllerAction;
import net.minecraft.world.item.Item;
import net.minecraft.world.item.ItemStack;
import net.minecraft.network.chat.Component;
import net.minecraft.resources.Identifier;
import net.minecraft.client.gui.GuiGraphicsExtractor;
import net.minecraft.client.gui.screens.Screen;
import net.minecraft.core.registries.BuiltInRegistries;

public final class BanditControllerRadialScreen extends Screen {
    // one page stays until eight slots are not enough
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

    public BanditControllerRadialScreen() {
        super(Component.nullToEmpty("Bandit Radial"));
        BanditControllerSettings settings = BanditControllerSettings.get();
        for (int slot = 0; slot < icons.length; slot++) {
            icons[slot] = iconFor(settings.radialSlot(slot));
        }
    }

    public void setSelectedSlot(int slot) {
        selectedSlot = slot;
    }

    public int selectedSlot() {
        return selectedSlot;
    }

    @Override
    public void extractRenderState(GuiGraphicsExtractor context, int mouseX, int mouseY, float delta) {
        BanditControllerCompat.updateScreenCursorBeforeRender(this, mouseX, mouseY);
        context.fill(0, 0, this.width, this.height, BACKDROP);
        int centerX = this.width / 2;
        int centerY = this.height / 2 - 12;
        int availableRadius = Math.min((this.width - TILE_SIZE) / 2 - 12, (this.height - TILE_SIZE) / 2 - 38);
        int radius = Math.max(46, Math.min(64, availableRadius));

        BanditControllerSettings settings = BanditControllerSettings.get();
        for (int slot = 0; slot < 8; slot++) {
            double angle = -Math.PI / 2.0 + slot * Math.PI / 4.0;
            boolean active = slot == selectedSlot;
            int size = active ? TILE_SIZE + 4 : TILE_SIZE;
            int x = centerX + (int)Math.round(Math.cos(angle) * radius) - size / 2;
            int y = centerY + (int)Math.round(Math.sin(angle) * radius) - size / 2;
            drawTile(context, x, y, size, active, slot, BanditControllerCompat.radialKeyGlyph(settings.radialSlot(slot)));
        }

        String selected = selectedSlot < 0
            ? "Point with right stick"
            : BanditControllerCompat.radialKeyLabel(settings.radialSlot(selectedSlot));
        String openBinding = settings.binding(ControllerAction.RADIAL_MENU).label;
        String cancelBinding = settings.binding(ControllerAction.MENU_CANCEL).label;
        String hint = "Release " + openBinding + " to use  |  " + cancelBinding + " cancel";
        int captionY = centerY + radius + TILE_SIZE / 2 + 12;
        int captionW = Math.min(this.width - 24, Math.max(this.font.width(selected), this.font.width(hint)) + 16);
        context.fill(centerX - captionW / 2, captionY - 5, centerX + captionW / 2, captionY + 22, 0xB812171C);
        context.centeredText(this.font, Component.nullToEmpty(shorten(selected, 36)), centerX, captionY, 0xFFFFFFFF);
        context.centeredText(this.font, Component.nullToEmpty(hint), centerX, captionY + 12, MUTED);
        BanditControllerCompat.renderCursor(this, context);
    }

    @Override
    public boolean isPauseScreen() {
        return false;
    }

    private void drawTile(GuiGraphicsExtractor context, int x, int y, int size, boolean active, int slot, String glyph) {
        context.fill(x + 2, y + 3, x + size + 3, y + size + 4, SHADOW);
        context.fill(x, y, x + size, y + size, active ? ACTIVE : BORDER);
        context.fill(x + 2, y + 2, x + size - 2, y + size - 2, active ? ACTIVE_INNER : TILE);
        context.fill(x + 4, y + 4, x + size - 4, y + size - 4, active ? ACTIVE_INNER : TILE_INNER);
        context.fill(x + 3, y + 3, x + size - 3, y + 4, active ? 0xFF91C8A2 : 0xFF4A535C);
        if (icons[slot] != null) {
            context.fakeItem(icons[slot], x + (size - 16) / 2, y + (size - 16) / 2);
        } else {
            String shortGlyph = shorten(glyph, 4);
            context.centeredText(this.font, Component.nullToEmpty(shortGlyph), x + size / 2, y + size / 2 - 4, active ? 0xFFFFFFFF : 0xFFE0E4E7);
        }
    }

    private static ItemStack iconFor(String keyId) {
        // key mappings have no icon metadata
        String item = null;
        if ("key.togglePerspective".equals(keyId)) item = "spyglass";
        else if ("key.advancements".equals(keyId)) item = "knowledge_book";
        else if ("key.swapOffhand".equals(keyId)) item = "shield";
        else if ("key.chat".equals(keyId)) item = "writable_book";
        else if ("key.command".equals(keyId)) item = "command_block";
        else if ("key.socialInteractions".equals(keyId)) item = "player_head";
        else if ("key.screenshot".equals(keyId)) item = "painting";
        if (item == null) {
            return null;
        }
        Item resolved = BuiltInRegistries.ITEM.getValue(Identifier.parse("minecraft:" + item));
        return resolved == null ? null : new ItemStack(resolved);
    }

    private static String shorten(String value, int length) {
        return value.length() <= length ? value : value.substring(0, length - 3) + "...";
    }
}
