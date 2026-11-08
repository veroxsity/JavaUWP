package banditvault.fabriccontroller.mixin;

import net.minecraft.client.gui.screens.inventory.AbstractRecipeBookScreen;
import net.minecraft.client.gui.screens.recipebook.RecipeBookComponent;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.gen.Accessor;

@Mixin(AbstractRecipeBookScreen.class)
public interface BanditControllerRecipeBookScreenAccessor {
    @Accessor("recipeBookComponent")
    RecipeBookComponent banditvault$recipeBook();
}
