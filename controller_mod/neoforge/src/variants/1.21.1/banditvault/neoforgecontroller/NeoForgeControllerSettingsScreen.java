package banditvault.neoforgecontroller;

import banditvault.controllercore.ControllerAction;
import banditvault.controllercore.ControllerInput;
import banditvault.controllercore.ControllerState;
import java.util.ArrayList;
import java.util.EnumSet;
import java.util.List;
import net.minecraft.client.KeyMapping;
import net.minecraft.network.chat.Component;
import net.minecraft.client.Minecraft;
import net.minecraft.client.gui.GuiGraphics;
import net.minecraft.client.gui.screens.Screen;
import org.lwjgl.glfw.GLFW;

public final class NeoForgeControllerSettingsScreen extends Screen {
    private static final int PICKER_ROW_HEIGHT = 24;
    private static final int PICKER_MAX_ROWS = 7;

    private final Screen parent;
    private final Option[] controlOptions;
    private Tab tab = Tab.BASIC;
    private Focus focus = Focus.LIST;
    private int selected;
    private int page;
    private Option captureOption;
    private boolean captureArmed;
    private String message = "";
    private Option pickerOption;
    private int pickerSelected;
    private int pickerTop;
    private String[] pickerRadialIds;

    public NeoForgeControllerSettingsScreen(Screen parent) {
        super(Component.literal("Bandit Controller"));
        this.parent = parent;
        this.controlOptions = buildControlOptions();
        validatePickerOptions();
    }

    @Override
    public void render(GuiGraphics context, int mouseX, int mouseY, float delta) {
        Layout layout = layout();
        context.fill(0, 0, this.width, this.height, 0x99000000);
        drawTabs(context, layout, mouseX, mouseY);
        drawList(context, layout, mouseX, mouseY);
        drawDetails(context, layout);
        drawFooter(context, layout, mouseX, mouseY);
        if (pickerOption != null) {
            NeoForgeVersionApi.beginOverlay(context);
            drawPicker(context, mouseX, mouseY);
            NeoForgeVersionApi.endOverlay(context);
        }
    }

    @Override
    public boolean mouseClicked(double mouseX, double mouseY, int button) {
        if (pickerOption != null) {
            return clickPicker(mouseX, mouseY);
        }
        Layout layout = layout();
        double mx = mouseX;
        double my = mouseY;
        for (Tab value : Tab.values()) {
            if (inside(mx, my, tabX(layout, value), layout.tabY, layout.tabW, layout.rowH)) {
                switchTab(value);
                return true;
            }
        }
        int row = rowAt(layout, mx, my);
        if (row >= 0) {
            int index = page * rowsPerPage(layout) + row;
            if (index < options().length) {
                focus = Focus.LIST;
                selected = index;
                activate(options()[index]);
                return true;
            }
        }
        if (inside(mx, my, layout.resetX, layout.bottomY - layout.rowH - layout.gap, layout.actionW, layout.rowH)) {
            focus = Focus.RESET;
            resetVisible();
            return true;
        }
        if (inside(mx, my, layout.resetX, layout.bottomY, layout.actionW, layout.rowH)) {
            focus = Focus.DONE;
            close();
            return true;
        }
        return true;
    }

    @Override
    public boolean keyPressed(int keyCode, int scanCode, int modifiers) {
        int key = keyCode;
        if (pickerOption != null) {
            if (key == GLFW.GLFW_KEY_ESCAPE) {
                closePicker();
            } else if (key == GLFW.GLFW_KEY_UP) {
                movePicker(-1);
            } else if (key == GLFW.GLFW_KEY_DOWN) {
                movePicker(1);
            } else if (key == GLFW.GLFW_KEY_PAGE_UP) {
                movePicker(-pickerRows());
            } else if (key == GLFW.GLFW_KEY_PAGE_DOWN) {
                movePicker(pickerRows());
            } else if (key == GLFW.GLFW_KEY_ENTER || key == GLFW.GLFW_KEY_SPACE) {
                choosePicker();
            }
            return true;
        }
        if (key == GLFW.GLFW_KEY_ESCAPE) {
            close();
            return true;
        }
        if ((key == GLFW.GLFW_KEY_DELETE || key == GLFW.GLFW_KEY_BACKSPACE) && clearSelected()) {
            return true;
        }
        if (key == GLFW.GLFW_KEY_UP) {
            moveVertical(-1);
            return true;
        }
        if (key == GLFW.GLFW_KEY_DOWN) {
            moveVertical(1);
            return true;
        }
        if (key == GLFW.GLFW_KEY_LEFT) {
            moveFocus(-1);
            return true;
        }
        if (key == GLFW.GLFW_KEY_RIGHT) {
            moveFocus(1);
            return true;
        }
        if (key == GLFW.GLFW_KEY_ENTER || key == GLFW.GLFW_KEY_SPACE) {
            activateFocus();
            return true;
        }
        return super.keyPressed(keyCode, scanCode, modifiers);
    }

    @Override
    public boolean mouseScrolled(double mouseX, double mouseY, double horizontal, double vertical) {
        if (pickerOption == null || vertical == 0.0) return false;
        movePicker(vertical > 0.0 ? -1 : 1);
        return true;
    }

    @Override
    public void onClose() {
        close();
    }

    public void close() {
        NeoForgeControllerSettings.save();
        NeoForgeClientApi.setScreen(Minecraft.getInstance(), parent);
    }


