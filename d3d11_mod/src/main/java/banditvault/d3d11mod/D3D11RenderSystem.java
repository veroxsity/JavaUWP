package banditvault.d3d11mod;

import banditvault.d3d11mod.nativebridge.D3D11Native;

public final class D3D11RenderSystem {
    private static boolean initialized;
    private static int width;
    private static int height;

    private D3D11RenderSystem() {
    }

    public static void initRenderer() {
        if (initialized) {
            return;
        }

        String logPath = System.getProperty("bandit.d3d11.log.path", "");
        if (!D3D11Native.init(0, 0, logPath)) {
            throw new IllegalStateException("Bandit D3D11 first-pixel native backend failed to initialize. See d3d11_backend.log.");
        }

        renderFirstPixel();
        initialized = true;
        D3D11ModInitializer.LOGGER.warn("D3D11 backend initialized: {}", D3D11Native.getAdapterDescription());
    }

    private static void renderFirstPixel() {
    }

    public static void renderTestFrame(int framebufferWidth, int framebufferHeight) {
        if (!initialized) {
            return;
        }

        int nextWidth = Math.max(1, framebufferWidth);
        int nextHeight = Math.max(1, framebufferHeight);
        if (nextWidth != width || nextHeight != height) {
            width = nextWidth;
            height = nextHeight;
            if (!D3D11Native.resize(width, height)) {
                throw new IllegalStateException("Bandit D3D11 first-pixel resize failed. See d3d11_backend.log.");
            }
        }
    }

    public static int maxSupportedTextureSize() {
        return 16384;
    }

    public static void present() {
        if (!D3D11Native.present()) {
            throw new IllegalStateException("Bandit D3D11 present failed. See d3d11_backend.log.");
        }
    }

    public static IllegalStateException unsupported(String operation) {
        D3D11ModInitializer.LOGGER.error("Unsupported D3D11 backend operation: {}", operation);
        return new IllegalStateException("Bandit D3D11 first-pixel backend does not support " + operation + " yet.");
    }

    public static void shutdown() {
        if (initialized) {
            D3D11Native.shutdown();
            initialized = false;
        }
    }
}
