package banditvault.fabriccontroller.mixin;

import banditvault.fabriccontroller.BanditControllerCompat;
import net.minecraft.client.gui.GuiGraphicsExtractor;
import net.minecraft.client.gui.screens.Screen;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.ModifyVariable;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

@Mixin(Screen.class)
public abstract class BanditControllerScreenMixin {
    @Inject(method = "extractRenderState", at = @At("HEAD"))
    private void banditvault$updateControllerCursor(GuiGraphicsExtractor context, int mouseX, int mouseY, float delta, CallbackInfo ci) {
        BanditControllerCompat.updateScreenCursorBeforeRender((Screen)(Object)this, mouseX, mouseY);
    }

    @ModifyVariable(method = "extractRenderState", at = @At("HEAD"), ordinal = 0, argsOnly = true)
    private int banditvault$useControllerMouseX(int mouseX) {
        return BanditControllerCompat.screenMouseX((Screen)(Object)this, mouseX);
    }

    @ModifyVariable(method = "extractRenderState", at = @At("HEAD"), ordinal = 1, argsOnly = true)
    private int banditvault$useControllerMouseY(int mouseY) {
        return BanditControllerCompat.screenMouseY((Screen)(Object)this, mouseY);
    }

    @Inject(method = "extractRenderState", at = @At("TAIL"))
    private void banditvault$renderControllerCursor(GuiGraphicsExtractor context, int mouseX, int mouseY, float delta, CallbackInfo ci) {
        Screen screen = (Screen)(Object)this;
        if (BanditControllerCompat.shouldRenderCursorInBaseScreen(screen)) {
            BanditControllerCompat.renderCursor(screen, context);
        }
    }
}
