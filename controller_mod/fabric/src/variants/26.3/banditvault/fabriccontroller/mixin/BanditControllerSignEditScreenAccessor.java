package banditvault.fabriccontroller.mixin;

import net.minecraft.client.gui.font.TextFieldHelper;
import net.minecraft.client.gui.screens.inventory.AbstractSignEditScreen;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.gen.Accessor;
import org.spongepowered.asm.mixin.gen.Invoker;

@Mixin(AbstractSignEditScreen.class)
public interface BanditControllerSignEditScreenAccessor {
    @Accessor("messages")
    String[] banditvault$lines();

    @Accessor("line")
    int banditvault$currentLine();

    @Accessor("signField")
    TextFieldHelper banditvault$selectionManager();

    @Invoker("setMessage")
    void banditvault$setCurrentLine(String text);

    @Invoker("lambda$init$2")
    boolean banditvault$acceptsLine(String text);
}
