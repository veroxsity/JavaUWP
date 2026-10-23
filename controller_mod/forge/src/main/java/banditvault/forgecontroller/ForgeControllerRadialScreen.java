package banditvault.forgecontroller;

import banditvault.controllercore.ControllerAction;
import net.minecraft.client.gui.GuiGraphics;
import net.minecraft.client.gui.screens.Screen;
import net.minecraft.network.chat.Component;
import net.minecraft.world.item.ItemStack;

public final class ForgeControllerRadialScreen extends Screen {
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

    public ForgeControllerRadialScreen() {
        super(ForgeControllerCompat.textLiteral("Bandit Radial"));
        ForgeControllerSettings settings = ForgeControllerSettings.get();
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
    public void m_88315_(GuiGraphics context, int mouseX, int mouseY, float delta) {
        context.m_280509_(0, 0, this.f_96543_, this.f_96544_, BACKDROP);
        int centerX = this.f_96543_ / 2;
        int centerY = this.f_96544_ / 2 - 12;
        int availableRadius = Math.min((this.f_96543_ - TILE_SIZE) / 2 - 12, (this.f_96544_ - TILE_SIZE) / 2 - 38);
        int radius = Math.max(46, Math.min(64, availableRadius));

        ForgeControllerSettings settings = ForgeControllerSettings.get();
        for (int slot = 0; slot < 8; slot++) {
            double angle = -Math.PI / 2.0 + slot * Math.PI / 4.0;
            boolean active = slot == selectedSlot;
            int size = active ? TILE_SIZE + 4 : TILE_SIZE;
            int x = centerX + (int)Math.round(Math.cos(angle) * radius) - size / 2;
            int y = centerY + (int)Math.round(Math.sin(angle) * radius) - size / 2;
            drawTile(context, x, y, size, active, slot, ForgeControllerCompat.radialKeyGlyph(settings.radialSlot(slot)));
        }

        String selected = selectedSlot < 0
            ? "Point with right stick"
            : ForgeControllerCompat.radialKeyLabel(settings.radialSlot(selectedSlot));
        String openBinding = settings.binding(ControllerAction.RADIAL_MENU).label;
        String cancelBinding = settings.binding(ControllerAction.MENU_CANCEL).label;
        String hint = "Release " + openBinding + " to use  |  " + cancelBinding + " cancel";
        int captionY = centerY + radius + TILE_SIZE / 2 + 12;
        int captionW = Math.min(this.f_96543_ - 24, Math.max(this.f_96547_.m_92895_(selected), this.f_96547_.m_92895_(hint)) + 16);
        context.m_280509_(centerX - captionW / 2, captionY - 5, centerX + captionW / 2, captionY + 22, 0xB812171C);
        context.m_280653_(this.f_96547_, ForgeControllerCompat.textLiteral(shorten(selected, 36)), centerX, captionY, 0xFFFFFFFF);
        context.m_280653_(this.f_96547_, ForgeControllerCompat.textLiteral(hint), centerX, captionY + 12, MUTED);
    }

    @Override
    public boolean m_7043_() {
        return false;
    }

    private void drawTile(GuiGraphics context, int x, int y, int size, boolean active, int slot, String glyph) {
        context.m_280509_(x + 2, y + 3, x + size + 3, y + size + 4, SHADOW);
        context.m_280509_(x, y, x + size, y + size, active ? ACTIVE : BORDER);
        context.m_280509_(x + 2, y + 2, x + size - 2, y + size - 2, active ? ACTIVE_INNER : TILE);
        context.m_280509_(x + 4, y + 4, x + size - 4, y + size - 4, active ? ACTIVE_INNER : TILE_INNER);
        context.m_280509_(x + 3, y + 3, x + size - 3, y + 4, active ? 0xFF91C8A2 : 0xFF4A535C);
        if (icons[slot] != null) {
            context.m_280203_(icons[slot], x + (size - 16) / 2, y + (size - 16) / 2);
        } else {
            String shortGlyph = shorten(glyph, 4);
            context.m_280653_(this.f_96547_, ForgeControllerCompat.textLiteral(shortGlyph), x + size / 2, y + size / 2 - 4, active ? 0xFFFFFFFF : 0xFFE0E4E7);
        }
    }

    private static ItemStack iconFor(String keyId) {
        String item = null;
        if ("key.togglePerspective".equals(keyId)) item = "spyglass";
        else if ("key.advancements".equals(keyId)) item = "knowledge_book";
        else if ("key.swapOffhand".equals(keyId)) item = "shield";
        else if ("key.chat".equals(keyId)) item = "writable_book";
        else if ("key.command".equals(keyId)) item = "command_block";
        else if ("key.socialInteractions".equals(keyId)) item = "player_head";
        else if ("key.screenshot".equals(keyId)) item = "painting";
        if (item == null) return null;
        return ForgeRadialApi.icon(item);
    }

    private static String shorten(String value, int length) {
        return value.length() <= length ? value : value.substring(0, length - 3) + "...";
    }
}
