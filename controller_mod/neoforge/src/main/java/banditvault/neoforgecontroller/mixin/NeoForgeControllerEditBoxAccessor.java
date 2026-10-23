package banditvault.neoforgecontroller.mixin;

import net.minecraft.client.gui.components.MultiLineEditBox;
import net.minecraft.client.gui.components.MultilineTextField;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.gen.Accessor;

@Mixin(MultiLineEditBox.class)
public interface NeoForgeControllerEditBoxAccessor {
    @Accessor("textField")
    MultilineTextField banditvault$textModel();
}
