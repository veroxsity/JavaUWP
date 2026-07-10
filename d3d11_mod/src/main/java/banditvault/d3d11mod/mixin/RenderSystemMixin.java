package banditvault.d3d11mod.mixin;

import banditvault.d3d11mod.D3D11RenderSystem;
import banditvault.d3d11mod.render.D3D11GpuDevice;
import com.mojang.blaze3d.shaders.ShaderSource;
import com.mojang.blaze3d.systems.GpuDevice;
import com.mojang.blaze3d.systems.RenderSystem;
import com.mojang.blaze3d.systems.SamplerCache;
import net.minecraft.client.renderer.DynamicUniforms;
import org.jetbrains.annotations.Nullable;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Overwrite;
import org.spongepowered.asm.mixin.Shadow;

@Mixin(RenderSystem.class)
public abstract class RenderSystemMixin {
    @Shadow
    private static @Nullable Thread renderThread;
    @Shadow
    private static @Nullable GpuDevice DEVICE;
    @Shadow
    private static @Nullable DynamicUniforms dynamicUniforms;
    @Shadow
    private static SamplerCache samplerCache;
    @Shadow
    private static String apiDescription;

    @Overwrite(remap = false)
    public static void initRenderer(long window, int debugVerbosity, boolean sync, ShaderSource shaderSource, boolean debugLabels) {
        if (renderThread != null) {
            renderThread.setPriority(Thread.NORM_PRIORITY + 2);
        }

        D3D11RenderSystem.initRenderer();
        DEVICE = new D3D11GpuDevice(window, debugVerbosity, sync, shaderSource, debugLabels);
        apiDescription = RenderSystem.getDevice().getImplementationInformation();
        dynamicUniforms = new DynamicUniforms();
        samplerCache.initialize();
    }
}
