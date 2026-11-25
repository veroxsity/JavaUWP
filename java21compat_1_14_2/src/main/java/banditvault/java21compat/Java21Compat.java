package banditvault.java21compat;

import net.fabricmc.api.ClientModInitializer;
import org.lwjgl.Version;

import java.util.logging.Logger;

/**
 * Java 21 diagnostics/bootstrap for the JavaUWP Minecraft 1.14.2 profile.
 * This class intentionally avoids changing JVM-wide behavior after startup.
 */
public final class Java21Compat implements ClientModInitializer {
    private static final Logger LOGGER = Logger.getLogger("Java21Compat/1.14.2");

    @Override
    public void onInitializeClient() {
        int javaMajor = getJavaMajorVersion();
        LOGGER.info("Java21Compat 1.14.2 initialized");
        LOGGER.info("Runtime: " + System.getProperty("java.runtime.version", System.getProperty("java.version")));
        LOGGER.info("VM: " + System.getProperty("java.vm.name"));
        LOGGER.info("OS/architecture: " + System.getProperty("os.name") + " / " + System.getProperty("os.arch"));

        if (javaMajor < 21) {
            LOGGER.warning("This JavaUWP profile expects Java 21 or newer; detected Java " + javaMajor);
        } else {
            LOGGER.info("Java 21+ runtime detected");
        }

        try {
            LOGGER.info("LWJGL: " + Version.getVersion());
        } catch (Throwable error) {
            LOGGER.warning("LWJGL version probe unavailable: " + error.getClass().getSimpleName());
        }

        LOGGER.info("This build currently performs runtime diagnostics only; it does not replace GLFW/LWJGL native binaries or patch JVM startup options.");
    }

    private static int getJavaMajorVersion() {
        String version = System.getProperty("java.specification.version", "0");
        try {
            if (version.startsWith("1.")) {
                return Integer.parseInt(version.substring(2));
            }
            int dot = version.indexOf('.');
            return Integer.parseInt(dot >= 0 ? version.substring(0, dot) : version);
        } catch (NumberFormatException ignored) {
            return 0;
        }
    }
}
