package banditvault.cleanroomcontroller;

import net.minecraftforge.common.MinecraftForge;
import net.minecraftforge.fml.common.Mod;
import net.minecraftforge.fml.common.event.FMLInitializationEvent;

@Mod(
    modid = CleanroomControllerMod.MOD_ID,
    name = "Bandit Controller",
    version = "1.0.0",
    clientSideOnly = true,
    acceptableRemoteVersions = "*",
    acceptedMinecraftVersions = "[1.12.2]")
public final class CleanroomControllerMod {
    public static final String MOD_ID = "banditvault_controller";

    @Mod.EventHandler
    public void init(FMLInitializationEvent event) {
        CleanroomControllerSettings.get();
        MinecraftForge.EVENT_BUS.register(new CleanroomControllerCompat());
        CleanroomControllerLog.log("Cleanroom controller registered");
    }
}
