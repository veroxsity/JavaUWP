package banditvault.fabriccontroller.mixin;

import net.minecraft.client.gui.components.EditBox;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.gen.Accessor;

@Mixin(EditBox.class)
public interface BanditControllerTextFieldAccessor {
    @Accessor("maxLength")
    int banditvault$maxLength();

    @Accessor("cursorPos")
    int banditvault$cursor();

    @Accessor("highlightPos")
    int banditvault$selectionStart();
}
