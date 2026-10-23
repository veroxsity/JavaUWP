package banditvault.neoforgecontroller.mixin;

import net.minecraft.client.KeyboardHandler;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.gen.Invoker;

@Mixin(KeyboardHandler.class)
public interface NeoForgeControllerKeyboardAccessor {
    @Invoker("keyPress")
    void banditvault$keyEvent(long window, int action, net.minecraft.client.input.KeyEvent event);
}
