package banditvault.forgecontroller.mixin;

import net.minecraft.client.gui.components.MultiLineEditBox;
import net.minecraft.client.gui.components.MultilineTextField;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.gen.Accessor;

@Mixin(MultiLineEditBox.class)
public interface ForgeControllerEditBoxAccessor {
    @Accessor("f_238540_")
    MultilineTextField banditvault$textModel();
}
