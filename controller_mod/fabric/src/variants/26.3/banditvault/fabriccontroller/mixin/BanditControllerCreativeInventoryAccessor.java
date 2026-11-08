package banditvault.fabriccontroller.mixin;

import net.minecraft.client.gui.screens.inventory.CreativeModeInventoryScreen;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.gen.Invoker;

@Mixin(CreativeModeInventoryScreen.class)
public interface BanditControllerCreativeInventoryAccessor {
    @Invoker("refreshSearchResults")
    void banditvault$refreshSearchResults();
}