    public boolean handleControllerInput(ControllerState state, float triggerDeadzone) {
        if (captureOption != null) {
            handleControllerCapture(state, triggerDeadzone);
            return true;
        }
        NeoForgeControllerSettings settings = NeoForgeControllerSettings.get();
        if (pickerOption != null) {
            if (ControllerInput.DPAD_UP.pressed(state, triggerDeadzone)) {
                movePicker(-1);
            } else if (ControllerInput.DPAD_DOWN.pressed(state, triggerDeadzone)) {
                movePicker(1);
            } else if (ControllerInput.LEFT_BUMPER.pressed(state, triggerDeadzone)) {
                movePicker(-pickerRows());
            } else if (ControllerInput.RIGHT_BUMPER.pressed(state, triggerDeadzone)) {
                movePicker(pickerRows());
            } else if (settingsInput(settings, ControllerAction.MENU_ACCEPT, ControllerInput.A).pressed(state, triggerDeadzone)) {
                choosePicker();
            } else if (settingsInput(settings, ControllerAction.MENU_CANCEL, ControllerInput.B).pressed(state, triggerDeadzone)) {
                closePicker();
            }
            return true;
        }
        if (ControllerInput.DPAD_UP.pressed(state, triggerDeadzone)) {
            moveVertical(-1);
            return true;
        }
        if (ControllerInput.DPAD_DOWN.pressed(state, triggerDeadzone)) {
            moveVertical(1);
            return true;
        }
        if (ControllerInput.DPAD_LEFT.pressed(state, triggerDeadzone)) {
            moveFocus(-1);
            return true;
        }
        if (ControllerInput.DPAD_RIGHT.pressed(state, triggerDeadzone)) {
            moveFocus(1);
            return true;
        }
        if (ControllerInput.LEFT_BUMPER.pressed(state, triggerDeadzone)) {
            switchTab(previousTab());
            return true;
        }
        if (ControllerInput.RIGHT_BUMPER.pressed(state, triggerDeadzone)) {
            switchTab(nextTab());
            return true;
        }
        if (settingsInput(settings, ControllerAction.MENU_ACCEPT, ControllerInput.A).pressed(state, triggerDeadzone)) {
            activateFocus();
            return true;
        }
        if (settingsInput(settings, ControllerAction.QUICK_MOVE, ControllerInput.Y).pressed(state, triggerDeadzone) && clearSelected()) {
            return true;
        }
        if (ControllerInput.X.pressed(state, triggerDeadzone)) {
            resetVisible();
            return true;
        }
        if (settingsInput(settings, ControllerAction.MENU_CANCEL, ControllerInput.B).pressed(state, triggerDeadzone) || ControllerInput.START.pressed(state, triggerDeadzone)) {
            close();
            return true;
        }
        return true;
    }

    public void handleControllerCapture(ControllerState state, float triggerDeadzone) {
        if (captureOption == null) {
            return;
        }
        if (!captureArmed) {
            captureArmed = !anyHeld(state, triggerDeadzone);
            return;
        }
        ControllerInput input = pressedInput(state, triggerDeadzone);
        if (input == null) {
            return;
        }

        NeoForgeControllerSettings settings = NeoForgeControllerSettings.get();
        Option option = captureOption;
        boolean conflict = option.action != null
            ? settings.rebindController(option.action, input)
            : settings.rebindJava(option.keyId, input);
        message = option.label + " = " + input.label + (conflict ? "; conflicts unbound" : "");
        captureOption = null;
        captureArmed = false;
        NeoForgeControllerSettings.save();
    }

    private void drawTabs(GuiGraphics context, Layout layout, int mouseX, int mouseY) {
        for (Tab value : Tab.values()) {
            int x = tabX(layout, value);
            boolean active = value == tab;
            boolean hover = inside(mouseX, mouseY, x, layout.tabY, layout.tabW, layout.rowH);
            drawPanel(context, x, layout.tabY, layout.tabW, layout.rowH, active, hover);
            context.drawCenteredString(this.font, Component.literal((active ? "> " : "") + value.title), x + layout.tabW / 2, layout.tabY + 6, active ? 0xFFFFFFFF : 0xFFD8D8D8);
        }
    }

    private void drawList(GuiGraphics context, Layout layout, int mouseX, int mouseY) {
        context.drawCenteredString(this.font, Component.literal(tab.title), layout.listX + layout.listW / 2, layout.titleY, 0xFFFFFFFF);
        Option[] options = options();
        int rows = rowsPerPage(layout);
        int start = page * rows;
        for (int i = 0; i < rows && start + i < options.length; i++) {
            int index = start + i;
            int y = layout.listY + i * (layout.rowH + layout.gap);
            boolean active = index == selected && focus == Focus.LIST;
            boolean hover = rowAt(layout, mouseX, mouseY) == i;
            drawPanel(context, layout.listX, y, layout.listW, layout.rowH, active, hover);
            String value = currentValue(options[index], NeoForgeControllerSettings.get());
            int labelWidth = Math.max(20, layout.listW - this.font.width(value) - 34);
            drawLeft(context, fitPickerLabel(rowLabel(options[index], active), labelWidth), layout.listX + 10, y + 6, rowColor(options[index], active));
            drawRight(context, value, layout.listX + layout.listW - 12, y + 6, active ? 0xFFFFFFFF : 0xFFDDDDDD);
        }
        drawScroll(context, layout);
    }

    private void drawDetails(GuiGraphics context, Layout layout) {
        context.fill(layout.detailX - 8, layout.titleY - 8, layout.detailX + layout.detailW + 8, layout.bottomY + layout.rowH + 3, 0x55000000);
        context.fill(layout.detailX - layout.paneGap / 2, layout.titleY - 8, layout.detailX - layout.paneGap / 2 + 2, layout.bottomY + layout.rowH + 3, 0xFF777777);
        Option option = options()[selected];
        NeoForgeControllerSettings settings = NeoForgeControllerSettings.get();
        int x = layout.detailX + 8;
        int y = layout.titleY;
        drawLeft(context, fitPickerLabel(option.label, layout.detailW - 16), x, y, 0xFFFFFFFF);
        y += 24;
        drawWrapped(context, detailText(option, settings), x, y, layout.detailW - 16, 0xFFDDDDDD);

        y = layout.bottomY - layout.rowH * 4;
        if (captureOption != null) {
            drawWrapped(context, "Press an Xbox control for " + captureOption.label + ".", x, y, layout.detailW - 16, 0xFFFFFF55);
        } else if (!message.isEmpty()) {
            drawWrapped(context, message, x, y, layout.detailW - 16, 0xFFFFFF55);
        } else {
            drawLeft(context, "Current: " + currentValue(option, settings), x, y, 0xFFFFFFFF);
            if (option.kind == Kind.BIND) {
                NeoForgeControllerGuide.drawBar(context, this.font, layout.detailX + layout.detailW / 2, y + 16,
                    new ControllerInput[] { settingsInput(settings, ControllerAction.MENU_ACCEPT, ControllerInput.A), settingsInput(settings, ControllerAction.QUICK_MOVE, ControllerInput.Y) },
                    new String[] { "Change", "Unbind" });
            } else if (option.kind == Kind.RADIAL) {
                NeoForgeControllerGuide.drawBar(context, this.font, layout.detailX + layout.detailW / 2, y + 16,
                    new ControllerInput[] { settingsInput(settings, ControllerAction.MENU_ACCEPT, ControllerInput.A), settingsInput(settings, ControllerAction.QUICK_MOVE, ControllerInput.Y) },
                    new String[] { "Choose", "Clear" });
            } else if (option.kind == Kind.STEP) {
                NeoForgeControllerGuide.drawBar(context, this.font, layout.detailX + layout.detailW / 2, y + 16,
                    new ControllerInput[] { settingsInput(settings, ControllerAction.MENU_ACCEPT, ControllerInput.A) }, new String[] { "Choose" });
            } else {
                NeoForgeControllerGuide.drawBar(context, this.font, layout.detailX + layout.detailW / 2, y + 16,
                    new ControllerInput[] { settingsInput(settings, ControllerAction.MENU_ACCEPT, ControllerInput.A) }, new String[] { "Toggle" });
            }
        }
    }

