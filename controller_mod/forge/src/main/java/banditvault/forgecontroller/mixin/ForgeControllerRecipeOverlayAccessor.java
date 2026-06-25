package banditvault.forgecontroller.mixin;

import java.util.List;
import net.minecraft.client.gui.components.AbstractWidget;
import net.minecraft.client.gui.screens.recipebook.OverlayRecipeComponent;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.gen.Accessor;

@Mixin(value = OverlayRecipeComponent.class, remap = false)
public interface ForgeControllerRecipeOverlayAccessor {
    @Accessor("f_100173_")
    List<AbstractWidget> banditvault$recipeButtons();
}
