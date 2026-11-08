package banditvault.fabriccontroller.mixin;

import banditvault.fabriccontroller.BanditControllerCompat;
import net.minecraft.class_310;
import net.minecraft.class_757;
import org.spongepowered.asm.mixin.Final;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Shadow;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

@Mixin(class_757.class)
public abstract class BanditControllerGameRendererMixin {
    @Shadow
    @Final
    private class_310 field_4015;

    @Inject(method = "method_3192", at = @At("HEAD"))
    private void banditvault$renderControllerLook(float tickDelta, long startTime, boolean tick, CallbackInfo ci) {
        BanditControllerCompat.renderFrame(this.field_4015);
    }
}
