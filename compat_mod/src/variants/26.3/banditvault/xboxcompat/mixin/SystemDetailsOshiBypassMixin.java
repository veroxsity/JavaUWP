package banditvault.xboxcompat.mixin;

import banditvault.xboxcompat.XboxCompatLog;
import net.minecraft.SystemReport;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;
import oshi.SystemInfo;

@Mixin(SystemReport.class)
public abstract class SystemDetailsOshiBypassMixin {
    @Inject(method = "putHardware", at = @At("HEAD"), cancellable = true, require = 0)
    private void banditvault$skipHardwareDetails(SystemInfo systemInfo, CallbackInfo ci) {
        XboxCompatLog.log("Skipping OSHI hardware details in Xbox sandbox");
        ci.cancel();
    }
}
