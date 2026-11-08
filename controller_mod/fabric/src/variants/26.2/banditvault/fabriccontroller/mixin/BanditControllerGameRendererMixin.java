package banditvault.fabriccontroller.mixin;

import banditvault.fabriccontroller.BanditControllerCompat;
import net.minecraft.client.DeltaTracker;
import net.minecraft.client.Minecraft;
import net.minecraft.client.renderer.GameRenderer;
import org.spongepowered.asm.mixin.Final;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Shadow;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

@Mixin(GameRenderer.class)
public abstract class BanditControllerGameRendererMixin {
    @Shadow
    @Final
    private Minecraft minecraft;

    @Inject(method = "render", at = @At("HEAD"))
    private void banditvault$renderControllerLook(DeltaTracker tickCounter, boolean tick, CallbackInfo ci) {
        BanditControllerCompat.renderFrame(this.minecraft);
    }
}
