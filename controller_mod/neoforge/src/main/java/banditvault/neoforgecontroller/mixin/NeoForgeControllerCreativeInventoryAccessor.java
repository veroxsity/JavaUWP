package banditvault.neoforgecontroller.mixin;

import net.minecraft.client.gui.screens.inventory.CreativeModeInventoryScreen;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.gen.Invoker;

@Mixin(CreativeModeInventoryScreen.class)
public interface NeoForgeControllerCreativeInventoryAccessor {
    @Invoker("refreshSearchResults")
    void banditvault$refreshSearchResults();
}
