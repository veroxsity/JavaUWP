package banditvault.fabriccontroller;

import banditvault.controllercore.ControllerAction;
import banditvault.controllercore.ControllerBindings;
import banditvault.controllercore.ControllerInput;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.util.HashMap;
import java.util.Iterator;
import java.util.Map;
import java.util.Properties;

public final class BanditControllerSettings {
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

    private static final BanditControllerSettings INSTANCE = new BanditControllerSettings();
    private static volatile boolean loaded;

    private BanditControllerSettings() {
    }

    public static BanditControllerSettings get() {
        if (!loaded) {
            load();
        }
        return INSTANCE;
    }

    public static synchronized void load() {
        if (loaded) {
            return;
        }
        loaded = true;
        File file = configFile();
        if (!file.isFile()) {
            save();
            return;
        }

        Properties props = new Properties();
        try (FileInputStream in = new FileInputStream(file)) {
            props.load(in);
        } catch (IOException e) {
            FabricControllerLog.logException("Bandit controller settings failed to load", e);
            return;
        }

        INSTANCE.toggleCrouch = bool(props, "toggleCrouch", INSTANCE.toggleCrouch);
        INSTANCE.toggleSprint = bool(props, "toggleSprint", INSTANCE.toggleSprint);
        INSTANCE.invertY = bool(props, "invertY", INSTANCE.invertY);
        INSTANCE.lookSpeed = (float)range(number(props, "lookSpeed", INSTANCE.lookSpeed), 30.0, 300.0);
        INSTANCE.cursorSpeed = range(number(props, "cursorSpeed", INSTANCE.cursorSpeed), 4.0, 40.0);
        INSTANCE.scrollAmount = range(number(props, "scrollAmount", INSTANCE.scrollAmount), 0.25, 4.0);
        INSTANCE.moveDeadzone = (float)range(number(props, "moveDeadzone", INSTANCE.moveDeadzone), 0.0, 0.75);
        INSTANCE.lookDeadzone = (float)range(number(props, "lookDeadzone", INSTANCE.lookDeadzone), 0.0, 0.75);
        INSTANCE.cursorDeadzone = (float)range(number(props, "cursorDeadzone", INSTANCE.cursorDeadzone), 0.0, 0.75);
        INSTANCE.triggerDeadzone = (float)range(number(props, "triggerDeadzone", INSTANCE.triggerDeadzone), 0.0, 0.95);
        for (ControllerAction action : ControllerAction.values()) {
            ControllerInput fallback = ControllerBindings.defaultInput(action);
            INSTANCE.bindings[action.ordinal()] = ControllerInput.byId(props.getProperty("binding." + action.id), fallback);
        }
        migrateControlifyDefaults(props);
        for (String name : props.stringPropertyNames()) {
            if (!name.startsWith("javaBinding.")) continue;
            String keyId = name.substring("javaBinding.".length()).trim();
            ControllerInput input = ControllerInput.byId(props.getProperty(name), ControllerInput.UNBOUND);
            if (keyId.isEmpty() || input == ControllerInput.UNBOUND) continue;
            ControllerAction action = controllerActionForJavaKey(keyId);
            if (action == null) {
                INSTANCE.javaBindings.put(keyId, input);
            } else {
                INSTANCE.rebindController(action, input);
            }
        }
        for (int i = 0; i < INSTANCE.radialSlots.length; i++) {
            INSTANCE.radialSlots[i] = props.getProperty("radial." + i, DEFAULT_RADIAL_SLOTS[i]).trim();
        }
    }