    private void drawFooter(GuiGraphics context, Layout layout, int mouseX, int mouseY) {
        drawPanel(context, layout.resetX, layout.bottomY - layout.rowH - layout.gap, layout.actionW, layout.rowH, focus == Focus.RESET, inside(mouseX, mouseY, layout.resetX, layout.bottomY - layout.rowH - layout.gap, layout.actionW, layout.rowH));
        drawPanel(context, layout.resetX, layout.bottomY, layout.actionW, layout.rowH, focus == Focus.DONE, inside(mouseX, mouseY, layout.resetX, layout.bottomY, layout.actionW, layout.rowH));
        context.drawCenteredString(this.font, Component.literal("Reset"), layout.resetX + layout.actionW / 2, layout.bottomY - layout.rowH - layout.gap + 6, 0xFFFFFFFF);
        context.drawCenteredString(this.font, Component.literal("Done"), layout.resetX + layout.actionW / 2, layout.bottomY + 6, 0xFFFFFFFF);
    }

    private void drawPanel(GuiGraphics context, int x, int y, int w, int h, boolean active, boolean hover) {
        int fill = active ? 0xBB6F7F96 : hover ? 0x99616C7E : 0x884B535F;
        int border = active ? 0xFFFFFFFF : 0xFF242A31;
        context.fill(x, y, x + w, y + h, fill);
        context.fill(x, y, x + w, y + 1, border);
        context.fill(x, y + h - 1, x + w, y + h, 0xFF111111);
        context.fill(x, y, x + 1, y + h, border);
        context.fill(x + w - 1, y, x + w, y + h, 0xFF111111);
    }

    private void drawScroll(GuiGraphics context, Layout layout) {
        int rows = rowsPerPage(layout);
        int maxPage = maxPage(layout);
        int trackX = layout.listX + layout.listW + 5;
        int trackTop = layout.listY;
        int trackBottom = listBottom(layout) - layout.gap;
        context.fill(trackX, trackTop, trackX + 3, trackBottom, 0x77505050);
        int thumbH = maxPage == 0 ? trackBottom - trackTop : Math.max(18, (trackBottom - trackTop) / (maxPage + 1));
        int thumbY = maxPage == 0 ? trackTop : trackTop + page * ((trackBottom - trackTop - thumbH) / maxPage);
        context.fill(trackX - 1, thumbY, trackX + 4, thumbY + thumbH, 0xFFE0E0E0);
    }

    private void drawPicker(GuiGraphics context, int mouseX, int mouseY) {
        PickerLayout layout = pickerLayout();
        context.fill(0, 0, this.width, this.height, 0xAA000000);
        context.fill(layout.x - 2, layout.y - 2, layout.x + layout.width + 2, layout.bottom + 2, 0xFFFFFFFF);
        context.fill(layout.x, layout.y, layout.x + layout.width, layout.bottom, 0xEE20262E);
        context.drawCenteredString(this.font, Component.literal("Choose " + pickerOption.label), this.width / 2, layout.y + 11, 0xFFFFFFFF);

        int count = pickerChoiceCount();
        for (int row = 0; row < layout.rows; row++) {
            int index = pickerTop + row;
            if (index >= count) break;
            int y = layout.listY + row * PICKER_ROW_HEIGHT;
            boolean active = index == pickerSelected;
            boolean hover = inside(mouseX, mouseY, layout.x + 8, y, layout.width - 16, PICKER_ROW_HEIGHT - 2);
            drawPanel(context, layout.x + 8, y, layout.width - 16, PICKER_ROW_HEIGHT - 2, active, hover);
            drawLeft(context, (active ? "> " : "") + fitPickerLabel(pickerChoiceLabel(index), layout.width - 44), layout.x + 16, y + 6, active ? 0xFFFFFFFF : 0xFFE0E0E0);
        }

        if (count > layout.rows) {
            int trackX = layout.x + layout.width - 7;
            int trackHeight = layout.rows * PICKER_ROW_HEIGHT - 2;
            int thumbHeight = Math.max(16, trackHeight * layout.rows / count);
            int thumbY = layout.listY + pickerTop * (trackHeight - thumbHeight) / (count - layout.rows);
            context.fill(trackX, layout.listY, trackX + 3, layout.listY + trackHeight, 0x77505050);
            context.fill(trackX - 1, thumbY, trackX + 4, thumbY + thumbHeight, 0xFFE0E0E0);
        }

        NeoForgeControllerSettings settings = NeoForgeControllerSettings.get();
        NeoForgeControllerGuide.drawBar(context, this.font, this.width / 2, layout.footerY,
            new ControllerInput[] { settingsInput(settings, ControllerAction.MENU_ACCEPT, ControllerInput.A), settingsInput(settings, ControllerAction.MENU_CANCEL, ControllerInput.B), ControllerInput.DPAD_DOWN },
            new String[] { "Select", "Cancel", "Scroll" });
    }

    private boolean clickPicker(double mouseX, double mouseY) {
        PickerLayout layout = pickerLayout();
        if (!inside(mouseX, mouseY, layout.x, layout.y, layout.width, layout.bottom - layout.y)) {
            closePicker();
            return true;
        }
        if (mouseY >= layout.listY && mouseY < layout.listY + layout.rows * PICKER_ROW_HEIGHT) {
            int index = pickerTop + (int)((mouseY - layout.listY) / PICKER_ROW_HEIGHT);
            if (index < pickerChoiceCount()) {
                pickerSelected = index;
                choosePicker();
            }
        }
        return true;
    }

