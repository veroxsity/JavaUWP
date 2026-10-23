package banditvault.forgecontroller.mixin;

import net.minecraft.client.gui.components.EditBox;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.gen.Accessor;

@Mixin(EditBox.class)
public interface ForgeControllerTextFieldAccessor {
    @Accessor("f_94094_")
    int banditvault$maxLength();

    @Accessor("f_94101_")
    int banditvault$cursor();

    @Accessor("f_94102_")
    int banditvault$selectionStart();
}
