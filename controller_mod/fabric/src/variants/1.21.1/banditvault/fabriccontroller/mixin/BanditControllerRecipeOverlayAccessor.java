package banditvault.fabriccontroller.mixin;

import java.util.List;
import net.minecraft.class_339;
import net.minecraft.class_508;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.gen.Accessor;

@Mixin(class_508.class)
public interface BanditControllerRecipeOverlayAccessor {
    @Accessor("field_3106")
    List<class_339> banditvault$recipeButtons();
}
