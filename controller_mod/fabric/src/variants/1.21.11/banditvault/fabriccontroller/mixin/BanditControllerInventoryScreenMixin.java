package banditvault.fabriccontroller.mixin;

import banditvault.fabriccontroller.BanditControllerCompat;
import net.minecraft.class_332;
import net.minecraft.class_437;
import net.minecraft.class_490;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.ModifyVariable;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

@Mixin(class_490.class)
public abstract class BanditControllerInventoryScreenMixin {
    @Inject(method = "method_25394", at = @At("HEAD"))
    private void banditvault$updateControllerCursor(class_332 context, int mouseX, int mouseY, float delta, CallbackInfo ci) {
        BanditControllerCompat.updateScreenCursorBeforeRender((class_437)(Object)this, mouseX, mouseY);
    }

    @ModifyVariable(method = "method_25394", at = @At("HEAD"), ordinal = 0, argsOnly = true)
    private int banditvault$useControllerMouseX(int mouseX) {
        return BanditControllerCompat.screenMouseX((class_437)(Object)this, mouseX);
    }

    @ModifyVariable(method = "method_25394", at = @At("HEAD"), ordinal = 1, argsOnly = true)
    private int banditvault$useControllerMouseY(int mouseY) {
        return BanditControllerCompat.screenMouseY((class_437)(Object)this, mouseY);
    }
}
