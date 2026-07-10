package banditvault.d3d11mod.render;

import banditvault.d3d11mod.D3D11RenderSystem;
import banditvault.d3d11mod.D3D11ModInitializer;
import banditvault.d3d11mod.nativebridge.D3D11Native;
import com.mojang.blaze3d.buffers.GpuBuffer;
import com.mojang.blaze3d.buffers.GpuBufferSlice;
import com.mojang.blaze3d.buffers.GpuFence;
import com.mojang.blaze3d.pipeline.RenderPipeline;
import com.mojang.blaze3d.platform.NativeImage;
import com.mojang.blaze3d.systems.CommandEncoder;
import com.mojang.blaze3d.systems.GpuQuery;
import com.mojang.blaze3d.systems.RenderPass;
import com.mojang.blaze3d.textures.GpuTexture;
import com.mojang.blaze3d.textures.GpuTextureView;
import org.lwjgl.system.MemoryUtil;
import org.jetbrains.annotations.Nullable;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.util.OptionalDouble;
import java.util.OptionalInt;
import java.util.function.Supplier;

public final class D3D11CommandEncoder implements CommandEncoder {
    private static boolean loggedSkippedMetadataTextureUpload;
    private static boolean loggedStubbedTextureReadback;
    private static boolean loggedSkippedTextureCopy;
    private static final java.util.HashSet<String> loggedPassTargets = new java.util.HashSet<>();

    private static IllegalStateException unsupported(String operation) {
        return D3D11RenderSystem.unsupported("CommandEncoder." + operation);
    }

    @Override
    public RenderPass createRenderPass(Supplier<String> supplier, GpuTextureView colorAttachmentView, OptionalInt clearColor) {
        return this.createRenderPass(supplier, colorAttachmentView, clearColor, null, OptionalDouble.empty());
    }

    @Override
    public RenderPass createRenderPass(Supplier<String> supplier, GpuTextureView colorAttachmentView, OptionalInt clearColor, @Nullable GpuTextureView depthTexture, OptionalDouble clearDepth) {
        if (!(colorAttachmentView instanceof D3D11GpuTextureView)) {
            throw unsupported("createRenderPass(foreign color texture)");
        }
        if (depthTexture != null && !(depthTexture instanceof D3D11GpuTextureView)) {
            throw unsupported("createRenderPass(foreign depth texture)");
        }
        GpuTexture colorTexture = colorAttachmentView.texture();
        boolean backbuffer = isMainTargetTexture(colorTexture);
        long targetHandle = 0L;
        int targetMip = 0;
        if (!backbuffer && colorTexture instanceof D3D11GpuTexture d3d11Texture && d3d11Texture.hasNativeHandle()) {
            targetHandle = d3d11Texture.nativeHandle();
            targetMip = colorAttachmentView.baseMipLevel();
        }
        if (loggedPassTargets.add(colorTexture.getLabel() + "|" + backbuffer)) {
            D3D11ModInitializer.LOGGER.warn("D3D11 render pass target label={} backbuffer={} texture={} mip={}", colorTexture.getLabel(), backbuffer, targetHandle, targetMip);
        }
        if (clearColor.isPresent()) {
            clearRoutedTarget(colorTexture, clearColor.getAsInt());
        }
        if (depthTexture != null && clearDepth.isPresent()) {
            if (backbuffer) {
                clearRoutedDepth(depthTexture.texture(), clearDepth.getAsDouble());
            } else if (targetHandle != 0L) {
                // Offscreen bakes (UI items atlas, UI entity texture) share size-keyed native depth
                // buffers; clear the one matching this pass's color target.
                D3D11Native.clearOffscreenDepth(colorTexture.getWidth(targetMip), colorTexture.getHeight(targetMip), (float) clearDepth.getAsDouble());
            }
        }
        return new D3D11RenderPass(depthTexture != null, backbuffer, targetHandle, targetMip);
    }

    private static boolean isMainTargetTexture(GpuTexture texture) {
        String label = texture.getLabel();
        return label != null && label.startsWith("Main");
    }

    private static void clearRoutedDepth(GpuTexture depthAttachment, double clearDepth) {
        if (isMainTargetTexture(depthAttachment)) {
            D3D11Native.clearDepth((float) clearDepth);
        } else {
            // Offscreen depth buffers are size-keyed; the depth texture's own dimensions pick
            // the right one (bake depth size == bake color size).
            D3D11Native.clearOffscreenDepth(depthAttachment.getWidth(0), depthAttachment.getHeight(0), (float) clearDepth);
        }
    }

