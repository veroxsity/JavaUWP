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
            case SNEAK: return ControllerInput.RIGHT_THUMB;
            case SPRINT: return ControllerInput.LEFT_THUMB;
            case INVENTORY: return ControllerInput.Y;
            case DROP: return ControllerInput.B;
            case SWAP_HANDS: return ControllerInput.X;
            case PICK_BLOCK: return ControllerInput.UNBOUND;
            case PAUSE: return ControllerInput.START;
            case HOTBAR_PREVIOUS: return ControllerInput.LEFT_BUMPER;
            case HOTBAR_NEXT: return ControllerInput.RIGHT_BUMPER;
            case MENU_ACCEPT: return ControllerInput.A;
            case MENU_CANCEL: return ControllerInput.B;
            case SNAP_FREE_TOGGLE: return ControllerInput.BACK;
            case QUICK_MOVE: return ControllerInput.Y;
            case MENU_SECONDARY: return ControllerInput.X;
            case RADIAL_MENU: return ControllerInput.DPAD_UP;
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
        if (input == null || input == ControllerInput.UNBOUND) {
            return null;
        }
        for (ControllerAction candidate : ControllerAction.values()) {
            if (candidate != action && overlaps(action, candidate) && bindings[candidate.ordinal()] == input) {
                return candidate;
            }
        }
        return null;
    }

    public static ControllerAction rebind(ControllerAction action, ControllerInput input, ControllerInput[] bindings) {
        ControllerAction firstConflict = null;
        if (input != null && input != ControllerInput.UNBOUND) {
            for (ControllerAction candidate : ControllerAction.values()) {
                if (candidate != action && overlaps(action, candidate) && bindings[candidate.ordinal()] == input) {
                    if (firstConflict == null) {
                        firstConflict = candidate;
                    }
                    bindings[candidate.ordinal()] = ControllerInput.UNBOUND;
                }
            }
        }
        bindings[action.ordinal()] = input == null ? defaultInput(action) : input;
        return firstConflict;
    }

    private static boolean overlaps(ControllerAction left, ControllerAction right) {
        return (contexts(left) & contexts(right)) != 0;
    }

    private static int contexts(ControllerAction action) {
        switch (action) {
            case MENU_ACCEPT:
            case MENU_CANCEL:
            case SNAP_FREE_TOGGLE:
            case QUICK_MOVE:
            case MENU_SECONDARY:
                return MENU;
            default:
                return GAMEPLAY;
        }
    }

    public static void main(String[] args) {
        ControllerInput[] bindings = defaults();
        assert bindings[ControllerAction.ATTACK.ordinal()] == ControllerInput.RIGHT_TRIGGER;
        assert bindings[ControllerAction.SNEAK.ordinal()] == ControllerInput.RIGHT_THUMB;
        assert bindings[ControllerAction.DROP.ordinal()] == ControllerInput.B;
        assert bindings[ControllerAction.SWAP_HANDS.ordinal()] == ControllerInput.X;
        assert bindings[ControllerAction.PICK_BLOCK.ordinal()] == ControllerInput.UNBOUND;
        assert conflict(ControllerAction.MENU_ACCEPT, ControllerInput.A, bindings) == null;
        assert conflict(ControllerAction.MENU_ACCEPT, ControllerInput.Y, bindings) == ControllerAction.QUICK_MOVE;
        assert conflict(ControllerAction.QUICK_MOVE, ControllerInput.X, bindings) == ControllerAction.MENU_SECONDARY;
        assert conflict(ControllerAction.DROP, ControllerInput.B, bindings) == null;
        ControllerState state = new ControllerState();
        assert !ControllerInput.UNBOUND.held(state, 0.25f);
        assert !ControllerInput.UNBOUND.pressed(state, 0.25f);
        assert !ControllerInput.UNBOUND.released(state, 0.25f);

        ControllerAction replaced = rebind(ControllerAction.DROP, ControllerInput.Y, bindings);
        assert replaced == ControllerAction.INVENTORY;
        assert bindings[ControllerAction.DROP.ordinal()] == ControllerInput.Y;
        assert bindings[ControllerAction.INVENTORY.ordinal()] == ControllerInput.UNBOUND;
        assert bindings[ControllerAction.QUICK_MOVE.ordinal()] == ControllerInput.Y;
        assert conflict(ControllerAction.DROP, ControllerInput.Y, bindings) == null;

        rebind(ControllerAction.DROP, ControllerInput.UNBOUND, bindings);
        assert bindings[ControllerAction.DROP.ordinal()] == ControllerInput.UNBOUND;
        assert conflict(ControllerAction.DROP, ControllerInput.UNBOUND, bindings) == null;
    }
}
