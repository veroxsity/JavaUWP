package banditvault.controllercore;

public final class ControllerBindings {
    private static final int GAMEPLAY = 1;
    private static final int MENU = 2;

    private ControllerBindings() {
    }

    public static ControllerInput defaultInput(ControllerAction action) {
        switch (action) {
            case ATTACK: return ControllerInput.RIGHT_TRIGGER;
            case USE: return ControllerInput.LEFT_TRIGGER;
            case JUMP: return ControllerInput.A;
            case SNEAK: return ControllerInput.B;
            case SPRINT: return ControllerInput.LEFT_THUMB;
            case INVENTORY: return ControllerInput.Y;
            case DROP: return ControllerInput.X;
            case PICK_BLOCK: return ControllerInput.RIGHT_THUMB;
            case PAUSE: return ControllerInput.START;
            case HOTBAR_PREVIOUS: return ControllerInput.LEFT_BUMPER;
            case HOTBAR_NEXT: return ControllerInput.RIGHT_BUMPER;
            case MENU_ACCEPT: return ControllerInput.A;
            case MENU_CANCEL: return ControllerInput.B;
            case SNAP_FREE_TOGGLE: return ControllerInput.BACK;
            case QUICK_MOVE: return ControllerInput.Y;
            default: return ControllerInput.A;
        }
    }

    public static ControllerInput[] defaults() {
        ControllerInput[] bindings = new ControllerInput[ControllerAction.values().length];
        resetDefaults(bindings);
        return bindings;
    }

    public static void resetDefaults(ControllerInput[] bindings) {
        for (ControllerAction action : ControllerAction.values()) {
            bindings[action.ordinal()] = defaultInput(action);
        }
    }

    public static ControllerAction conflict(ControllerAction action, ControllerInput input, ControllerInput[] bindings) {
        for (ControllerAction candidate : ControllerAction.values()) {
            if (candidate != action && overlaps(action, candidate) && bindings[candidate.ordinal()] == input) {
                return candidate;
            }
        }
        return null;
    }

    private static boolean overlaps(ControllerAction left, ControllerAction right) {
        return (contexts(left) & contexts(right)) != 0;
    }

    private static int contexts(ControllerAction action) {
        switch (action) {
            case DROP:
                return GAMEPLAY | MENU;
            case MENU_ACCEPT:
            case MENU_CANCEL:
            case SNAP_FREE_TOGGLE:
            case QUICK_MOVE:
                return MENU;
            default:
                return GAMEPLAY;
        }
    }

    public static void main(String[] args) {
        ControllerInput[] bindings = defaults();
        assert bindings[ControllerAction.ATTACK.ordinal()] == ControllerInput.RIGHT_TRIGGER;
        assert conflict(ControllerAction.MENU_ACCEPT, ControllerInput.A, bindings) == null;
        assert conflict(ControllerAction.MENU_ACCEPT, ControllerInput.Y, bindings) == ControllerAction.QUICK_MOVE;
        assert conflict(ControllerAction.QUICK_MOVE, ControllerInput.X, bindings) == ControllerAction.DROP;
    }
}
