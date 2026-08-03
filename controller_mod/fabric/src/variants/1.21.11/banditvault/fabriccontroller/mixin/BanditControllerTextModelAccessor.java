package banditvault.fabriccontroller.mixin;

import net.minecraft.class_7530;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.gen.Accessor;

@Mixin(class_7530.class)
public interface BanditControllerTextModelAccessor {
    @Accessor("field_39516")
    int banditvault$cursor();

    @Accessor("field_39517")
    int banditvault$selectionStart();
}
