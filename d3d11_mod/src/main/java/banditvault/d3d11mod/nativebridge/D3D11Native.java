package banditvault.d3d11mod.nativebridge;

import java.nio.file.Files;
import java.nio.file.Path;

public final class D3D11Native {
    private static boolean loaded;

    private D3D11Native() {
    }

    private static synchronized void load() {
        if (loaded) {
            return;
        }

        String nativePath = System.getProperty("bandit.d3d11.native.path");
        if (nativePath == null || nativePath.isBlank()) {
            throw new IllegalStateException("Missing -Dbandit.d3d11.native.path for Bandit D3D11Mod.");
        }

        Path path = Path.of(nativePath);
        if (!Files.isRegularFile(path)) {
            throw new IllegalStateException("Bandit D3D11 native DLL not found: " + path);
        }

        System.load(path.toAbsolutePath().toString());
        loaded = true;
    }

    public static boolean init(int width, int height, String logPath) {
        load();
        return nativeInit(width, height, logPath == null ? "" : logPath);
    }

    public static boolean resize(int width, int height) {
        load();
        return nativeResize(width, height);
    }

    public static void beginFrame(float r, float g, float b, float a) {
        load();
        nativeBeginFrame(r, g, b, a);
    }

    public static void drawTestTriangle() {
        load();
        nativeDrawTestTriangle();
    }

    public static boolean present() {
        load();
        return nativePresent();
    }

    public static String getAdapterDescription() {
        load();
        return nativeGetAdapterDescription();
    }

    public static long createBuffer(int usage, long size) {
        load();
        long handle = nativeCreateBuffer(usage, size);
        if (handle == 0L) {
            throw new IllegalStateException("Failed to create native D3D11 buffer size=" + size + " usage=" + usage);
        }
        return handle;
    }

    public static void updateBuffer(long handle, long offset, java.nio.ByteBuffer source) {
        load();
        nativeUpdateBuffer(handle, offset, source.slice());
    }

    public static void destroyBuffer(long handle) {
        if (loaded && handle != 0L) {
            nativeDestroyBuffer(handle);
        }
    }

    public static long createTexture(int width, int height, int layers, boolean cube, int mipLevels, int pixelSize) {
        load();
        return nativeCreateTexture(width, height, layers, cube, mipLevels, pixelSize);
    }

    public static void updateTexture(long handle, int level, int layer, int xOffset, int yOffset, int width, int height, int rowPitch, java.nio.ByteBuffer source) {
        load();
        nativeUpdateTexture(handle, level, layer, xOffset, yOffset, width, height, rowPitch, source.slice());
    }

    public static void destroyTexture(long handle) {
        if (loaded && handle != 0L) {
            nativeDestroyTexture(handle);
        }
    }

    public static void clearTexture(long handle, float r, float g, float b, float a) {
        load();
        nativeClearTexture(handle, r, g, b, a);
    }

    public static void clearDepth(float depth) {
        load();
        nativeClearDepth(depth);
    }

    public static boolean drawLightmap(long targetTexture, int targetMip, int vertexOffset, int vertexCount, java.nio.ByteBuffer lightmapInfo) {
        load();
        return nativeDrawLightmap(targetTexture, targetMip, vertexOffset, vertexCount, lightmapInfo);
    }

    public static boolean beginTerrainBatch(float alphaCutoff, boolean translucent, long atlasTexture, long lightmapTexture, java.nio.ByteBuffer projection, java.nio.ByteBuffer fog, java.nio.ByteBuffer globals) {
        load();
        return nativeBeginTerrainBatch(alphaCutoff, translucent, atlasTexture, lightmapTexture, projection, fog, globals);
    }

    public static boolean drawTerrainSection(long vertexBuffer, int vertexStride, long indexBuffer, int indexBytes, int firstIndex, int indexCount, java.nio.ByteBuffer chunkSection) {
        load();
        return nativeDrawTerrainSection(vertexBuffer, vertexStride, indexBuffer, indexBytes, firstIndex, indexCount, chunkSection);
    }

    public static void endTerrainBatch() {
        load();
        nativeEndTerrainBatch();
    }

    public static boolean drawWorldSimple(int kind, int blendMode, boolean fan, long vertexBuffer, int vertexStride, long indexBuffer, int indexBytes, int vertexOffset, int firstIndex, int count, long textureHandle, java.nio.ByteBuffer dynamicTransforms, java.nio.ByteBuffer projection, java.nio.ByteBuffer fog, java.nio.ByteBuffer globals, java.nio.ByteBuffer cloudInfo, long cloudFacesBuffer, int cloudFacesOffset, int cloudFacesLength) {
        load();
        return nativeDrawWorldSimple(kind, blendMode, fan, vertexBuffer, vertexStride, indexBuffer, indexBytes, vertexOffset, firstIndex, count, textureHandle, dynamicTransforms, projection, fog, globals, cloudInfo, cloudFacesBuffer, cloudFacesOffset, cloudFacesLength);
    }

