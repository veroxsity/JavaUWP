package banditvault.cleanroomcontroller;

import banditvault.controllercore.ControllerInput;

import net.minecraft.client.gui.FontRenderer;
import net.minecraft.client.gui.Gui;

final class CleanroomControllerGuide {
    private static final int GLYPH_HEIGHT = 12;
    private static final int ITEM_GAP = 8;
    private static final int ROW_HEIGHT = 16;

    private CleanroomControllerGuide() {
    }

    static void drawBar(FontRenderer font, int centerX, int y, ControllerInput[] inputs, String[] labels) {
        if (font == null || inputs == null || labels == null || inputs.length != labels.length) return;
        int width = barWidth(font, inputs, labels);
        if (width == 0) return;

        int x = centerX - width / 2;
        for (int i = 0; i < inputs.length; i++) {
            ControllerInput input = inputs[i];
            if (input == null || input == ControllerInput.UNBOUND) continue;
            int glyphWidth = glyphWidth(font, input);
            drawGlyph(font, x, y, glyphWidth, input);
            x += glyphWidth + 4;
            text(font, labels[i], x, y + 2, 0xFFFFFFFF);
            x += font.func_78256_a(labels[i]) + ITEM_GAP; // getStringWidth
        }
    }

    static void drawVerticalLeft(FontRenderer font, int x, int y, ControllerInput[] inputs, String[] labels) {
        if (font == null || inputs == null || labels == null || inputs.length != labels.length) return;
        for (int i = 0; i < inputs.length; i++) {
            ControllerInput input = inputs[i];
            if (input == null || input == ControllerInput.UNBOUND) continue;
            int width = glyphWidth(font, input);
            drawGlyph(font, x, y, width, input);
            text(font, labels[i], x + width + 4, y + 2, 0xFFFFFFFF);
            y += ROW_HEIGHT;
        }
    }

    static void drawVerticalRight(FontRenderer font, int rightX, int y, ControllerInput[] inputs, String[] labels) {
        if (font == null || inputs == null || labels == null || inputs.length != labels.length) return;
        for (int i = 0; i < inputs.length; i++) {
            ControllerInput input = inputs[i];
            if (input == null || input == ControllerInput.UNBOUND) continue;
            int glyphWidth = glyphWidth(font, input);
            int labelWidth = font.func_78256_a(labels[i]); // getStringWidth
            text(font, labels[i], rightX - glyphWidth - 4 - labelWidth, y + 2, 0xFFFFFFFF);
            drawGlyph(font, rightX - glyphWidth, y, glyphWidth, input);
            y += ROW_HEIGHT;
        }
    }

    static int barWidth(FontRenderer font, ControllerInput[] inputs, String[] labels) {
        if (font == null || inputs == null || labels == null || inputs.length != labels.length) return 0;
        int width = 0;
        int count = 0;
        for (int i = 0; i < inputs.length; i++) {
            if (inputs[i] == null || inputs[i] == ControllerInput.UNBOUND) continue;
            width += glyphWidth(font, inputs[i]) + 4 + font.func_78256_a(labels[i]); // getStringWidth
            count++;
        }
        return width + Math.max(0, count - 1) * ITEM_GAP;
    }

    static void text(FontRenderer font, String value, int x, int y, int color) {
        font.func_175063_a(value, x, y, color); // drawStringWithShadow
    }

    static void centered(FontRenderer font, String value, int centerX, int y, int color) {
        text(font, value, centerX - font.func_78256_a(value) / 2, y, color); // getStringWidth
    }

    private static int glyphWidth(FontRenderer font, ControllerInput input) {
        return Math.max(GLYPH_HEIGHT, font.func_78256_a(input.label) + 5); // getStringWidth
    }

    private static void drawGlyph(FontRenderer font, int x, int y, int width, ControllerInput input) {
        int fill = color(input);
        Gui.func_73734_a(x + 2, y, x + width - 2, y + 1, 0xDD080A0C); // drawRect
        Gui.func_73734_a(x, y + 2, x + width, y + GLYPH_HEIGHT - 2, 0xDD080A0C);
        Gui.func_73734_a(x + 2, y + GLYPH_HEIGHT - 1, x + width - 2, y + GLYPH_HEIGHT, 0xDD080A0C);
        Gui.func_73734_a(x + 2, y + 1, x + width - 2, y + GLYPH_HEIGHT - 1, fill);
        Gui.func_73734_a(x + 1, y + 3, x + width - 1, y + GLYPH_HEIGHT - 3, fill);
        centered(font, input.label, x + width / 2, y + 2, 0xFFFFFFFF);
    }

    private static int color(ControllerInput input) {
        switch (input) {
            case A: return 0xDD287F45;
            case B: return 0xDDA83A3A;
            case X: return 0xDD2B70A6;
            case Y: return 0xDDB48722;
            default: return 0xDD34383D;
        }
    }
}
