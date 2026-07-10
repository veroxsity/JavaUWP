package banditvault.d3d11mod.render;

import banditvault.d3d11mod.nativebridge.D3D11Native;
import com.mojang.blaze3d.textures.GpuTexture;
import com.mojang.blaze3d.textures.TextureFormat;

final class D3D11GpuTexture extends GpuTexture {
    private final long nativeHandle;
    private boolean closed;

    D3D11GpuTexture(int usage, String label, TextureFormat format, int width, int height, int layers, int mipLevels) {
        super(usage, label, format, width, height, layers, mipLevels);
        if (width <= 0 || height <= 0 || layers <= 0 || mipLevels <= 0) {
            throw new IllegalArgumentException("Invalid D3D11 first-pixel texture size: " + width + "x" + height + " layers=" + layers + " mips=" + mipLevels);
        }
        // Cubemaps (layers 6 + CUBEMAP_COMPATIBLE) get native backing; RTV/DSV views are still metadata-only.
        boolean cube = layers == 6 && (usage & USAGE_CUBEMAP_COMPATIBLE) != 0;
        this.nativeHandle = (layers == 1 || cube) && (format == TextureFormat.RGBA8 || format == TextureFormat.RED8)
            ? D3D11Native.createTexture(width, height, layers, cube, mipLevels, format.pixelSize())
            : 0L;
    }

    long nativeHandle() {
        return this.nativeHandle;
    }

    boolean hasNativeHandle() {
        return this.nativeHandle != 0L;
    }

    @Override
    public void close() {
        if (!this.closed) {
            D3D11Native.destroyTexture(this.nativeHandle);
        }
        this.closed = true;
    }

    @Override
    public boolean isClosed() {
        return this.closed;
    }
}
