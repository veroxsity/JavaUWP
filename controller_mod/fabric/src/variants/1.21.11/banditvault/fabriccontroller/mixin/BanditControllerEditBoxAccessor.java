package banditvault.fabriccontroller.mixin;

import net.minecraft.class_7529;
import net.minecraft.class_7530;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.gen.Accessor;

@Mixin(class_7529.class)
public interface BanditControllerEditBoxAccessor {
    @Accessor("field_39509")
    class_7530 banditvault$textModel();
}
