package banditvault.fabriccontroller.mixin;

import java.util.List;
import net.minecraft.client.gui.components.AbstractWidget;
import net.minecraft.client.gui.screens.recipebook.OverlayRecipeComponent;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.gen.Accessor;

@Mixin(OverlayRecipeComponent.class)
public interface BanditControllerRecipeOverlayAccessor {
    @Accessor("recipeButtons")
    List<AbstractWidget> banditvault$recipeButtons();
}
