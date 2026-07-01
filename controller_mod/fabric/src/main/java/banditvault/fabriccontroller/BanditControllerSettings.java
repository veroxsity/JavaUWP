package banditvault.fabriccontroller;

import banditvault.controllercore.ControllerAction;
import banditvault.controllercore.ControllerBindings;
import banditvault.controllercore.ControllerInput;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.util.Properties;

public final class BanditControllerSettings {
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

    public ControllerAction conflictFor(ControllerAction action, ControllerInput input) {
        return ControllerBindings.conflict(action, input, bindings);
    }

    public void resetBindings() {
        ControllerBindings.resetDefaults(bindings);
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
