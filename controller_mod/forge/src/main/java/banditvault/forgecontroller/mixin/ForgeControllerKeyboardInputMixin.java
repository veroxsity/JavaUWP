package banditvault.forgecontroller.mixin;

import banditvault.forgecontroller.ForgeControllerCompat;
import net.minecraft.client.player.Input;
import net.minecraft.client.player.KeyboardInput;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

@Mixin(value = KeyboardInput.class, remap = false)
public abstract class ForgeControllerKeyboardInputMixin extends Input {
    @Inject(method = "m_214106_", at = @At("TAIL"), remap = false)
    private void banditvault$applyAnalogMovement(boolean slowDown, float slowDownFactor, CallbackInfo ci) {
        float[] movement = ForgeControllerCompat.analogMovement();
        if (movement == null || (movement[0] == 0.0f && movement[1] == 0.0f)) {
            return;
        }

        float factor = slowDown ? slowDownFactor : 1.0f;
        // leftImpulse, forwardImpulse, up, down, left and right
        f_108566_ = movement[0] * factor;
        f_108567_ = movement[1] * factor;
        f_108568_ = movement[1] > 0.0f;
        f_108569_ = movement[1] < 0.0f;
        f_108570_ = movement[0] > 0.0f;
        f_108571_ = movement[0] < 0.0f;
    }
}