    @Override
    public void clearColorTexture(GpuTexture colorAttachment, int clearColor) {
        if (!(colorAttachment instanceof D3D11GpuTexture)) {
            throw unsupported("clearColorTexture(foreign texture)");
        }
        clearRoutedTarget(colorAttachment, clearColor);
    }

    @Override
    public void clearColorAndDepthTextures(GpuTexture colorAttachment, int clearColor, GpuTexture depthAttachment, double clearDepth) {
        if (!(colorAttachment instanceof D3D11GpuTexture) || !(depthAttachment instanceof D3D11GpuTexture)) {
            throw unsupported("clearColorAndDepthTextures(foreign texture)");
        }
        clearRoutedTarget(colorAttachment, clearColor);
        clearRoutedDepth(depthAttachment, clearDepth);
    }

    @Override
    public void clearColorAndDepthTextures(GpuTexture colorAttachment, int clearColor, GpuTexture depthAttachment, double clearDepth, int x0, int y0, int width, int height) {
        if (!(colorAttachment instanceof D3D11GpuTexture) || !(depthAttachment instanceof D3D11GpuTexture)) {
            throw unsupported("clearColorAndDepthTextures(foreign texture)");
        }
    }

    @Override
    public void clearDepthTexture(GpuTexture depthAttachment, double clearDepth) {
        if (!(depthAttachment instanceof D3D11GpuTexture)) {
            throw unsupported("clearDepthTexture(foreign texture)");
        }
        clearRoutedDepth(depthAttachment, clearDepth);
    }

    @Override
    public void writeToBuffer(GpuBufferSlice slice, ByteBuffer source) {
        if (!(slice.buffer() instanceof D3D11GpuBuffer buffer)) {
            throw unsupported("writeToBuffer(foreign buffer)");
        }
        int bytes = Math.toIntExact(Math.min(slice.length(), source.remaining()));
        ByteBuffer upload = source.duplicate();
        upload.limit(upload.position() + bytes);
        upload = upload.slice();
        ByteBuffer target = buffer.data().duplicate();
        target.position(Math.toIntExact(slice.offset()));
        target.limit(Math.toIntExact(slice.offset() + bytes));
        target.put(upload.duplicate());
        buffer.upload(slice.offset(), upload);
    }

    @Override
    public GpuBuffer.MappedView mapBuffer(GpuBuffer buffer, boolean readable, boolean writable) {
        if (!(buffer instanceof D3D11GpuBuffer d3d11Buffer)) {
            throw unsupported("mapBuffer(foreign buffer)");
        }
        // duplicate() resets byte order to BIG_ENDIAN; vanilla's Std140Builder writes uniforms
        // through this view with ByteBuffer.putFloat, so the view must stay in native order.
        return new D3D11GpuBuffer.MappedView(d3d11Buffer, d3d11Buffer.data().duplicate().order(ByteOrder.nativeOrder()), 0L, writable);
    }

    @Override
    public GpuBuffer.MappedView mapBuffer(GpuBufferSlice slice, boolean readable, boolean writable) {
        if (!(slice.buffer() instanceof D3D11GpuBuffer buffer)) {
            throw unsupported("mapBuffer(foreign buffer)");
        }
        ByteBuffer data = buffer.data().duplicate();
        data.position(Math.toIntExact(slice.offset()));
        data.limit(Math.toIntExact(slice.offset() + slice.length()));
        // slice() also resets byte order to BIG_ENDIAN; keep native order (see mapBuffer above).
        return new D3D11GpuBuffer.MappedView(buffer, data.slice().order(ByteOrder.nativeOrder()), slice.offset(), writable);
    }

    @Override
    public void copyToBuffer(GpuBufferSlice source, GpuBufferSlice destination) {
        if (!(source.buffer() instanceof D3D11GpuBuffer sourceBuffer) || !(destination.buffer() instanceof D3D11GpuBuffer destinationBuffer)) {
            throw unsupported("copyToBuffer(foreign buffer)");
        }
        if (source.length() != destination.length()) {
            throw new IllegalArgumentException("Buffer copy size mismatch: " + source.length() + " != " + destination.length());
        }
        ByteBuffer data = sourceBuffer.data().duplicate();
        int start = Math.toIntExact(source.offset());
        data.position(start);
        data.limit(Math.toIntExact(start + source.length()));
        ByteBuffer payload = data.slice();
        ByteBuffer target = destinationBuffer.data().duplicate();
        target.position(Math.toIntExact(destination.offset()));
        target.put(payload.duplicate());
        destinationBuffer.upload(destination.offset(), payload);
    }

