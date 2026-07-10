package banditvault.d3d11mod.render;

import com.mojang.blaze3d.textures.GpuTexture;
import com.mojang.blaze3d.textures.GpuTextureView;

final class D3D11GpuTextureView extends GpuTextureView {
    private boolean closed;

    D3D11GpuTextureView(GpuTexture texture, int baseMipLevel, int mipLevels) {
        super(texture, baseMipLevel, mipLevels);
        if (baseMipLevel < 0 || mipLevels <= 0 || baseMipLevel + mipLevels > texture.getMipLevels()) {
            throw new IllegalArgumentException("Invalid D3D11 first-pixel texture view mips: base=" + baseMipLevel + " levels=" + mipLevels);
        }
    }

    @Override
    public void close() {
        this.closed = true;
    }

    @Override
    public boolean isClosed() {
        return this.closed;
    }
}
