package banditvault.fabriccontroller;

import net.minecraft.class_310;
import net.minecraft.class_332;
import net.minecraft.class_4185;
import net.minecraft.class_437;
import net.minecraft.class_2561;

public final class BanditControllerSettingsScreen extends class_437 {
    private final class_437 parent;

    public BanditControllerSettingsScreen(class_437 parent) {
        super(class_2561.method_30163("Bandit Controller"));
        this.parent = parent;
    }

    @Override
    protected void method_25426() {
        rebuildButtons();
    }

    @Override
    public void method_25394(class_332 context, int mouseX, int mouseY, float delta) {
        FabricScreenApi.renderBackground(this, context, mouseX, mouseY, delta);
        context.method_27534(this.field_22793, class_2561.method_30163("Bandit Controller"), this.field_22789 / 2, 18, 0xFFFFFF);
        super.method_25394(context, mouseX, mouseY, delta);
    }

    @Override
    public void method_25419() {
        close();
    }

    public void close() {
        BanditControllerSettings.save();
        class_310.method_1551().method_1507(parent);
    }

    private void rebuildButtons() {
        clearWidgets();
        BanditControllerSettings settings = BanditControllerSettings.get();
        int panelWidth = Math.min(426, Math.max(250, this.field_22789 - 24));
        int x = this.field_22789 / 2 - panelWidth / 2;
        int y = this.field_22790 < 260 ? 32 : 42;
        int row = this.field_22790 < 260 ? 21 : 24;

        if (panelWidth >= 390) {
            int leftWidth = 158;
            int gap = 12;
            int rightX = x + leftWidth + gap;
            int rightWidth = panelWidth - leftWidth - gap;
            int leftY = y;
            int rightY = y;

            addToggle(x, leftY, leftWidth, crouchLabel(settings), () -> settings.toggleCrouch = !settings.toggleCrouch);
            leftY += row;
            addToggle(x, leftY, leftWidth, sprintLabel(settings), () -> settings.toggleSprint = !settings.toggleSprint);
            leftY += row;
            addToggle(x, leftY, leftWidth, invertLabel(settings), () -> settings.invertY = !settings.invertY);
            leftY += row + 4;
            addButton(newButton(x, leftY, leftWidth, 20, "Reset defaults", button -> reset(settings)));

            addStepper(rightX, rightY, rightWidth, "Look", Math.round(settings.lookSpeed), -15.0, 15.0, 30.0, 300.0, value -> settings.lookSpeed = (float)value);
            rightY += row;
            addStepper(rightX, rightY, rightWidth, "Cursor", settings.cursorSpeed, -1.0, 1.0, 4.0, 40.0, value -> settings.cursorSpeed = value);
            rightY += row;
            addStepper(rightX, rightY, rightWidth, "Scroll", settings.scrollAmount, -0.25, 0.25, 0.25, 4.0, value -> settings.scrollAmount = value);
            rightY += row;
            addStepper(rightX, rightY, rightWidth, "Move dz", settings.moveDeadzone, -0.05, 0.05, 0.0, 0.75, value -> settings.moveDeadzone = (float)value);
            rightY += row;
            addStepper(rightX, rightY, rightWidth, "Look dz", settings.lookDeadzone, -0.05, 0.05, 0.0, 0.75, value -> settings.lookDeadzone = (float)value);
            rightY += row;
            addStepper(rightX, rightY, rightWidth, "Cursor dz", settings.cursorDeadzone, -0.05, 0.05, 0.0, 0.75, value -> settings.cursorDeadzone = (float)value);
            rightY += row;
            addStepper(rightX, rightY, rightWidth, "Trigger dz", settings.triggerDeadzone, -0.05, 0.05, 0.0, 0.95, value -> settings.triggerDeadzone = (float)value);
        } else {
            addToggle(x, y, panelWidth, crouchLabel(settings), () -> settings.toggleCrouch = !settings.toggleCrouch);
            y += row;
            addToggle(x, y, panelWidth, sprintLabel(settings), () -> settings.toggleSprint = !settings.toggleSprint);
            y += row;
            addToggle(x, y, panelWidth, invertLabel(settings), () -> settings.invertY = !settings.invertY);
            y += row + 4;
            addStepper(x, y, panelWidth, "Look", Math.round(settings.lookSpeed), -15.0, 15.0, 30.0, 300.0, value -> settings.lookSpeed = (float)value);
            y += row;
            addStepper(x, y, panelWidth, "Cursor", settings.cursorSpeed, -1.0, 1.0, 4.0, 40.0, value -> settings.cursorSpeed = value);
            y += row;
            addStepper(x, y, panelWidth, "Move dz", settings.moveDeadzone, -0.05, 0.05, 0.0, 0.75, value -> settings.moveDeadzone = (float)value);
            y += row;
            addButton(newButton(x, y, panelWidth, 20, "Reset defaults", button -> reset(settings)));
        }

        int doneWidth = Math.min(210, panelWidth);
        int doneY = Math.max(4, this.field_22790 - 26);
        addButton(newButton(this.field_22789 / 2 - doneWidth / 2, doneY, doneWidth, 20, "Done", button -> close()));
    }