    private String fitPickerLabel(String text, int width) {
        if (this.font.width(text) <= width) return text;
        String suffix = "...";
        return this.font.plainSubstrByWidth(text, Math.max(1, width - this.font.width(suffix))) + suffix;
    }

    private void switchTab(Tab next) {
        tab = next;
        focus = Focus.LIST;
        selected = 0;
        page = 0;
        captureOption = null;
        captureArmed = false;
        message = "";
    }

    private Tab previousTab() {
        Tab[] values = Tab.values();
        return values[(tab.ordinal() + values.length - 1) % values.length];
    }

    private Tab nextTab() {
        Tab[] values = Tab.values();
        return values[(tab.ordinal() + 1) % values.length];
    }

    private void moveSelection(int delta) {
        focus = Focus.LIST;
        selected = clamp(selected + delta, 0, options().length - 1);
        int rows = rowsPerPage(layout());
        if (selected < page * rows) {
            page = selected / rows;
        } else if (selected >= (page + 1) * rows) {
            page = selected / rows;
        }
        message = "";
    }

    private void moveVertical(int delta) {
        if (focus == Focus.LIST) {
            moveSelection(delta);
        } else {
            focus = focus == Focus.RESET ? Focus.DONE : Focus.RESET;
            message = "";
        }
    }

    private void moveFocus(int delta) {
        if (focus == Focus.LIST) {
            focus = delta < 0 ? Focus.DONE : Focus.RESET;
        } else if (focus == Focus.RESET) {
            focus = delta < 0 ? Focus.LIST : Focus.DONE;
        } else {
            focus = delta < 0 ? Focus.RESET : Focus.LIST;
        }
        message = "";
    }

    private void activateFocus() {
        if (focus == Focus.RESET) {
            resetVisible();
        } else if (focus == Focus.DONE) {
            close();
        } else {
            activate(options()[selected]);
        }
    }

    private void activate(Option option) {
        selected = indexOf(option);
        message = "";
        NeoForgeControllerSettings settings = NeoForgeControllerSettings.get();
        switch (option.kind) {
            case TOGGLE:
                option.setter.set(settings, option.value(settings) == 0.0 ? 1.0 : 0.0);
                NeoForgeControllerSettings.save();
                return;
            case STEP:
                openPicker(option);
                return;
            case BIND:
                captureOption = option;
                captureArmed = false;
                return;
            case RADIAL:
                openPicker(option);
                return;
            default:
        }
    }

    private void resetVisible() {
        NeoForgeControllerSettings settings = NeoForgeControllerSettings.get();
        if (tab == Tab.CONTROLS) {
            settings.resetBindings();
        } else if (tab == Tab.RADIAL) {
            settings.resetRadialSlots();
        } else {
            resetSettings(settings);
        }
        message = tab.title + " reset";
        NeoForgeControllerSettings.save();
    }

    private boolean clearSelected() {
        if (focus != Focus.LIST) {
            return false;
        }
        Option option = options()[selected];
        NeoForgeControllerSettings settings = NeoForgeControllerSettings.get();
        if (option.kind == Kind.BIND) {
            if (option.action != null) {
                settings.rebindController(option.action, ControllerInput.UNBOUND);
            } else {
                settings.rebindJava(option.keyId, ControllerInput.UNBOUND);
            }
            message = option.label + " unbound";
        } else if (option.kind == Kind.RADIAL) {
            settings.setRadialSlot(option.radialSlot, "");
            message = option.label + " cleared";
        } else {
            return false;
        }
        NeoForgeControllerSettings.save();
        return true;
    }

    private void openPicker(Option option) {
        pickerOption = option;
        pickerRadialIds = null;
        pickerSelected = 0;
        pickerTop = 0;
        NeoForgeControllerSettings settings = NeoForgeControllerSettings.get();
        message = "";
        if (option.kind == Kind.STEP) {
            pickerSelected = clamp((int)Math.round((option.value(settings) - option.min) / option.step), 0, stepChoiceCount(option) - 1);
        } else {
            pickerRadialIds = NeoForgeControllerCompat.radialKeyIds();
            String current = settings.radialSlot(option.radialSlot);
            int currentIndex = indexOf(pickerRadialIds, current);
            if (currentIndex < 0) {
                String[] withMissing = new String[pickerRadialIds.length + 1];
                System.arraycopy(pickerRadialIds, 0, withMissing, 0, pickerRadialIds.length);
                withMissing[withMissing.length - 1] = current;
                pickerRadialIds = withMissing;
                currentIndex = withMissing.length - 1;
            }
            pickerSelected = currentIndex;
        }
        ensurePickerVisible();
    }

    private void movePicker(int delta) {
        pickerSelected = clamp(pickerSelected + delta, 0, pickerChoiceCount() - 1);
        ensurePickerVisible();
    }

    private void ensurePickerVisible() {
        int rows = pickerRows();
        if (pickerSelected < pickerTop) {
            pickerTop = pickerSelected;
        } else if (pickerSelected >= pickerTop + rows) {
            pickerTop = pickerSelected - rows + 1;
        }
        pickerTop = clamp(pickerTop, 0, Math.max(0, pickerChoiceCount() - rows));
    }

    private void choosePicker() {
        Option option = pickerOption;
        String value = pickerChoiceLabel(pickerSelected);
        NeoForgeControllerSettings settings = NeoForgeControllerSettings.get();
        if (option.kind == Kind.STEP) {
            option.setter.set(settings, stepValue(option, pickerSelected));
        } else {
            settings.setRadialSlot(option.radialSlot, pickerRadialIds[pickerSelected]);
        }
        closePicker();
        message = option.label + " = " + value;
        NeoForgeControllerSettings.save();
    }

    private void closePicker() {
        pickerOption = null;
        pickerRadialIds = null;
        pickerSelected = 0;
        pickerTop = 0;
    }

    private int pickerChoiceCount() {
        return pickerOption.kind == Kind.STEP ? stepChoiceCount(pickerOption) : pickerRadialIds.length;
    }

