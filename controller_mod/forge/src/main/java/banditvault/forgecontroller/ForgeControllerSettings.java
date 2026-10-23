package banditvault.forgecontroller;

import banditvault.controllercore.ControllerSettingsStore;

import java.io.File;
import java.io.FileInputStream;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.StandardCopyOption;
import java.io.IOException;
import java.util.Properties;

public final class ForgeControllerSettings extends ControllerSettingsStore {
    private static final ForgeControllerSettings INSTANCE = new ForgeControllerSettings();
    private static volatile boolean loaded;

    private ForgeControllerSettings() {
    }

    public static ForgeControllerSettings get() {
        if (!loaded) {
            load();
        }
        return INSTANCE;
    }

    public static synchronized void load() {
        if (loaded) {
            return;
        }
        File file = configFile();
        if (!file.isFile()) {
            save();
            loaded = true;
            return;
        }

        Properties props = new Properties();
        try (FileInputStream in = new FileInputStream(file)) {
            props.load(in);
        } catch (IOException | IllegalArgumentException e) {
            ForgeControllerLog.logException("Forge controller settings failed to load", e);
            loaded = true;
            return;
        }

        INSTANCE.readFrom(props);
        loaded = true;
    }

    public static void save() {
        File file = configFile();
        File dir = file.getParentFile();
        if (dir != null) {
            dir.mkdirs();
        }

        Properties props = new Properties();
        INSTANCE.writeTo(props);

        Path temporary = null;
        try {
            temporary = Files.createTempFile(file.toPath().getParent(), "bandit-controller-", ".tmp");
            try (java.io.OutputStream out = Files.newOutputStream(temporary)) {
                props.store(out, "Bandit controller compatibility settings");
            }
            Files.move(temporary, file.toPath(), StandardCopyOption.ATOMIC_MOVE, StandardCopyOption.REPLACE_EXISTING);
        } catch (IOException e) {
            ForgeControllerLog.logException("Forge controller settings failed to save", e);
        } finally {
            if (temporary != null) {
                try { Files.deleteIfExists(temporary); } catch (IOException ignored) { }
            }
        }
    }
}
