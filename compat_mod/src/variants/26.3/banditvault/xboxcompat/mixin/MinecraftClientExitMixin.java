package banditvault.xboxcompat.mixin;

import banditvault.xboxcompat.ReturnToLauncherSignal;
import banditvault.xboxcompat.XboxCompatLog;
import net.minecraft.client.Minecraft;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

@Mixin(Minecraft.class)
public abstract class MinecraftClientExitMixin {
    @Inject(method = "run", at = @At("TAIL"))
    private void banditvault$returnToLauncherWhenMainLoopExits(CallbackInfo ci) {
        XboxCompatLog.log("MinecraftClient main loop exited, signaling launcher return");
        throw new ReturnToLauncherSignal();
    }
}