    private String pickerChoiceLabel(int index) {
        return pickerOption.kind == Kind.STEP
            ? format(stepValue(pickerOption, index), pickerOption.percent)
            : NeoForgeControllerCompat.radialKeyLabel(pickerRadialIds[index]);
    }

    private int pickerRows() {
        int available = Math.max(1, (this.height - 104) / PICKER_ROW_HEIGHT);
        return Math.min(pickerChoiceCount(), Math.min(PICKER_MAX_ROWS, available));
    }

    private PickerLayout pickerLayout() {
        int rows = pickerRows();
        int width = Math.min(this.width - 32, 460);
        int height = 66 + rows * PICKER_ROW_HEIGHT;
        int x = this.width / 2 - width / 2;
        int y = this.height / 2 - height / 2;
        int listY = y + 34;
        int footerY = listY + rows * PICKER_ROW_HEIGHT + 8;
        return new PickerLayout(x, y, width, listY, footerY, y + height, rows);
    }

    private static int stepChoiceCount(Option option) {
        return (int)Math.round((option.max - option.min) / option.step) + 1;
    }

    private static double stepValue(Option option, int index) {
        return Math.min(option.max, option.min + option.step * index);
    }

    private static int indexOf(String[] values, String value) {
        for (int i = 0; i < values.length; i++) {
            if (values[i].equals(value)) return i;
        }
        return -1;
    }

    private static void validatePickerOptions() {
        for (Option[] group : new Option[][] { BASIC, ADVANCED }) {
            for (Option option : group) {
                if (option.kind == Kind.STEP && (option.step <= 0.0 || stepChoiceCount(option) < 3)) {
                    throw new IllegalStateException("Invalid multi-choice controller setting: " + option.label);
                }
            }
        }
    }

    private static ControllerInput settingsInput(NeoForgeControllerSettings settings, ControllerAction action, ControllerInput fallback) {
        ControllerInput input = settings.binding(action);
        return input == ControllerInput.UNBOUND ? fallback : input;
    }

    private void resetSettings(NeoForgeControllerSettings settings) {
        settings.toggleCrouch = false;
        settings.toggleSprint = false;
        settings.invertY = false;
        settings.showGuides = true;
        settings.keyboardMovement = false;
        settings.lookSpeed = 135.0f;
        settings.cursorSpeed = 14.0;
        settings.scrollAmount = 1.0;
        settings.moveDeadzone = 0.35f;
        settings.lookDeadzone = 0.12f;
        settings.cursorDeadzone = 0.12f;
        settings.triggerDeadzone = 0.25f;
    }

    private String rowLabel(Option option, boolean active) {
        return (active ? "> " : "") + option.label;
    }

    private int rowColor(Option option, boolean active) {
        if (option.kind == Kind.BIND && option.action != null) {
            ControllerInput input = NeoForgeControllerSettings.get().binding(option.action);
            if (NeoForgeControllerSettings.get().conflictFor(option.action, input) != null) {
                return 0xFFFF6666;
            }
        }
        return active ? 0xFFFFFFFF : 0xFFE0E0E0;
    }

    private String currentValue(Option option, NeoForgeControllerSettings settings) {
        if (option.kind == Kind.TOGGLE) {
            if (option == SNEAK_MODE || option == SPRINT_MODE) {
                return option.value(settings) == 0.0 ? "Hold" : "Toggle";
            }
            return option.value(settings) == 0.0 ? "Off" : "On";
        }
        if (option.kind == Kind.STEP) {
            return format(option.value(settings), option.percent);
        }
        if (option.kind == Kind.RADIAL) {
            String label = NeoForgeControllerCompat.radialKeyLabel(settings.radialSlot(option.radialSlot));
            return label.length() <= 24 ? label : label.substring(0, 21) + "...";
        }
        ControllerInput input = option.binding(settings);
        ControllerAction conflict = option.action == null ? null : settings.conflictFor(option.action, input);
        return input.label + (conflict == null ? "" : " !");
    }

    private String detailText(Option option, NeoForgeControllerSettings settings) {
        if (option.kind == Kind.BIND) {
            ControllerInput input = option.binding(settings);
            ControllerAction conflict = option.action == null ? null : settings.conflictFor(option.action, input);
            if (conflict != null) {
                return input.label + " is also bound to " + conflict.label + ". Choose a different control.";
            }
            return option.action == null
                ? javaActionDescription(option.keyId)
                : actionDescription(option.action);
        }
        if (option.kind == Kind.RADIAL) {
            return "Runs this registered Minecraft key mapping when the radial direction is selected.";
        }
        if (option.kind == Kind.STEP) {
            return settingDescription(option);
        }
        return settingDescription(option);
    }

    private String settingDescription(Option option) {
        if (option == LOOK_SPEED) {
            return "Adjusts how fast the camera turns while playing.";
        }
        if (option == SNEAK_MODE) {
            return "Hold means sneak only while the button is held. Toggle means press once to sneak.";
        }
        if (option == SPRINT_MODE) {
            return "Hold means sprint only while the button is held. Toggle means press once to sprint.";
        }
        if ("Cursor Speed".equals(option.label)) {
            return "Adjusts how fast the free menu cursor moves.";
        }
        if ("Scroll Amount".equals(option.label)) {
            return "Adjusts how far each controller scroll step moves lists.";
        }
        if ("Show Button Guides".equals(option.label)) {
            return "Shows the on-screen controller button hints in menus and on the HUD.";
        }
        if ("Keyboard Like Movement".equals(option.label)) {
            return "Moves at full speed in set directions like the keyboard, for servers that reject analogue movement.";
        }
        if ("Invert Look Y".equals(option.label)) {
            return "Flips vertical look so pushing up looks down.";
        }
        if ("Move Deadzone".equals(option.label)) {
            return "Sets how far the left stick must move before walking starts.";
        }
        if ("Look Deadzone".equals(option.label)) {
            return "Sets how far the right stick must move before the camera turns.";
        }
        if ("Cursor Deadzone".equals(option.label)) {
            return "Sets how far the stick must move before the menu cursor moves.";
        }
        if ("Trigger Threshold".equals(option.label)) {
            return "Sets how far LT or RT must be pressed before it counts.";
        }
        return "Changes this controller setting.";
    }

