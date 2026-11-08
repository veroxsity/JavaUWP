package banditvault.neoforgecontroller.mixin;

import banditvault.neoforgecontroller.NeoForgeControllerCompat;
import net.minecraft.client.DeltaTracker;
import net.minecraft.client.Minecraft;
import net.minecraft.client.renderer.GameRenderer;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

@Mixin(value = GameRenderer.class, remap = false)
public abstract class NeoForgeControllerGameRendererMixin {
    @Inject(method = "render", at = @At("HEAD"), remap = false)
    private void banditvault$renderControllerLook(DeltaTracker deltaTracker, boolean tick, CallbackInfo ci) {
        NeoForgeControllerCompat.markRenderFrameActive();
        NeoForgeControllerCompat.renderFrame(Minecraft.getInstance());
    }
}
