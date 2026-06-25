package banditvault.forgecontroller.mixin;

import java.util.List;
import net.minecraft.client.gui.components.StateSwitchingButton;
import net.minecraft.client.gui.screens.recipebook.OverlayRecipeComponent;
import net.minecraft.client.gui.screens.recipebook.RecipeBookPage;
import net.minecraft.client.gui.screens.recipebook.RecipeButton;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.gen.Accessor;

@Mixin(value = RecipeBookPage.class, remap = false)
public interface ForgeControllerRecipeBookPageAccessor {
    @Accessor("f_100394_")
    List<RecipeButton> banditvault$buttons();

    @Accessor("f_100400_")
    StateSwitchingButton banditvault$forwardButton();

    @Accessor("f_100401_")
    StateSwitchingButton banditvault$backButton();

    @Accessor("f_100396_")
    OverlayRecipeComponent banditvault$overlay();
}
