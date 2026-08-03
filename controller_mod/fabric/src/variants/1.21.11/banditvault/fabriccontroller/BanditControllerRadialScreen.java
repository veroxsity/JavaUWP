package banditvault.fabriccontroller;

import banditvault.controllercore.ControllerAction;
import net.minecraft.class_1792;
import net.minecraft.class_1799;
import net.minecraft.class_2561;
import net.minecraft.class_2960;
import net.minecraft.class_332;
import net.minecraft.class_437;
import net.minecraft.class_7923;

public final class BanditControllerRadialScreen extends class_437 {
    // one page is intentional; add pages only when eight slots prove insufficient.
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
    private final class_1799[] icons = new class_1799[8];

    public BanditControllerRadialScreen() {
        super(class_2561.method_30163("Bandit Radial"));
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
    public void method_25394(class_332 context, int mouseX, int mouseY, float delta) {
        context.method_25294(0, 0, this.field_22789, this.field_22790, BACKDROP);
        int centerX = this.field_22789 / 2;
        int centerY = this.field_22790 / 2 - 12;
        int availableRadius = Math.min((this.field_22789 - TILE_SIZE) / 2 - 12, (this.field_22790 - TILE_SIZE) / 2 - 38);
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
        int captionW = Math.min(this.field_22789 - 24, Math.max(this.field_22793.method_1727(selected), this.field_22793.method_1727(hint)) + 16);
        context.method_25294(centerX - captionW / 2, captionY - 5, centerX + captionW / 2, captionY + 22, 0xB812171C);
        context.method_27534(this.field_22793, class_2561.method_30163(shorten(selected, 36)), centerX, captionY, 0xFFFFFFFF);
        context.method_27534(this.field_22793, class_2561.method_30163(hint), centerX, captionY + 12, MUTED);
    }

    @Override
    public boolean method_25421() {
        return false;
    }

    private void drawTile(class_332 context, int x, int y, int size, boolean active, int slot, String glyph) {
        context.method_25294(x + 2, y + 3, x + size + 3, y + size + 4, SHADOW);
        context.method_25294(x, y, x + size, y + size, active ? ACTIVE : BORDER);
        context.method_25294(x + 2, y + 2, x + size - 2, y + size - 2, active ? ACTIVE_INNER : TILE);
        context.method_25294(x + 4, y + 4, x + size - 4, y + size - 4, active ? ACTIVE_INNER : TILE_INNER);
        context.method_25294(x + 3, y + 3, x + size - 3, y + 4, active ? 0xFF91C8A2 : 0xFF4A535C);
        if (icons[slot] != null) {
            context.method_51445(icons[slot], x + (size - 16) / 2, y + (size - 16) / 2);
        } else {
            String shortGlyph = shorten(glyph, 4);
            context.method_27534(this.field_22793, class_2561.method_30163(shortGlyph), x + size / 2, y + size / 2 - 4, active ? 0xFFFFFFFF : 0xFFE0E4E7);
        }
    }

    private static class_1799 iconFor(String keyId) {
        // key mappings expose no icon metadata; defaults use native items, custom actions use their bound key.
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
        class_1792 resolved = class_7923.field_41178.method_63535(class_2960.method_60654("minecraft:" + item));
        return resolved == null ? null : new class_1799(resolved);
    }

    private static String shorten(String value, int length) {
        return value.length() <= length ? value : value.substring(0, length - 3) + "...";
    }
}
