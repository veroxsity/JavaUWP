package banditvault.fabriccontroller.mixin;

import net.minecraft.class_342;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.gen.Accessor;

@Mixin(class_342.class)
public interface BanditControllerTextFieldAccessor {
    @Accessor("field_2108")
    int banditvault$maxLength();

    @Accessor("field_2102")
    int banditvault$cursor();

    @Accessor("field_2101")
    int banditvault$selectionStart();
}
