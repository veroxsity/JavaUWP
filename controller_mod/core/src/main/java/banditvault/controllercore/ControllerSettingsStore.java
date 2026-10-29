package banditvault.controllercore;

import java.io.File;
import java.util.HashMap;
import java.util.Iterator;
import java.util.Map;
import java.util.Properties;

public class ControllerSettingsStore {
    private static final String[] DEFAULT_RADIAL_SLOTS = new String[] {
        "key.togglePerspective",
        "key.advancements",
        "key.swapOffhand",
        "key.chat",
        "key.command",
        "key.socialInteractions",
        "key.screenshot",
        ""
    };

    public boolean toggleCrouch = false;
    public boolean toggleSprint = false;
    public boolean invertY = false;
    public boolean showGuides = true;
    public boolean keyboardMovement = false;
    public float lookSpeed = 135.0f;
    public double cursorSpeed = 14.0;
    public double scrollAmount = 1.0;
    public float moveDeadzone = 0.35f;
    public float lookDeadzone = 0.12f;
    public float cursorDeadzone = 0.12f;
    public float triggerDeadzone = 0.25f;
    private final ControllerInput[] bindings = ControllerBindings.defaults();
    private final Map<String, ControllerInput> javaBindings = new HashMap<String, ControllerInput>();
    private final String[] radialSlots = DEFAULT_RADIAL_SLOTS.clone();

    protected ControllerSettingsStore() {
    }

    public static File configFile() {
        return new File(new File(System.getProperty("user.dir", "."), "config"), "bandit-controller.properties");
    }

    public void readFrom(Properties props) {
        toggleCrouch = bool(props, "toggleCrouch", toggleCrouch);
        toggleSprint = bool(props, "toggleSprint", toggleSprint);
        invertY = bool(props, "invertY", invertY);
        showGuides = bool(props, "showGuides", showGuides);
        keyboardMovement = bool(props, "keyboardMovement", keyboardMovement);
        lookSpeed = (float) range(number(props, "lookSpeed", lookSpeed), 30.0, 300.0);
        cursorSpeed = range(number(props, "cursorSpeed", cursorSpeed), 4.0, 40.0);
        scrollAmount = range(number(props, "scrollAmount", scrollAmount), 0.25, 4.0);
        moveDeadzone = (float) range(number(props, "moveDeadzone", moveDeadzone), 0.0, 0.75);
        lookDeadzone = (float) range(number(props, "lookDeadzone", lookDeadzone), 0.0, 0.75);
        cursorDeadzone = (float) range(number(props, "cursorDeadzone", cursorDeadzone), 0.0, 0.75);
        triggerDeadzone = (float) range(number(props, "triggerDeadzone", triggerDeadzone), 0.0, 0.95);
        for (ControllerAction action : ControllerAction.values()) {
            ControllerInput fallback = ControllerBindings.defaultInput(action);
            bindings[action.ordinal()] = ControllerInput.byId(props.getProperty("binding." + action.id), fallback);
        }
        migrateControlifyDefaults(props);
        for (String name : props.stringPropertyNames()) {
            if (!name.startsWith("javaBinding.")) continue;
            String keyId = name.substring("javaBinding.".length()).trim();
            ControllerInput input = ControllerInput.byId(props.getProperty(name), ControllerInput.UNBOUND);
            if (keyId.isEmpty() || input == ControllerInput.UNBOUND) continue;
            ControllerAction action = controllerActionForJavaKey(keyId);
            if (action == null) {
                javaBindings.put(keyId, input);
            } else {
                rebindController(action, input);
            }
        }
        for (int i = 0; i < radialSlots.length; i++) {
            radialSlots[i] = props.getProperty("radial." + i, DEFAULT_RADIAL_SLOTS[i]).trim();
        }
    }

    public void writeTo(Properties props) {
        props.setProperty("toggleCrouch", Boolean.toString(toggleCrouch));
        props.setProperty("toggleSprint", Boolean.toString(toggleSprint));
        props.setProperty("invertY", Boolean.toString(invertY));
        props.setProperty("showGuides", Boolean.toString(showGuides));
        props.setProperty("keyboardMovement", Boolean.toString(keyboardMovement));
        props.setProperty("lookSpeed", Float.toString(lookSpeed));
        props.setProperty("cursorSpeed", Double.toString(cursorSpeed));
        props.setProperty("scrollAmount", Double.toString(scrollAmount));
        props.setProperty("moveDeadzone", Float.toString(moveDeadzone));
        props.setProperty("lookDeadzone", Float.toString(lookDeadzone));
        props.setProperty("cursorDeadzone", Float.toString(cursorDeadzone));
        props.setProperty("triggerDeadzone", Float.toString(triggerDeadzone));
        for (ControllerAction action : ControllerAction.values()) {
            props.setProperty("binding." + action.id, binding(action).id);
        }
        for (Map.Entry<String, ControllerInput> binding : javaBindings.entrySet()) {
            props.setProperty("javaBinding." + binding.getKey(), binding.getValue().id);
        }
        for (int i = 0; i < radialSlots.length; i++) {
            props.setProperty("radial." + i, radialSlots[i]);
        }
    }

    public ControllerInput binding(ControllerAction action) {
        ControllerInput input = bindings[action.ordinal()];
        return input == null ? ControllerBindings.defaultInput(action) : input;
    }

