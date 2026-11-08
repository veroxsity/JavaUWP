package banditvault.fabriccontroller.mixin;

import net.minecraft.client.KeyMapping;
import com.mojang.blaze3d.platform.InputConstants;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.gen.Accessor;

@Mixin(KeyMapping.class)
public interface BanditControllerKeyAccessor {
    @Accessor("key")
    InputConstants.Key banditvault$currentKey();
}
