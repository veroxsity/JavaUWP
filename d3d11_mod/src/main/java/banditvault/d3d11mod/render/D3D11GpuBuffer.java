package banditvault.d3d11mod.render;

import banditvault.d3d11mod.nativebridge.D3D11Native;
import com.mojang.blaze3d.buffers.GpuBuffer;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;

final class D3D11GpuBuffer extends GpuBuffer {
    private final ByteBuffer data;
    private final long nativeHandle;
    private boolean closed;

    D3D11GpuBuffer(int usage, long size) {
        super(usage, size);
        if (size <= 0 || size > Integer.MAX_VALUE) {
            throw new IllegalArgumentException("Unsupported D3D11 first-pixel buffer size: " + size);
        }
        this.data = ByteBuffer.allocateDirect((int) size).order(ByteOrder.nativeOrder());
        this.nativeHandle = D3D11Native.createBuffer(usage, size);
    }

    D3D11GpuBuffer(int usage, ByteBuffer source) {
        this(usage, source.remaining());
        this.data.put(source.duplicate());
        this.data.position(0);
        upload(0L, this.data.duplicate());
    }

    ByteBuffer data() {
        return this.data;
    }

    long nativeHandle() {
        return this.nativeHandle;
    }

    void upload(long offset, ByteBuffer source) {
        if (this.closed) {
            throw new IllegalStateException("D3D11 buffer is closed");
        }
        D3D11Native.updateBuffer(this.nativeHandle, offset, source);
    }

    @Override
    public boolean isClosed() {
        return this.closed;
    }

    @Override
    public void close() {
        if (!this.closed) {
            D3D11Native.destroyBuffer(this.nativeHandle);
            this.closed = true;
        }
    }

    static final class MappedView implements GpuBuffer.MappedView {
        private final D3D11GpuBuffer buffer;
        private final ByteBuffer data;
        private final long offset;
        private final boolean writable;
        private boolean closed;

        MappedView(D3D11GpuBuffer buffer, ByteBuffer data, long offset, boolean writable) {
            this.buffer = buffer;
            this.data = data;
            this.offset = offset;
            this.writable = writable;
        }

        @Override
        public ByteBuffer data() {
            return this.data;
        }

        @Override
        public void close() {
            if (!this.closed && this.writable) {
                // upload whole mapped slice on close; replace with dirty ranges if this becomes hot.
                ByteBuffer upload = this.data.duplicate();
                upload.position(0);
                this.buffer.upload(this.offset, upload);
            }
            this.closed = true;
        }
    }
}
