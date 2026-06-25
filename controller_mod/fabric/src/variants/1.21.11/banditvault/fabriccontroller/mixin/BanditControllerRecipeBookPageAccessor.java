package banditvault.fabriccontroller.mixin;

import java.util.List;
import net.minecraft.class_344;
import net.minecraft.class_508;
import net.minecraft.class_513;
import net.minecraft.class_514;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.gen.Accessor;

@Mixin(class_513.class)
public interface BanditControllerRecipeBookPageAccessor {
    @Accessor("field_3131")
    List<class_514> banditvault$buttons();

    @Accessor("field_3128")
    class_344 banditvault$backButton();

    @Accessor("field_3130")
    class_344 banditvault$forwardButton();

    @Accessor("field_3132")
    class_508 banditvault$overlay();
}