    private void addToggle(int x, int y, int width, String label, Runnable action) {
        addButton(newButton(x, y, width, 20, label, button -> {
            action.run();
            saveAndRebuild();
        }));
    }

    private void addStepper(int x, int y, int width, String label, double value, double down, double up, double min, double max, Setter setter) {
        int buttonWidth = 36;
        int gap = 6;
        int labelWidth = Math.max(82, width - buttonWidth * 2 - gap * 2);
        addButton(newButton(x, y, buttonWidth, 20, "-", button -> adjust(value, down, min, max, setter)));
        addButton(newButton(x + buttonWidth + gap, y, labelWidth, 20, label + ": " + format(value), button -> { }));
        addButton(newButton(x + buttonWidth + gap + labelWidth + gap, y, buttonWidth, 20, "+", button -> adjust(value, up, min, max, setter)));
    }

    private void adjust(double value, double delta, double min, double max, Setter setter) {
        setter.set(Math.max(min, Math.min(max, value + delta)));
        saveAndRebuild();
    }

    private void reset(BanditControllerSettings settings) {
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
        saveAndRebuild();
    }

    private void saveAndRebuild() {
        BanditControllerSettings.save();
        rebuildButtons();
    }

    private class_4185 newButton(int x, int y, int w, int h, String label, class_4185.class_4241 press) {
        return class_4185.method_46430(class_2561.method_30163(label), press)
            .method_46434(x, y, w, h)
            .method_46431();
    }

    private void addButton(class_4185 button) {
        if (invokeWidgetMethod("method_37063", button)) {
            return;
        }
        if (invokeWidgetMethod("method_25411", button)) {
            return;
        }
        invokeWidgetMethod("method_25429", button);
    }

    private boolean invokeWidgetMethod(String name, Object widget) {
        Class<?> current = getClass();
        while (current != null) {
            java.lang.reflect.Method[] methods = current.getDeclaredMethods();
            for (int i = 0; i < methods.length; i++) {
                java.lang.reflect.Method method = methods[i];
                if (!method.getName().equals(name) || method.getParameterTypes().length != 1) {
                    continue;
                }
                if (!method.getParameterTypes()[0].isAssignableFrom(widget.getClass())) {
                    continue;
                }
                try {
                    method.setAccessible(true);
                    method.invoke(this, widget);
                    return true;
                } catch (Throwable ignored) {
                }
            }
            current = current.getSuperclass();
        }
        return false;
    }

    private void clearWidgets() {
        if (invokeNoArg("method_37067")) {
            return;
        }
        try {
            java.lang.reflect.Field children = findField(getClass(), "field_22786");
            children.setAccessible(true);
            ((java.util.List<?>)children.get(this)).clear();
            java.lang.reflect.Field buttons = findField(getClass(), "field_22791");
            buttons.setAccessible(true);
            ((java.util.List<?>)buttons.get(this)).clear();
        } catch (Throwable ignored) {
        }
    }

    private boolean invokeNoArg(String name) {
        Class<?> current = getClass();
        while (current != null) {
            try {
                java.lang.reflect.Method method = current.getDeclaredMethod(name);
                method.setAccessible(true);
                method.invoke(this);
                return true;
            } catch (Throwable ignored) {
                current = current.getSuperclass();
            }
        }
        return false;
    }

    private java.lang.reflect.Field findField(Class<?> type, String name) throws NoSuchFieldException {
        Class<?> current = type;
        while (current != null) {
            try {
                return current.getDeclaredField(name);
            } catch (NoSuchFieldException ignored) {
                current = current.getSuperclass();
            }
        }
        throw new NoSuchFieldException(name);
    }

    private String crouchLabel(BanditControllerSettings settings) {
        return "Crouch: " + (settings.toggleCrouch ? "Toggle" : "Hold");
    }

    private String sprintLabel(BanditControllerSettings settings) {
        return "Sprint: " + (settings.toggleSprint ? "Toggle" : "Hold");
    }

    private String invertLabel(BanditControllerSettings settings) {
        return "Invert Y: " + (settings.invertY ? "On" : "Off");
    }

    private String format(double value) {
        if (Math.abs(value - Math.round(value)) < 0.001) {
            return Long.toString(Math.round(value));
        }
        return String.format(java.util.Locale.ROOT, "%.2f", value);
    }

    private interface Setter {
        void set(double value);
    }
}
