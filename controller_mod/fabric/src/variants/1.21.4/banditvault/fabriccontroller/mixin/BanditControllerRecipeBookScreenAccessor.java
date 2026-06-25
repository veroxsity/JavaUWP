package banditvault.fabriccontroller.mixin;

import net.minecraft.class_10260;
import net.minecraft.class_507;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.gen.Accessor;

@Mixin(class_10260.class)
public interface BanditControllerRecipeBookScreenAccessor {
    @Accessor("field_54474")
    class_507 banditvault$recipeBook();
}
