package banditvault.fabriccontroller.mixin;

import net.minecraft.class_3728;
import net.minecraft.class_7743;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.gen.Accessor;
import org.spongepowered.asm.mixin.gen.Invoker;

@Mixin(class_7743.class)
public interface BanditControllerSignEditScreenAccessor {
    @Accessor("field_40425")
    String[] banditvault$lines();

    @Accessor("field_40428")
    int banditvault$currentLine();

    @Accessor("field_40429")
    class_3728 banditvault$selectionManager();

    @Invoker("method_49913")
    void banditvault$setCurrentLine(String text);

    @Invoker("method_45658")
    boolean banditvault$acceptsLine(String text);
}