    @Override
    public void writeToTexture(GpuTexture texture, NativeImage image) {
        if (!(texture instanceof D3D11GpuTexture)) {
            throw unsupported("writeToTexture(foreign texture)");
        }
        this.writeToTexture(texture, image, 0, 0, 0, 0, image.getWidth(), image.getHeight(), 0, 0);
    }

    @Override
    public void writeToTexture(GpuTexture texture, NativeImage image, int level, int arrayLayer, int xOffset, int yOffset, int width, int height, int unpackSkipPixels, int unpackSkipRows) {
        if (!(texture instanceof D3D11GpuTexture d3d11Texture)) {
            throw unsupported("writeToTexture(foreign texture)");
        }
        if (!hasNativeTextureForUpload(d3d11Texture)) {
            return;
        }
        validateTextureUpload(d3d11Texture, image.format(), level, arrayLayer, xOffset, yOffset, width, height);
        if (unpackSkipPixels < 0 || unpackSkipRows < 0 || unpackSkipPixels + width > image.getWidth() || unpackSkipRows + height > image.getHeight()) {
            throw new IllegalArgumentException("Texture upload source rectangle is out of range");
        }
        int components = image.format().components();
        int rowPitch = image.getWidth() * components;
        int byteCount = rowPitch * (height - 1) + width * components;
        ByteBuffer source = MemoryUtil.memByteBuffer(image.getPointer() + (long) (unpackSkipRows * image.getWidth() + unpackSkipPixels) * components, byteCount);
        D3D11Native.updateTexture(d3d11Texture.nativeHandle(), level, arrayLayer, xOffset, yOffset, width, height, rowPitch, source);
    }

    @Override
    public void writeToTexture(GpuTexture texture, ByteBuffer source, NativeImage.Format format, int level, int arrayLayer, int xOffset, int yOffset, int width, int height) {
        if (!(texture instanceof D3D11GpuTexture d3d11Texture)) {
            throw unsupported("writeToTexture(foreign texture)");
        }
        if (!hasNativeTextureForUpload(d3d11Texture)) {
            return;
        }
        validateTextureUpload(d3d11Texture, format, level, arrayLayer, xOffset, yOffset, width, height);
        int components = format.components();
        int byteCount = width * height * components;
        if (source.remaining() < byteCount) {
            throw new IllegalArgumentException("Texture upload source is too small");
        }
        ByteBuffer upload = source.duplicate();
        upload.limit(upload.position() + byteCount);
        upload = upload.slice();
        if (!upload.isDirect()) {
            ByteBuffer direct = ByteBuffer.allocateDirect(byteCount);
            direct.put(upload.duplicate());
            direct.flip();
            upload = direct;
        }
        D3D11Native.updateTexture(d3d11Texture.nativeHandle(), level, arrayLayer, xOffset, yOffset, width, height, width * components, upload);
    }

    @Override
    public void copyTextureToBuffer(GpuTexture texture, GpuBuffer buffer, long offset, Runnable callback, int mipLevel) {
        this.copyTextureToBuffer(texture, buffer, offset, callback, mipLevel, 0, 0, texture.getWidth(mipLevel), texture.getHeight(mipLevel));
    }

    @Override
    public void copyTextureToBuffer(GpuTexture texture, GpuBuffer buffer, long offset, Runnable callback, int mipLevel, int xOffset, int yOffset, int width, int height) {
        if (!(texture instanceof D3D11GpuTexture d3d11Texture) || !(buffer instanceof D3D11GpuBuffer d3d11Buffer)) {
            throw unsupported("copyTextureToBuffer(foreign resource)");
        }
        // Main is presented via the swapchain, so its readback comes from the backbuffer (marker 0).
        String label = texture.getLabel();
        boolean mainTarget = label != null && label.startsWith("Main");
        boolean readable = mainTarget || d3d11Texture.hasNativeHandle();
        int byteCount = width * height * 4;
        ByteBuffer staging = ByteBuffer.allocateDirect(byteCount);
        boolean ok = readable && D3D11Native.readbackTexture(
            mainTarget ? 0L : d3d11Texture.nativeHandle(), mipLevel, xOffset, yOffset, width, height, staging);
        if (ok) {
            ByteBuffer destination = d3d11Buffer.data().duplicate();
            destination.position(Math.toIntExact(offset));
            staging.limit(byteCount);
            destination.put(staging);
        } else if (!loggedStubbedTextureReadback) {
            D3D11ModInitializer.LOGGER.warn("D3D11 copyTextureToBuffer failed for {}; contents are undefined.", label);
            loggedStubbedTextureReadback = true;
        }
        callback.run();
    }

