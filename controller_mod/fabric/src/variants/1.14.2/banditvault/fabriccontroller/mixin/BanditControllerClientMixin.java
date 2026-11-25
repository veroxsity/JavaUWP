package banditvault.fabriccontroller.mixin;

import banditvault.fabriccontroller.BanditControllerCompat;
import net.minecraft.class_310;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

@Mixin(class_310.class)
public abstract class BanditControllerClientMixin {
    @Inject(method = "method_1574", at = @At("TAIL"))
    private void banditvault$tickLegacyController(CallbackInfo ci) {
        BanditControllerCompat.tick((class_310)(Object)this);
    }
}
