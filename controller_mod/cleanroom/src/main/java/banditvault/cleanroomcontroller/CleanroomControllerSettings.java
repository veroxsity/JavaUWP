package banditvault.cleanroomcontroller;

import banditvault.controllercore.ControllerSettingsStore;

import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.io.OutputStream;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.StandardCopyOption;
import java.util.Properties;

// same file as the fabric, forge and neoforge layers, so settings follow the profile across loaders
final class CleanroomControllerSettings extends ControllerSettingsStore {
    private static final CleanroomControllerSettings INSTANCE = new CleanroomControllerSettings();
    private static volatile boolean loaded;

    private CleanroomControllerSettings() {
    }

    static CleanroomControllerSettings get() {
        if (!loaded) {
            load();
        }
        return INSTANCE;
    }

    private static synchronized void load() {
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
            CleanroomControllerLog.logException("Cleanroom controller settings failed to load", e);
            loaded = true;
            return;
        }

        INSTANCE.readFrom(props);
        loaded = true;
    }

    static void save() {
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
            try (OutputStream out = Files.newOutputStream(temporary)) {
                props.store(out, "Bandit controller compatibility settings");
            }
            Files.move(temporary, file.toPath(), StandardCopyOption.ATOMIC_MOVE, StandardCopyOption.REPLACE_EXISTING);
        } catch (IOException e) {
            CleanroomControllerLog.logException("Cleanroom controller settings failed to save", e);
        } finally {
            if (temporary != null) {
                try { Files.deleteIfExists(temporary); } catch (IOException ignored) { }
            }
        }
    }
}