    @Override
    public void copyTextureToTexture(GpuTexture source, GpuTexture destination, int mipLevel, int sourceX, int sourceY, int destinationX, int destinationY, int width, int height) {
        if (!(source instanceof D3D11GpuTexture) || !(destination instanceof D3D11GpuTexture)) {
            throw unsupported("copyTextureToTexture(foreign resource)");
        }
        if (!loggedSkippedTextureCopy) {
            // ponytail: needs CopySubresourceRegion plumbing; skip until texture copies matter.
            D3D11ModInitializer.LOGGER.warn("Skipping D3D11 copyTextureToTexture until texture copies are implemented.");
            loggedSkippedTextureCopy = true;
        }
    }

    @Override
    public GpuFence createFence() {
        // D3D11 immediate path is treated as complete until real GPU wait points exist.
        return new GpuFence() {
            @Override
            public void close() {
            }

            @Override
            public boolean awaitCompletion(long timeout) {
                return true;
            }
        };
    }

    @Override
    public GpuQuery timerQueryBegin() {
        throw unsupported("timerQueryBegin");
    }

    @Override
    public void timerQueryEnd(GpuQuery query) {
        throw unsupported("timerQueryEnd");
    }

    @Override
    public void presentTexture(GpuTextureView texture) {
        if (!(texture instanceof D3D11GpuTextureView)) {
            throw unsupported("presentTexture(foreign texture)");
        }
        D3D11RenderSystem.present();
    }

    private static void clearRoutedTarget(GpuTexture colorAttachment, int clearColor) {
        float alpha = ((clearColor >>> 24) & 0xFF) / 255.0f;
        float red = ((clearColor >>> 16) & 0xFF) / 255.0f;
        float green = ((clearColor >>> 8) & 0xFF) / 255.0f;
        float blue = (clearColor & 0xFF) / 255.0f;
        if (isMainTargetTexture(colorAttachment)) {
            // The Main render target is presented via the swapchain, so its clear goes to the
            // backbuffer.
            D3D11Native.beginFrame(red, green, blue, alpha);
        } else if (colorAttachment instanceof D3D11GpuTexture d3d11Texture && d3d11Texture.hasNativeHandle()) {
            D3D11Native.clearTexture(d3d11Texture.nativeHandle(), red, green, blue, alpha);
        }
    }

    private static boolean hasNativeTextureForUpload(D3D11GpuTexture texture) {
        if (texture.isClosed()) {
            throw new IllegalStateException("Destination texture is closed");
        }
        if (!texture.hasNativeHandle()) {
            if (!loggedSkippedMetadataTextureUpload) {
                // ponytail: render-target/depth/cubemap backing is still metadata-only; add RTV/DSV/array SRVs when a draw needs them.
                D3D11ModInitializer.LOGGER.warn("Skipping D3D11 texture upload for metadata-only texture {} {} {}x{} layers={} mips={}.",
                    texture.getLabel(),
                    texture.getFormat(),
                    texture.getWidth(0),
                    texture.getHeight(0),
                    texture.getDepthOrLayers(),
                    texture.getMipLevels());
                loggedSkippedMetadataTextureUpload = true;
            }
            return false;
        }
        return true;
    }

    private static void validateTextureUpload(D3D11GpuTexture texture, NativeImage.Format format, int level, int arrayLayer, int xOffset, int yOffset, int width, int height) {
        if (arrayLayer < 0 || arrayLayer >= texture.getDepthOrLayers()) {
            throw unsupported("writeToTexture(array layer " + arrayLayer + " of " + texture.getDepthOrLayers() + ")");
        }
        if (level < 0 || level >= texture.getMipLevels()) {
            throw new IllegalArgumentException("Invalid texture mip level: " + level);
        }
        if (format.components() != texture.getFormat().pixelSize()) {
            throw unsupported("writeToTexture(format " + format + " -> " + texture.getFormat() + ")");
        }
        if (width <= 0 || height <= 0 || xOffset < 0 || yOffset < 0 || xOffset + width > texture.getWidth(level) || yOffset + height > texture.getHeight(level)) {
            throw new IllegalArgumentException("Texture upload destination rectangle is out of range");
        }
    }
}