    public static boolean drawEntity(int kind, int blendMode, float alphaCutoff, int flags, long vertexBuffer, int vertexStride, long indexBuffer, int indexBytes, int vertexOffset, int firstIndex, int indexCount, long texture0, long overlayTexture, long lightmapTexture, long targetTexture, int targetMip, boolean targetHasDepth, java.nio.ByteBuffer dynamicTransforms, java.nio.ByteBuffer projection, java.nio.ByteBuffer fog, java.nio.ByteBuffer lighting) {
        load();
        return nativeDrawEntity(kind, blendMode, alphaCutoff, flags, vertexBuffer, vertexStride, indexBuffer, indexBytes, vertexOffset, firstIndex, indexCount, texture0, overlayTexture, lightmapTexture, targetTexture, targetMip, targetHasDepth, dynamicTransforms, projection, fog, lighting);
    }

    public static void clearOffscreenDepth(int width, int height, float depth) {
        load();
        nativeClearOffscreenDepth(width, height, depth);
    }

    public static boolean drawSpriteBlit(long targetTexture, int targetMip, boolean interpolate, long spriteTexture, long nextSpriteTexture, int vertexOffset, int vertexCount, java.nio.ByteBuffer spriteInfo) {
        load();
        return nativeDrawSpriteBlit(targetTexture, targetMip, interpolate, spriteTexture, nextSpriteTexture, vertexOffset, vertexCount, spriteInfo);
    }

    public static boolean drawGuiIndexed(long vertexBuffer, int vertexStride, long indexBuffer, int indexBytes, int vertexOffset, int firstIndex, int indexCount, int instanceCount, int uvOffset, int colorOffset, int psMode, int blendMode, int scissorX, int scissorY, int scissorWidth, int scissorHeight, long textureHandle, java.nio.ByteBuffer dynamicTransforms, java.nio.ByteBuffer projection) {
        load();
        return nativeDrawGuiIndexed(vertexBuffer, vertexStride, indexBuffer, indexBytes, vertexOffset, firstIndex, indexCount, instanceCount, uvOffset, colorOffset, psMode, blendMode, scissorX, scissorY, scissorWidth, scissorHeight, textureHandle, dynamicTransforms, projection);
    }

    public static boolean drawPostPass(long inputTexture, long targetTexture, int targetMip, int vertexOffset, int vertexCount, java.nio.ByteBuffer blurConfig, java.nio.ByteBuffer globals) {
        load();
        return nativeDrawPostPass(inputTexture, targetTexture, targetMip, vertexOffset, vertexCount, blurConfig, globals);
    }
    public static boolean drawGlintToTexture(long vertexBuffer, int vertexStride, long indexBuffer, int indexBytes, int vertexOffset, int firstIndex, int count, long glintTexture, long targetTexture, int targetMip, boolean targetHasDepth, java.nio.ByteBuffer dynamicTransforms, java.nio.ByteBuffer projection, java.nio.ByteBuffer fog, java.nio.ByteBuffer globals) {
        load();
        return nativeDrawGlintToTexture(vertexBuffer, vertexStride, indexBuffer, indexBytes, vertexOffset, firstIndex, count, glintTexture, targetTexture, targetMip, targetHasDepth, dynamicTransforms, projection, fog, globals);
    }
    public static boolean readbackTexture(long textureHandle, int mipLevel, int x, int y, int width, int height, java.nio.ByteBuffer destination) {
        load();
        return nativeReadbackTexture(textureHandle, mipLevel, x, y, width, height, destination);
    }
    public static boolean drawText(long vertexBuffer, int vertexStride, long indexBuffer, int indexBytes, int vertexOffset, int firstIndex, int count, long fontTexture, long lightmapTexture, int variant, java.nio.ByteBuffer dynamicTransforms, java.nio.ByteBuffer projection, java.nio.ByteBuffer fog) {
        load();
        return nativeDrawText(vertexBuffer, vertexStride, indexBuffer, indexBytes, vertexOffset, firstIndex, count, fontTexture, lightmapTexture, variant, dynamicTransforms, projection, fog);
    }
    public static void shutdown() {
        if (loaded) {
            nativeShutdown();
        }
    }

