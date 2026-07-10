package banditvault.d3d11mod.mixin;

import banditvault.d3d11mod.D3D11RenderSystem;
import com.mojang.blaze3d.TracyFrameCapture;
import com.mojang.blaze3d.platform.DisplayData;
import com.mojang.blaze3d.platform.ScreenManager;
import com.mojang.blaze3d.platform.Window;
import com.mojang.blaze3d.platform.WindowEventHandler;
import org.jetbrains.annotations.Nullable;
import org.lwjgl.glfw.GLFW;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Shadow;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.Redirect;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

@Mixin(Window.class)
public abstract class WindowMixin {
    @Shadow
    public abstract int getWidth();

    @Shadow
    public abstract int getHeight();

    @Redirect(method = "<init>", at = @At(value = "INVOKE", target = "Lorg/lwjgl/glfw/GLFW;glfwWindowHint(II)V"))
    private void ignoreVanillaWindowHint(int hint, int value) {
    }

    @Inject(method = "<init>", at = @At(value = "INVOKE", target = "Lorg/lwjgl/glfw/GLFW;glfwCreateWindow(IILjava/lang/CharSequence;JJ)J"))
    private void requestNoApiWindow(WindowEventHandler handler, ScreenManager screenManager, DisplayData displayData, String title, String preferredFullscreenVideoMode, CallbackInfo ci) {
        GLFW.glfwWindowHint(GLFW.GLFW_CLIENT_API, GLFW.GLFW_NO_API);
    }

    @Inject(method = "updateDisplay", at = @At("HEAD"))
    private void renderD3D11FirstPixel(@Nullable TracyFrameCapture tracyFrameCapture, CallbackInfo ci) {
        D3D11RenderSystem.renderTestFrame(this.getWidth(), this.getHeight());
    }
}
