package com.onyx.controller.babric;

import net.fabricmc.api.ModInitializer;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

public final class OnyxXboxController implements ModInitializer {
    public static final Logger LOGGER = LoggerFactory.getLogger("onyx-xbox-controller");

    @Override
    public void onInitialize() {
        LOGGER.info("Onyx Xbox Controller loaded for Babric / Minecraft Beta 1.7.3.");
        LOGGER.info("Gamepad input is polled through the host-provided GLFW 3 bridge.");
    }
}
