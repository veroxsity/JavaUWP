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

    // The toast API changed name across versions and 26.x builds the GUI before this
    // method, so each version matches one of these. The queue empties on the first draw.
    @Inject(method = "render", at = {
        @At(value = "INVOKE", target = "Lnet/minecraft/client/gui/components/toasts/ToastComponent;render(Lnet/minecraft/client/gui/GuiGraphics;)V", shift = At.Shift.AFTER),
        @At(value = "INVOKE", target = "Lnet/minecraft/client/gui/components/toasts/ToastManager;render(Lnet/minecraft/client/gui/GuiGraphics;)V", shift = At.Shift.AFTER),
        @At(value = "INVOKE", target = "Lnet/minecraft/client/gui/render/GuiRenderer;render")
    }, remap = false)
    private void banditvault$renderControllerCursor(DeltaTracker deltaTracker, boolean tick, CallbackInfo ci) {
        NeoForgeControllerCompat.renderQueuedCursor();
    }
}
