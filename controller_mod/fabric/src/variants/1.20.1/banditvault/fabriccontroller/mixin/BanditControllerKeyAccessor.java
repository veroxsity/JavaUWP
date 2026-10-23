package banditvault.fabriccontroller.mixin;

import net.minecraft.class_304;
import net.minecraft.class_3675;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.gen.Accessor;

@Mixin(class_304.class)
public interface BanditControllerKeyAccessor {
    @Accessor("field_1655")
    class_3675.class_306 banditvault$currentKey();
}
