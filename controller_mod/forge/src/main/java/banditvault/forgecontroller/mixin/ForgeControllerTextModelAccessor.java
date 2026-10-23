package banditvault.forgecontroller.mixin;

import net.minecraft.client.gui.components.MultilineTextField;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.gen.Accessor;

@Mixin(MultilineTextField.class)
public interface ForgeControllerTextModelAccessor {
    @Accessor("f_238566_")
    int banditvault$cursor();

    @Accessor("f_238550_")
    int banditvault$selectionStart();
}
