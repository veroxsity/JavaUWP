package banditvault.neoforgecontroller.mixin;

import banditvault.neoforgecontroller.NeoForgeControllerCompat;
import net.minecraft.client.player.Input;
import net.minecraft.client.player.KeyboardInput;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

@Mixin(value = KeyboardInput.class, remap = false)
public abstract class NeoForgeControllerKeyboardInputMixin extends Input {
    @Inject(method = "tick", at = @At("TAIL"), remap = false)
    private void banditvault$applyAnalogMovement(boolean slowDown, float slowDownFactor, CallbackInfo ci) {
        float[] movement = NeoForgeControllerCompat.analogMovement();
        if (movement == null || (movement[0] == 0.0f && movement[1] == 0.0f)) {
            return;
        }

        float factor = slowDown ? slowDownFactor : 1.0f;
        leftImpulse = movement[0] * factor;
        forwardImpulse = movement[1] * factor;
        up = movement[1] > 0.0f;
        down = movement[1] < 0.0f;
        left = movement[0] > 0.0f;
        right = movement[0] < 0.0f;
    }
}
