package banditvault.neoforgecontroller.mixin;

import net.minecraft.client.gui.font.TextFieldHelper;
import net.minecraft.client.gui.screens.inventory.BookEditScreen;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.gen.Accessor;

@Mixin(BookEditScreen.class)
public interface NeoForgeControllerBookAccessor {
    @Accessor("isSigning")
    boolean banditvault$signing();

    @Accessor("pageEdit")
    TextFieldHelper banditvault$page();

    @Accessor("titleEdit")
    TextFieldHelper banditvault$title();
}
