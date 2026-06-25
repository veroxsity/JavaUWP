package banditvault.fabriccontroller.mixin;

import java.util.List;
import net.minecraft.class_342;
import net.minecraft.class_507;
import net.minecraft.class_512;
import net.minecraft.class_513;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.gen.Accessor;

@Mixin(class_507.class)
public interface BanditControllerRecipeBookAccessor {
    @Accessor("field_3089")
    class_342 banditvault$searchBox();

    @Accessor("field_3094")
    List<class_512> banditvault$tabButtons();

    @Accessor("field_3086")
    class_513 banditvault$recipeBookPage();
}
