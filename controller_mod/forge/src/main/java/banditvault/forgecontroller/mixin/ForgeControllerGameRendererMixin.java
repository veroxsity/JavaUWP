package banditvault.forgecontroller.mixin;

import banditvault.forgecontroller.ForgeControllerCompat;
import net.minecraft.client.Minecraft;
import net.minecraft.client.renderer.GameRenderer;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

@Mixin(value = GameRenderer.class, remap = false)
public abstract class ForgeControllerGameRendererMixin {
    @Inject(method = "m_109093_", at = @At("HEAD"), remap = false)
    private void banditvault$renderControllerLook(float tickDelta, long startTime, boolean tick, CallbackInfo ci) {
        ForgeControllerCompat.markRenderFrameActive();
        ForgeControllerCompat.renderFrame(Minecraft.m_91087_());
    }
}
