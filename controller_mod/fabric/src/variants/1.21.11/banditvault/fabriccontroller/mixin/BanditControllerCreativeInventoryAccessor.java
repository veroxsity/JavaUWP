package banditvault.fabriccontroller.mixin;

import net.minecraft.class_481;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.gen.Invoker;

@Mixin(class_481.class)
public interface BanditControllerCreativeInventoryAccessor {
    @Invoker("method_2464")
    void banditvault$refreshSearchResults();
}
