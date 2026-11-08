package banditvault.fabriccontroller.mixin;

import java.util.List;
import net.minecraft.client.gui.components.EditBox;
import net.minecraft.client.gui.screens.recipebook.RecipeBookComponent;
import net.minecraft.client.gui.screens.recipebook.RecipeBookTabButton;
import net.minecraft.client.gui.screens.recipebook.RecipeBookPage;
import net.minecraft.client.gui.components.CycleButton;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.gen.Accessor;

@Mixin(RecipeBookComponent.class)
public interface BanditControllerRecipeBookAccessor {
    @Accessor("searchBox")
    EditBox banditvault$searchBox();

    @Accessor("filterButton")
    CycleButton<?> banditvault$filterButton();

    @Accessor("tabButtons")
    List<RecipeBookTabButton> banditvault$tabButtons();

    @Accessor("recipeBookPage")
    RecipeBookPage banditvault$recipeBookPage();
}
