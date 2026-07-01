package banditvault.fabriccontroller;

import banditvault.controllercore.ControllerAction;
import banditvault.controllercore.ControllerInput;
import banditvault.controllercore.ControllerState;
import net.minecraft.class_11908;
import net.minecraft.class_11909;
import net.minecraft.class_2561;
import net.minecraft.class_310;
import net.minecraft.class_332;
import net.minecraft.class_437;
import org.lwjgl.glfw.GLFW;

public final class BanditControllerSettingsScreen extends class_437 {
    private final class_437 parent;
    private Tab tab = Tab.BASIC;
    private Focus focus = Focus.LIST;
    private int selected;
    private int page;
    private ControllerAction captureAction;
    private boolean captureArmed;
    private String message = "";

    public BanditControllerSettingsScreen(class_437 parent) {
        super(class_2561.method_30163("Bandit Controller"));
        this.parent = parent;
    }

    @Override
    public void method_25394(class_332 context, int mouseX, int mouseY, float delta) {
        FabricScreenApi.renderBackground(this, context, mouseX, mouseY, delta);
        Layout layout = layout();
        context.method_25294(0, 0, this.field_22789, this.field_22790, 0x99000000);
        drawTabs(context, layout, mouseX, mouseY);
        drawList(context, layout, mouseX, mouseY);
        drawDetails(context, layout);
        drawFooter(context, layout, mouseX, mouseY);
    }

