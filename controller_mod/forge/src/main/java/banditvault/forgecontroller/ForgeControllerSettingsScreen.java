package banditvault.forgecontroller;

import java.util.Locale;
import net.minecraft.client.gui.GuiGraphics;
import net.minecraft.client.gui.components.Button;
import net.minecraft.client.gui.screens.Screen;
import org.lwjgl.glfw.GLFW;

public final class ForgeControllerSettingsScreen extends Screen {
    private final Screen parent;

    public ForgeControllerSettingsScreen(Screen parent) {
        super(ForgeControllerCompat.textLiteral("Bandit Controller"));
        this.parent = parent;
    }

    @Override
    protected void m_7856_() {
        rebuildButtons();
    }

    @Override
    public void m_88315_(GuiGraphics graphics, int mouseX, int mouseY, float delta) {
        this.m_280273_(graphics);
        graphics.m_280653_(this.f_96547_, ForgeControllerCompat.textLiteral("Bandit Controller"), this.f_96543_ / 2, 18, 0xFFFFFF);
        super.m_88315_(graphics, mouseX, mouseY, delta);
    }

    @Override
    public boolean m_7933_(int keyCode, int scanCode, int modifiers) {
        if (keyCode == GLFW.GLFW_KEY_ESCAPE) {
            close();
            return true;
        }
        return super.m_7933_(keyCode, scanCode, modifiers);
    }

    public void close() {
        ForgeControllerSettings.save();
        if (this.f_96541_ != null) {
            this.f_96541_.m_91152_(parent);
        }
    }

    private void rebuildButtons() {
        clearWidgets();
        ForgeControllerSettings settings = ForgeControllerSettings.get();
        int panelWidth = Math.min(426, Math.max(250, this.f_96543_ - 24));
        int x = this.f_96543_ / 2 - panelWidth / 2;
        int y = this.f_96544_ < 260 ? 32 : 42;
        int row = this.f_96544_ < 260 ? 21 : 24;

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
            addButton(ForgeControllerCompat.createButton(x, leftY, leftWidth, 20, "Reset defaults", button -> {
                resetDefaults(settings);
                ForgeControllerSettings.save();
                rebuildButtons();
            }));

            addStepper(rightX, rightY, rightWidth, "Look", Math.round(settings.lookSpeed), -15.0, 15.0, 30.0, 300.0, value -> settings.lookSpeed = (float) value);
            rightY += row;
            addStepper(rightX, rightY, rightWidth, "Cursor", settings.cursorSpeed, -2.0, 2.0, 4.0, 60.0, value -> settings.cursorSpeed = value);
            rightY += row;
            addStepper(rightX, rightY, rightWidth, "Scroll", settings.scrollAmount, -0.25, 0.25, 0.25, 4.0, value -> settings.scrollAmount = value);
            rightY += row;
            addStepper(rightX, rightY, rightWidth, "Move dz", settings.moveDeadzone, -0.05, 0.05, 0.0, 0.75, value -> settings.moveDeadzone = (float) value);
            rightY += row;
            addStepper(rightX, rightY, rightWidth, "Look dz", settings.lookDeadzone, -0.05, 0.05, 0.0, 0.75, value -> settings.lookDeadzone = (float) value);
            rightY += row;
            addStepper(rightX, rightY, rightWidth, "Cursor dz", settings.cursorDeadzone, -0.05, 0.05, 0.0, 0.75, value -> settings.cursorDeadzone = (float) value);
            rightY += row;
            addStepper(rightX, rightY, rightWidth, "Trigger dz", settings.triggerDeadzone, -0.05, 0.05, 0.0, 0.95, value -> settings.triggerDeadzone = (float) value);
        } else {
            addToggle(x, y, panelWidth, crouchLabel(settings), () -> settings.toggleCrouch = !settings.toggleCrouch);
            y += row;
            addToggle(x, y, panelWidth, sprintLabel(settings), () -> settings.toggleSprint = !settings.toggleSprint);
            y += row;
            addToggle(x, y, panelWidth, invertLabel(settings), () -> settings.invertY = !settings.invertY);
            y += row + 4;
            addStepper(x, y, panelWidth, "Look", Math.round(settings.lookSpeed), -15.0, 15.0, 30.0, 300.0, value -> settings.lookSpeed = (float) value);
            y += row;
            addStepper(x, y, panelWidth, "Cursor", settings.cursorSpeed, -2.0, 2.0, 4.0, 60.0, value -> settings.cursorSpeed = value);
            y += row;
            addStepper(x, y, panelWidth, "Move dz", settings.moveDeadzone, -0.05, 0.05, 0.0, 0.75, value -> settings.moveDeadzone = (float) value);
            y += row;
            addButton(ForgeControllerCompat.createButton(x, y, panelWidth, 20, "Reset defaults", button -> {
                resetDefaults(settings);
                ForgeControllerSettings.save();
                rebuildButtons();
            }));
        }

