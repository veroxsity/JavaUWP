package banditvault.xboxcompat.mixin;

import banditvault.xboxcompat.XboxCompatLog;
import net.minecraft.class_310;
import net.minecraft.class_442;
import net.minecraft.class_11228;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Unique;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

@Mixin(class_11228.class)
public abstract class TitleScreenSpecialGuiBypassMixin {
    @Unique
    private static boolean banditvault$loggedTitleBypass = false;

    @Inject(method = "method_70893", at = @At("HEAD"), cancellable = true)
    private void banditvault$skipTitleSpecialGuiElements(CallbackInfo ci) {
        class_310 client = class_310.method_1551();
        if (client == null || !(client.field_1755 instanceof class_442)) {
            return;
        }

        if (!banditvault$loggedTitleBypass) {
            XboxCompatLog.log("Bypassing title-screen special GUI elements");
            banditvault$loggedTitleBypass = true;
        }
        ci.cancel();
    }
}
