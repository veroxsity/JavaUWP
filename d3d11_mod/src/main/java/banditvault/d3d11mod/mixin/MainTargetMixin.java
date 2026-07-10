package banditvault.d3d11mod.mixin;

import com.mojang.blaze3d.pipeline.MainTarget;
import com.mojang.blaze3d.pipeline.RenderTarget;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Overwrite;

@Mixin(MainTarget.class)
public class MainTargetMixin extends RenderTarget {
    public MainTargetMixin(boolean useDepth) {
        super("Main", useDepth);
    }

    @Overwrite
    private void createFrameBuffer(int width, int height) {
        this.createBuffers(width, height);
    }

    @Override
    public void createBuffers(int width, int height) {
        super.createBuffers(width, height);
    }
}