    @Override
    public boolean method_25402(class_11909 event, boolean doubleClick) {
        Layout layout = layout();
        double mx = event.comp_4798();
        double my = event.comp_4799();
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
    public boolean method_25404(class_11908 event) {
        int key = event.comp_4795();
        if (key == GLFW.GLFW_KEY_ESCAPE) {
            close();
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
        return super.method_25404(event);
    }

    @Override
    public void method_25419() {
        close();
    }

    public void close() {
        BanditControllerSettings.save();
        class_310.method_1551().method_1507(parent);
    }

    public boolean isCapturingBinding() {
        return captureAction != null;
    }

    public boolean handleControllerInput(ControllerState state, float triggerDeadzone) {
        if (captureAction != null) {
            handleControllerCapture(state, triggerDeadzone);
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
        if (ControllerInput.A.pressed(state, triggerDeadzone)) {
            activateFocus();
            return true;
        }
        if (ControllerInput.X.pressed(state, triggerDeadzone)) {
            resetVisible();
            return true;
        }
        if (ControllerInput.B.pressed(state, triggerDeadzone) || ControllerInput.START.pressed(state, triggerDeadzone)) {
            close();
            return true;
        }
        return true;
    }

    public void handleControllerCapture(ControllerState state, float triggerDeadzone) {
        if (captureAction == null) {
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

        BanditControllerSettings settings = BanditControllerSettings.get();
        ControllerAction conflict = settings.conflictFor(captureAction, input);
        settings.setBinding(captureAction, input);
        message = conflict == null
            ? captureAction.label + " = " + input.label
            : "Conflict: " + captureAction.label + " and " + conflict.label + " use " + input.label;
        captureAction = null;
        captureArmed = false;
        BanditControllerSettings.save();
    }

    private void drawTabs(class_332 context, Layout layout, int mouseX, int mouseY) {
        for (Tab value : Tab.values()) {
            int x = tabX(layout, value);
            boolean active = value == tab;
            boolean hover = inside(mouseX, mouseY, x, layout.tabY, layout.tabW, layout.rowH);
            drawPanel(context, x, layout.tabY, layout.tabW, layout.rowH, active, hover);
            context.method_27534(this.field_22793, class_2561.method_30163((active ? "> " : "") + value.title), x + layout.tabW / 2, layout.tabY + 6, active ? 0xFFFFFFFF : 0xFFD8D8D8);
        }
    }

    private void drawList(class_332 context, Layout layout, int mouseX, int mouseY) {
        context.method_27534(this.field_22793, class_2561.method_30163(tab.title), layout.listX + layout.listW / 2, layout.titleY, 0xFFFFFFFF);
        Option[] options = options();
        int rows = rowsPerPage(layout);
        int start = page * rows;
        for (int i = 0; i < rows && start + i < options.length; i++) {
            int index = start + i;
            int y = layout.listY + i * (layout.rowH + layout.gap);
            boolean active = index == selected && focus == Focus.LIST;
            boolean hover = rowAt(layout, mouseX, mouseY) == i;
            drawPanel(context, layout.listX, y, layout.listW, layout.rowH, active, hover);
            drawLeft(context, rowLabel(options[index], active), layout.listX + 10, y + 6, rowColor(options[index], active));
            drawRight(context, currentValue(options[index], BanditControllerSettings.get()), layout.listX + layout.listW - 12, y + 6, active ? 0xFFFFFFFF : 0xFFDDDDDD);
        }
        drawScroll(context, layout);
    }

    private void drawDetails(class_332 context, Layout layout) {
        context.method_25294(layout.detailX - 8, layout.titleY - 8, layout.detailX + layout.detailW + 8, layout.bottomY + layout.rowH + 3, 0x55000000);
        context.method_25294(layout.detailX - layout.paneGap / 2, layout.titleY - 8, layout.detailX - layout.paneGap / 2 + 2, layout.bottomY + layout.rowH + 3, 0xFF777777);
        Option option = options()[selected];
        BanditControllerSettings settings = BanditControllerSettings.get();
        int x = layout.detailX + 8;
        int y = layout.titleY;
        drawLeft(context, option.label, x, y, 0xFFFFFFFF);
        y += 24;
        drawWrapped(context, detailText(option, settings), x, y, layout.detailW - 16, 0xFFDDDDDD);

        y = layout.bottomY - layout.rowH * 4;
        if (captureAction != null) {
            drawWrapped(context, "Press an Xbox control for " + captureAction.label + ".", x, y, layout.detailW - 16, 0xFFFFFF55);
        } else if (!message.isEmpty()) {
            drawWrapped(context, message, x, y, layout.detailW - 16, 0xFFFFFF55);
        } else {
            drawLeft(context, "Current: " + currentValue(option, settings), x, y, 0xFFFFFFFF);
        }
    }

    private void drawFooter(class_332 context, Layout layout, int mouseX, int mouseY) {
        drawPanel(context, layout.resetX, layout.bottomY - layout.rowH - layout.gap, layout.actionW, layout.rowH, focus == Focus.RESET, inside(mouseX, mouseY, layout.resetX, layout.bottomY - layout.rowH - layout.gap, layout.actionW, layout.rowH));
        drawPanel(context, layout.resetX, layout.bottomY, layout.actionW, layout.rowH, focus == Focus.DONE, inside(mouseX, mouseY, layout.resetX, layout.bottomY, layout.actionW, layout.rowH));
        context.method_27534(this.field_22793, class_2561.method_30163("Reset"), layout.resetX + layout.actionW / 2, layout.bottomY - layout.rowH - layout.gap + 6, 0xFFFFFFFF);
        context.method_27534(this.field_22793, class_2561.method_30163("Done"), layout.resetX + layout.actionW / 2, layout.bottomY + 6, 0xFFFFFFFF);
    }

    private void drawPanel(class_332 context, int x, int y, int w, int h, boolean active, boolean hover) {
        int fill = active ? 0xBB6F7F96 : hover ? 0x99616C7E : 0x884B535F;
        int border = active ? 0xFFFFFFFF : 0xFF242A31;
        context.method_25294(x, y, x + w, y + h, fill);
        context.method_25294(x, y, x + w, y + 1, border);
        context.method_25294(x, y + h - 1, x + w, y + h, 0xFF111111);
        context.method_25294(x, y, x + 1, y + h, border);
        context.method_25294(x + w - 1, y, x + w, y + h, 0xFF111111);
    }

    private void drawScroll(class_332 context, Layout layout) {
        int rows = rowsPerPage(layout);
        int maxPage = maxPage(layout);
        int trackX = layout.listX + layout.listW + 5;
        int trackTop = layout.listY;
        int trackBottom = listBottom(layout) - layout.gap;
        context.method_25294(trackX, trackTop, trackX + 3, trackBottom, 0x77505050);
        int thumbH = maxPage == 0 ? trackBottom - trackTop : Math.max(18, (trackBottom - trackTop) / (maxPage + 1));
        int thumbY = maxPage == 0 ? trackTop : trackTop + page * ((trackBottom - trackTop - thumbH) / maxPage);
        context.method_25294(trackX - 1, thumbY, trackX + 4, thumbY + thumbH, 0xFFE0E0E0);
    }

    private void switchTab(Tab next) {
        tab = next;
        focus = Focus.LIST;
        selected = 0;
        page = 0;
        captureAction = null;
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
        BanditControllerSettings settings = BanditControllerSettings.get();
        switch (option.kind) {
            case TOGGLE:
                option.setter.set(settings, option.value(settings) == 0.0 ? 1.0 : 0.0);
                BanditControllerSettings.save();
                return;
            case STEP:
                option.setter.set(settings, wrap(option.value(settings) + option.step, option.min, option.max));
                BanditControllerSettings.save();
                return;
            case BIND:
                captureAction = option.action;
                captureArmed = false;
                return;
            default:
        }
    }

    private void resetVisible() {
        BanditControllerSettings settings = BanditControllerSettings.get();
        if (tab == Tab.CONTROLS) {
            settings.resetBindings();
        } else {
            resetSettings(settings);
        }
        message = tab.title + " reset";
        BanditControllerSettings.save();
    }

    private void resetSettings(BanditControllerSettings settings) {
        settings.toggleCrouch = false;
        settings.toggleSprint = false;
        settings.invertY = false;
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
        if (option.kind == Kind.BIND) {
            ControllerInput input = BanditControllerSettings.get().binding(option.action);
            if (BanditControllerSettings.get().conflictFor(option.action, input) != null) {
                return 0xFFFF6666;
            }
        }
        return active ? 0xFFFFFFFF : 0xFFE0E0E0;
    }

    private String currentValue(Option option, BanditControllerSettings settings) {
        if (option.kind == Kind.TOGGLE) {
            if (option == SNEAK_MODE || option == SPRINT_MODE) {
                return option.value(settings) == 0.0 ? "Hold" : "Toggle";
            }
            return option.value(settings) == 0.0 ? "Off" : "On";
        }
        if (option.kind == Kind.STEP) {
            return format(option.value(settings), option.percent);
        }
        ControllerInput input = settings.binding(option.action);
        ControllerAction conflict = settings.conflictFor(option.action, input);
        return input.label + (conflict == null ? "" : " !");
    }

    private String detailText(Option option, BanditControllerSettings settings) {
        if (option.kind == Kind.BIND) {
            ControllerInput input = settings.binding(option.action);
            ControllerAction conflict = settings.conflictFor(option.action, input);
            if (conflict != null) {
                return input.label + " is also bound to " + conflict.label + ". Choose a different control.";
            }
            return actionDescription(option.action);
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
            case DROP: return "Drops the selected item. In menus, right-clicks.";
            case PICK_BLOCK: return "Picks the targeted block or item.";
            case PAUSE: return "Opens the pause menu.";
            case HOTBAR_PREVIOUS: return "Selects the previous hotbar slot.";
            case HOTBAR_NEXT: return "Selects the next hotbar slot.";
            case MENU_ACCEPT: return "Activates the focused menu item.";
            case MENU_CANCEL: return "Goes back or closes the menu.";
            case SNAP_FREE_TOGGLE: return "Switches menu navigation mode.";
            case QUICK_MOVE: return "Quick-moves the focused inventory stack.";
            default: return "Changes this controller binding.";
        }
    }

    private Option[] options() {
        switch (tab) {
            case ADVANCED:
                return ADVANCED;
            case CONTROLS:
                return CONTROLS;
            default:
                return BASIC;
        }
    }

    private Layout layout() {
        int rowH = 22;
        int gap = 6;
        int width = Math.min(this.field_22789 - 48, 980);
        int x = this.field_22789 / 2 - width / 2;
        int tabGap = 18;
        int paneGap = 24;
        int detailW = Math.max(170, Math.min(300, width * 32 / 100));
        int listW = width - paneGap - detailW;
        int tabW = (width - tabGap * 2) / 3;
        int tabY = Math.max(24, this.field_22790 / 10);
        int titleY = tabY + rowH + 22;
        int listY = titleY + 18;
        int bottomY = Math.max(listY + rowH + gap, this.field_22790 - 26);
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

    private double wrap(double value, double min, double max) {
        if (value > max) {
            return min;
        }
        if (value < min) {
            return max;
        }
        return value;
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

    private void drawLeft(class_332 context, String text, int x, int y, int color) {
        context.method_27535(this.field_22793, class_2561.method_30163(text), x, y, color);
    }

    private void drawRight(class_332 context, String text, int rightX, int y, int color) {
        context.method_27535(this.field_22793, class_2561.method_30163(text), rightX - this.field_22793.method_1727(text), y, color);
    }

    private void drawWrapped(class_332 context, String text, int x, int y, int width, int color) {
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
        CONTROLS("Controls");

        final String title;

        Tab(String title) {
            this.title = title;
        }
    }

    private enum Kind {
        TOGGLE,
        STEP,
        BIND
    }

    private enum Focus {
        LIST,
        RESET,
        DONE
    }

    private interface Getter {
        double get(BanditControllerSettings settings);
    }

    private interface Setter {
        void set(BanditControllerSettings settings, double value);
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

        Option(String label, Getter getter, Setter setter) {
            this(label, Kind.TOGGLE, getter, setter, 0.0, 1.0, 1.0, false, null);
        }

        Option(String label, Getter getter, Setter setter, double min, double max, double step, boolean percent) {
            this(label, Kind.STEP, getter, setter, min, max, step, percent, null);
        }

        Option(ControllerAction action) {
            this(action.label, Kind.BIND, null, null, 0.0, 0.0, 0.0, false, action);
        }

        Option(String label, Kind kind, Getter getter, Setter setter, double min, double max, double step, boolean percent, ControllerAction action) {
            this.label = label;
            this.kind = kind;
            this.getter = getter;
            this.setter = setter;
            this.min = min;
            this.max = max;
            this.step = step;
            this.percent = percent;
            this.action = action;
        }

        double value(BanditControllerSettings settings) {
            return getter.get(settings);
        }
    }

    private static final Option LOOK_SPEED = new Option("Look Sensitivity", s -> s.lookSpeed, (s, v) -> s.lookSpeed = (float)v, 30.0, 300.0, 15.0, false);
    private static final Option SNEAK_MODE = new Option("Sneak", s -> s.toggleCrouch ? 1.0 : 0.0, (s, v) -> s.toggleCrouch = v != 0.0);
    private static final Option SPRINT_MODE = new Option("Sprint", s -> s.toggleSprint ? 1.0 : 0.0, (s, v) -> s.toggleSprint = v != 0.0);

    private static final Option[] BASIC = new Option[] {
        LOOK_SPEED,
        new Option("Cursor Speed", s -> s.cursorSpeed, (s, v) -> s.cursorSpeed = v, 4.0, 40.0, 1.0, false),
        new Option("Scroll Amount", s -> s.scrollAmount, (s, v) -> s.scrollAmount = v, 0.25, 4.0, 0.25, false),
        SNEAK_MODE,
        SPRINT_MODE,
        new Option("Invert Look Y", s -> s.invertY ? 1.0 : 0.0, (s, v) -> s.invertY = v != 0.0)
    };

    private static final Option[] ADVANCED = new Option[] {
        new Option("Move Deadzone", s -> s.moveDeadzone, (s, v) -> s.moveDeadzone = (float)v, 0.0, 0.75, 0.05, true),
        new Option("Look Deadzone", s -> s.lookDeadzone, (s, v) -> s.lookDeadzone = (float)v, 0.0, 0.75, 0.05, true),
        new Option("Cursor Deadzone", s -> s.cursorDeadzone, (s, v) -> s.cursorDeadzone = (float)v, 0.0, 0.75, 0.05, true),
        new Option("Trigger Threshold", s -> s.triggerDeadzone, (s, v) -> s.triggerDeadzone = (float)v, 0.0, 0.95, 0.05, true)
    };

    private static final Option[] CONTROLS = new Option[] {
        new Option(ControllerAction.ATTACK),
        new Option(ControllerAction.USE),
        new Option(ControllerAction.JUMP),
        new Option(ControllerAction.SNEAK),
        new Option(ControllerAction.SPRINT),
        new Option(ControllerAction.INVENTORY),
        new Option(ControllerAction.DROP),
        new Option(ControllerAction.PICK_BLOCK),
        new Option(ControllerAction.PAUSE),
        new Option(ControllerAction.HOTBAR_PREVIOUS),
        new Option(ControllerAction.HOTBAR_NEXT),
        new Option(ControllerAction.MENU_ACCEPT),
        new Option(ControllerAction.MENU_CANCEL),
        new Option(ControllerAction.SNAP_FREE_TOGGLE),
        new Option(ControllerAction.QUICK_MOVE)
    };
}
