package banditvault.d3d11mod;

import net.fabricmc.api.ClientModInitializer;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

public final class D3D11ModInitializer implements ClientModInitializer {
    public static final Logger LOGGER = LoggerFactory.getLogger("BanditD3D11Mod");

    @Override
    public void onInitializeClient() {
        LOGGER.warn("Bandit D3D11Mod first-pixel backend is enabled. Vanilla rendering is not implemented yet.");
    }
}
