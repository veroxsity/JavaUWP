package banditvault.d3d11mod.render;

import banditvault.d3d11mod.D3D11RenderSystem;
import banditvault.d3d11mod.nativebridge.D3D11Native;
import com.mojang.blaze3d.buffers.GpuBuffer;
import com.mojang.blaze3d.pipeline.CompiledRenderPipeline;
import com.mojang.blaze3d.pipeline.RenderPipeline;
import com.mojang.blaze3d.shaders.ShaderSource;
import com.mojang.blaze3d.systems.CommandEncoder;
import com.mojang.blaze3d.systems.GpuDevice;
import com.mojang.blaze3d.textures.AddressMode;
import com.mojang.blaze3d.textures.FilterMode;
import com.mojang.blaze3d.textures.GpuSampler;
import com.mojang.blaze3d.textures.GpuTexture;
import com.mojang.blaze3d.textures.GpuTextureView;
import com.mojang.blaze3d.textures.TextureFormat;
import org.jetbrains.annotations.Nullable;

import java.nio.ByteBuffer;
import java.util.Collections;
import java.util.List;
import java.util.OptionalDouble;
import java.util.function.Supplier;

@SuppressWarnings("NullableProblems")
public final class D3D11GpuDevice implements GpuDevice {
    static final int UNIFORM_OFFSET_ALIGNMENT = 256;

    private final CommandEncoder commandEncoder = new D3D11CommandEncoder();
    private final ShaderSource defaultShaderSource;

    private static volatile String cachedBackendApi;
    private static volatile String cachedAdapterName;

    // The native DLL reports "backend|adapter" (the D3D12 build ships under the D3D11 filename
    // for A/B swaps); an unmarked string means the original Direct3D 11 backend.
    private static void resolveAdapterInfo() {
        if (cachedBackendApi != null) {
            return;
        }
        String raw = D3D11Native.getAdapterDescription();
        int sep = raw.indexOf('|');
        if (sep >= 0) {
            cachedBackendApi = raw.substring(0, sep);
            cachedAdapterName = raw.substring(sep + 1);
        } else {
            cachedBackendApi = "Direct3D 11";
            cachedAdapterName = raw;
        }
    }

    static String backendApi() {
        resolveAdapterInfo();
        return cachedBackendApi;
    }

    static String adapterName() {
        resolveAdapterInfo();
        return cachedAdapterName;
    }

    public D3D11GpuDevice(long window, int debugVerbosity, boolean sync, ShaderSource shaderSource, boolean debugLabels) {
        this.defaultShaderSource = shaderSource;
    }

    private static IllegalStateException unsupported(String operation) {
        return D3D11RenderSystem.unsupported("GpuDevice." + operation);
    }

    @Override
    public CommandEncoder createCommandEncoder() {
        return this.commandEncoder;
    }

    @Override
    public GpuSampler createSampler(AddressMode addressModeU, AddressMode addressModeV, FilterMode minFilter, FilterMode magFilter, int maxAnisotropy, OptionalDouble maxLod) {
        return new D3D11Sampler(addressModeU, addressModeV, minFilter, magFilter, maxAnisotropy, maxLod);
    }

    @Override
    public GpuTexture createTexture(@Nullable Supplier<String> label, int usage, TextureFormat format, int width, int height, int layers, int mipLevels) {
        return this.createTexture(label == null ? null : label.get(), usage, format, width, height, layers, mipLevels);
    }

    @Override
    public GpuTexture createTexture(@Nullable String label, int usage, TextureFormat format, int width, int height, int layers, int mipLevels) {
        return new D3D11GpuTexture(usage, label == null ? "D3D11 texture" : label, format, width, height, layers, mipLevels);
    }

    @Override
    public GpuTextureView createTextureView(GpuTexture texture) {
        return this.createTextureView(texture, 0, texture.getMipLevels());
    }

    @Override
    public GpuTextureView createTextureView(GpuTexture texture, int startLevel, int levels) {
        if (!(texture instanceof D3D11GpuTexture)) {
            throw unsupported("createTextureView(foreign texture)");
        }
        return new D3D11GpuTextureView(texture, startLevel, levels);
    }

    @Override
    public GpuBuffer createBuffer(@Nullable Supplier<String> label, @GpuBuffer.Usage int usage, long size) {
        return new D3D11GpuBuffer(usage, size);
    }

    @Override
    public GpuBuffer createBuffer(@Nullable Supplier<String> label, int usage, ByteBuffer source) {
        if (!source.hasRemaining()) {
            throw new IllegalArgumentException("Buffer source must not be empty");
        }
        return new D3D11GpuBuffer(usage, source);
    }

    @Override
    public String getImplementationInformation() {
        return "Bandit " + backendApi() + " backend, " + adapterName();
    }

    @Override
    public List<String> getLastDebugMessages() {
        return Collections.emptyList();
    }

    @Override
    public boolean isDebuggingEnabled() {
        return false;
    }

    @Override
    public String getRenderer() {
        return adapterName();
    }

    @Override
    public String getVendor() {
        return backendApi();
    }

    @Override
    public String getBackendName() {
        return "Direct3D 12".equals(backendApi()) ? "D3D12" : "D3D11";
    }

    @Override
    public String getVersion() {
        return "native";
    }

    @Override
    public int getMaxTextureSize() {
        return D3D11RenderSystem.maxSupportedTextureSize();
    }

    @Override
    public int getUniformOffsetAlignment() {
        return UNIFORM_OFFSET_ALIGNMENT;
    }

    @Override
    public void clearPipelineCache() {
    }

    @Override
    public List<String> getEnabledExtensions() {
        return Collections.emptyList();
    }

    @Override
    public int getMaxSupportedAnisotropy() {
        return 16;
    }

    @Override
    public void close() {
        D3D11RenderSystem.shutdown();
    }

    @Override
    public CompiledRenderPipeline precompilePipeline(RenderPipeline renderPipeline, @Nullable ShaderSource shaderSource) {
        return new D3D11CompiledRenderPipeline();
    }
}
