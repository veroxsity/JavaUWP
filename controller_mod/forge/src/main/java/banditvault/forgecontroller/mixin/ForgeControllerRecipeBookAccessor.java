package banditvault.forgecontroller.mixin;

import java.util.List;
import net.minecraft.client.gui.components.EditBox;
import net.minecraft.client.gui.components.StateSwitchingButton;
import net.minecraft.client.gui.screens.recipebook.RecipeBookComponent;
import net.minecraft.client.gui.screens.recipebook.RecipeBookPage;
import net.minecraft.client.gui.screens.recipebook.RecipeBookTabButton;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.gen.Accessor;

@Mixin(value = RecipeBookComponent.class, remap = false)
public interface ForgeControllerRecipeBookAccessor {
    @Accessor("f_100281_")
    EditBox banditvault$searchBox();

    @Accessor("f_100270_")
    StateSwitchingButton banditvault$filterButton();

    @Accessor("f_100279_")
    List<RecipeBookTabButton> banditvault$tabButtons();

    @Accessor("f_100284_")
    RecipeBookPage banditvault$recipeBookPage();
}
