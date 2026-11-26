package com.onyx.mcef.uwp;

import java.nio.ByteBuffer;
import java.util.concurrent.atomic.AtomicBoolean;

/**
 * AWT-free browser surface contract for the Xbox UWP host.
 *
 * This class deliberately has no java.awt, Swing, GLFW, or desktop HWND
 * dependency. The UWP host must provide the native CoreWindow callbacks.
 * It is a transport boundary, not a CEF implementation by itself.
 */
public final class CoreWindowBrowserSurface implements AutoCloseable {
    public interface Host {
        void onFrame(ByteBuffer bgraPixels, int width, int height, int stride);
        void onPointer(int kind, float x, float y, int buttons, int modifiers);
        void onKey(int kind, int virtualKey, char character, int modifiers);
        void onFocusChanged(boolean focused);
        void onResize(int width, int height, float scale);
        void onClosed();
    }

    public interface BrowserInput {
        void mouseMove(int x, int y, int modifiers);
        void mouseButton(int x, int y, int button, boolean pressed, int modifiers);
        void mouseWheel(int x, int y, int delta, int modifiers);
        void keyEvent(int virtualKey, char character, boolean pressed, int modifiers);
        void setFocus(boolean focused);
        void resize(int width, int height);
        void close();
    }

    public static final int POINTER_MOVE = 0;
    public static final int POINTER_DOWN = 1;
    public static final int POINTER_UP = 2;
    public static final int KEY_DOWN = 0;
    public static final int KEY_UP = 1;

    private final Host host;
    private final BrowserInput browser;
    private final AtomicBoolean closed = new AtomicBoolean(false);
    private volatile int width;
    private volatile int height;
    private volatile float scale = 1.0f;
    private volatile boolean focused;

    public CoreWindowBrowserSurface(Host host, BrowserInput browser,
                                    int width, int height) {
        if (host == null || browser == null) throw new IllegalArgumentException("host/browser");
        if (width <= 0 || height <= 0) throw new IllegalArgumentException("invalid surface size");
        this.host = host;
        this.browser = browser;
        this.width = width;
        this.height = height;
        browser.resize(width, height);
    }

    /** Called by the CEF OSR callback after a complete BGRA frame is available. */
    public void presentFrame(ByteBuffer pixels, int frameWidth, int frameHeight, int stride) {
        if (closed.get() || pixels == null || frameWidth <= 0 || frameHeight <= 0
                || stride < frameWidth * 4 || pixels.remaining() < (long) stride * frameHeight) return;
        host.onFrame(pixels.asReadOnlyBuffer(), frameWidth, frameHeight, stride);
    }

    /** CoreWindow pointer coordinates are converted from physical to browser pixels. */
    public void pointer(int kind, float x, float y, int buttons, int modifiers) {
        if (closed.get()) return;
        int px = clamp(Math.round(x * scale), 0, Math.max(0, width - 1));
        int py = clamp(Math.round(y * scale), 0, Math.max(0, height - 1));
        host.onPointer(kind, x, y, buttons, modifiers);
        if (kind == POINTER_MOVE) browser.mouseMove(px, py, modifiers);
        else if (kind == POINTER_DOWN || kind == POINTER_UP) {
            int button = (buttons & 1) != 0 ? 0 : (buttons & 2) != 0 ? 1 : 2;
            browser.mouseButton(px, py, button, kind == POINTER_DOWN, modifiers);
        }
    }

    public void wheel(float x, float y, int delta, int modifiers) {
        if (closed.get()) return;
        browser.mouseWheel(clamp(Math.round(x * scale), 0, Math.max(0, width - 1)),
                clamp(Math.round(y * scale), 0, Math.max(0, height - 1)), delta, modifiers);
    }

    public void key(int kind, int virtualKey, char character, int modifiers) {
        if (closed.get()) return;
        host.onKey(kind, virtualKey, character, modifiers);
        if (kind == KEY_DOWN || kind == KEY_UP)
            browser.keyEvent(virtualKey, character, kind == KEY_DOWN, modifiers);
    }

    public void focus(boolean value) {
        if (closed.get() || focused == value) return;
        focused = value;
        browser.setFocus(value);
        host.onFocusChanged(value);
    }

    public void resize(int newWidth, int newHeight, float newScale) {
        if (closed.get() || newWidth <= 0 || newHeight <= 0 || newScale <= 0) return;
        width = newWidth;
        height = newHeight;
        scale = newScale;
        browser.resize(newWidth, newHeight);
        host.onResize(newWidth, newHeight, newScale);
    }

    @Override
    public void close() {
        if (!closed.compareAndSet(false, true)) return;
        try { browser.close(); } finally { host.onClosed(); }
    }

    public boolean isClosed() { return closed.get(); }
    public boolean isFocused() { return focused; }
    public int getWidth() { return width; }
    public int getHeight() { return height; }

    private static int clamp(int value, int min, int max) {
        return Math.max(min, Math.min(max, value));
    }
}
