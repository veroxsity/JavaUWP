package com.onyx.controller.babric;

import java.util.logging.Logger;
import net.fabricmc.api.ModInitializer;

public final class OnyxXboxController implements ModInitializer {
    public static final Logger LOGGER = Logger.getLogger("OnyxXboxController");

    @Override
    public void onInitialize() {
        LOGGER.info("Onyx Xbox Controller loaded for Babric / Minecraft Beta 1.7.3.");
        LOGGER.info("Gamepad input is polled through the host-provided GLFW 3 bridge.");
    }
}
