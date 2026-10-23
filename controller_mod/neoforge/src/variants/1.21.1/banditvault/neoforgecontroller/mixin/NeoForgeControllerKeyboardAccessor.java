package banditvault.neoforgecontroller.mixin;

import net.minecraft.client.KeyboardHandler;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.gen.Invoker;

@Mixin(KeyboardHandler.class)
public interface NeoForgeControllerKeyboardAccessor {
    @Invoker("keyPress")
    void banditvault$keyPress(long window, int key, int scan, int action, int modifiers);
}