    public static void save() {
        File file = configFile();
        File dir = file.getParentFile();
        if (dir != null) {
            dir.mkdirs();
        }

        Properties props = new Properties();
        props.setProperty("toggleCrouch", Boolean.toString(INSTANCE.toggleCrouch));
        props.setProperty("toggleSprint", Boolean.toString(INSTANCE.toggleSprint));
        props.setProperty("invertY", Boolean.toString(INSTANCE.invertY));
        props.setProperty("lookSpeed", Float.toString(INSTANCE.lookSpeed));
        props.setProperty("cursorSpeed", Double.toString(INSTANCE.cursorSpeed));
        props.setProperty("scrollAmount", Double.toString(INSTANCE.scrollAmount));
        props.setProperty("moveDeadzone", Float.toString(INSTANCE.moveDeadzone));
        props.setProperty("lookDeadzone", Float.toString(INSTANCE.lookDeadzone));
        props.setProperty("cursorDeadzone", Float.toString(INSTANCE.cursorDeadzone));
        props.setProperty("triggerDeadzone", Float.toString(INSTANCE.triggerDeadzone));
        for (ControllerAction action : ControllerAction.values()) {
            props.setProperty("binding." + action.id, INSTANCE.binding(action).id);
        }
        for (Map.Entry<String, ControllerInput> binding : INSTANCE.javaBindings.entrySet()) {
            props.setProperty("javaBinding." + binding.getKey(), binding.getValue().id);
        }
        for (int i = 0; i < INSTANCE.radialSlots.length; i++) {
            props.setProperty("radial." + i, INSTANCE.radialSlots[i]);
        }

        try (FileOutputStream out = new FileOutputStream(file)) {
            props.store(out, "Bandit controller compatibility settings");
        } catch (IOException e) {
            FabricControllerLog.logException("Bandit controller settings failed to save", e);
        }
    }

    private static File configFile() {
        return new File(new File(System.getProperty("user.dir", "."), "config"), "bandit-controller.properties");
    }

    public ControllerInput binding(ControllerAction action) {
        ControllerInput input = bindings[action.ordinal()];
        return input == null ? ControllerBindings.defaultInput(action) : input;
    }

    public void setBinding(ControllerAction action, ControllerInput input) {
        bindings[action.ordinal()] = input == null ? ControllerBindings.defaultInput(action) : input;
    }

    public ControllerAction rebind(ControllerAction action, ControllerInput input) {
        return ControllerBindings.rebind(action, input, bindings);
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

    static ControllerAction controllerActionForJavaKey(String keyId) {
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

    private static void migrateControlifyDefaults(Properties props) {
        if (props.containsKey("binding.swapHands")) return;
        if (INSTANCE.binding(ControllerAction.SNEAK) == ControllerInput.B
            && INSTANCE.binding(ControllerAction.DROP) == ControllerInput.X
            && INSTANCE.binding(ControllerAction.PICK_BLOCK) == ControllerInput.RIGHT_THUMB) {
            INSTANCE.bindings[ControllerAction.SNEAK.ordinal()] = ControllerInput.RIGHT_THUMB;
            INSTANCE.bindings[ControllerAction.DROP.ordinal()] = ControllerInput.B;
            INSTANCE.bindings[ControllerAction.SWAP_HANDS.ordinal()] = ControllerInput.X;
            INSTANCE.bindings[ControllerAction.PICK_BLOCK.ordinal()] = ControllerInput.UNBOUND;
            return;
        }
        for (ControllerAction action : ControllerAction.values()) {
            if (action != ControllerAction.SWAP_HANDS && usesGameplayContext(action) && INSTANCE.binding(action) == ControllerInput.X) {
                INSTANCE.bindings[ControllerAction.SWAP_HANDS.ordinal()] = ControllerInput.UNBOUND;
                return;
            }
        }
    }

    private static boolean usesGameplayContext(ControllerAction action) {
        // ponytail: Java/mod key mappings are gameplay-scoped; add per-context metadata if Fabric exposes it.
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
        return "true".equalsIgnoreCase(value) || "1".equals(value) || "yes".equalsIgnoreCase(value) || "on".equalsIgnoreCase(value);
    }

    private static double number(Properties props, String key, double fallback) {
        String value = props.getProperty(key);
        if (value == null) {
            return fallback;
        }
        try {
            return Double.parseDouble(value.trim());
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