    private static native boolean nativeInit(int width, int height, String logPath);
    private static native boolean nativeResize(int width, int height);
    private static native void nativeBeginFrame(float r, float g, float b, float a);
    private static native void nativeDrawTestTriangle();
    private static native boolean nativePresent();
    private static native String nativeGetAdapterDescription();
    private static native long nativeCreateBuffer(int usage, long size);
    private static native void nativeUpdateBuffer(long handle, long offset, java.nio.ByteBuffer source);
    private static native void nativeDestroyBuffer(long handle);
    private static native long nativeCreateTexture(int width, int height, int layers, boolean cube, int mipLevels, int pixelSize);
    private static native void nativeUpdateTexture(long handle, int level, int layer, int xOffset, int yOffset, int width, int height, int rowPitch, java.nio.ByteBuffer source);
    private static native void nativeDestroyTexture(long handle);
    private static native void nativeClearTexture(long handle, float r, float g, float b, float a);
    private static native void nativeClearDepth(float depth);
    private static native boolean nativeDrawLightmap(long targetTexture, int targetMip, int vertexOffset, int vertexCount, java.nio.ByteBuffer lightmapInfo);
    private static native boolean nativeBeginTerrainBatch(float alphaCutoff, boolean translucent, long atlasTexture, long lightmapTexture, java.nio.ByteBuffer projection, java.nio.ByteBuffer fog, java.nio.ByteBuffer globals);
    private static native boolean nativeDrawTerrainSection(long vertexBuffer, int vertexStride, long indexBuffer, int indexBytes, int firstIndex, int indexCount, java.nio.ByteBuffer chunkSection);
    private static native void nativeEndTerrainBatch();
    private static native boolean nativeDrawWorldSimple(int kind, int blendMode, boolean fan, long vertexBuffer, int vertexStride, long indexBuffer, int indexBytes, int vertexOffset, int firstIndex, int count, long textureHandle, java.nio.ByteBuffer dynamicTransforms, java.nio.ByteBuffer projection, java.nio.ByteBuffer fog, java.nio.ByteBuffer globals, java.nio.ByteBuffer cloudInfo, long cloudFacesBuffer, int cloudFacesOffset, int cloudFacesLength);
    private static native boolean nativeDrawEntity(int kind, int blendMode, float alphaCutoff, int flags, long vertexBuffer, int vertexStride, long indexBuffer, int indexBytes, int vertexOffset, int firstIndex, int indexCount, long texture0, long overlayTexture, long lightmapTexture, long targetTexture, int targetMip, boolean targetHasDepth, java.nio.ByteBuffer dynamicTransforms, java.nio.ByteBuffer projection, java.nio.ByteBuffer fog, java.nio.ByteBuffer lighting);
    private static native void nativeClearOffscreenDepth(int width, int height, float depth);
    private static native boolean nativeDrawSpriteBlit(long targetTexture, int targetMip, boolean interpolate, long spriteTexture, long nextSpriteTexture, int vertexOffset, int vertexCount, java.nio.ByteBuffer spriteInfo);
    private static native boolean nativeDrawGuiIndexed(long vertexBuffer, int vertexStride, long indexBuffer, int indexBytes, int vertexOffset, int firstIndex, int indexCount, int instanceCount, int uvOffset, int colorOffset, int psMode, int blendMode, int scissorX, int scissorY, int scissorWidth, int scissorHeight, long textureHandle, java.nio.ByteBuffer dynamicTransforms, java.nio.ByteBuffer projection);
    private static native boolean nativeDrawPostPass(long inputTexture, long targetTexture, int targetMip, int vertexOffset, int vertexCount, java.nio.ByteBuffer blurConfig, java.nio.ByteBuffer globals);
    private static native boolean nativeDrawGlintToTexture(long vertexBuffer, int vertexStride, long indexBuffer, int indexBytes, int vertexOffset, int firstIndex, int count, long glintTexture, long targetTexture, int targetMip, boolean targetHasDepth, java.nio.ByteBuffer dynamicTransforms, java.nio.ByteBuffer projection, java.nio.ByteBuffer fog, java.nio.ByteBuffer globals);
    private static native boolean nativeReadbackTexture(long textureHandle, int mipLevel, int x, int y, int width, int height, java.nio.ByteBuffer destination);
    private static native boolean nativeDrawText(long vertexBuffer, int vertexStride, long indexBuffer, int indexBytes, int vertexOffset, int firstIndex, int count, long fontTexture, long lightmapTexture, int variant, java.nio.ByteBuffer dynamicTransforms, java.nio.ByteBuffer projection, java.nio.ByteBuffer fog);
    private static native void nativeShutdown();
}
