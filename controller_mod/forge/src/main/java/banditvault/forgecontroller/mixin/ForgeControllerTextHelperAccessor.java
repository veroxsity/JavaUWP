package banditvault.forgecontroller.mixin;

import java.util.function.Supplier;
import net.minecraft.client.gui.font.TextFieldHelper;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.gen.Accessor;

@Mixin(TextFieldHelper.class)
public interface ForgeControllerTextHelperAccessor {
    @Accessor("f_95129_")
    Supplier<String> banditvault$text();
}
