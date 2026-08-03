package banditvault.fabriccontroller;

import banditvault.controllercore.ControllerInput;
import net.minecraft.class_2561;
import net.minecraft.class_327;
import net.minecraft.class_332;

final class BanditControllerGuide {
    private static final int GLYPH_HEIGHT = 12;
    private static final int ITEM_GAP = 8;
    private static final int ROW_HEIGHT = 16;

    private BanditControllerGuide() {
    }

    static void drawBar(class_332 context, class_327 font, int centerX, int y, ControllerInput[] inputs, String[] labels) {
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
            context.method_27535(font, class_2561.method_30163(labels[i]), x, y + 2, 0xFFFFFFFF);
            x += font.method_1727(labels[i]) + ITEM_GAP;
        }
    }

    static void drawVerticalLeft(class_332 context, class_327 font, int x, int y, ControllerInput[] inputs, String[] labels) {
        if (context == null || font == null || inputs == null || labels == null || inputs.length != labels.length) return;
        for (int i = 0; i < inputs.length; i++) {
            ControllerInput input = inputs[i];
            if (input == null || input == ControllerInput.UNBOUND) continue;
            int width = glyphWidth(font, input);
            drawGlyph(context, font, x, y, width, input);
            context.method_27535(font, class_2561.method_30163(labels[i]), x + width + 4, y + 2, 0xFFFFFFFF);
            y += ROW_HEIGHT;
        }
    }

    static void drawVerticalRight(class_332 context, class_327 font, int rightX, int y, ControllerInput[] inputs, String[] labels) {
        if (context == null || font == null || inputs == null || labels == null || inputs.length != labels.length) return;
        for (int i = 0; i < inputs.length; i++) {
            ControllerInput input = inputs[i];
            if (input == null || input == ControllerInput.UNBOUND) continue;
            int glyphWidth = glyphWidth(font, input);
            int labelWidth = font.method_1727(labels[i]);
            context.method_27535(font, class_2561.method_30163(labels[i]), rightX - glyphWidth - 4 - labelWidth, y + 2, 0xFFFFFFFF);
            drawGlyph(context, font, rightX - glyphWidth, y, glyphWidth, input);
            y += ROW_HEIGHT;
        }
    }

    static int barWidth(class_327 font, ControllerInput[] inputs, String[] labels) {
        if (font == null || inputs == null || labels == null || inputs.length != labels.length) return 0;
        int width = 0;
        int count = 0;
        for (int i = 0; i < inputs.length; i++) {
            if (inputs[i] == null || inputs[i] == ControllerInput.UNBOUND) continue;
            width += glyphWidth(font, inputs[i]) + 4 + font.method_1727(labels[i]);
            count++;
        }
        return width + Math.max(0, count - 1) * ITEM_GAP;
    }

    private static int glyphWidth(class_327 font, ControllerInput input) {
        return Math.max(GLYPH_HEIGHT, font.method_1727(input.label) + 5);
    }

    private static void drawGlyph(class_332 context, class_327 font, int x, int y, int width, ControllerInput input) {
        int fill = color(input);
        context.method_25294(x + 2, y, x + width - 2, y + 1, 0xDD080A0C);
        context.method_25294(x, y + 2, x + width, y + GLYPH_HEIGHT - 2, 0xDD080A0C);
        context.method_25294(x + 2, y + GLYPH_HEIGHT - 1, x + width - 2, y + GLYPH_HEIGHT, 0xDD080A0C);
        context.method_25294(x + 2, y + 1, x + width - 2, y + GLYPH_HEIGHT - 1, fill);
        context.method_25294(x + 1, y + 3, x + width - 1, y + GLYPH_HEIGHT - 3, fill);
        context.method_27534(font, class_2561.method_30163(input.label), x + width / 2, y + 2, 0xFFFFFFFF);
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