    public boolean rebindController(ControllerAction action, ControllerInput input) {
        boolean conflict = false;
        if (usesGameplayContext(action) && input != null && input != ControllerInput.UNBOUND) {
            Iterator<Map.Entry<String, ControllerInput>> entries = javaBindings.entrySet().iterator();
            while (entries.hasNext()) {
                if (entries.next().getValue() == input) {
                    entries.remove();
                    conflict = true;
                }
            }
        }
        return ControllerBindings.rebind(action, input, bindings) != null || conflict;
    }

    public ControllerInput javaBinding(String keyId) {
        ControllerInput input = javaBindings.get(keyId);
        return input == null ? ControllerInput.UNBOUND : input;
    }

    public boolean rebindJava(String keyId, ControllerInput input) {
        if (keyId == null || keyId.isEmpty()) return false;
        ControllerAction fixedAction = controllerActionForJavaKey(keyId);
        if (fixedAction != null) return rebindController(fixedAction, input);
        boolean conflict = false;
        if (input == null || input == ControllerInput.UNBOUND) {
            javaBindings.remove(keyId);
            return false;
        }
        Iterator<Map.Entry<String, ControllerInput>> entries = javaBindings.entrySet().iterator();
        while (entries.hasNext()) {
            Map.Entry<String, ControllerInput> entry = entries.next();
            if (!entry.getKey().equals(keyId) && entry.getValue() == input) {
                entries.remove();
                conflict = true;
            }
        }
        for (ControllerAction action : ControllerAction.values()) {
            if (usesGameplayContext(action) && binding(action) == input) {
                bindings[action.ordinal()] = ControllerInput.UNBOUND;
                conflict = true;
            }
        }
        javaBindings.put(keyId, input);
        return conflict;
    }

    public ControllerAction conflictFor(ControllerAction action, ControllerInput input) {
        return ControllerBindings.conflict(action, input, bindings);
    }

    public void resetBindings() {
        ControllerBindings.resetDefaults(bindings);
        javaBindings.clear();
    }

    public String radialSlot(int slot) {
        return radialSlots[slot];
    }

    public void setRadialSlot(int slot, String keyId) {
        radialSlots[slot] = keyId == null ? "" : keyId;
    }

    public void resetRadialSlots() {
        System.arraycopy(DEFAULT_RADIAL_SLOTS, 0, radialSlots, 0, radialSlots.length);
    }

    public static ControllerAction controllerActionForJavaKey(String keyId) {
        if ("key.attack".equals(keyId)) return ControllerAction.ATTACK;
        if ("key.use".equals(keyId)) return ControllerAction.USE;
        if ("key.jump".equals(keyId)) return ControllerAction.JUMP;
        if ("key.sneak".equals(keyId)) return ControllerAction.SNEAK;
        if ("key.sprint".equals(keyId)) return ControllerAction.SPRINT;
        if ("key.inventory".equals(keyId)) return ControllerAction.INVENTORY;
        if ("key.drop".equals(keyId)) return ControllerAction.DROP;
        if ("key.swapOffhand".equals(keyId)) return ControllerAction.SWAP_HANDS;
        if ("key.pickItem".equals(keyId)) return ControllerAction.PICK_BLOCK;
        return null;
    }

    private void migrateControlifyDefaults(Properties props) {
        if (props.containsKey("binding.swapHands")) return;
        if (binding(ControllerAction.SNEAK) == ControllerInput.B
            && binding(ControllerAction.DROP) == ControllerInput.X
            && binding(ControllerAction.PICK_BLOCK) == ControllerInput.RIGHT_THUMB) {
            bindings[ControllerAction.SNEAK.ordinal()] = ControllerInput.RIGHT_THUMB;
            bindings[ControllerAction.DROP.ordinal()] = ControllerInput.B;
            bindings[ControllerAction.SWAP_HANDS.ordinal()] = ControllerInput.X;
            bindings[ControllerAction.PICK_BLOCK.ordinal()] = ControllerInput.UNBOUND;
            return;
        }
        for (ControllerAction action : ControllerAction.values()) {
            if (action != ControllerAction.SWAP_HANDS && usesGameplayContext(action) && binding(action) == ControllerInput.X) {
                bindings[ControllerAction.SWAP_HANDS.ordinal()] = ControllerInput.UNBOUND;
                return;
            }
        }
    }

    private static boolean usesGameplayContext(ControllerAction action) {
        switch (action) {
            case MENU_ACCEPT:
            case MENU_CANCEL:
            case SNAP_FREE_TOGGLE:
            case QUICK_MOVE:
            case MENU_SECONDARY:
                return false;
            default:
                return true;
        }
    }

    private static boolean bool(Properties props, String key, boolean fallback) {
        String value = props.getProperty(key);
        if (value == null) {
            return fallback;
        }
        return "true".equalsIgnoreCase(value)
            || "1".equals(value)
            || "yes".equalsIgnoreCase(value)
            || "on".equalsIgnoreCase(value);
    }

    private static double number(Properties props, String key, double fallback) {
        String value = props.getProperty(key);
        if (value == null) {
            return fallback;
        }
        try {
            double parsed = Double.parseDouble(value.trim());
            return Double.isFinite(parsed) ? parsed : fallback;
        } catch (NumberFormatException ignored) {
            return fallback;
        }
    }

    private static double range(double value, double min, double max) {
        if (value < min) {
            return min;
        }
        if (value > max) {
            return max;
        }
        return value;
    }
}
