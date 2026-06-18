package banditvault.xboxcompat.mixin;

import banditvault.xboxcompat.BanditMouseCursorOverlay;
import net.minecraft.class_332;
import net.minecraft.class_437;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

@Mixin(class_437.class)
public abstract class BanditMouseCursorScreenMixin {
    @Inject(method = "method_25394", at = @At("TAIL"))
    private void banditvault$renderMouseCursor(class_332 context, int mouseX, int mouseY, float delta, CallbackInfo ci) {
        BanditMouseCursorOverlay.render((class_437)(Object)this, context, mouseX, mouseY);
    }
}