    private String actionDescription(ControllerAction action) {
        switch (action) {
            case ATTACK: return "Breaks blocks or attacks.";
            case USE: return "Uses items, places blocks, and interacts.";
            case JUMP: return "Jumps.";
            case SNEAK: return "Activates sneak.";
            case SPRINT: return "Activates sprint.";
            case INVENTORY: return "Opens and closes the inventory.";
            case DROP: return "Drops the selected item.";
            case SWAP_HANDS: return "Swaps the main-hand and off-hand items.";
            case PICK_BLOCK: return "Picks the targeted block or item.";
            case PAUSE: return "Opens the pause menu.";
            case HOTBAR_PREVIOUS: return "Selects the previous hotbar slot.";
            case HOTBAR_NEXT: return "Selects the next hotbar slot.";
            case MENU_ACCEPT: return "Activates the focused menu item.";
            case MENU_CANCEL: return "Goes back or closes the menu.";
            case SNAP_FREE_TOGGLE: return "Switches menu navigation mode.";
            case QUICK_MOVE: return "Quick-moves the focused inventory stack.";
            case MENU_SECONDARY: return "Performs the secondary action on the focused menu item or slot.";
            case RADIAL_MENU: return "Hold to open the radial menu, point with the right stick, and release to activate.";
            default: return "Changes this controller binding.";
        }
    }

    private String javaActionDescription(String keyId) {
        if (keyId == null) return "Activates this Minecraft or mod control during gameplay.";
        switch (keyId) {
            case "key.forward": return "Moves forward.";
            case "key.left": return "Moves left.";
            case "key.back": return "Moves backward.";
            case "key.right": return "Moves right.";
            case "key.chat": return "Opens chat so you can send a message.";
            case "key.playerlist": return "Shows the players currently connected.";
            case "key.command": return "Opens chat with a slash ready for a command.";
            case "key.socialInteractions": return "Opens controls for managing other players' chat messages.";
            case "key.screenshot": return "Saves a screenshot.";
            case "key.togglePerspective": return "Switches between first-person and third-person views.";
            case "key.smoothCamera": return "Turns cinematic camera smoothing on or off.";
            case "key.fullscreen": return "Turns full-screen display on or off.";
            case "key.advancements": return "Opens the Advancements screen.";
            case "key.quickActions": return "Opens the Quick Actions menu.";
            case "key.toggleGui": return "Shows or hides the HUD and your hand.";
            case "key.toggleSpectatorShaderEffects": return "Turns the current spectator shader effect on or off.";
            case "key.hotbar.1": return "Selects hotbar slot 1.";
            case "key.hotbar.2": return "Selects hotbar slot 2.";
            case "key.hotbar.3": return "Selects hotbar slot 3.";
            case "key.hotbar.4": return "Selects hotbar slot 4.";
            case "key.hotbar.5": return "Selects hotbar slot 5.";
            case "key.hotbar.6": return "Selects hotbar slot 6.";
            case "key.hotbar.7": return "Selects hotbar slot 7.";
            case "key.hotbar.8": return "Selects hotbar slot 8.";
            case "key.hotbar.9": return "Selects hotbar slot 9.";
            case "key.saveToolbarActivator": return "In Creative mode, hold this and select a number to save the current hotbar.";
            case "key.loadToolbarActivator": return "In Creative mode, hold this and select a number to load a saved hotbar.";
            case "key.spectatorOutlines": return "Highlights players while you are spectating.";
            case "key.spectatorHotbar": return "Uses the hotbar controls to choose a spectator menu option.";
            case "key.debug.overlay": return "Shows or hides the debug overlay.";
            case "key.debug.modifier": return "Acts as Minecraft's F3 key. Hold it with another debug control to run that action.";
            case "key.debug.crash": return "With Debug Modifier held, hold this to intentionally crash the game.";
            case "key.debug.reloadChunk": return "With Debug Modifier held, reloads all visible chunks.";
            case "key.debug.showHitboxes": return "With Debug Modifier held, shows or hides entity hitboxes.";
            case "key.debug.clearChat": return "With Debug Modifier held, clears visible chat messages.";
            case "key.debug.showChunkBorders": return "With Debug Modifier held, shows or hides chunk boundaries.";
            case "key.debug.showAdvancedTooltips": return "With Debug Modifier held, shows or hides extra item details in tooltips.";
            case "key.debug.copyRecreateCommand": return "With Debug Modifier held, copies data for the targeted block or entity.";
            case "key.debug.spectate": return "With Debug Modifier held, switches between Spectator and your previous game mode.";
            case "key.debug.switchGameMode": return "With Debug Modifier held, opens the game mode switcher.";
            case "key.debug.debugOptions": return "With Debug Modifier held, opens the Debug Options screen.";
            case "key.debug.focusPause": return "With Debug Modifier held, changes whether the game pauses when it loses focus.";
            case "key.debug.dumpDynamicTextures": return "With Debug Modifier held, saves loaded dynamic textures to disk.";
            case "key.debug.reloadResourcePacks": return "With Debug Modifier held, reloads resource packs.";
            case "key.debug.profiling": return "With Debug Modifier held, starts or stops performance profiling.";
            case "key.debug.copyLocation": return "With Debug Modifier held, copies your location as a teleport command.";
            case "key.debug.dumpVersion": return "With Debug Modifier held, shows detailed client version information.";
            case "key.debug.profilingChart": return "With Debug Modifier held, shows or hides the profiling chart.";
            case "key.debug.fpsCharts": return "With Debug Modifier held, shows or hides performance charts.";
            case "key.debug.networkCharts": return "With Debug Modifier held, shows or hides network traffic charts.";
            default: return "Activates this Minecraft or mod control during gameplay.";
        }
    }

    private static Option[] buildControlOptions() {
        List<Option> options = new ArrayList<Option>();
        EnumSet<ControllerAction> shown = EnumSet.noneOf(ControllerAction.class);
        Minecraft client = Minecraft.getInstance();
        KeyMapping[] keys = client == null || client.options == null ? null : client.options.keyMappings;
        if (keys != null) {
            for (KeyMapping key : keys) {
                if (key != null) {
                    String keyId = key.getName();
                    String label = Component.translatable(keyId).getString();
                    ControllerAction action = NeoForgeControllerSettings.controllerActionForJavaKey(keyId);
                    options.add(action == null
                        ? new Option(label, keyId)
                        : new Option(label, action));
                    if (action != null) shown.add(action);
                }
            }
        }
        for (Option option : CONTROLLER_CONTROLS) {
            if (!shown.contains(option.action)) options.add(option);
        }
        return options.toArray(new Option[options.size()]);
    }