        int doneWidth = Math.min(210, panelWidth);
        int doneY = Math.max(4, this.f_96544_ - 26);
        addButton(ForgeControllerCompat.createButton(this.f_96543_ / 2 - doneWidth / 2, doneY, doneWidth, 20, "Done", button -> close()));
    }

    private void addToggle(int x, int y, int w, String label, Runnable action) {
        addButton(ForgeControllerCompat.createButton(x, y, w, 20, label, button -> {
            action.run();
            ForgeControllerSettings.save();
            rebuildButtons();
        }));
    }

    private void addStepper(int x, int y, int w, String label, double value, double down, double up, double min, double max, Setter setter) {
        int buttonWidth = 36;
        int gap = 6;
        int labelWidth = Math.max(82, w - buttonWidth * 2 - gap * 2);
        addButton(ForgeControllerCompat.createButton(x, y, 42, 20, "-", button -> adjust(value, down, min, max, setter)));
        addButton(ForgeControllerCompat.createButton(x + buttonWidth + gap, y, labelWidth, 20, label + ": " + format(value), button -> {
        }));
        addButton(ForgeControllerCompat.createButton(x + buttonWidth + gap + labelWidth + gap, y, buttonWidth, 20, "+", button -> adjust(value, up, min, max, setter)));
    }

    private void adjust(double value, double delta, double min, double max, Setter setter) {
        double next = value + delta;
        if (next < min) {
            next = min;
        }
        if (next > max) {
            next = max;
        }
        setter.set(next);
        ForgeControllerSettings.save();
        rebuildButtons();
    }

    private void addButton(Button button) {
        this.m_142416_(button);
    }

    private void resetDefaults(ForgeControllerSettings settings) {
        settings.toggleCrouch = false;
        settings.toggleSprint = false;
        settings.invertY = false;
        settings.lookSpeed = 135.0f;
        settings.cursorSpeed = 18.0;
        settings.scrollAmount = 1.0;
        settings.moveDeadzone = 0.35f;
        settings.lookDeadzone = 0.12f;
        settings.cursorDeadzone = 0.12f;
        settings.triggerDeadzone = 0.25f;
    }

    private void clearWidgets() {
        this.m_169413_();
    }

    private String crouchLabel(ForgeControllerSettings settings) {
        return "Crouch: " + (settings.toggleCrouch ? "Toggle" : "Hold");
    }

    private String sprintLabel(ForgeControllerSettings settings) {
        return "Sprint: " + (settings.toggleSprint ? "Toggle" : "Hold");
    }

    private String invertLabel(ForgeControllerSettings settings) {
        return "Invert Y: " + (settings.invertY ? "On" : "Off");
    }

    private String format(double value) {
        if (Math.abs(value - Math.round(value)) < 0.001) {
            return Long.toString(Math.round(value));
        }
        return String.format(Locale.ROOT, "%.2f", value);
    }

    private interface Setter {
        void set(double value);
    }
}
