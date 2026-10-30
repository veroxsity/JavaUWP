package banditvault.neoforgecontroller.mixin;

import banditvault.neoforgecontroller.NeoForgeControllerCompat;
import net.minecraft.client.player.ClientInput;
import net.minecraft.client.player.KeyboardInput;
import net.minecraft.world.entity.player.Input;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

@Mixin(value = KeyboardInput.class, remap = false)
public abstract class NeoForgeControllerKeyboardInputMixin extends ClientInput {
    @Inject(method = "tick", at = @At("TAIL"), remap = false)
    private void banditvault$applyAnalogMovement(CallbackInfo ci) {
        float[] movement = NeoForgeControllerCompat.analogMovement();
        if (movement == null || (movement[0] == 0.0f && movement[1] == 0.0f)) {
            return;
        }

        leftImpulse = movement[0];
        forwardImpulse = movement[1];
        keyPresses = new Input(
            movement[1] > 0.0f,
            movement[1] < 0.0f,
            movement[0] > 0.0f,
            movement[0] < 0.0f,
            keyPresses.jump(),
            keyPresses.shift(),
            keyPresses.sprint());
    }
}