    private Option[] options() {
        switch (tab) {
            case ADVANCED:
                return ADVANCED;
            case CONTROLS:
                return controlOptions;
            case RADIAL:
                return RADIAL;
            default:
                return BASIC;
        }
    }

    private Layout layout() {
        int rowH = 22;
        int gap = 6;
        int width = Math.min(this.width - 48, 980);
        int x = this.width / 2 - width / 2;
        int tabGap = 18;
        int paneGap = 24;
        int detailW = Math.max(170, Math.min(300, width * 32 / 100));
        int listW = width - paneGap - detailW;
        int tabW = (width - tabGap * (Tab.values().length - 1)) / Tab.values().length;
        int tabY = Math.max(24, this.height / 10);
        int titleY = tabY + rowH + 22;
        int listY = titleY + 18;
        int bottomY = Math.max(listY + rowH + gap, this.height - 26);
        int detailX = x + listW + paneGap;
        return new Layout(x, width, tabY, tabW, tabGap, x, listW, detailX, detailW, paneGap, titleY, listY, bottomY, rowH, gap, detailX, detailW);
    }

    private int tabX(Layout layout, Tab value) {
        return layout.x + value.ordinal() * (layout.tabW + layout.tabGap);
    }

    private int rowsPerPage(Layout layout) {
        return Math.max(1, (listBottom(layout) - layout.listY - layout.gap) / (layout.rowH + layout.gap));
    }

    private int maxPage(Layout layout) {
        return Math.max(0, (options().length - 1) / rowsPerPage(layout));
    }

    private int rowAt(Layout layout, double mx, double my) {
        if (mx < layout.listX || mx > layout.listX + layout.listW || my < layout.listY || my > listBottom(layout) - layout.gap) {
            return -1;
        }
        int row = (int)((my - layout.listY) / (layout.rowH + layout.gap));
        int y = layout.listY + row * (layout.rowH + layout.gap);
        if (row >= rowsPerPage(layout) || my > y + layout.rowH) {
            return -1;
        }
        return row;
    }

    private int listBottom(Layout layout) {
        return layout.bottomY + layout.rowH;
    }

    private boolean inside(double mx, double my, int x, int y, int w, int h) {
        return mx >= x && mx <= x + w && my >= y && my <= y + h;
    }

    private int clamp(int value, int min, int max) {
        if (value < min) return min;
        if (value > max) return max;
        return value;
    }

    private int indexOf(Option option) {
        Option[] options = options();
        for (int i = 0; i < options.length; i++) {
            if (options[i] == option) {
                return i;
            }
        }
        return selected;
    }

    private boolean anyHeld(ControllerState state, float triggerDeadzone) {
        for (ControllerInput input : ControllerInput.values()) {
            if (input.held(state, triggerDeadzone)) {
                return true;
            }
        }
        return false;
    }

    private ControllerInput pressedInput(ControllerState state, float triggerDeadzone) {
        for (ControllerInput input : ControllerInput.values()) {
            if (input.pressed(state, triggerDeadzone)) {
                return input;
            }
        }
        return null;
    }

    private void drawLeft(GuiGraphics context, String text, int x, int y, int color) {
        context.drawString(this.font, Component.literal(text), x, y, color);
    }

    private void drawRight(GuiGraphics context, String text, int rightX, int y, int color) {
        context.drawString(this.font, Component.literal(text), rightX - this.font.width(text), y, color);
    }

    private void drawWrapped(GuiGraphics context, String text, int x, int y, int width, int color) {
        int lineWidth = Math.max(18, width / 6);
        String[] words = text.split(" ");
        String line = "";
        for (int i = 0; i < words.length; i++) {
            String next = line.isEmpty() ? words[i] : line + " " + words[i];
            if (next.length() > lineWidth) {
                drawLeft(context, line, x, y, color);
                y += 12;
                line = words[i];
            } else {
                line = next;
            }
        }
        if (!line.isEmpty()) {
            drawLeft(context, line, x, y, color);
        }
    }

    private String format(double value, boolean percent) {
        if (percent) {
            return Long.toString(Math.round(value * 100.0)) + "%";
        }
        if (Math.abs(value - Math.round(value)) < 0.001) {
            return Long.toString(Math.round(value));
        }
        return String.format(java.util.Locale.ROOT, "%.2f", value);
    }

    private enum Tab {
        BASIC("Basic"),
        ADVANCED("Advanced"),
        CONTROLS("Controls"),
        RADIAL("Radial");

        final String title;

        Tab(String title) {
            this.title = title;
        }
    }

    private enum Kind {
        TOGGLE,
        STEP,
        BIND,
        RADIAL
    }

    private enum Focus {
        LIST,
        RESET,
        DONE
    }

    private interface Getter {
        double get(NeoForgeControllerSettings settings);
    }

    private interface Setter {
        void set(NeoForgeControllerSettings settings, double value);
    }

    private static final class Layout {
        final int x;
        final int width;
        final int tabY;
        final int tabW;
        final int tabGap;
        final int listX;
        final int listW;
        final int detailX;
        final int detailW;
        final int paneGap;
        final int titleY;
        final int listY;
        final int bottomY;
        final int rowH;
        final int gap;
        final int resetX;
        final int actionW;

        Layout(int x, int width, int tabY, int tabW, int tabGap, int listX, int listW, int detailX, int detailW, int paneGap, int titleY, int listY, int bottomY, int rowH, int gap, int resetX, int actionW) {
            this.x = x;
            this.width = width;
            this.tabY = tabY;
            this.tabW = tabW;
            this.tabGap = tabGap;
            this.listX = listX;
            this.listW = listW;
            this.detailX = detailX;
            this.detailW = detailW;
            this.paneGap = paneGap;
            this.titleY = titleY;
            this.listY = listY;
            this.bottomY = bottomY;
            this.rowH = rowH;
            this.gap = gap;
            this.resetX = resetX;
            this.actionW = actionW;
        }
    }

