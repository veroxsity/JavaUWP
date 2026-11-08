package banditvault.fabriccontroller;

import banditvault.controllercore.ControllerInput;
import net.minecraft.network.chat.Component;
import net.minecraft.client.gui.Font;
import net.minecraft.client.gui.GuiGraphicsExtractor;

final class BanditControllerGuide {
    private static final int GLYPH_HEIGHT = 12;
    private static final int ITEM_GAP = 8;
    private static final int ROW_HEIGHT = 16;

    private BanditControllerGuide() {
    }

    static void drawBar(GuiGraphicsExtractor context, Font font, int centerX, int y, ControllerInput[] inputs, String[] labels) {
        if (context == null || font == null || inputs == null || labels == null || inputs.length != labels.length) return;
        int width = barWidth(font, inputs, labels);
        if (width == 0) return;

        int x = centerX - width / 2;
        for (int i = 0; i < inputs.length; i++) {
            ControllerInput input = inputs[i];
            if (input == null || input == ControllerInput.UNBOUND) continue;
            int glyphWidth = glyphWidth(font, input);
            drawGlyph(context, font, x, y, glyphWidth, input);
            x += glyphWidth + 4;
            context.text(font, Component.nullToEmpty(labels[i]), x, y + 2, 0xFFFFFFFF);
            x += font.width(labels[i]) + ITEM_GAP;
        }
    }

    static void drawVerticalLeft(GuiGraphicsExtractor context, Font font, int x, int y, ControllerInput[] inputs, String[] labels) {
        if (context == null || font == null || inputs == null || labels == null || inputs.length != labels.length) return;
        for (int i = 0; i < inputs.length; i++) {
            ControllerInput input = inputs[i];
            if (input == null || input == ControllerInput.UNBOUND) continue;
            int width = glyphWidth(font, input);
            drawGlyph(context, font, x, y, width, input);
            context.text(font, Component.nullToEmpty(labels[i]), x + width + 4, y + 2, 0xFFFFFFFF);
            y += ROW_HEIGHT;
        }
    }

    static void drawVerticalRight(GuiGraphicsExtractor context, Font font, int rightX, int y, ControllerInput[] inputs, String[] labels) {
        if (context == null || font == null || inputs == null || labels == null || inputs.length != labels.length) return;
        for (int i = 0; i < inputs.length; i++) {
            ControllerInput input = inputs[i];
            if (input == null || input == ControllerInput.UNBOUND) continue;
            int glyphWidth = glyphWidth(font, input);
            int labelWidth = font.width(labels[i]);
            context.text(font, Component.nullToEmpty(labels[i]), rightX - glyphWidth - 4 - labelWidth, y + 2, 0xFFFFFFFF);
            drawGlyph(context, font, rightX - glyphWidth, y, glyphWidth, input);
            y += ROW_HEIGHT;
        }
    }

    static int barWidth(Font font, ControllerInput[] inputs, String[] labels) {
        if (font == null || inputs == null || labels == null || inputs.length != labels.length) return 0;
        int width = 0;
        int count = 0;
        for (int i = 0; i < inputs.length; i++) {
            if (inputs[i] == null || inputs[i] == ControllerInput.UNBOUND) continue;
            width += glyphWidth(font, inputs[i]) + 4 + font.width(labels[i]);
            count++;
        }
        return width + Math.max(0, count - 1) * ITEM_GAP;
    }

    private static int glyphWidth(Font font, ControllerInput input) {
        return Math.max(GLYPH_HEIGHT, font.width(input.label) + 5);
    }

    private static void drawGlyph(GuiGraphicsExtractor context, Font font, int x, int y, int width, ControllerInput input) {
        int fill = color(input);
        context.fill(x + 2, y, x + width - 2, y + 1, 0xDD080A0C);
        context.fill(x, y + 2, x + width, y + GLYPH_HEIGHT - 2, 0xDD080A0C);
        context.fill(x + 2, y + GLYPH_HEIGHT - 1, x + width - 2, y + GLYPH_HEIGHT, 0xDD080A0C);
        context.fill(x + 2, y + 1, x + width - 2, y + GLYPH_HEIGHT - 1, fill);
        context.fill(x + 1, y + 3, x + width - 1, y + GLYPH_HEIGHT - 3, fill);
        context.centeredText(font, Component.nullToEmpty(input.label), x + width / 2, y + 2, 0xFFFFFFFF);
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
