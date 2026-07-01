package banditvault.controllercore;

public final class ControllerState {
    private static final int AXIS_COUNT = 6;
    private static final int BUTTON_COUNT = 15;

    private final float[] axes = new float[AXIS_COUNT];
    private final boolean[] buttons = new boolean[BUTTON_COUNT];
    private final boolean[] previousButtons = new boolean[BUTTON_COUNT];
    private final boolean[] previousTriggers = new boolean[2];

    public void capture(float[] axisValues, boolean[] buttonValues) {
        for (int i = 0; i < axes.length; i++) {
            axes[i] = clampAxis(i < axisValues.length ? axisValues[i] : 0.0f);
        }
        for (int i = 0; i < buttons.length; i++) {
            buttons[i] = i < buttonValues.length && buttonValues[i];
        }
    }

    public void finishFrame(float triggerDeadzone) {
        for (int i = 0; i < previousButtons.length; i++) {
            previousButtons[i] = buttons[i];
        }
        previousTriggers[0] = trigger(ControllerAxis.LEFT_TRIGGER, triggerDeadzone);
        previousTriggers[1] = trigger(ControllerAxis.RIGHT_TRIGGER, triggerDeadzone);
    }

    public float axis(ControllerAxis axis) {
        return axes[axis.index];
    }

    public boolean button(ControllerButton button) {
        return buttons[button.index];
    }

    public boolean pressed(ControllerButton button) {
        return buttons[button.index] && !previousButtons[button.index];
    }

    public boolean released(ControllerButton button) {
        return !buttons[button.index] && previousButtons[button.index];
    }

    public boolean trigger(ControllerAxis axis, float deadzone) {
        return axis(axis) > deadzone;
    }

    public boolean triggerPressed(ControllerAxis axis, float deadzone) {
        int triggerIndex = axis.index - ControllerAxis.LEFT_TRIGGER.index;
        return trigger(axis, deadzone) && triggerIndex >= 0 && triggerIndex < previousTriggers.length && !previousTriggers[triggerIndex];
    }

    public boolean triggerReleased(ControllerAxis axis, float deadzone) {
        int triggerIndex = axis.index - ControllerAxis.LEFT_TRIGGER.index;
        return !trigger(axis, deadzone) && triggerIndex >= 0 && triggerIndex < previousTriggers.length && previousTriggers[triggerIndex];
    }

    public static float clampAxis(float value) {
        if (value < -1.0f) {
            return -1.0f;
        }
        if (value > 1.0f) {
            return 1.0f;
        }
        return value;
    }
}