    private static final class PickerLayout {
        final int x;
        final int y;
        final int width;
        final int listY;
        final int footerY;
        final int bottom;
        final int rows;

        PickerLayout(int x, int y, int width, int listY, int footerY, int bottom, int rows) {
            this.x = x;
            this.y = y;
            this.width = width;
            this.listY = listY;
            this.footerY = footerY;
            this.bottom = bottom;
            this.rows = rows;
        }
    }

    private static final class Option {
        final String label;
        final Kind kind;
        final Getter getter;
        final Setter setter;
        final double min;
        final double max;
        final double step;
        final boolean percent;
        final ControllerAction action;
        final String keyId;
        final int radialSlot;

        Option(String label, Getter getter, Setter setter) {
            this(label, Kind.TOGGLE, getter, setter, 0.0, 1.0, 1.0, false, null, null, -1);
        }

        Option(String label, Getter getter, Setter setter, double min, double max, double step, boolean percent) {
            this(label, Kind.STEP, getter, setter, min, max, step, percent, null, null, -1);
        }

        Option(ControllerAction action) {
            this(action.label, Kind.BIND, null, null, 0.0, 0.0, 0.0, false, action, null, -1);
        }

        Option(String label, String keyId) {
            this(label, Kind.BIND, null, null, 0.0, 0.0, 0.0, false, null, keyId, -1);
        }

        Option(String label, ControllerAction action) {
            this(label, Kind.BIND, null, null, 0.0, 0.0, 0.0, false, action, null, -1);
        }

        Option(String label, int radialSlot) {
            this(label, Kind.RADIAL, null, null, 0.0, 0.0, 0.0, false, null, null, radialSlot);
        }

        Option(String label, Kind kind, Getter getter, Setter setter, double min, double max, double step, boolean percent, ControllerAction action, String keyId, int radialSlot) {
            this.label = label;
            this.kind = kind;
            this.getter = getter;
            this.setter = setter;
            this.min = min;
            this.max = max;
            this.step = step;
            this.percent = percent;
            this.action = action;
            this.keyId = keyId;
            this.radialSlot = radialSlot;
        }

        double value(NeoForgeControllerSettings settings) {
            return getter.get(settings);
        }

        ControllerInput binding(NeoForgeControllerSettings settings) {
            return action == null ? settings.javaBinding(keyId) : settings.binding(action);
        }
    }

    private static final Option LOOK_SPEED = new Option("Look Sensitivity", s -> s.lookSpeed, (s, v) -> s.lookSpeed = (float)v, 30.0, 300.0, 15.0, false);
    private static final Option SNEAK_MODE = new Option("Sneak", s -> s.toggleCrouch ? 1.0 : 0.0, (s, v) -> s.toggleCrouch = v != 0.0);
    private static final Option SPRINT_MODE = new Option("Sprint", s -> s.toggleSprint ? 1.0 : 0.0, (s, v) -> s.toggleSprint = v != 0.0);

    private static final Option[] BASIC = withMovementOption(new Option[] {
        LOOK_SPEED,
        new Option("Cursor Speed", s -> s.cursorSpeed, (s, v) -> s.cursorSpeed = v, 4.0, 40.0, 1.0, false),
        new Option("Scroll Amount", s -> s.scrollAmount, (s, v) -> s.scrollAmount = v, 0.25, 4.0, 0.25, false),
        SNEAK_MODE,
        SPRINT_MODE,
        new Option("Invert Look Y", s -> s.invertY ? 1.0 : 0.0, (s, v) -> s.invertY = v != 0.0),
        new Option("Show Button Guides", s -> s.showGuides ? 1.0 : 0.0, (s, v) -> s.showGuides = v != 0.0)
    });

    // the option only does something on versions that ship the keyboard input mixin
    private static Option[] withMovementOption(Option[] options) {
        if (!NeoForgeMovementApi.analogSupported()) {
            return options;
        }
        Option[] all = java.util.Arrays.copyOf(options, options.length + 1);
        all[options.length] = new Option("Keyboard Like Movement", s -> s.keyboardMovement ? 1.0 : 0.0, (s, v) -> s.keyboardMovement = v != 0.0);
        return all;
    }

    private static final Option[] ADVANCED = new Option[] {
        new Option("Move Deadzone", s -> s.moveDeadzone, (s, v) -> s.moveDeadzone = (float)v, 0.0, 0.75, 0.05, true),
        new Option("Look Deadzone", s -> s.lookDeadzone, (s, v) -> s.lookDeadzone = (float)v, 0.0, 0.75, 0.05, true),
        new Option("Cursor Deadzone", s -> s.cursorDeadzone, (s, v) -> s.cursorDeadzone = (float)v, 0.0, 0.75, 0.05, true),
        new Option("Trigger Threshold", s -> s.triggerDeadzone, (s, v) -> s.triggerDeadzone = (float)v, 0.0, 0.95, 0.05, true)
    };

    private static final Option[] CONTROLLER_CONTROLS = new Option[] {
        new Option(ControllerAction.ATTACK),
        new Option(ControllerAction.USE),
        new Option(ControllerAction.JUMP),
        new Option(ControllerAction.SNEAK),
        new Option(ControllerAction.SPRINT),
        new Option(ControllerAction.INVENTORY),
        new Option(ControllerAction.DROP),
        new Option(ControllerAction.SWAP_HANDS),
        new Option(ControllerAction.PICK_BLOCK),
        new Option(ControllerAction.PAUSE),
        new Option(ControllerAction.HOTBAR_PREVIOUS),
        new Option(ControllerAction.HOTBAR_NEXT),
        new Option(ControllerAction.MENU_ACCEPT),
        new Option(ControllerAction.MENU_CANCEL),
        new Option(ControllerAction.SNAP_FREE_TOGGLE),
        new Option(ControllerAction.QUICK_MOVE),
        new Option(ControllerAction.MENU_SECONDARY),
        new Option(ControllerAction.RADIAL_MENU)
    };

    private static final Option[] RADIAL = new Option[] {
        new Option("Up", 0),
        new Option("Up Right", 1),
        new Option("Right", 2),
        new Option("Down Right", 3),
        new Option("Down", 4),
        new Option("Down Left", 5),
        new Option("Left", 6),
        new Option("Up Left", 7)
    };
}
