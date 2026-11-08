package banditvault.fabriccontroller.mixin;

import net.minecraft.client.gui.components.MultilineTextField;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.gen.Accessor;

@Mixin(MultilineTextField.class)
public interface BanditControllerTextModelAccessor {
    @Accessor("cursor")
    int banditvault$cursor();

    @Accessor("selectCursor")
    int banditvault$selectionStart();
}
