package banditvault.fabriccontroller.mixin;

import banditvault.fabriccontroller.BanditControllerCompat;
import net.minecraft.class_10185;
import net.minecraft.class_241;
import net.minecraft.class_743;
import net.minecraft.class_744;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

@Mixin(class_743.class)
public abstract class BanditControllerKeyboardInputMixin extends class_744 {
    @Inject(method = "method_3129", at = @At("TAIL"))
    private void banditvault$applyAnalogMovement(CallbackInfo ci) {
        float[] movement = BanditControllerCompat.analogMovement();
        if (movement == null || (movement[0] == 0.0f && movement[1] == 0.0f)) {
            return;
        }

        field_55868 = new class_241(movement[0], movement[1]);
        field_54155 = new class_10185(
            movement[1] > 0.0f,
            movement[1] < 0.0f,
            movement[0] > 0.0f,
            movement[0] < 0.0f,
            field_54155.comp_3163(),
            field_54155.comp_3164(),
            field_54155.comp_3165());
    }
}
