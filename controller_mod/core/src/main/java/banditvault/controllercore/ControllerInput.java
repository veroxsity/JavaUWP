package banditvault.controllercore;

public enum ControllerInput {
    A("a", "A", ControllerButton.A, null),
    B("b", "B", ControllerButton.B, null),
    X("x", "X", ControllerButton.X, null),
    Y("y", "Y", ControllerButton.Y, null),
    LEFT_BUMPER("leftBumper", "LB", ControllerButton.LEFT_BUMPER, null),
    RIGHT_BUMPER("rightBumper", "RB", ControllerButton.RIGHT_BUMPER, null),
    BACK("back", "View", ControllerButton.BACK, null),
    START("start", "Menu", ControllerButton.START, null),
    LEFT_THUMB("leftThumb", "LS", ControllerButton.LEFT_THUMB, null),
    RIGHT_THUMB("rightThumb", "RS", ControllerButton.RIGHT_THUMB, null),
    DPAD_UP("dpadUp", "D-Up", ControllerButton.DPAD_UP, null),
    DPAD_RIGHT("dpadRight", "D-Right", ControllerButton.DPAD_RIGHT, null),
    DPAD_DOWN("dpadDown", "D-Down", ControllerButton.DPAD_DOWN, null),
    DPAD_LEFT("dpadLeft", "D-Left", ControllerButton.DPAD_LEFT, null),
    LEFT_TRIGGER("leftTrigger", "LT", null, ControllerAxis.LEFT_TRIGGER),
    RIGHT_TRIGGER("rightTrigger", "RT", null, ControllerAxis.RIGHT_TRIGGER);

    public final String id;
    public final String label;
    private final ControllerButton button;
    private final ControllerAxis trigger;

    ControllerInput(String id, String label, ControllerButton button, ControllerAxis trigger) {
        this.id = id;
        this.label = label;
        this.button = button;
        this.trigger = trigger;
    }

    public boolean held(ControllerState state, float triggerDeadzone) {
        return button != null ? state.button(button) : state.trigger(trigger, triggerDeadzone);
    }

    public boolean pressed(ControllerState state, float triggerDeadzone) {
        return button != null ? state.pressed(button) : state.triggerPressed(trigger, triggerDeadzone);
    }

    public boolean released(ControllerState state, float triggerDeadzone) {
        return button != null ? state.released(button) : state.triggerReleased(trigger, triggerDeadzone);
    }

    public static ControllerInput byId(String id, ControllerInput fallback) {
        if (id == null) {
            return fallback;
        }
        for (ControllerInput input : values()) {
            if (input.id.equals(id)) {
                return input;
            }
        }
        return fallback;
    }
}
