package banditvault.forgecontroller.mixin;

import net.minecraft.client.gui.font.TextFieldHelper;
import net.minecraft.client.gui.screens.inventory.AbstractSignEditScreen;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.gen.Accessor;

@Mixin(AbstractSignEditScreen.class)
public interface ForgeControllerSignEditScreenAccessor {
    @Accessor("f_243993_")
    TextFieldHelper banditvault$selectionManager();
}
