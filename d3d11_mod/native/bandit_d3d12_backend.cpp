// Bandit D3D12 backend — native Direct3D 12 renderer for Minecraft Java on Xbox UWP.
//
// Full port of bandit_d3d11_backend.cpp. Exports the identical JNI symbols; selected at
// package time via build.ps1 -UseD3D12Backend. Architecture follows the VulkanMod model
// adapted to D3D12: per-frame upload arenas reset each frame, root CBVs into the arena for
// per-draw uniform snapshots, a PSO cache keyed by (program, blend, depth, raster, layout),
// static samplers (s0 linear-clamp-mip0, s1 point-clamp, s2 linear-clamp-mips), a per-frame
// shader-visible SRV ring, and explicit resource state tracking for the render-to-texture
// paths (sprite blits, item baking, lightmap).

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <dxgi1_4.h>
#include <jni.h>
#include <roapi.h>
#include <wrl.h>
#include <wrl/wrappers/corewrappers.h>
#include <windows.applicationmodel.core.h>
#include <windows.foundation.collections.h>
#include <windows.ui.core.h>

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

using Microsoft::WRL::ComPtr;
using Microsoft::WRL::Wrappers::HStringReference;
using namespace ABI::Windows::ApplicationModel::Core;
using namespace ABI::Windows::Foundation::Collections;
using namespace ABI::Windows::UI::Core;

static constexpr wchar_t kCoreWindowProperty[] = L"EGLNativeWindowTypeProperty";
static constexpr UINT kFrameCount = 3;
static constexpr UINT64 kCbufferRingSize = 8ull * 1024 * 1024;
static constexpr UINT kGpuSrvHeapCapacity = 12288;
static constexpr UINT kCpuSrvHeapCapacity = 4096;
static constexpr UINT kRtvHeapCapacity = 512;
static constexpr UINT kDsvHeapCapacity = 32;

static void Log(const char* fmt, ...);
static void LogHr(const char* stage, HRESULT hr);
static bool SucceededOrLog(const char* stage, HRESULT hr);

// ---- Core device/frame state ----------------------------------------------------------------

static ComPtr<ICoreWindow> g_window;
static ComPtr<ID3D12Device> g_device;
static ComPtr<ID3D12CommandQueue> g_commandQueue;
static ComPtr<IDXGISwapChain3> g_swapChain;
static ComPtr<ID3D12DescriptorHeap> g_backBufferRtvHeap;
static UINT g_rtvDescriptorSize = 0;
static UINT g_dsvDescriptorSize = 0;
static UINT g_srvDescriptorSize = 0;
static ComPtr<ID3D12Resource> g_backBuffers[kFrameCount];
static ComPtr<ID3D12GraphicsCommandList> g_commandList;
static ComPtr<ID3D12Fence> g_fence;
static HANDLE g_fenceEvent = nullptr;
static UINT64 g_nextFenceValue = 1;
static UINT g_frameIndex = 0;
static bool g_frameOpen = false;

// A persistently-mapped upload buffer that outlives single frames. Chunk-meshing bursts
// overflow any fixed ring; overflow allocations suballocate ring-sized blocks borrowed from a
// recycled pool (one committed allocation per BLOCK, not per upload).
struct PooledUpload {
    ComPtr<ID3D12Resource> resource;
    UINT64 size = 0;
    uint8_t* cpu = nullptr;
};

// A recycled DEFAULT-heap buffer. Section meshing creates and destroys hundreds of vertex/index
// buffers while chunks generate; recycling them by size bucket removes that allocation churn.
struct PooledBuffer {
    ComPtr<ID3D12Resource> resource;
    UINT64 byteWidth = 0;
    D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_GENERIC_READ;
};

struct FrameResources {
    ComPtr<ID3D12CommandAllocator> allocator;
    UINT64 fenceValue = 0;
    // Per-frame upload arena for uniform snapshots + small resource updates (VulkanMod's
    // per-frame UniformBuffer.reset() pattern).
    ComPtr<ID3D12Resource> uploadRing;
    uint8_t* uploadRingCpu = nullptr;
    UINT64 uploadRingOffset = 0;
    // Transient upload buffers (big texture uploads) and resources destroyed while possibly
    // still referenced by this frame's command list; released once the fence passes.
    std::vector<ComPtr<ID3D12Resource>> garbage;
    // Pooled upload blocks referenced by this frame's copies; recycled once the fence passes.
    // The last block is the active one; pooledOffset is the suballocation cursor within it.
    std::vector<PooledUpload> pooledInFlight;
    UINT64 pooledOffset = 0;
    // Destroyed DEFAULT-heap buffers possibly still referenced by this frame; returned to the
    // buffer pool once the fence passes.
    std::vector<PooledBuffer> bufferReturns;
    // Shader-visible SRV ring for this frame's draws.
    UINT gpuSrvOffset = 0;
    // 1-entry dedup cache: consecutive draws sharing the same SRV triple (a sprite's mip blits,
    // gui runs on one atlas) reuse one table instead of allocating a new one per draw.
    UINT srvCacheSlots[3] = { 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu };
    D3D12_GPU_DESCRIPTOR_HANDLE srvCacheTable = {};
    UINT srvCacheGeneration = 0;
    bool srvCacheValid = false;
};
static FrameResources g_frames[kFrameCount];
static ComPtr<ID3D12DescriptorHeap> g_gpuSrvHeaps[kFrameCount];
// Bumped whenever a CPU-heap SRV slot is (re)written so the per-frame table dedup cache can
// never return a table copied from since-overwritten descriptor bytes.
static UINT g_srvWriteGeneration = 0;
static UINT g_midFrameFlushCount = 0;
static bool g_terrainBatchActive = false;
// Free pooled upload blocks (persistently mapped), bounded so idle scenes shed the peak.
static std::vector<PooledUpload> g_uploadPool;
static UINT64 g_uploadPoolBytes = 0;
static UINT g_uploadPoolCreated = 0;
static constexpr UINT64 kUploadPoolMaxBytes = 96ull * 1024 * 1024;
// Free pooled DEFAULT-heap buffers, bounded likewise.
static std::vector<PooledBuffer> g_defaultBufferPool;
static UINT64 g_defaultBufferPoolBytes = 0;
static constexpr UINT64 kDefaultBufferPoolMaxBytes = 128ull * 1024 * 1024;

// Returns a frame's fence-cleared pooled resources to the free pools (excess is released).
static void RecyclePooledUploads(FrameResources& frame) {
    for (auto& pooled : frame.pooledInFlight) {
        if (g_uploadPoolBytes + pooled.size <= kUploadPoolMaxBytes) {
            g_uploadPoolBytes += pooled.size;
            g_uploadPool.push_back(std::move(pooled));
        }
    }
    frame.pooledInFlight.clear();
    frame.pooledOffset = 0;
    for (auto& returned : frame.bufferReturns) {
        if (g_defaultBufferPoolBytes + returned.byteWidth <= kDefaultBufferPoolMaxBytes) {
            g_defaultBufferPoolBytes += returned.byteWidth;
            g_defaultBufferPool.push_back(std::move(returned));
        }
    }
    frame.bufferReturns.clear();
}

static wchar_t g_logPath[MAX_PATH] = {};
static wchar_t g_adapterDescription[256] = L"Direct3D 12|unknown adapter";
// Swapchain sync interval: 0 = uncapped (flip-model presents never tear; queued frames are
// replaced), 1 = vsync. Defaults to uncapped so the in-game framerate limiter is the cap.
static UINT g_syncInterval = 0;
static int g_width = 0;
static int g_height = 0;
static bool g_loggedFirstPresent = false;
static bool g_loggedFirstTriangle = false;
static bool g_loggedFirstGuiDraw = false;
static bool g_loggedFirstGuiUniforms = false;
static bool g_loggedFirstTerrainDraw = false;
static bool g_loggedFirstLightmapDraw = false;
static bool g_loggedFirstEntityDraw = false;
static bool g_loggedFirstParticleDraw = false;
// Backbuffer snapshot for post passes whose input is minecraft:main.
static ComPtr<ID3D12Resource> g_postSnapshot;
static UINT g_postSnapshotWidth = 0;
static UINT g_postSnapshotHeight = 0;
static UINT g_postSnapshotSrvSlot = 0;
static D3D12_RESOURCE_STATES g_postSnapshotState = D3D12_RESOURCE_STATE_COPY_DEST;
static int g_loggedPostPasses = 0;
static int g_loggedGlintBakes = 0;
static int g_loggedReadbacks = 0;
static int g_loggedTextDraws = 0;
static int g_loggedSpriteBlits = 0;
static bool g_loggedWorldSimpleKinds[11] = {};
static int g_loggedBufferCreates = 0;
static int g_loggedTextureCreates = 0;
static int g_loggedBufferUpdates = 0;
static int g_loggedTextureUpdates = 0;

static void Log(const char* fmt, ...) {
    char line[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf_s(line, sizeof(line), _TRUNCATE, fmt, args);
    va_end(args);
    OutputDebugStringA("bandit_d3d12_backend: ");
    OutputDebugStringA(line);
    OutputDebugStringA("\n");
    if (g_logPath[0] == L'\0') {
        return;
    }
    FILE* f = nullptr;
    if (_wfopen_s(&f, g_logPath, L"a, ccs=UTF-8") == 0 && f) {
        fwprintf(f, L"%S\n", line);
        fclose(f);
    }
}

static void LogHr(const char* stage, HRESULT hr) {
    Log("%s failed hr=0x%08X", stage, static_cast<unsigned>(hr));
}

static bool SucceededOrLog(const char* stage, HRESULT hr) {
    if (SUCCEEDED(hr)) {
        Log("%s ok", stage);
        return true;
    }
    LogHr(stage, hr);
    return false;
}

static bool CompileShader(const char* name, const char* source, const char* target, ID3DBlob** outBlob) {
    UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
    ComPtr<ID3DBlob> errors;
    HRESULT hr = D3DCompile(source, strlen(source), name, nullptr, nullptr, "main", target, flags, 0, outBlob, errors.GetAddressOf());
    if (FAILED(hr)) {
        if (errors) {
            Log("%s compile errors: %.*s", name, static_cast<int>(errors->GetBufferSize()), static_cast<const char*>(errors->GetBufferPointer()));
        }
        LogHr(name, hr);
        return false;
    }
    return true;
}

// ---- Descriptor heaps ------------------------------------------------------------------------

static ComPtr<ID3D12DescriptorHeap> g_cpuSrvHeap;      // staging SRVs, one per texture + buffer views
static UINT g_cpuSrvNext = 1;                          // slot 0 = null SRV
static std::vector<UINT> g_cpuSrvFree;
static ComPtr<ID3D12DescriptorHeap> g_rtvHeap;         // texture-mip RTVs
static UINT g_rtvNext = 0;
static std::vector<UINT> g_rtvFree;
static ComPtr<ID3D12DescriptorHeap> g_dsvHeap;
static UINT g_dsvNext = 0;

static D3D12_CPU_DESCRIPTOR_HANDLE CpuSrvHandle(UINT slot) {
    D3D12_CPU_DESCRIPTOR_HANDLE h = g_cpuSrvHeap->GetCPUDescriptorHandleForHeapStart();
    h.ptr += static_cast<SIZE_T>(slot) * g_srvDescriptorSize;
    return h;
}

static UINT AllocCpuSrvSlot() {
    if (!g_cpuSrvFree.empty()) {
        UINT slot = g_cpuSrvFree.back();
        g_cpuSrvFree.pop_back();
        return slot;
    }
    if (g_cpuSrvNext >= kCpuSrvHeapCapacity) {
        Log("cpu srv heap exhausted");
        return 0;
    }
    return g_cpuSrvNext++;
}

static D3D12_CPU_DESCRIPTOR_HANDLE RtvHandle(UINT slot) {
    D3D12_CPU_DESCRIPTOR_HANDLE h = g_rtvHeap->GetCPUDescriptorHandleForHeapStart();
    h.ptr += static_cast<SIZE_T>(slot) * g_rtvDescriptorSize;
    return h;
}

static UINT AllocRtvSlot() {
    if (!g_rtvFree.empty()) {
        UINT slot = g_rtvFree.back();
        g_rtvFree.pop_back();
        return slot;
    }
    if (g_rtvNext >= kRtvHeapCapacity) {
        Log("rtv heap exhausted");
        return UINT_MAX;
    }
    return g_rtvNext++;
}

static D3D12_CPU_DESCRIPTOR_HANDLE DsvHandle(UINT slot) {
    D3D12_CPU_DESCRIPTOR_HANDLE h = g_dsvHeap->GetCPUDescriptorHandleForHeapStart();
    h.ptr += static_cast<SIZE_T>(slot) * g_dsvDescriptorSize;
    return h;
}

// ---- Resources -------------------------------------------------------------------------------

struct NativeBuffer {
    ComPtr<ID3D12Resource> resource;
    UINT64 byteWidth = 0;
    int usage = 0;
    D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_GENERIC_READ;
};

struct NativeTexture {
    ComPtr<ID3D12Resource> resource;
    UINT width = 0;
    UINT height = 0;
    UINT mipLevels = 0;
    UINT pixelSize = 0;
    DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM;
    D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COPY_DEST;
    UINT srvSlot = 0;
    UINT layers = 1;    // 6 for cubemaps
    bool cube = false;  // SRV is TEXTURECUBE
    UINT rtvSlots[16];  // per mip, UINT_MAX = not created
};

static std::mutex g_resourceMutex;
static std::unordered_map<int64_t, NativeBuffer> g_buffers;
static std::unordered_map<int64_t, NativeTexture> g_textures;
static std::unordered_map<uint64_t, UINT> g_bufferSrvSlots;  // CloudFaces-style typed buffer SRVs
static int64_t g_nextBufferHandle = 1;
static int64_t g_nextTextureHandle = 1;

static constexpr int USAGE_VERTEX = 32;
static constexpr int USAGE_INDEX = 64;
static constexpr int USAGE_UNIFORM = 128;

static constexpr D3D12_RESOURCE_STATES kTextureReadState =
    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;

// Offscreen depth buffers for texture-target bakes, keyed by size (cleared to 1.0 at creation).
struct OffscreenDepth {
    ComPtr<ID3D12Resource> texture;
    UINT dsvSlot = UINT_MAX;
};
static std::unordered_map<uint64_t, OffscreenDepth> g_offscreenDepths;
static ComPtr<ID3D12Resource> g_mainDepth;
static UINT g_mainDepthDsvSlot = UINT_MAX;

// ---- Frame lifecycle -------------------------------------------------------------------------

static void TransitionResource(ID3D12Resource* resource, D3D12_RESOURCE_STATES& state, D3D12_RESOURCE_STATES newState) {
    if (state == newState || !resource) {
        return;
    }
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = resource;
    barrier.Transition.StateBefore = state;
    barrier.Transition.StateAfter = newState;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    g_commandList->ResourceBarrier(1, &barrier);
    state = newState;
}

static D3D12_CPU_DESCRIPTOR_HANDLE BackBufferRtv(UINT index) {
    D3D12_CPU_DESCRIPTOR_HANDLE handle = g_backBufferRtvHeap->GetCPUDescriptorHandleForHeapStart();
    handle.ptr += static_cast<SIZE_T>(index) * g_rtvDescriptorSize;
    return handle;
}

static void WaitForGpuIdle() {
    if (!g_commandQueue || !g_fence) {
        return;
    }
    const UINT64 value = g_nextFenceValue++;
    g_commandQueue->Signal(g_fence.Get(), value);
    if (g_fence->GetCompletedValue() < value) {
        g_fence->SetEventOnCompletion(value, g_fenceEvent);
        WaitForSingleObjectEx(g_fenceEvent, INFINITE, FALSE);
    }
}

static bool EnsureFrameOpen() {
    if (!g_device || !g_commandList) {
        return false;
    }
    if (g_frameOpen) {
        return true;
    }
    g_frameIndex = g_swapChain->GetCurrentBackBufferIndex();
    FrameResources& frame = g_frames[g_frameIndex];
    if (g_fence->GetCompletedValue() < frame.fenceValue) {
        g_fence->SetEventOnCompletion(frame.fenceValue, g_fenceEvent);
        WaitForSingleObjectEx(g_fenceEvent, INFINITE, FALSE);
    }
    RecyclePooledUploads(frame);
    frame.garbage.clear();
    frame.uploadRingOffset = 0;
    frame.gpuSrvOffset = 0;
    frame.srvCacheValid = false;

    HRESULT hr = frame.allocator->Reset();
    if (FAILED(hr)) {
        LogHr("CommandAllocator::Reset", hr);
        return false;
    }
    hr = g_commandList->Reset(frame.allocator.Get(), nullptr);
    if (FAILED(hr)) {
        LogHr("CommandList::Reset", hr);
        return false;
    }

    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = g_backBuffers[g_frameIndex].Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    g_commandList->ResourceBarrier(1, &barrier);

    ID3D12DescriptorHeap* heaps[] = { g_gpuSrvHeaps[g_frameIndex].Get() };
    g_commandList->SetDescriptorHeaps(1, heaps);
    return g_frameOpen = true;
}

// Allocates from the frame's upload arena (256-aligned). Falls back to a transient committed
// upload buffer parked on the frame's garbage list when the arena is full or the payload huge.
struct UploadAlloc {
    ID3D12Resource* resource = nullptr;
    UINT64 offset = 0;
    uint8_t* cpu = nullptr;
    D3D12_GPU_VIRTUAL_ADDRESS gpuVa = 0;
};

static bool CreateUploadBuffer(UINT64 size, ComPtr<ID3D12Resource>& out) {
    D3D12_HEAP_PROPERTIES heap = {};
    heap.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = size;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    HRESULT hr = g_device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(out.GetAddressOf()));
    if (FAILED(hr)) {
        LogHr("CreateCommittedResource(upload)", hr);
        return false;
    }
    return true;
}

static bool AllocUpload(UINT64 size, UINT64 alignment, UploadAlloc& out) {
    FrameResources& frame = g_frames[g_frameIndex];
    UINT64 offset = (frame.uploadRingOffset + alignment - 1) & ~(alignment - 1);
    if (frame.uploadRing && offset + size <= kCbufferRingSize) {
        frame.uploadRingOffset = offset + size;
        out.resource = frame.uploadRing.Get();
        out.offset = offset;
        out.cpu = frame.uploadRingCpu + offset;
        out.gpuVa = frame.uploadRing->GetGPUVirtualAddress() + offset;
        return true;
    }
    // Ring is full: suballocate from the frame's current pooled block.
    if (!frame.pooledInFlight.empty()) {
        PooledUpload& block = frame.pooledInFlight.back();
        UINT64 blockOffset = (frame.pooledOffset + alignment - 1) & ~(alignment - 1);
        if (blockOffset + size <= block.size) {
            frame.pooledOffset = blockOffset + size;
            out.resource = block.resource.Get();
            out.offset = blockOffset;
            out.cpu = block.cpu + blockOffset;
            out.gpuVa = block.resource->GetGPUVirtualAddress() + blockOffset;
            return true;
        }
    }
    // Need a fresh block: ring-sized (or bigger for a huge single payload) so meshing bursts
    // suballocate instead of paying one committed allocation per upload.
    const UINT64 want = size > kCbufferRingSize ? size : kCbufferRingSize;
    PooledUpload pooled;
    int best = -1;
    for (int i = 0; i < static_cast<int>(g_uploadPool.size()); ++i) {
        if (g_uploadPool[i].size >= want && (best < 0 || g_uploadPool[i].size < g_uploadPool[best].size)) {
            best = i;
        }
    }
    if (best >= 0) {
        pooled = std::move(g_uploadPool[best]);
        g_uploadPool.erase(g_uploadPool.begin() + best);
        g_uploadPoolBytes -= pooled.size;
    } else {
        UINT64 rounded = kCbufferRingSize;
        while (rounded < want) {
            rounded <<= 1;
        }
        ComPtr<ID3D12Resource> res;
        if (!CreateUploadBuffer(rounded, res)) {
            return false;
        }
        void* mapped = nullptr;
        D3D12_RANGE readRange = {};
        if (FAILED(res->Map(0, &readRange, &mapped))) {
            Log("Map(pooled upload) failed");
            return false;
        }
        pooled.resource = std::move(res);
        pooled.size = rounded;
        pooled.cpu = static_cast<uint8_t*>(mapped);
        ++g_uploadPoolCreated;
        if (g_uploadPoolCreated <= 16) {
            Log("upload pool grew: block #%u (%llu MB)", g_uploadPoolCreated, static_cast<unsigned long long>(pooled.size / (1024 * 1024)));
        }
    }
    out.resource = pooled.resource.Get();
    out.offset = 0;
    out.cpu = pooled.cpu;
    out.gpuVa = pooled.resource->GetGPUVirtualAddress();
    frame.pooledOffset = size;
    frame.pooledInFlight.push_back(std::move(pooled));
    return true;
}

// Copies uniform bytes into the frame arena and returns the GPU VA for a root CBV.
static D3D12_GPU_VIRTUAL_ADDRESS UploadCbuffer(const void* data, UINT64 size, UINT64 padTo) {
    UploadAlloc alloc;
    UINT64 total = size > padTo ? size : padTo;
    if (!AllocUpload(total, 256, alloc)) {
        return 0;
    }
    std::memcpy(alloc.cpu, data, static_cast<size_t>(size));
    if (total > size) {
        std::memset(alloc.cpu + size, 0, static_cast<size_t>(total - size));
    }
    return alloc.gpuVa;
}

// Submits the open command list mid-frame, drains the GPU, and resets the frame's arenas so
// recording can continue with a clean SRV ring. Resource-reload frames record thousands of
// draws (one sprite blit per atlas sprite per mip); wrapping the ring instead would alias
// descriptors still referenced by earlier draws in the same unsubmitted list, because the GPU
// reads descriptors at execution time — that scrambled every atlas. Every draw path re-binds
// root signature/PSO/targets/viewport itself, so the only list-level state to restore here is
// the descriptor heap. Never fires inside a terrain batch (sections allocate no tables).
static void FlushMidFrame() {
    FrameResources& frame = g_frames[g_frameIndex];
    if (!g_frameOpen) {
        frame.gpuSrvOffset = 0;
        return;
    }
    if (g_terrainBatchActive) {
        Log("WARNING: mid-frame flush during terrain batch (batch uniforms invalidated)");
    }
    HRESULT hr = g_commandList->Close();
    if (FAILED(hr)) {
        LogHr("CommandList::Close(midframe)", hr);
        return;
    }
    ID3D12CommandList* lists[] = { g_commandList.Get() };
    g_commandQueue->ExecuteCommandLists(1, lists);
    WaitForGpuIdle();
    RecyclePooledUploads(frame);
    frame.garbage.clear();
    frame.uploadRingOffset = 0;
    frame.gpuSrvOffset = 0;
    frame.srvCacheValid = false;
    hr = frame.allocator->Reset();
    if (FAILED(hr)) {
        LogHr("CommandAllocator::Reset(midframe)", hr);
    }
    hr = g_commandList->Reset(frame.allocator.Get(), nullptr);
    if (FAILED(hr)) {
        LogHr("CommandList::Reset(midframe)", hr);
        g_frameOpen = false;
        return;
    }
    ID3D12DescriptorHeap* heaps[] = { g_gpuSrvHeaps[g_frameIndex].Get() };
    g_commandList->SetDescriptorHeaps(1, heaps);
    ++g_midFrameFlushCount;
    if (g_midFrameFlushCount <= 16) {
        Log("mid-frame flush #%u: srv ring full, gpu drained, arenas reset", g_midFrameFlushCount);
    }
}

// Allocates a contiguous 3-descriptor SRV table in the frame's shader-visible heap and copies
// the given CPU-side SRV slots into it (slot 0 = null SRV for unbound).
static D3D12_GPU_DESCRIPTOR_HANDLE AllocSrvTable(UINT slot0, UINT slot1, UINT slot2) {
    FrameResources& frame = g_frames[g_frameIndex];
    if (frame.srvCacheValid && frame.srvCacheGeneration == g_srvWriteGeneration &&
        frame.srvCacheSlots[0] == slot0 && frame.srvCacheSlots[1] == slot1 && frame.srvCacheSlots[2] == slot2) {
        return frame.srvCacheTable;
    }
    if (frame.gpuSrvOffset + 3 > kGpuSrvHeapCapacity) {
        FlushMidFrame();
    }
    UINT base = frame.gpuSrvOffset;
    frame.gpuSrvOffset += 3;
    D3D12_CPU_DESCRIPTOR_HANDLE dst = g_gpuSrvHeaps[g_frameIndex]->GetCPUDescriptorHandleForHeapStart();
    dst.ptr += static_cast<SIZE_T>(base) * g_srvDescriptorSize;
    const UINT slots[3] = { slot0, slot1, slot2 };
    for (int i = 0; i < 3; ++i) {
        D3D12_CPU_DESCRIPTOR_HANDLE d = dst;
        d.ptr += static_cast<SIZE_T>(i) * g_srvDescriptorSize;
        g_device->CopyDescriptorsSimple(1, d, CpuSrvHandle(slots[i]), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    }
    D3D12_GPU_DESCRIPTOR_HANDLE gpu = g_gpuSrvHeaps[g_frameIndex]->GetGPUDescriptorHandleForHeapStart();
    gpu.ptr += static_cast<UINT64>(base) * g_srvDescriptorSize;
    frame.srvCacheSlots[0] = slot0;
    frame.srvCacheSlots[1] = slot1;
    frame.srvCacheSlots[2] = slot2;
    frame.srvCacheTable = gpu;
    frame.srvCacheGeneration = g_srvWriteGeneration;
    frame.srvCacheValid = true;
    return gpu;
}
// ---- Root signature, shaders, PSO cache ------------------------------------------------------

enum Program {
    PROG_TRIANGLE = 0,
    PROG_GUI,
    PROG_GUI_TEXTURED,
    PROG_GUI_TEXT,
    PROG_SPRITE_BLIT,
    PROG_SPRITE_INTERP,
    PROG_LIGHTMAP,
    PROG_TERRAIN,
    PROG_SKY,
    PROG_POS_COLOR,
    PROG_STARS,
    PROG_POS_TEX,
    PROG_LINES,
    PROG_SHADOW,
    PROG_CLOUDS,
    PROG_ENTITY,
    PROG_PARTICLE,
    PROG_CRUMBLING,
    PROG_GUI_TEXTURED_POINT,
    PROG_GUI_TEXT_POINT,
    PROG_PANORAMA,
    PROG_LEASH,
    PROG_POST_BLUR,
    PROG_GLINT,
    PROG_GLINT_FLIP,   // icon re-bake variant (render-to-texture Y flip)
    PROG_TEXT,
    PROG_COUNT
};

enum BlendMode {
    BLEND_NONE = 0,
    BLEND_TRANSLUCENT,
    BLEND_ADDITIVE,       // SRC_ALPHA, ONE (mojang_logo, nausea, OVERLAY)
    BLEND_PREMULTIPLIED,  // ONE, INV_SRC_ALPHA
    BLEND_INVERT,         // INV_DST_COLOR, INV_SRC_COLOR (crosshair)
    BLEND_VIGNETTE,       // ZERO, INV_SRC_COLOR
    BLEND_CRUMBLING,      // DST_COLOR, SRC_COLOR
    BLEND_GLINT,          // SRC_COLOR, ONE; alpha ZERO, ONE
    BLEND_MODE_COUNT
};

enum DepthMode {
    DEPTH_OFF = 0,
    DEPTH_LEQUAL_WRITE,
    DEPTH_LEQUAL_NO_WRITE,
    DEPTH_EQUAL_NO_WRITE,  // vanilla EQUAL_DEPTH_TEST (glint)
    DEPTH_MODE_COUNT
};

enum RasterMode {
    RASTER_NONE_CCW = 0,   // cull none, GL winding
    RASTER_BACK_CCW,       // cull back, GL winding
    RASTER_NONE_CW,        // flipped-Y render-to-texture variants
    RASTER_BACK_CW,
    RASTER_CRUMBLING,      // cull back CCW + polygonOffset(-3,-3) equivalent bias
    RASTER_TEXT_OFFSET,    // cull back CCW + polygonOffset(-1,-10)
    RASTER_MODE_COUNT
};

static ComPtr<ID3D12RootSignature> g_rootSignature;
static ComPtr<ID3DBlob> g_vsBlobs[PROG_COUNT];
static ComPtr<ID3DBlob> g_psBlobs[PROG_COUNT];
static std::unordered_map<uint64_t, ComPtr<ID3D12PipelineState>> g_psoCache;
static bool g_shadersReady = false;

static bool CreateRootSignature() {
    if (g_rootSignature) {
        return true;
    }
    // params: b0..b4 root CBVs (frame-arena snapshots), one SRV table t0..t2.
    D3D12_ROOT_PARAMETER params[6] = {};
    for (int i = 0; i < 5; ++i) {
        params[i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        params[i].Descriptor.ShaderRegister = static_cast<UINT>(i);
        params[i].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    }
    D3D12_DESCRIPTOR_RANGE srvRange = {};
    srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvRange.NumDescriptors = 3;
    srvRange.BaseShaderRegister = 0;
    params[5].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[5].DescriptorTable.NumDescriptorRanges = 1;
    params[5].DescriptorTable.pDescriptorRanges = &srvRange;
    params[5].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    // Static samplers: s0 linear-clamp mip0 (gui/lightmap/shadow), s1 point-clamp
    // (sprites/entities/particles), s2 linear-clamp all mips (terrain atlas),
    // s3 point-wrap (rain/snow V spans repeats), s4 linear-wrap (glint UV scroll).
    D3D12_STATIC_SAMPLER_DESC samplers[5] = {};
    for (int i = 0; i < 5; ++i) {
        samplers[i].AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samplers[i].AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samplers[i].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samplers[i].ShaderRegister = static_cast<UINT>(i);
        samplers[i].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        samplers[i].MaxAnisotropy = 1;
        samplers[i].ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    }
    samplers[0].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    samplers[0].MaxLOD = 0.0f;
    samplers[1].Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
    samplers[1].MaxLOD = D3D12_FLOAT32_MAX;
    samplers[2].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    samplers[2].MaxLOD = D3D12_FLOAT32_MAX;
    samplers[3].Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
    samplers[3].MaxLOD = D3D12_FLOAT32_MAX;
    samplers[3].AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    samplers[3].AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    samplers[3].AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    samplers[4].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    samplers[4].MaxLOD = D3D12_FLOAT32_MAX;
    samplers[4].AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    samplers[4].AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    samplers[4].AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;

    D3D12_ROOT_SIGNATURE_DESC desc = {};
    desc.NumParameters = 6;
    desc.pParameters = params;
    desc.NumStaticSamplers = 5;
    desc.pStaticSamplers = samplers;
    desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ComPtr<ID3DBlob> blob;
    ComPtr<ID3DBlob> errors;
    HRESULT hr = D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, blob.GetAddressOf(), errors.GetAddressOf());
    if (FAILED(hr)) {
        if (errors) {
            Log("root signature errors: %.*s", static_cast<int>(errors->GetBufferSize()), static_cast<const char*>(errors->GetBufferPointer()));
        }
        LogHr("D3D12SerializeRootSignature", hr);
        return false;
    }
    hr = g_device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(g_rootSignature.GetAddressOf()));
    return SucceededOrLog("CreateRootSignature", hr);
}

// All HLSL below is carried verbatim from the D3D11 backend (proven on hardware) with sampler
// registers remapped to the static-sampler plan. Compiled once as vs_5_0/ps_5_0.
static bool CompileAllShaders() {
    if (g_shadersReady) {
        return true;
    }

static const char kGuiUniformBlocks[] =
    "cbuffer DynamicTransforms : register(b0) {"
    "  float4 MV0; float4 MV1; float4 MV2; float4 MV3;"
    "  float4 ColorModulator;"
    "  float4 ModelOffset;"
    "  float4 TexMat0; float4 TexMat1; float4 TexMat2; float4 TexMat3;"
    "};"
    "cbuffer Projection : register(b1) {"
    "  float4 P0; float4 P1; float4 P2; float4 P3;"
    "};"
    // core/gui, core/position_tex_color and core/rendertype_text all compute
    // ProjMat * ModelViewMat * pos and ignore ModelOffset/TextureMat, so the GUI
    // transform does the same. GL clip z is [-w,w] while D3D expects [0,w]; remap so
    // vanilla ortho matrices (0..w, 0..h, near 1000, far 11000) work unchanged.
    "float4 gui_transform(float3 p) {"
    "  float4 world = MV0 * p.x + MV1 * p.y + MV2 * p.z + MV3;"
    "  float4 clip = P0 * world.x + P1 * world.y + P2 * world.z + P3 * world.w;"
    "  clip.z = (clip.z + clip.w) * 0.5;"
    "  return clip;"
    "}";

    // Mirrors core/gui.vsh: clip = Proj * ModelView * pos, vertex color passthrough.
    const std::string guiVsSource = std::string(kGuiUniformBlocks) +
        "struct VSIn { float3 pos : POSITION; float4 color : COLOR0; };"
        "struct VSOut { float4 pos : SV_Position; float4 color : COLOR0; };"
        "VSOut main(VSIn input) {"
        "  VSOut output;"
        "  output.pos = gui_transform(input.pos);"
        "  output.color = input.color;"
        "  return output;"
        "}";

    // Mirrors core/gui.fsh: discard on zero vertex alpha, then multiply by ColorModulator.
    const std::string guiPsSource = std::string(kGuiUniformBlocks) +
        "struct PSIn { float4 pos : SV_Position; float4 color : COLOR0; };"
        "float4 main(PSIn input) : SV_Target {"
        "  float4 color = input.color;"
        "  if (color.a == 0.0) discard;"
        "  return color * ColorModulator;"
        "}";

    const std::string guiTexturedVsSource = std::string(kGuiUniformBlocks) +
        "struct VSIn { float3 pos : POSITION; float2 uv : TEXCOORD0; float4 color : COLOR0; };"
        "struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; float4 color : COLOR0; };"
        "VSOut main(VSIn input) {"
        "  VSOut output;"
        "  output.pos = gui_transform(input.pos);"
        "  output.uv = input.uv;"
        "  output.color = input.color;"
        "  return output;"
        "}";

    // Mirrors core/position_tex_color.fsh. psMode bit 0x100 (bound sampler mag filter, from
    // Java) picks the s0 linear / s1 point variant.
    static const char* guiTexturedPsBody =
        "struct PSIn { float4 pos : SV_Position; float2 uv : TEXCOORD0; float4 color : COLOR0; };"
        "float4 main(PSIn input) : SV_Target {"
        "  float4 color = GuiTexture.Sample(GuiSampler, input.uv) * input.color;"
        "  if (color.a == 0.0) discard;"
        "  return color * ColorModulator;"
        "}";
    const std::string guiTexturedPsSource = std::string(kGuiUniformBlocks) +
        "Texture2D GuiTexture : register(t0);"
        "SamplerState GuiSampler : register(s0);" + guiTexturedPsBody;
    const std::string guiTexturedPsPointSource = std::string(kGuiUniformBlocks) +
        "Texture2D GuiTexture : register(t0);"
        "SamplerState GuiSampler : register(s1);" + guiTexturedPsBody;

    static const char* guiTextPsBody =
        "struct PSIn { float4 pos : SV_Position; float2 uv : TEXCOORD0; float4 color : COLOR0; };"
        "float4 main(PSIn input) : SV_Target {"
        "  float4 color = GuiTexture.Sample(GuiSampler, input.uv) * input.color * ColorModulator;"
        "  if (color.a < 0.1) discard;"
        "  return color;"
        "}";
    const std::string guiTextPsSource = std::string(kGuiUniformBlocks) +
        "Texture2D GuiTexture : register(t0);"
        "SamplerState GuiSampler : register(s0);" + guiTextPsBody;
    const std::string guiTextPsPointSource = std::string(kGuiUniformBlocks) +
        "Texture2D GuiTexture : register(t0);"
        "SamplerState GuiSampler : register(s1);" + guiTextPsBody;

    static const char* spriteCommon =
        "cbuffer SpriteAnimationInfo : register(b0) {"
        "  float4 PM0; float4 PM1; float4 PM2; float4 PM3;"
        "  float4 SM0; float4 SM1; float4 SM2; float4 SM3;"
        "  float UPadding; float VPadding; int MipMapLevel; float SpritePad0;"
        "};"
        "struct SpriteVSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; float progress : TEXCOORD1; };";

    const std::string spriteVsSource = std::string(spriteCommon) +
        // Mirrors core/animate_sprite.vsh. D3D11 SV_VertexID includes StartVertexLocation for
        // non-indexed draws, matching gl_VertexID with glDrawArrays(first, count).
        "static const float2 kPositions[6] = { float2(0,0), float2(1,0), float2(0,1), float2(0,1), float2(1,0), float2(1,1) };"
        "SpriteVSOut main(uint vertexId : SV_VertexID) {"
        "  uint index = vertexId & 7u;"
        "  float frameProgress = float(vertexId >> 3) / 1000.0;"
        "  float2 p = kPositions[index];"
        "  float4 spritePos = SM0 * p.x + SM1 * p.y + SM3;"
        "  float4 clip = PM0 * spritePos.x + PM1 * spritePos.y + PM2 * spritePos.z + PM3 * spritePos.w;"
        // GL fbo space is bottom-up while D3D render targets are top-down; flip Y so the
        // rendered rect lands on the same texel rows the game later samples.
        "  SpriteVSOut output;"
        "  output.pos = float4(clip.x, -clip.y, 0.0, clip.w);"
        "  float2 direction = p * 2.0 - 1.0;"
        "  output.uv = p + float2(UPadding, VPadding) * direction;"
        "  output.progress = frameProgress;"
        "  return output;"
        "}";

    const std::string spriteBlitPsSource = std::string(spriteCommon) +
        "Texture2D Sprite : register(t0);"
        "SamplerState SpriteSampler : register(s1);"
        "float4 main(SpriteVSOut input) : SV_Target {"
        "  return Sprite.SampleLevel(SpriteSampler, input.uv, MipMapLevel);"
        "}";

    const std::string spriteInterpolatePsSource = std::string(spriteCommon) +
        "Texture2D CurrentSprite : register(t0);"
        "Texture2D NextSprite : register(t1);"
        "SamplerState SpriteSampler : register(s1);"
        "float4 main(SpriteVSOut input) : SV_Target {"
        "  float4 currentColor = CurrentSprite.SampleLevel(SpriteSampler, input.uv, MipMapLevel);"
        "  float4 nextColor = NextSprite.SampleLevel(SpriteSampler, input.uv, MipMapLevel);"
        "  return lerp(currentColor, nextColor, input.progress);"
        "}";

    static const char* worldUniformBlocks =
        // ChunkSection (per draw): mat4 ModelViewMat @0, float ChunkVisibility @64,
        // ivec2 TextureSize @72, ivec3 ChunkPosition @80 (total 96).
        "cbuffer ChunkSection : register(b0) {"
        "  float4 CS_MV0; float4 CS_MV1; float4 CS_MV2; float4 CS_MV3;"
        "  float4 CS_Misc0;"  // x=ChunkVisibility, y=pad, z=TextureSize.x, w=TextureSize.y
        "  float4 CS_Misc1;"  // xyz=ChunkPosition (ints)
        "};"
        // Projection: mat4 @0.
        "cbuffer Projection : register(b1) {"
        "  float4 P0; float4 P1; float4 P2; float4 P3;"
        "};"
        // Globals: ivec3 CameraBlockPos @0, vec3 CameraOffset @16, vec2 ScreenSize @32,
        // float GlintAlpha @40, float GameTime @44, int MenuBlurRadius @48, int UseRgss @52.
        "cbuffer Globals : register(b2) {"
        "  float4 G_Misc0;"  // xyz=CameraBlockPos (ints)
        "  float4 G_Misc1;"  // xyz=CameraOffset
        "  float4 G_Misc2;"  // xy=ScreenSize, z=GlintAlpha, w=GameTime
        "  float4 G_Misc3;"  // x=MenuBlurRadius (int), y=UseRgss (int)
        "};"
        // Fog: vec4 FogColor @0, floats @16,20,24,28, FogSkyEnd @32, FogCloudsEnd @36.
        "cbuffer Fog : register(b3) {"
        "  float4 FogColor;"
        "  float4 Fog_Misc0;"  // x=EnvStart, y=EnvEnd, z=RenderDistStart, w=RenderDistEnd
        "  float4 Fog_Misc1;"  // x=FogSkyEnd, y=FogCloudsEnd
        "};"
        "cbuffer TerrainParams : register(b4) {"
        "  float TerrainAlphaCutoff; float3 TerrainPad0;"
        "};"
        "float linear_fog_value(float vertexDistance, float fogStart, float fogEnd) {"
        "  if (vertexDistance <= fogStart) return 0.0;"
        "  if (vertexDistance >= fogEnd) return 1.0;"
        "  return (vertexDistance - fogStart) / (fogEnd - fogStart);"
        "}"
        "float4 apply_fog(float4 inColor, float sphericalDistance, float cylindricalDistance) {"
        "  float fogValue = max(linear_fog_value(sphericalDistance, Fog_Misc0.x, Fog_Misc0.y), linear_fog_value(cylindricalDistance, Fog_Misc0.z, Fog_Misc0.w));"
        "  return float4(lerp(inColor.rgb, FogColor.rgb, fogValue * FogColor.a), inColor.a);"
        "}";

    const std::string terrainVertexSource = std::string(worldUniformBlocks) +
        "Texture2D Lightmap : register(t2);"
        "SamplerState LightmapSampler : register(s0);"
        "struct VSIn { float3 pos : POSITION; float4 color : COLOR0; float2 uv0 : TEXCOORD0; int2 uv2 : TEXCOORD1; };"
        "struct VSOut { float4 pos : SV_Position; float4 color : COLOR0; float2 uv : TEXCOORD0; float sphericalDistance : TEXCOORD2; float cylindricalDistance : TEXCOORD3; };"
        "VSOut main(VSIn input) {"
        "  float3 chunkPosition = float3(asint(CS_Misc1.x), asint(CS_Misc1.y), asint(CS_Misc1.z));"
        "  float3 cameraBlockPos = float3(asint(G_Misc0.x), asint(G_Misc0.y), asint(G_Misc0.z));"
        "  float3 pos = input.pos + (chunkPosition - cameraBlockPos) + G_Misc1.xyz;"
        "  float4 world = CS_MV0 * pos.x + CS_MV1 * pos.y + CS_MV2 * pos.z + CS_MV3;"
        "  float4 clip = P0 * world.x + P1 * world.y + P2 * world.z + P3 * world.w;"
        "  clip.z = (clip.z + clip.w) * 0.5;"
        "  VSOut output;"
        "  output.pos = clip;"
        "  output.sphericalDistance = length(pos);"
        "  output.cylindricalDistance = max(length(pos.xz), abs(pos.y));"
        "  float2 lightUv = clamp((float2(input.uv2) / 256.0) + (0.5 / 16.0), 0.5 / 16.0, 15.5 / 16.0);"
        "  float4 light = Lightmap.SampleLevel(LightmapSampler, lightUv, 0);"
        "  output.color = input.color * light;"
        "  output.uv = input.uv0;"
        "  return output;"
        "}";

    const std::string terrainPixelSource = std::string(worldUniformBlocks) +
        "Texture2D BlockAtlas : register(t0);"
        "SamplerState AtlasSampler : register(s2);"
        "struct PSIn { float4 pos : SV_Position; float4 color : COLOR0; float2 uv : TEXCOORD0; float sphericalDistance : TEXCOORD2; float cylindricalDistance : TEXCOORD3; };"
        "float4 sampleNearestGrad(float2 uv, float2 pixelSize, float2 du, float2 dv, float2 texelScreenSize) {"
        "  float2 uvTexelCoords = uv / pixelSize;"
        "  float2 texelCenter = round(uvTexelCoords) - 0.5;"
        "  float2 texelOffset = uvTexelCoords - texelCenter;"
        "  texelOffset = (texelOffset - 0.5) * pixelSize / texelScreenSize + 0.5;"
        "  texelOffset = clamp(texelOffset, 0.0, 1.0);"
        "  float2 sampleUv = (texelCenter + texelOffset) * pixelSize;"
        "  return BlockAtlas.SampleGrad(AtlasSampler, sampleUv, du, dv);"
        "}"
        "float4 sampleRgss(float2 uv, float2 pixelSize, float2 du, float2 dv) {"
        "  float2 texelScreenSize = sqrt(du * du + dv * dv);"
        "  float maxTexelSize = max(texelScreenSize.x, texelScreenSize.y);"
        "  float minPixelSize = min(pixelSize.x, pixelSize.y);"
        "  float blendFactor = smoothstep(minPixelSize, minPixelSize * 2.0, maxTexelSize);"
        "  float duLength = length(du);"
        "  float dvLength = length(dv);"
        "  float effectiveDerivative = sqrt(min(duLength, dvLength) * max(duLength, dvLength));"
        "  float mipLevelExact = max(0.0, log2(effectiveDerivative / minPixelSize));"
        "  float mipLevelLow = floor(mipLevelExact);"
        "  float mipBlend = frac(mipLevelExact);"
        "  const float2 offsets[4] = { float2(0.125, 0.375), float2(-0.125, -0.375), float2(0.375, -0.125), float2(-0.375, 0.125) };"
        "  float4 rgssLow = 0; float4 rgssHigh = 0;"
        "  [unroll] for (int i = 0; i < 4; ++i) {"
        "    float2 sampleUv = uv + offsets[i] * pixelSize;"
        "    rgssLow += BlockAtlas.SampleLevel(AtlasSampler, sampleUv, mipLevelLow);"
        "    rgssHigh += BlockAtlas.SampleLevel(AtlasSampler, sampleUv, mipLevelLow + 1.0);"
        "  }"
        "  float4 rgssColor = lerp(rgssLow * 0.25, rgssHigh * 0.25, mipBlend);"
        "  float4 nearestColor = sampleNearestGrad(uv, pixelSize, du, dv, texelScreenSize);"
        "  return lerp(nearestColor, rgssColor, blendFactor);"
        "}"
        "float4 main(PSIn input) : SV_Target {"
        "  float2 textureSize = float2(asint(CS_Misc0.z), asint(CS_Misc0.w));"
        "  float2 pixelSize = 1.0 / textureSize;"
        "  float2 du = ddx(input.uv);"
        "  float2 dv = ddy(input.uv);"
        "  float2 texelScreenSize = sqrt(du * du + dv * dv);"
        "  int useRgss = asint(G_Misc3.y);"
        "  float4 sampled = useRgss == 1 ? sampleRgss(input.uv, pixelSize, du, dv) : sampleNearestGrad(input.uv, pixelSize, du, dv, texelScreenSize);"
        "  float4 color = sampled * input.color;"
        "  color = lerp(FogColor * float4(1, 1, 1, color.a), color, CS_Misc0.x);"
        "  if (TerrainAlphaCutoff > 0.0 && color.a < TerrainAlphaCutoff) discard;"
        "  return apply_fog(color, input.sphericalDistance, input.cylindricalDistance);"
        "}";

    static const char* screenquadVertexSource =
        "struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };"
        "VSOut main(uint vertexId : SV_VertexID) {"
        "  float2 uv = float2((vertexId << 1) & 2, vertexId & 2);"
        "  VSOut output;"
        "  output.pos = float4(uv.x * 2.0 - 1.0, -(uv.y * 2.0 - 1.0), 0.0, 1.0);"
        "  output.uv = uv;"
        "  return output;"
        "}";

    static const char* lightmapPixelSource =
        "cbuffer LightmapInfo : register(b0) {"
        "  float4 L0;"  // AmbientLightFactor, SkyFactor, BlockFactor, NightVisionFactor
        "  float4 L1;"  // DarknessScale, DarkenWorldFactor, BrightnessFactor, pad
        "  float4 L2;"  // SkyLightColor.xyz
        "  float4 L3;"  // AmbientColor.xyz
        "};"
        "struct PSIn { float4 pos : SV_Position; float2 uv : TEXCOORD0; };"
        "float get_brightness(float level) { return level / (4.0 - 3.0 * level); }"
        "float3 notGamma(float3 color) {"
        "  float maxComponent = max(max(color.x, color.y), color.z);"
        "  float maxInverted = 1.0 - maxComponent;"
        "  float maxScaled = 1.0 - maxInverted * maxInverted * maxInverted * maxInverted;"
        "  return color * (maxScaled / maxComponent);"
        "}"
        "float4 main(PSIn input) : SV_Target {"
        "  float blockBrightness = get_brightness(floor(input.uv.x * 16.0) / 15.0) * L0.z;"
        "  float skyBrightness = get_brightness(floor(input.uv.y * 16.0) / 15.0) * L0.y;"
        "  float3 color = float3("
        "    blockBrightness,"
        "    blockBrightness * ((blockBrightness * 0.6 + 0.4) * 0.6 + 0.4),"
        "    blockBrightness * (blockBrightness * blockBrightness * 0.6 + 0.4));"
        "  color = lerp(color, L3.xyz, L0.x);"
        "  color += L2.xyz * skyBrightness;"
        "  color = lerp(color, 0.75, 0.04);"
        "  if (L0.x == 0.0) {"
        "    float3 darkened = color * float3(0.7, 0.6, 0.6);"
        "    color = lerp(color, darkened, L1.y);"
        "  }"
        "  if (L0.w > 0.0) {"
        "    float maxComponent = max(color.r, max(color.g, color.b));"
        "    if (maxComponent < 1.0) {"
        "      float3 bright = color / maxComponent;"
        "      color = lerp(color, bright, L0.w);"
        "    }"
        "  }"
        "  if (L0.x == 0.0) color = color - L1.x;"
        "  color = clamp(color, 0.0, 1.0);"
        "  color = lerp(color, notGamma(color), L1.z);"
        "  color = lerp(color, 0.75, 0.04);"
        "  return float4(color, 1.0);"
        "}";

    static const char* skyCommon =
        "cbuffer DynamicTransforms : register(b0) {"
        "  float4 MV0; float4 MV1; float4 MV2; float4 MV3;"
        "  float4 ColorModulator;"
        "  float4 ModelOffset;"
        "  float4 TexMat0; float4 TexMat1; float4 TexMat2; float4 TexMat3;"
        "};"
        "cbuffer Projection : register(b1) {"
        "  float4 P0; float4 P1; float4 P2; float4 P3;"
        "};"
        "cbuffer Fog : register(b2) {"
        "  float4 FogColor;"
        "  float4 Fog_Misc0;"
        "  float4 Fog_Misc1;"
        "};"
        "cbuffer Globals : register(b3) {"
        "  float4 G_Misc0;"  // xyz=CameraBlockPos (ints)
        "  float4 G_Misc1;"  // xyz=CameraOffset
        "  float4 G_Misc2;"  // xy=ScreenSize, z=GlintAlpha, w=GameTime
        "  float4 G_Misc3;"  // x=MenuBlurRadius (int), y=UseRgss (int)
        "};"
        "cbuffer CloudInfo : register(b4) {"
        "  float4 CloudColor;"
        "  float4 CloudOffset;"  // xyz used
        "  float4 CellSize;"     // xyz used
        "};"
        "float4 world_transform_noremap(float3 p) {"
        "  float4 world = MV0 * p.x + MV1 * p.y + MV2 * p.z + MV3;"
        "  return P0 * world.x + P1 * world.y + P2 * world.z + P3 * world.w;"
        "}"
        "float4 world_transform(float3 p) {"
        "  float4 clip = world_transform_noremap(p);"
        "  clip.z = (clip.z + clip.w) * 0.5;"
        "  return clip;"
        "}"
        "float linear_fog_value(float vertexDistance, float fogStart, float fogEnd) {"
        "  if (vertexDistance <= fogStart) return 0.0;"
        "  if (vertexDistance >= fogEnd) return 1.0;"
        "  return (vertexDistance - fogStart) / (fogEnd - fogStart);"
        "}";

    // POSITION_COLOR_NORMAL_LINE_WIDTH has no padding (normal snorm8x3 @16, LineWidth float @19,
    // stride 23), so typed vertex fetch of the 32-bit elements lands on unaligned addresses for
    // odd vertices (23*i + 16). The Xbox D3D12 driver returns garbage for those fetches (the
    // D3D11 driver tolerated them), which blew line widths up into screen-sized quads. Bypass
    // the input assembler entirely: read raw vertex bytes from a ByteAddressBuffer SRV (t0) —
    // raw loads are DWORD-aligned by construction (shift-combined) and out-of-bounds reads are
    // defined to return zero. SV_VertexID already includes BaseVertexLocation, so vertexId*23
    // addresses the right vertex inside the shared mesh buffer.
    const std::string linesVsSource = std::string(skyCommon) +
        "ByteAddressBuffer LinesVb : register(t0);"
        "float snorm8(uint b) { int v = int(b & 0xFFu); if (v > 127) v -= 256; return max(float(v) / 127.0, -1.0); }"
        "uint loadByte(uint addr) { return (LinesVb.Load(addr & ~3u) >> ((addr & 3u) * 8u)) & 0xFFu; }"
        "uint loadDword(uint addr) {"
        "  uint aligned = addr & ~3u;"
        "  uint sh = (addr & 3u) * 8u;"
        "  uint lo = LinesVb.Load(aligned);"
        "  if (sh == 0u) return lo;"
        "  uint hi = LinesVb.Load(aligned + 4u);"
        "  return (lo >> sh) | (hi << (32u - sh));"
        "}"
        "struct VSOut { float4 pos : SV_Position; float4 color : COLOR0; float2 dist : TEXCOORD3; };"
        "VSOut main(uint vertexId : SV_VertexID) {"
        "  uint base = vertexId * 23u;"
        "  float3 inPos = float3(asfloat(loadDword(base)), asfloat(loadDword(base + 4u)), asfloat(loadDword(base + 8u)));"
        "  uint colorBits = loadDword(base + 12u);"
        "  float4 inColor = float4(float(colorBits & 0xFFu), float((colorBits >> 8u) & 0xFFu), float((colorBits >> 16u) & 0xFFu), float((colorBits >> 24u) & 0xFFu)) / 255.0;"
        "  float3 normal = float3(snorm8(loadByte(base + 16u)), snorm8(loadByte(base + 17u)), snorm8(loadByte(base + 18u)));"
        "  float lineWidth = asfloat(loadDword(base + 19u));"
        "  const float VIEW_SHRINK = 1.0 - (1.0 / 256.0);"
        "  float3 shrunkStart = inPos * VIEW_SHRINK;"
        "  float3 shrunkEnd = (inPos + normal) * VIEW_SHRINK;"
        "  float4 linePosStart = world_transform_noremap(shrunkStart);"
        "  float4 linePosEnd = world_transform_noremap(shrunkEnd);"
        "  float3 ndc1 = linePosStart.xyz / linePosStart.w;"
        "  float3 ndc2 = linePosEnd.xyz / linePosEnd.w;"
        "  float2 screenSize = G_Misc2.xy;"
        "  float2 lineScreenDirection = normalize((ndc2.xy - ndc1.xy) * screenSize);"
        "  float2 lineOffset = float2(-lineScreenDirection.y, lineScreenDirection.x) * lineWidth / screenSize;"
        "  if (lineOffset.x < 0.0) lineOffset = -lineOffset;"
        "  float3 offsetNdc = (vertexId % 2u) == 0u ? (ndc1 + float3(lineOffset, 0.0)) : (ndc1 - float3(lineOffset, 0.0));"
        "  float4 clip = float4(offsetNdc * linePosStart.w, linePosStart.w);"
        "  clip.z = (clip.z + clip.w) * 0.5;"
        "  VSOut output;"
        "  output.pos = clip;"
        "  output.color = inColor;"
        "  output.dist = float2(length(inPos), max(length(inPos.xz), abs(inPos.y)));"
        "  return output;"
        "}";

    const std::string linesPsSource = std::string(skyCommon) +
        "struct PSIn { float4 pos : SV_Position; float4 color : COLOR0; float2 dist : TEXCOORD3; };"
        "float4 main(PSIn input) : SV_Target {"
        "  float4 color = input.color * ColorModulator;"
        "  float fogValue = max(linear_fog_value(input.dist.x, Fog_Misc0.x, Fog_Misc0.y), linear_fog_value(input.dist.y, Fog_Misc0.z, Fog_Misc0.w));"
        "  return float4(lerp(color.rgb, FogColor.rgb, fogValue * FogColor.a), color.a);"
        "}";

    const std::string shadowVsSource = std::string(skyCommon) +
        "struct VSIn { float3 pos : POSITION; float4 color : COLOR0; float2 uv : TEXCOORD0; };"
        "struct VSOut { float4 pos : SV_Position; float4 color : COLOR0; float2 uv : TEXCOORD0; float2 dist : TEXCOORD3; };"
        "VSOut main(VSIn input) {"
        "  VSOut output;"
        "  output.pos = world_transform(input.pos);"
        "  output.color = input.color;"
        "  output.uv = input.uv;"
        "  output.dist = float2(length(input.pos), max(length(input.pos.xz), abs(input.pos.y)));"
        "  return output;"
        "}";

    const std::string shadowPsSource = std::string(skyCommon) +
        "Texture2D Tex : register(t0);"
        "SamplerState TexSampler : register(s0);"
        "struct PSIn { float4 pos : SV_Position; float4 color : COLOR0; float2 uv : TEXCOORD0; float2 dist : TEXCOORD3; };"
        "float4 main(PSIn input) : SV_Target {"
        "  float4 color = Tex.Sample(TexSampler, clamp(input.uv, 0.0, 1.0));"
        "  color *= input.color * ColorModulator;"
        "  float fogValue = max(linear_fog_value(input.dist.x, Fog_Misc0.x, Fog_Misc0.y), linear_fog_value(input.dist.y, Fog_Misc0.z, Fog_Misc0.w));"
        "  return float4(lerp(color.rgb, FogColor.rgb, fogValue * FogColor.a), color.a);"
        "}";

    // Crumbling (block breaking overlay) shares the shadow vertex path but must discard the
    // crack texture's transparent texels: with the (DST_COLOR, SRC_COLOR) blend every surviving
    // pixel computes 2*src*dst, so white-under-alpha-0 texels double the framebuffer brightness
    // ("block goes bright while breaking"). Vanilla's crumbling shader discards below 0.1 alpha.
    // Must sample NEAREST (s1): destroy_stage textures are white under alpha-0, so linear-filtered
    // crack edges pass the discard as near-white and 2*s*d doubles the framebuffer (glow halo around cracks).
    const std::string crumblingPsSource = std::string(skyCommon) +
        "Texture2D Tex : register(t0);"
        "SamplerState TexSampler : register(s1);"
        "struct PSIn { float4 pos : SV_Position; float4 color : COLOR0; float2 uv : TEXCOORD0; float2 dist : TEXCOORD3; };"
        "float4 main(PSIn input) : SV_Target {"
        "  float4 color = Tex.Sample(TexSampler, input.uv);"
        "  color *= input.color * ColorModulator;"
        "  if (color.a < 0.1) discard;"
        "  float fogValue = max(linear_fog_value(input.dist.x, Fog_Misc0.x, Fog_Misc0.y), linear_fog_value(input.dist.y, Fog_Misc0.z, Fog_Misc0.w));"
        "  return float4(lerp(color.rgb, FogColor.rgb, fogValue * FogColor.a), color.a);"
        "}";

    const std::string cloudsVsSource = std::string(skyCommon) +
        "Buffer<int> CloudFaces : register(t0);"
        "static const float3 kCloudVertices[24] = {"
        "  float3(1,0,0), float3(1,0,1), float3(0,0,1), float3(0,0,0),"
        "  float3(0,1,0), float3(0,1,1), float3(1,1,1), float3(1,1,0),"
        "  float3(0,0,0), float3(0,1,0), float3(1,1,0), float3(1,0,0),"
        "  float3(1,0,1), float3(1,1,1), float3(0,1,1), float3(0,0,1),"
        "  float3(0,0,1), float3(0,1,1), float3(0,1,0), float3(0,0,0),"
        "  float3(1,0,0), float3(1,1,0), float3(1,1,1), float3(1,0,1)"
        "};"
        "static const float4 kFaceColors[6] = {"
        "  float4(0.7,0.7,0.7,1.0), float4(1.0,1.0,1.0,1.0), float4(0.8,0.8,0.8,1.0),"
        "  float4(0.8,0.8,0.8,1.0), float4(0.9,0.9,0.9,1.0), float4(0.9,0.9,0.9,1.0)"
        "};"
        "struct VSOut { float4 pos : SV_Position; float4 color : COLOR0; float dist : TEXCOORD3; };"
        "VSOut main(uint vertexId : SV_VertexID) {"
        "  uint quadVertex = vertexId % 4u;"
        "  int index = int(vertexId / 4u) * 3;"
        "  int cellX = CloudFaces[index];"
        "  int cellZ = CloudFaces[index + 1];"
        "  int dirAndFlags = CloudFaces[index + 2];"
        "  int direction = dirAndFlags & 7;"
        "  bool isInsideFace = (dirAndFlags & 16) != 0;"
        "  bool useTopColor = (dirAndFlags & 32) != 0;"
        "  cellX = (cellX << 1) | ((dirAndFlags & 128) >> 7);"
        "  cellZ = (cellZ << 1) | ((dirAndFlags & 64) >> 6);"
        "  uint corner = isInsideFace ? (3u - quadVertex) : quadVertex;"
        "  float3 faceVertex = kCloudVertices[direction * 4 + int(corner)];"
        "  float3 pos = (faceVertex * CellSize.xyz) + (float3(cellX, 0, cellZ) * CellSize.xyz) + CloudOffset.xyz;"
        "  VSOut output;"
        "  output.pos = world_transform(pos);"
        "  output.dist = length(pos);"
        "  output.color = (useTopColor ? kFaceColors[1] : kFaceColors[direction]) * CloudColor;"
        "  return output;"
        "}";

    const std::string cloudsPsSource = std::string(skyCommon) +
        "struct PSIn { float4 pos : SV_Position; float4 color : COLOR0; float dist : TEXCOORD3; };"
        "float4 main(PSIn input) : SV_Target {"
        "  float4 color = input.color;"
        "  color.a *= 1.0 - linear_fog_value(input.dist, 0.0, Fog_Misc1.y);"
        "  return color;"
        "}";

    // In-world text: font atlas at t0, lightmap at t1. The polygon-offset and see-through
    // variants differ only in raster/depth state.
    const std::string textVsSource = std::string(skyCommon) +
        "Texture2D LightmapTex : register(t1);"
        "struct VSIn { float3 pos : POSITION; float4 color : COLOR0; float2 uv0 : TEXCOORD0; int2 uv2 : TEXCOORD1; };"
        "struct VSOut { float4 pos : SV_Position; float4 color : COLOR0; float2 uv : TEXCOORD0; float2 dist : TEXCOORD3; };"
        "VSOut main(VSIn input) {"
        "  VSOut output;"
        "  output.pos = world_transform(input.pos);"
        "  output.color = input.color * LightmapTex.Load(int3(input.uv2 / 16, 0));"
        "  output.uv = input.uv0;"
        "  output.dist = float2(length(input.pos), max(length(input.pos.xz), abs(input.pos.y)));"
        "  return output;"
        "}";

    const std::string textPsSource = std::string(skyCommon) +
        "Texture2D FontTex : register(t0);"
        "SamplerState FontSampler : register(s1);"
        "struct PSIn { float4 pos : SV_Position; float4 color : COLOR0; float2 uv : TEXCOORD0; float2 dist : TEXCOORD3; };"
        "float4 main(PSIn input) : SV_Target {"
        "  float4 color = FontTex.Sample(FontSampler, input.uv) * input.color * ColorModulator;"
        "  if (color.a < 0.1) discard;"
        "  float fogValue = max(linear_fog_value(input.dist.x, Fog_Misc0.x, Fog_Misc0.y), linear_fog_value(input.dist.y, Fog_Misc0.z, Fog_Misc0.w));"
        "  return float4(lerp(color.rgb, FogColor.rgb, fogValue * FogColor.a), color.a);"
        "}";
    // Glint re-rasterizes enchanted geometry with depth EQUAL. Fog FADES the effect (no tint);
    // the scrolled UVs leave [0,1] by design, hence s4 linear-wrap.
    const std::string glintVsSource = std::string(skyCommon) +
        "struct VSIn { float3 pos : POSITION; float2 uv : TEXCOORD0; };"
        "struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; float2 dist : TEXCOORD3; };"
        "VSOut main(VSIn input) {"
        "  VSOut output;"
        "  output.pos = world_transform(input.pos);"
        "  float4 uvt = TexMat0 * input.uv.x + TexMat1 * input.uv.y + TexMat3;"
        "  output.uv = uvt.xy;"
        "  output.dist = float2(length(input.pos), max(length(input.pos.xz), abs(input.pos.y)));"
        "  return output;"
        "}";

    const std::string glintPsSource = std::string(skyCommon) +
        "Texture2D GlintTex : register(t0);"
        "SamplerState GlintSampler : register(s4);"
        "struct PSIn { float4 pos : SV_Position; float2 uv : TEXCOORD0; float2 dist : TEXCOORD3; };"
        "float4 main(PSIn input) : SV_Target {"
        "  float4 color = GlintTex.Sample(GlintSampler, input.uv) * ColorModulator;"
        "  if (color.a < 0.1) discard;"
        "  float fogValue = max(linear_fog_value(input.dist.x, Fog_Misc0.x, Fog_Misc0.y), linear_fog_value(input.dist.y, Fog_Misc0.z, Fog_Misc0.w));"
        "  float fade = (1.0 - fogValue) * G_Misc2.z;"
        "  return float4(color.rgb * fade, color.a);"
        "}";

    // Icon re-bakes render Y-flipped (entity bake convention); the overlay must match the base
    // item pixels exactly or the EQUAL depth test rejects everything.
    const std::string glintFlipVsSource = std::string(skyCommon) +
        "struct VSIn { float3 pos : POSITION; float2 uv : TEXCOORD0; };"
        "struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; float2 dist : TEXCOORD3; };"
        "VSOut main(VSIn input) {"
        "  VSOut output;"
        "  output.pos = world_transform(input.pos);"
        "  output.pos.y = -output.pos.y;"
        "  float4 uvt = TexMat0 * input.uv.x + TexMat1 * input.uv.y + TexMat3;"
        "  output.uv = uvt.xy;"
        "  output.dist = float2(length(input.pos), max(length(input.pos.xz), abs(input.pos.y)));"
        "  return output;"
        "}";
    // Menu blur. One top-left uv convention for both sampling and rendering keeps orientation
    // stable across main -> swap -> main with no flips. b0 = SamplerInfo (computed natively),
    // b1 = BlurConfig, b2 = Globals (MenuBlurRadius@48).
    static const char* postBlurVs =
        "struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };"
        "VSOut main(uint id : SV_VertexID) {"
        "  VSOut o;"
        "  float2 uv = float2((id << 1) & 2, id & 2);"
        "  o.pos = float4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, 0.0, 1.0);"
        "  o.uv = uv;"
        "  return o;"
        "}";

    static const char* postBlurPs =
        "cbuffer SamplerInfo : register(b0) { float2 OutSize; float2 InSize; };"
        "cbuffer BlurConfig : register(b1) { float2 BlurDir; float BlurRadius; float BlurPad0; };"
        "cbuffer PostGlobals : register(b2) { float4 PG0; float4 PG1; float4 PG2; int MenuBlurRadius; int PGPad0; int PGPad1; int PGPad2; };"
        "Texture2D InTex : register(t0);"
        "SamplerState InSamp : register(s0);"
        "struct PSIn { float4 pos : SV_Position; float2 uv : TEXCOORD0; };"
        "float4 main(PSIn input) : SV_Target {"
        "  float2 sampleStep = (1.0 / InSize) * BlurDir;"
        "  float4 blurred = float4(0.0, 0.0, 0.0, 0.0);"
        "  float actualRadius = BlurRadius >= 0.5 ? round(BlurRadius) : (float)MenuBlurRadius;"
        "  [loop] for (float a = -actualRadius + 0.5; a <= actualRadius; a += 2.0) {"
        "    blurred += InTex.Sample(InSamp, input.uv + sampleStep * a);"
        "  }"
        "  blurred += InTex.Sample(InSamp, input.uv + sampleStep * actualRadius) / 2.0;"
        "  return blurred / (actualRadius + 0.5);"
        "}";
    // Leash: strip with identity indices (vanilla sequential buffer); nointerpolation matches
    // the GLSL flat qualifier. Lightmap rides t0.
    const std::string leashVsSource = std::string(skyCommon) +
        "Texture2D LightmapTex : register(t0);"
        "struct VSIn { float3 pos : POSITION; float4 color : COLOR0; int2 uv2 : TEXCOORD1; };"
        "struct VSOut { float4 pos : SV_Position; nointerpolation float4 color : COLOR0; float2 dist : TEXCOORD3; };"
        "VSOut main(VSIn input) {"
        "  VSOut output;"
        "  output.pos = world_transform(input.pos);"
        "  output.color = input.color * ColorModulator * LightmapTex.Load(int3(input.uv2 / 16, 0));"
        "  output.dist = float2(length(input.pos), max(length(input.pos.xz), abs(input.pos.y)));"
        "  return output;"
        "}";

    const std::string leashPsSource = std::string(skyCommon) +
        "struct PSIn { float4 pos : SV_Position; nointerpolation float4 color : COLOR0; float2 dist : TEXCOORD3; };"
        "float4 main(PSIn input) : SV_Target {"
        "  float fogValue = max(linear_fog_value(input.dist.x, Fog_Misc0.x, Fog_Misc0.y), linear_fog_value(input.dist.y, Fog_Misc0.z, Fog_Misc0.w));"
        "  return float4(lerp(input.color.rgb, FogColor.rgb, fogValue * FogColor.a), input.color.a);"
        "}";
    // Panorama: the vertex position doubles as the cube sample direction. Vanilla masks alpha
    // writes; forcing a=1 is equivalent since nothing reads backbuffer alpha.
    const std::string panoramaVsSource = std::string(skyCommon) +
        "struct VSIn { float3 pos : POSITION; };"
        "struct VSOut { float4 pos : SV_Position; float3 dir : TEXCOORD0; };"
        "VSOut main(VSIn input) {"
        "  VSOut output;"
        "  output.pos = world_transform(input.pos);"
        "  output.dir = input.pos;"
        "  return output;"
        "}";

    const std::string panoramaPsSource = std::string(skyCommon) +
        "TextureCube PanoramaTex : register(t0);"
        "SamplerState PanoramaSampler : register(s0);"
        "struct PSIn { float4 pos : SV_Position; float3 dir : TEXCOORD0; };"
        "float4 main(PSIn input) : SV_Target {"
        "  return float4(PanoramaTex.Sample(PanoramaSampler, input.dir).rgb, 1.0);"
        "}";
    const std::string skyVsSource = std::string(skyCommon) +
        "struct VSOut { float4 pos : SV_Position; float2 dist : TEXCOORD0; };"
        "VSOut main(float3 pos : POSITION) {"
        "  VSOut output;"
        "  output.pos = world_transform(pos);"
        "  output.dist = float2(length(pos), max(length(pos.xz), abs(pos.y)));"
        "  return output;"
        "}";

    const std::string skyPsSource = std::string(skyCommon) +
        "struct PSIn { float4 pos : SV_Position; float2 dist : TEXCOORD0; };"
        "float4 main(PSIn input) : SV_Target {"
        "  float skyEnd = Fog_Misc1.x;"
        "  float fogValue = max(linear_fog_value(input.dist.x, 0.0, skyEnd), linear_fog_value(input.dist.y, skyEnd, skyEnd));"
        "  return float4(lerp(ColorModulator.rgb, FogColor.rgb, fogValue * FogColor.a), ColorModulator.a);"
        "}";

    const std::string posColorVsSource = std::string(skyCommon) +
        "struct VSIn { float3 pos : POSITION; float4 color : COLOR0; };"
        "struct VSOut { float4 pos : SV_Position; float4 color : COLOR0; };"
        "VSOut main(VSIn input) {"
        "  VSOut output;"
        "  output.pos = world_transform(input.pos);"
        "  output.color = input.color;"
        "  return output;"
        "}";

    const std::string posColorPsSource = std::string(skyCommon) +
        "struct PSIn { float4 pos : SV_Position; float4 color : COLOR0; };"
        "float4 main(PSIn input) : SV_Target {"
        "  if (input.color.a == 0.0) discard;"
        "  return input.color * ColorModulator;"
        "}";

    const std::string starsVsSource = std::string(skyCommon) +
        "float4 main(float3 pos : POSITION) : SV_Position {"
        "  return world_transform(pos);"
        "}";

    const std::string starsPsSource = std::string(skyCommon) +
        "float4 main(float4 pos : SV_Position) : SV_Target {"
        "  return ColorModulator;"
        "}";

    const std::string posTexVsSource = std::string(skyCommon) +
        "struct VSIn { float3 pos : POSITION; float2 uv : TEXCOORD0; };"
        "struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };"
        "VSOut main(VSIn input) {"
        "  VSOut output;"
        "  output.pos = world_transform(input.pos);"
        "  output.uv = input.uv;"
        "  return output;"
        "}";

    const std::string posTexPsSource = std::string(skyCommon) +
        "Texture2D Tex : register(t0);"
        "SamplerState TexSampler : register(s0);"
        "struct PSIn { float4 pos : SV_Position; float2 uv : TEXCOORD0; };"
        "float4 main(PSIn input) : SV_Target {"
        "  float4 color = Tex.Sample(TexSampler, input.uv);"
        "  if (color.a == 0.0) discard;"
        "  return color * ColorModulator;"
        "}";

    static const char* entityCommon =
        "cbuffer DynamicTransforms : register(b0) {"
        "  float4 MV0; float4 MV1; float4 MV2; float4 MV3;"
        "  float4 ColorModulator;"
        "  float4 ModelOffset;"
        "  float4 TexMat0; float4 TexMat1; float4 TexMat2; float4 TexMat3;"
        "};"
        "cbuffer Projection : register(b1) {"
        "  float4 P0; float4 P1; float4 P2; float4 P3;"
        "};"
        "cbuffer Fog : register(b2) {"
        "  float4 FogColor;"
        "  float4 Fog_Misc0;"
        "  float4 Fog_Misc1;"
        "};"
        "cbuffer Lighting : register(b3) {"
        "  float4 Light0Dir;"
        "  float4 Light1Dir;"
        "};"
        "cbuffer EntityParams : register(b4) {"
        "  float EntityCutoff; int EntityFlags; float2 EntityPad;"
        "};"
        "float4 world_transform(float3 p) {"
        "  float4 world = MV0 * p.x + MV1 * p.y + MV2 * p.z + MV3;"
        "  float4 clip = P0 * world.x + P1 * world.y + P2 * world.z + P3 * world.w;"
        "  clip.z = (clip.z + clip.w) * 0.5;"
        "  return clip;"
        "}"
        "float linear_fog_value(float vertexDistance, float fogStart, float fogEnd) {"
        "  if (vertexDistance <= fogStart) return 0.0;"
        "  if (vertexDistance >= fogEnd) return 1.0;"
        "  return (vertexDistance - fogStart) / (fogEnd - fogStart);"
        "}"
        "float4 apply_fog(float4 inColor, float sphericalDistance, float cylindricalDistance) {"
        "  float fogValue = max(linear_fog_value(sphericalDistance, Fog_Misc0.x, Fog_Misc0.y), linear_fog_value(cylindricalDistance, Fog_Misc0.z, Fog_Misc0.w));"
        "  return float4(lerp(inColor.rgb, FogColor.rgb, fogValue * FogColor.a), inColor.a);"
        "}"
        "float4 minecraft_mix_light_separate(float2 light, float4 color) {"
        "  float2 lightValue = max(float2(0.0, 0.0), light);"
        "  float lightAccum = min(1.0, (lightValue.x + lightValue.y) * 0.6 + 0.4);"
        "  return float4(color.rgb * lightAccum, color.a);"
        "}";

    const std::string entityVsSource = std::string(entityCommon) +
        "Texture2D OverlayTex : register(t1);"
        "Texture2D LightmapTex : register(t2);"
        "struct VSIn { float3 pos : POSITION; float4 color : COLOR0; float2 uv0 : TEXCOORD0; int2 uv1 : TEXCOORD1; int2 uv2 : TEXCOORD2; float4 normal : NORMAL; };"
        "struct VSOut { float4 pos : SV_Position; float4 colorFront : COLOR0; float4 colorBack : COLOR1; float4 lightMap : COLOR2; float4 overlay : COLOR3; float2 uv : TEXCOORD0; float2 dist : TEXCOORD3; };"
        "VSOut main(VSIn input) {"
        "  VSOut output;"
        "  float4 clip = world_transform(input.pos);"
        "  if ((EntityFlags & 32) != 0) clip.y = -clip.y;"
        "  output.pos = clip;"
        "  output.dist = float2(length(input.pos), max(length(input.pos.xz), abs(input.pos.y)));"
        "  float2 light = float2(dot(Light0Dir.xyz, input.normal.xyz), dot(Light1Dir.xyz, input.normal.xyz));"
        "  if ((EntityFlags & 8) != 0) {"           // NO_CARDINAL_LIGHTING
        "    output.colorFront = input.color;"
        "    output.colorBack = input.color;"
        "  } else if ((EntityFlags & 1) != 0) {"    // PER_FACE_LIGHTING
        "    output.colorFront = minecraft_mix_light_separate(light, input.color);"
        "    output.colorBack = minecraft_mix_light_separate(-light, input.color);"
        "  } else {"
        "    float4 lit = minecraft_mix_light_separate(light, input.color);"
        "    output.colorFront = lit;"
        "    output.colorBack = lit;"
        "  }"
        "  output.lightMap = (EntityFlags & 4) != 0 ? float4(1, 1, 1, 1) : LightmapTex.Load(int3(input.uv2 / 16, 0));"
        "  output.overlay = OverlayTex.Load(int3(input.uv1, 0));"
        "  output.uv = input.uv0;"
        "  return output;"
        "}";

    const std::string entityPsSource = std::string(entityCommon) +
        "Texture2D EntityTex : register(t0);"
        "SamplerState EntitySampler : register(s1);"
        "struct PSIn { float4 pos : SV_Position; float4 colorFront : COLOR0; float4 colorBack : COLOR1; float4 lightMap : COLOR2; float4 overlay : COLOR3; float2 uv : TEXCOORD0; float2 dist : TEXCOORD3; bool isFront : SV_IsFrontFace; };"
        "float4 main(PSIn input) : SV_Target {"
        "  float4 color = EntityTex.Sample(EntitySampler, input.uv);"
        "  if (EntityCutoff > 0.0 && color.a < EntityCutoff) discard;"
        "  float4 vertexColor = ((EntityFlags & 1) != 0 && !input.isFront) ? input.colorBack : input.colorFront;"
        "  color *= vertexColor * ColorModulator;"
        "  if ((EntityFlags & 2) == 0) color.rgb = lerp(input.overlay.rgb, color.rgb, input.overlay.a);"
        "  if ((EntityFlags & 4) == 0) color *= input.lightMap;"
        "  return apply_fog(color, input.dist.x, input.dist.y);"
        "}";

    const std::string particleVsSource = std::string(entityCommon) +
        "Texture2D LightmapTex : register(t2);"
        "struct VSIn { float3 pos : POSITION; float2 uv0 : TEXCOORD0; float4 color : COLOR0; int2 uv2 : TEXCOORD2; };"
        "struct VSOut { float4 pos : SV_Position; float4 color : COLOR0; float2 uv : TEXCOORD0; float2 dist : TEXCOORD3; };"
        "VSOut main(VSIn input) {"
        "  VSOut output;"
        "  output.pos = world_transform(input.pos);"
        "  output.dist = float2(length(input.pos), max(length(input.pos.xz), abs(input.pos.y)));"
        "  output.color = input.color * LightmapTex.Load(int3(input.uv2 / 16, 0));"
        "  output.uv = input.uv0;"
        "  return output;"
        "}";

    const std::string particlePsSource = std::string(entityCommon) +
        "Texture2D ParticleTex : register(t0);"
        "SamplerState ParticleSampler : register(s3);"
        "struct PSIn { float4 pos : SV_Position; float4 color : COLOR0; float2 uv : TEXCOORD0; float2 dist : TEXCOORD3; };"
        "float4 main(PSIn input) : SV_Target {"
        "  float4 color = ParticleTex.Sample(ParticleSampler, input.uv) * input.color * ColorModulator;"
        "  if (color.a < 0.1) discard;"
        "  return apply_fog(color, input.dist.x, input.dist.y);"
        "}";


    static const char* triangleVs =
        "struct VSIn { float3 pos : POSITION; float4 color : COLOR0; };"
        "struct VSOut { float4 pos : SV_Position; float4 color : COLOR0; };"
        "VSOut main(VSIn input) { VSOut o; o.pos = float4(input.pos, 1.0); o.color = input.color; return o; }";
    static const char* trianglePs =
        "struct PSIn { float4 pos : SV_Position; float4 color : COLOR0; };"
        "float4 main(PSIn input) : SV_Target { return input.color; }";

    struct ShaderJob {
        Program program;
        const char* name;
        std::string vs;
        std::string ps;
    };
    const ShaderJob jobs[] = {
        { PROG_TRIANGLE, "triangle", triangleVs, trianglePs },
        { PROG_GUI, "gui", guiVsSource, guiPsSource },
        { PROG_GUI_TEXTURED, "gui textured", guiTexturedVsSource, guiTexturedPsSource },
        { PROG_GUI_TEXT, "gui text", guiTexturedVsSource, guiTextPsSource },
        { PROG_GUI_TEXTURED_POINT, "gui textured point", guiTexturedVsSource, guiTexturedPsPointSource },
        { PROG_GUI_TEXT_POINT, "gui text point", guiTexturedVsSource, guiTextPsPointSource },
        { PROG_PANORAMA, "panorama", panoramaVsSource, panoramaPsSource },
        { PROG_LEASH, "leash", leashVsSource, leashPsSource },
        { PROG_POST_BLUR, "post blur", postBlurVs, postBlurPs },
        { PROG_GLINT, "glint", glintVsSource, glintPsSource },
        { PROG_GLINT_FLIP, "glint flip", glintFlipVsSource, glintPsSource },
        { PROG_TEXT, "text", textVsSource, textPsSource },
        { PROG_SPRITE_BLIT, "sprite blit", spriteVsSource, spriteBlitPsSource },
        { PROG_SPRITE_INTERP, "sprite interpolate", spriteVsSource, spriteInterpolatePsSource },
        { PROG_LIGHTMAP, "lightmap", std::string(screenquadVertexSource), std::string(lightmapPixelSource) },
        { PROG_TERRAIN, "terrain", terrainVertexSource, terrainPixelSource },
        { PROG_SKY, "sky", skyVsSource, skyPsSource },
        { PROG_POS_COLOR, "position_color", posColorVsSource, posColorPsSource },
        { PROG_STARS, "stars", starsVsSource, starsPsSource },
        { PROG_POS_TEX, "position_tex", posTexVsSource, posTexPsSource },
        { PROG_LINES, "lines", linesVsSource, linesPsSource },
        { PROG_SHADOW, "shadow", shadowVsSource, shadowPsSource },
        { PROG_CRUMBLING, "crumbling", shadowVsSource, crumblingPsSource },
        { PROG_CLOUDS, "clouds", cloudsVsSource, cloudsPsSource },
        { PROG_ENTITY, "entity", entityVsSource, entityPsSource },
        { PROG_PARTICLE, "particle", particleVsSource, particlePsSource },
    };
    for (const ShaderJob& job : jobs) {
        char nameVs[64];
        char namePs[64];
        sprintf_s(nameVs, "%s vs", job.name);
        sprintf_s(namePs, "%s ps", job.name);
        if (!CompileShader(nameVs, job.vs.c_str(), "vs_5_0", g_vsBlobs[job.program].ReleaseAndGetAddressOf()) ||
            !CompileShader(namePs, job.ps.c_str(), "ps_5_0", g_psBlobs[job.program].ReleaseAndGetAddressOf())) {
            return false;
        }
    }
    Log("D3D12 shaders compiled (%d programs)", static_cast<int>(PROG_COUNT));
    g_shadersReady = true;
    return true;
}

static void FillBlend(D3D12_BLEND_DESC& desc, int blend) {
    D3D12_RENDER_TARGET_BLEND_DESC& rt = desc.RenderTarget[0];
    rt.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    rt.BlendOp = D3D12_BLEND_OP_ADD;
    rt.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    rt.SrcBlendAlpha = D3D12_BLEND_ONE;
    rt.DestBlendAlpha = D3D12_BLEND_ZERO;
    switch (blend) {
        case BLEND_TRANSLUCENT:
            rt.BlendEnable = TRUE; rt.SrcBlend = D3D12_BLEND_SRC_ALPHA; rt.DestBlend = D3D12_BLEND_INV_SRC_ALPHA; break;
        case BLEND_ADDITIVE:
            rt.BlendEnable = TRUE; rt.SrcBlend = D3D12_BLEND_SRC_ALPHA; rt.DestBlend = D3D12_BLEND_ONE; break;
        case BLEND_PREMULTIPLIED:
            rt.BlendEnable = TRUE; rt.SrcBlend = D3D12_BLEND_ONE; rt.DestBlend = D3D12_BLEND_INV_SRC_ALPHA; break;
        case BLEND_INVERT:
            rt.BlendEnable = TRUE; rt.SrcBlend = D3D12_BLEND_INV_DEST_COLOR; rt.DestBlend = D3D12_BLEND_INV_SRC_COLOR; break;
        case BLEND_VIGNETTE:
            rt.BlendEnable = TRUE; rt.SrcBlend = D3D12_BLEND_ZERO; rt.DestBlend = D3D12_BLEND_INV_SRC_COLOR; break;
        case BLEND_CRUMBLING:
            rt.BlendEnable = TRUE; rt.SrcBlend = D3D12_BLEND_DEST_COLOR; rt.DestBlend = D3D12_BLEND_SRC_COLOR; break;
        case BLEND_GLINT:
            rt.BlendEnable = TRUE; rt.SrcBlend = D3D12_BLEND_SRC_COLOR; rt.DestBlend = D3D12_BLEND_ONE;
            rt.SrcBlendAlpha = D3D12_BLEND_ZERO; rt.DestBlendAlpha = D3D12_BLEND_ONE; break;
        default:
            rt.BlendEnable = FALSE; rt.SrcBlend = D3D12_BLEND_ONE; rt.DestBlend = D3D12_BLEND_ZERO; break;
    }
}

static void FillRaster(D3D12_RASTERIZER_DESC& desc, int raster) {
    desc.FillMode = D3D12_FILL_MODE_SOLID;
    desc.DepthClipEnable = TRUE;
    switch (raster) {
        case RASTER_BACK_CCW: desc.CullMode = D3D12_CULL_MODE_BACK; desc.FrontCounterClockwise = TRUE; break;
        case RASTER_NONE_CW: desc.CullMode = D3D12_CULL_MODE_NONE; desc.FrontCounterClockwise = FALSE; break;
        case RASTER_BACK_CW: desc.CullMode = D3D12_CULL_MODE_BACK; desc.FrontCounterClockwise = FALSE; break;
        case RASTER_CRUMBLING:
            desc.CullMode = D3D12_CULL_MODE_BACK;
            desc.FrontCounterClockwise = TRUE;
            desc.DepthBias = -4;
            desc.SlopeScaledDepthBias = -3.0f;
            break;
        case RASTER_TEXT_OFFSET:
            desc.CullMode = D3D12_CULL_MODE_BACK;
            desc.FrontCounterClockwise = TRUE;
            desc.DepthBias = -10;
            desc.SlopeScaledDepthBias = -1.0f;
            break;
        default: desc.CullMode = D3D12_CULL_MODE_NONE; desc.FrontCounterClockwise = TRUE; break;
    }
}

// Input layouts per program. GUI layouts are parameterized by uv/color offsets from the
// pipeline's vertex format (layoutKey packs them).
static UINT BuildInputLayout(Program program, UINT layoutKey, D3D12_INPUT_ELEMENT_DESC* out) {
    const UINT uvOffset = layoutKey & 0xFFFu;
    const UINT colorOffset = (layoutKey >> 12) & 0xFFFu;
    UINT n = 0;
    auto add = [&](const char* name, UINT index, DXGI_FORMAT format, UINT offset) {
        out[n++] = { name, index, format, 0, offset, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 };
    };
    switch (program) {
        case PROG_TRIANGLE:
            add("POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0);
            add("COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 12);
            break;
        case PROG_GUI:
            add("POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0);
            add("COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, colorOffset);
            break;
        case PROG_GUI_TEXTURED:
        case PROG_GUI_TEXT:
        case PROG_GUI_TEXTURED_POINT:
        case PROG_GUI_TEXT_POINT:
            add("POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0);
            add("TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, uvOffset);
            add("COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, colorOffset);
            break;
        case PROG_LEASH:
            add("POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0);
            add("COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 12);
            add("TEXCOORD", 1, DXGI_FORMAT_R16G16_SINT, 16);
            break;
        case PROG_TERRAIN:
            add("POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0);
            add("COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 12);
            add("TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 16);
            add("TEXCOORD", 1, DXGI_FORMAT_R16G16_SINT, 24);
            break;
        case PROG_SKY:
        case PROG_STARS:
        case PROG_PANORAMA:
            add("POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0);
            break;
        case PROG_POS_COLOR:
            add("POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0);
            add("COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 12);
            break;
        case PROG_POS_TEX:
        case PROG_GLINT:
        case PROG_GLINT_FLIP:
            add("POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0);
            add("TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 12);
            break;
        case PROG_LINES:
            break;  // vertices fetched from a raw buffer SRV in the VS (unaligned 23-byte stride)
        case PROG_TEXT:
            add("POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0);
            add("COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 12);
            add("TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 16);
            add("TEXCOORD", 1, DXGI_FORMAT_R16G16_SINT, 24);
            break;
        case PROG_POST_BLUR:
            break;  // vertex-buffer-less fullscreen triangle (SV_VertexID)
        case PROG_SHADOW:
        case PROG_CRUMBLING:
            add("POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0);
            add("COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 12);
            add("TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 16);
            break;
        case PROG_ENTITY:
            add("POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0);
            add("COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 12);
            add("TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 16);
            add("TEXCOORD", 1, DXGI_FORMAT_R16G16_SINT, 24);
            add("TEXCOORD", 2, DXGI_FORMAT_R16G16_SINT, 28);
            add("NORMAL", 0, DXGI_FORMAT_R8G8B8A8_SNORM, 32);
            break;
        case PROG_PARTICLE:
            add("POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0);
            add("TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 12);
            add("COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 20);
            add("TEXCOORD", 2, DXGI_FORMAT_R16G16_SINT, 24);
            break;
        default:
            break;  // clouds/lightmap/sprite: vertex-buffer-less
    }
    return n;
}

static ID3D12PipelineState* GetPso(Program program, int blend, int depth, int raster, UINT layoutKey, DXGI_FORMAT rtvFormat) {
    const uint64_t key = static_cast<uint64_t>(program)
        | (static_cast<uint64_t>(blend) << 6)
        | (static_cast<uint64_t>(depth) << 10)
        | (static_cast<uint64_t>(raster) << 13)
        | (static_cast<uint64_t>(layoutKey) << 17)
        | (static_cast<uint64_t>(rtvFormat == DXGI_FORMAT_R8G8B8A8_UNORM ? 1 : 0) << 42);
    auto it = g_psoCache.find(key);
    if (it != g_psoCache.end()) {
        return it->second.Get();
    }

    D3D12_INPUT_ELEMENT_DESC elements[8] = {};
    UINT elementCount = BuildInputLayout(program, layoutKey, elements);

    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
    desc.pRootSignature = g_rootSignature.Get();
    desc.VS = { g_vsBlobs[program]->GetBufferPointer(), g_vsBlobs[program]->GetBufferSize() };
    desc.PS = { g_psBlobs[program]->GetBufferPointer(), g_psBlobs[program]->GetBufferSize() };
    FillBlend(desc.BlendState, blend);
    desc.SampleMask = UINT_MAX;
    FillRaster(desc.RasterizerState, raster);
    if (depth != DEPTH_OFF) {
        desc.DepthStencilState.DepthEnable = TRUE;
        desc.DepthStencilState.DepthWriteMask = depth == DEPTH_LEQUAL_WRITE ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
        desc.DepthStencilState.DepthFunc = depth == DEPTH_EQUAL_NO_WRITE ? D3D12_COMPARISON_FUNC_EQUAL : D3D12_COMPARISON_FUNC_LESS_EQUAL;
        desc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    }
    desc.InputLayout = { elementCount ? elements : nullptr, elementCount };
    desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    desc.NumRenderTargets = 1;
    desc.RTVFormats[0] = rtvFormat;
    desc.SampleDesc.Count = 1;

    ComPtr<ID3D12PipelineState> pso;
    HRESULT hr = g_device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(pso.GetAddressOf()));
    if (FAILED(hr)) {
        Log("CreateGraphicsPipelineState(program=%d blend=%d depth=%d raster=%d layout=0x%X) failed hr=0x%08X",
            static_cast<int>(program), blend, depth, raster, layoutKey, static_cast<unsigned>(hr));
        return nullptr;
    }
    ID3D12PipelineState* raw = pso.Get();
    g_psoCache.emplace(key, std::move(pso));
    return raw;
}
// ---- Render targets & depth ------------------------------------------------------------------

static ComPtr<ID3D12Resource> g_triangleVertexBuffer;
static D3D12_VERTEX_BUFFER_VIEW g_triangleVbv = {};
static ComPtr<ID3D12Resource> g_fanIndexBuffer;
static UINT g_fanIndexVertexCapacity = 0;
static D3D12_GPU_VIRTUAL_ADDRESS g_terrainProjVa = 0;
static D3D12_GPU_VIRTUAL_ADDRESS g_terrainGlobalsVa = 0;
static D3D12_GPU_VIRTUAL_ADDRESS g_terrainFogVa = 0;
static D3D12_GPU_VIRTUAL_ADDRESS g_terrainParamsVa = 0;
static D3D12_GPU_DESCRIPTOR_HANDLE g_terrainSrvTable = {};
static ID3D12PipelineState* g_terrainPso = nullptr;

static bool EnsureMainDepth() {
    if (g_mainDepth) {
        D3D12_RESOURCE_DESC desc = g_mainDepth->GetDesc();
        if (desc.Width == static_cast<UINT64>(g_width) && desc.Height == static_cast<UINT>(g_height)) {
            return true;
        }
        g_frames[g_frameIndex].garbage.push_back(g_mainDepth);
        g_mainDepth.Reset();
    }
    D3D12_HEAP_PROPERTIES heap = {};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = static_cast<UINT64>(g_width);
    desc.Height = static_cast<UINT>(g_height);
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_D32_FLOAT;
    desc.SampleDesc.Count = 1;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    D3D12_CLEAR_VALUE clear = {};
    clear.Format = DXGI_FORMAT_D32_FLOAT;
    clear.DepthStencil.Depth = 1.0f;
    HRESULT hr = g_device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_DEPTH_WRITE, &clear, IID_PPV_ARGS(g_mainDepth.ReleaseAndGetAddressOf()));
    if (FAILED(hr)) {
        LogHr("CreateCommittedResource(main depth)", hr);
        return false;
    }
    if (g_mainDepthDsvSlot == UINT_MAX) {
        g_mainDepthDsvSlot = g_dsvNext++;
    }
    g_device->CreateDepthStencilView(g_mainDepth.Get(), nullptr, DsvHandle(g_mainDepthDsvSlot));
    Log("Depth buffer created %dx%d", g_width, g_height);
    return true;
}

static OffscreenDepth* GetOrCreateOffscreenDepth(UINT width, UINT height) {
    const uint64_t key = (static_cast<uint64_t>(width) << 20) | height;
    auto it = g_offscreenDepths.find(key);
    if (it != g_offscreenDepths.end()) {
        return &it->second;
    }
    OffscreenDepth entry;
    D3D12_HEAP_PROPERTIES heap = {};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = width;
    desc.Height = height;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_D32_FLOAT;
    desc.SampleDesc.Count = 1;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    D3D12_CLEAR_VALUE clear = {};
    clear.Format = DXGI_FORMAT_D32_FLOAT;
    clear.DepthStencil.Depth = 1.0f;
    HRESULT hr = g_device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_DEPTH_WRITE, &clear, IID_PPV_ARGS(entry.texture.GetAddressOf()));
    if (FAILED(hr)) {
        LogHr("CreateCommittedResource(offscreen depth)", hr);
        return nullptr;
    }
    entry.dsvSlot = g_dsvNext++;
    g_device->CreateDepthStencilView(entry.texture.Get(), nullptr, DsvHandle(entry.dsvSlot));
    if (EnsureFrameOpen()) {
        g_commandList->ClearDepthStencilView(DsvHandle(entry.dsvSlot), D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
    }
    Log("Offscreen depth created %ux%u", width, height);
    auto inserted = g_offscreenDepths.emplace(key, std::move(entry));
    return &inserted.first->second;
}

static UINT GetOrCreateTextureRtvSlot(NativeTexture& texture, UINT mip) {
    if (mip >= 16) {
        return UINT_MAX;
    }
    if (texture.rtvSlots[mip] != UINT_MAX) {
        return texture.rtvSlots[mip];
    }
    UINT slot = AllocRtvSlot();
    if (slot == UINT_MAX) {
        return UINT_MAX;
    }
    D3D12_RENDER_TARGET_VIEW_DESC desc = {};
    desc.Format = texture.format;
    desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    desc.Texture2D.MipSlice = mip;
    g_device->CreateRenderTargetView(texture.resource.Get(), &desc, RtvHandle(slot));
    texture.rtvSlots[mip] = slot;
    return slot;
}

// Binds the draw target (backbuffer or texture mip) with viewport + full scissor default.
struct DrawTarget {
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = {};
    bool hasDsv = false;
    D3D12_CPU_DESCRIPTOR_HANDLE dsv = {};
    UINT width = 0;
    UINT height = 0;
    DXGI_FORMAT format = DXGI_FORMAT_B8G8R8A8_UNORM;
};

static void BindTarget(const DrawTarget& target) {
    g_commandList->OMSetRenderTargets(1, &target.rtv, FALSE, target.hasDsv ? &target.dsv : nullptr);
    D3D12_VIEWPORT viewport = { 0.0f, 0.0f, static_cast<float>(target.width), static_cast<float>(target.height), 0.0f, 1.0f };
    D3D12_RECT scissor = { 0, 0, static_cast<LONG>(target.width), static_cast<LONG>(target.height) };
    g_commandList->RSSetViewports(1, &viewport);
    g_commandList->RSSetScissorRects(1, &scissor);
}

static bool EnsurePipelines() {
    return CreateRootSignature() && CompileAllShaders();
}

static void SetVertexBuffer(const NativeBuffer& buffer, UINT stride) {
    D3D12_VERTEX_BUFFER_VIEW view = {};
    view.BufferLocation = buffer.resource->GetGPUVirtualAddress();
    view.SizeInBytes = static_cast<UINT>(buffer.byteWidth);
    view.StrideInBytes = stride;
    g_commandList->IASetVertexBuffers(0, 1, &view);
}

static void SetIndexBuffer(const NativeBuffer& buffer, int indexBytes) {
    D3D12_INDEX_BUFFER_VIEW view = {};
    view.BufferLocation = buffer.resource->GetGPUVirtualAddress();
    view.SizeInBytes = static_cast<UINT>(buffer.byteWidth);
    view.Format = indexBytes == 2 ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT;
    g_commandList->IASetIndexBuffer(&view);
}

// ---- Device init -----------------------------------------------------------------------------

static bool ResolveCoreWindow() {
    if (g_window) {
        return true;
    }
    ComPtr<ICoreApplication> coreApp;
    HRESULT hr = RoGetActivationFactory(
        HStringReference(RuntimeClass_Windows_ApplicationModel_Core_CoreApplication).Get(),
        IID_PPV_ARGS(coreApp.GetAddressOf()));
    if (FAILED(hr) || !coreApp) {
        LogHr("CoreApplication activation", hr);
        return false;
    }
    ComPtr<IPropertySet> props;
    hr = coreApp->get_Properties(props.GetAddressOf());
    if (FAILED(hr) || !props) {
        LogHr("CoreApplication.get_Properties", hr);
        return false;
    }
    ComPtr<IMap<HSTRING, IInspectable*>> propMap;
    hr = props.As(&propMap);
    if (FAILED(hr) || !propMap) {
        LogHr("CoreApplication properties map", hr);
        return false;
    }
    boolean hasWindow = false;
    hr = propMap->HasKey(HStringReference(kCoreWindowProperty).Get(), &hasWindow);
    if (FAILED(hr) || !hasWindow) {
        Log("CoreWindow property '%S' missing", kCoreWindowProperty);
        return false;
    }
    ComPtr<IInspectable> inspectable;
    hr = propMap->Lookup(HStringReference(kCoreWindowProperty).Get(), inspectable.GetAddressOf());
    if (FAILED(hr) || !inspectable) {
        LogHr("CoreWindow property lookup", hr);
        return false;
    }
    hr = inspectable.As(&g_window);
    if (FAILED(hr) || !g_window) {
        LogHr("CoreWindow property cast", hr);
        return false;
    }
    Log("CoreWindow resolved from CoreApplication properties");
    return true;
}

static int ScaleDimension(float value, int fallback) {
    return value > 0.0f ? static_cast<int>(value + 0.5f) : fallback;
}

static bool GetWindowSize(int requestedWidth, int requestedHeight, int& width, int& height) {
    width = requestedWidth > 0 ? requestedWidth : 1280;
    height = requestedHeight > 0 ? requestedHeight : 720;
    if (g_window) {
        ABI::Windows::Foundation::Rect bounds = {};
        if (SUCCEEDED(g_window->get_Bounds(&bounds))) {
            width = ScaleDimension(bounds.Width, width);
            height = ScaleDimension(bounds.Height, height);
        }
    }
    return width > 0 && height > 0;
}

static bool CreateBackBufferViews() {
    for (UINT i = 0; i < kFrameCount; ++i) {
        g_backBuffers[i].Reset();
        HRESULT hr = g_swapChain->GetBuffer(i, IID_PPV_ARGS(g_backBuffers[i].GetAddressOf()));
        if (FAILED(hr)) {
            LogHr("IDXGISwapChain3::GetBuffer", hr);
            return false;
        }
        g_device->CreateRenderTargetView(g_backBuffers[i].Get(), nullptr, BackBufferRtv(i));
    }
    g_frameIndex = g_swapChain->GetCurrentBackBufferIndex();
    return true;
}

static bool CreateTriangleResources() {
    struct TriangleVertex { float x, y, z, r, g, b, a; };
    const TriangleVertex vertices[] = {
        {  0.0f,  0.58f, 0.5f, 0.35f, 0.80f, 1.00f, 1.0f },
        {  0.62f, -0.50f, 0.5f, 0.55f, 1.00f, 0.55f, 1.0f },
        { -0.62f, -0.50f, 0.5f, 1.00f, 0.60f, 0.35f, 1.0f },
    };
    if (!CreateUploadBuffer(sizeof(vertices), g_triangleVertexBuffer)) {
        return false;
    }
    void* mapped = nullptr;
    D3D12_RANGE readRange = {};
    if (FAILED(g_triangleVertexBuffer->Map(0, &readRange, &mapped))) {
        return false;
    }
    std::memcpy(mapped, vertices, sizeof(vertices));
    g_triangleVertexBuffer->Unmap(0, nullptr);
    g_triangleVbv.BufferLocation = g_triangleVertexBuffer->GetGPUVirtualAddress();
    g_triangleVbv.StrideInBytes = sizeof(TriangleVertex);
    g_triangleVbv.SizeInBytes = sizeof(vertices);
    return true;
}

static bool CreateDeviceAndSwapchain(int requestedWidth, int requestedHeight) {
    if (!ResolveCoreWindow() || !GetWindowSize(requestedWidth, requestedHeight, g_width, g_height)) {
        return false;
    }
    HRESULT hr = D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(g_device.GetAddressOf()));
    if (!SucceededOrLog("D3D12CreateDevice", hr)) {
        return false;
    }
    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    hr = g_device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(g_commandQueue.GetAddressOf()));
    if (!SucceededOrLog("CreateCommandQueue", hr)) {
        return false;
    }
    ComPtr<IDXGIFactory4> factory;
    hr = CreateDXGIFactory2(0, IID_PPV_ARGS(factory.GetAddressOf()));
    if (!SucceededOrLog("CreateDXGIFactory2", hr)) {
        return false;
    }
    {
        ComPtr<IDXGIAdapter1> adapter;
        LUID luid = g_device->GetAdapterLuid();
        if (SUCCEEDED(factory->EnumAdapterByLuid(luid, IID_PPV_ARGS(adapter.GetAddressOf())))) {
            DXGI_ADAPTER_DESC1 desc = {};
            if (SUCCEEDED(adapter->GetDesc1(&desc))) {
                // "backend|adapter" marker: the Java side splits on '|' to report which API this
                // DLL actually is (the D3D12 build ships under the D3D11 filename for A/B swaps;
                // the unmarked D3D11 DLL falls back to "Direct3D 11" in the parser).
                wcsncpy_s(g_adapterDescription, L"Direct3D 12|", _TRUNCATE);
                wcsncat_s(g_adapterDescription, desc.Description, _TRUNCATE);
                Log("Adapter: %S (D3D12)", g_adapterDescription);
            }
        }
    }
    DXGI_SWAP_CHAIN_DESC1 swapDesc = {};
    swapDesc.Width = g_width;
    swapDesc.Height = g_height;
    swapDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    swapDesc.SampleDesc.Count = 1;
    swapDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapDesc.BufferCount = kFrameCount;
    swapDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swapDesc.Scaling = DXGI_SCALING_STRETCH;
    swapDesc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
    ComPtr<IDXGISwapChain1> swapChain1;
    hr = factory->CreateSwapChainForCoreWindow(g_commandQueue.Get(), reinterpret_cast<IUnknown*>(g_window.Get()), &swapDesc, nullptr, swapChain1.GetAddressOf());
    if (!SucceededOrLog("CreateSwapChainForCoreWindow(d3d12)", hr)) {
        return false;
    }
    hr = swapChain1.As(&g_swapChain);
    if (FAILED(hr)) {
        LogHr("Query IDXGISwapChain3", hr);
        return false;
    }

    g_rtvDescriptorSize = g_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    g_dsvDescriptorSize = g_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
    g_srvDescriptorSize = g_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
    heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    heapDesc.NumDescriptors = kFrameCount;
    hr = g_device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(g_backBufferRtvHeap.GetAddressOf()));
    if (!SucceededOrLog("CreateDescriptorHeap(backbuffer rtv)", hr)) return false;
    heapDesc.NumDescriptors = kRtvHeapCapacity;
    hr = g_device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(g_rtvHeap.GetAddressOf()));
    if (!SucceededOrLog("CreateDescriptorHeap(texture rtv)", hr)) return false;
    heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    heapDesc.NumDescriptors = kDsvHeapCapacity;
    hr = g_device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(g_dsvHeap.GetAddressOf()));
    if (!SucceededOrLog("CreateDescriptorHeap(dsv)", hr)) return false;
    heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heapDesc.NumDescriptors = kCpuSrvHeapCapacity;
    hr = g_device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(g_cpuSrvHeap.GetAddressOf()));
    if (!SucceededOrLog("CreateDescriptorHeap(cpu srv)", hr)) return false;
    heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    heapDesc.NumDescriptors = kGpuSrvHeapCapacity;
    for (UINT i = 0; i < kFrameCount; ++i) {
        hr = g_device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(g_gpuSrvHeaps[i].GetAddressOf()));
        if (FAILED(hr)) {
            LogHr("CreateDescriptorHeap(gpu srv)", hr);
            return false;
        }
    }

    // Null SRV at cpu slot 0 for unbound table entries.
    D3D12_SHADER_RESOURCE_VIEW_DESC nullSrv = {};
    nullSrv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    nullSrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    nullSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    nullSrv.Texture2D.MipLevels = 1;
    g_device->CreateShaderResourceView(nullptr, &nullSrv, CpuSrvHandle(0));

    if (!CreateBackBufferViews()) {
        return false;
    }
    for (UINT i = 0; i < kFrameCount; ++i) {
        hr = g_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(g_frames[i].allocator.GetAddressOf()));
        if (FAILED(hr)) {
            LogHr("CreateCommandAllocator", hr);
            return false;
        }
        if (!CreateUploadBuffer(kCbufferRingSize, g_frames[i].uploadRing)) {
            return false;
        }
        void* mapped = nullptr;
        D3D12_RANGE readRange = {};
        if (FAILED(g_frames[i].uploadRing->Map(0, &readRange, &mapped))) {
            Log("Map(upload ring) failed");
            return false;
        }
        g_frames[i].uploadRingCpu = static_cast<uint8_t*>(mapped);
    }
    hr = g_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, g_frames[0].allocator.Get(), nullptr, IID_PPV_ARGS(g_commandList.GetAddressOf()));
    if (!SucceededOrLog("CreateCommandList", hr)) {
        return false;
    }
    g_commandList->Close();
    hr = g_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(g_fence.GetAddressOf()));
    if (!SucceededOrLog("CreateFence", hr)) {
        return false;
    }
    g_fenceEvent = CreateEventEx(nullptr, nullptr, 0, EVENT_ALL_ACCESS);
    if (!g_fenceEvent) {
        return false;
    }
    if (!CreateTriangleResources()) {
        return false;
    }
    // Worldgen worker threads compete for the 8 shared cores; keep frame production ahead of
    // them (nativeInit runs on the render thread).
    if (SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL)) {
        Log("render thread priority raised to ABOVE_NORMAL");
    }
    Log("D3D12 backend initialized %dx%d", g_width, g_height);
    return true;
}

// ---- JNI: lifecycle --------------------------------------------------------------------------

extern "C" JNIEXPORT jboolean JNICALL
Java_banditvault_d3d11mod_nativebridge_D3D11Native_nativeInit(JNIEnv* env, jclass, jint width, jint height, jstring logPath) {
    if (logPath) {
        const jchar* chars = env->GetStringChars(logPath, nullptr);
        if (chars) {
            wcsncpy_s(g_logPath, reinterpret_cast<const wchar_t*>(chars), _TRUNCATE);
            env->ReleaseStringChars(logPath, chars);
            FILE* f = nullptr;
            if (_wfopen_s(&f, g_logPath, L"w, ccs=UTF-8") == 0 && f) {
                fclose(f);
            }
        }
    }
    HRESULT hr = RoInitialize(RO_INIT_MULTITHREADED);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
        LogHr("RoInitialize", hr);
        return JNI_FALSE;
    }
    Log("nativeInit (D3D12) requested size=%dx%d", static_cast<int>(width), static_cast<int>(height));
    return CreateDeviceAndSwapchain(width, height) ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_banditvault_d3d11mod_nativebridge_D3D11Native_nativeResize(JNIEnv*, jclass, jint width, jint height) {
    if (!g_swapChain || width <= 0 || height <= 0) {
        return JNI_FALSE;
    }
    if (width == g_width && height == g_height) {
        return JNI_TRUE;
    }
    WaitForGpuIdle();
    for (UINT i = 0; i < kFrameCount; ++i) {
        g_backBuffers[i].Reset();
        g_frames[i].garbage.clear();
    }
    g_mainDepth.Reset();
    g_width = width;
    g_height = height;
    HRESULT hr = g_swapChain->ResizeBuffers(kFrameCount, g_width, g_height, DXGI_FORMAT_UNKNOWN, 0);
    if (!SucceededOrLog("ResizeBuffers(d3d12)", hr)) {
        return JNI_FALSE;
    }
    return CreateBackBufferViews() ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT void JNICALL
Java_banditvault_d3d11mod_nativebridge_D3D11Native_nativeBeginFrame(JNIEnv*, jclass, jfloat r, jfloat g, jfloat b, jfloat a) {
    if (!EnsureFrameOpen()) {
        return;
    }
    const float clear[] = { r, g, b, a };
    g_commandList->ClearRenderTargetView(BackBufferRtv(g_frameIndex), clear, 0, nullptr);
}

extern "C" JNIEXPORT void JNICALL
Java_banditvault_d3d11mod_nativebridge_D3D11Native_nativeDrawTestTriangle(JNIEnv*, jclass) {
    if (!EnsureFrameOpen() || !EnsurePipelines()) {
        return;
    }
    if (!g_loggedFirstTriangle) {
        Log("nativeDrawTestTriangle (D3D12) first draw");
        g_loggedFirstTriangle = true;
    }
    ID3D12PipelineState* pso = GetPso(PROG_TRIANGLE, BLEND_NONE, DEPTH_OFF, RASTER_NONE_CCW, 0, DXGI_FORMAT_B8G8R8A8_UNORM);
    if (!pso) {
        return;
    }
    DrawTarget target;
    target.rtv = BackBufferRtv(g_frameIndex);
    target.width = static_cast<UINT>(g_width);
    target.height = static_cast<UINT>(g_height);
    BindTarget(target);
    g_commandList->SetGraphicsRootSignature(g_rootSignature.Get());
    g_commandList->SetPipelineState(pso);
    g_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    g_commandList->IASetVertexBuffers(0, 1, &g_triangleVbv);
    g_commandList->DrawInstanced(3, 1, 0, 0);
}

extern "C" JNIEXPORT jboolean JNICALL
Java_banditvault_d3d11mod_nativebridge_D3D11Native_nativePresent(JNIEnv*, jclass) {
    if (!g_swapChain || !EnsureFrameOpen()) {
        return JNI_FALSE;
    }
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = g_backBuffers[g_frameIndex].Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    g_commandList->ResourceBarrier(1, &barrier);
    HRESULT hr = g_commandList->Close();
    if (FAILED(hr)) {
        LogHr("CommandList::Close", hr);
        g_frameOpen = false;
        return JNI_FALSE;
    }
    ID3D12CommandList* lists[] = { g_commandList.Get() };
    g_commandQueue->ExecuteCommandLists(1, lists);
    hr = g_swapChain->Present(g_syncInterval, 0);
    if (FAILED(hr)) {
        LogHr("Present(d3d12)", hr);
    }
    const UINT64 fenceValue = g_nextFenceValue++;
    g_commandQueue->Signal(g_fence.Get(), fenceValue);
    g_frames[g_frameIndex].fenceValue = fenceValue;
    g_frameOpen = false;
    if (!g_loggedFirstPresent) {
        Log("nativePresent (D3D12) first frame ok");
        g_loggedFirstPresent = true;
    }
    return SUCCEEDED(hr) ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT void JNICALL
Java_banditvault_d3d11mod_nativebridge_D3D11Native_nativeSetVsync(JNIEnv*, jclass, jboolean enabled) {
    g_syncInterval = enabled ? 1u : 0u;
    Log("nativeSetVsync (D3D12) interval=%u", g_syncInterval);
}

extern "C" JNIEXPORT jstring JNICALL
Java_banditvault_d3d11mod_nativebridge_D3D11Native_nativeGetAdapterDescription(JNIEnv* env, jclass) {
    return env->NewString(reinterpret_cast<const jchar*>(g_adapterDescription), static_cast<jsize>(wcslen(g_adapterDescription)));
}

// ---- JNI: resources --------------------------------------------------------------------------

extern "C" JNIEXPORT jlong JNICALL
Java_banditvault_d3d11mod_nativebridge_D3D11Native_nativeCreateBuffer(JNIEnv*, jclass, jint usage, jlong size) {
    if (!g_device || size <= 0) {
        return 0;
    }
    NativeBuffer buffer;
    // Pow2 size bucket (min 512) so destroyed buffers recycle across differently-sized requests.
    const UINT64 padded = (static_cast<UINT64>(size) + 255ull) & ~255ull;  // room for CBV alignment reads
    UINT64 bucket = 512;
    while (bucket < padded) {
        bucket <<= 1;
    }
    buffer.usage = usage;
    std::lock_guard<std::mutex> lock(g_resourceMutex);
    int best = -1;
    for (int i = 0; i < static_cast<int>(g_defaultBufferPool.size()); ++i) {
        if (g_defaultBufferPool[i].byteWidth >= bucket &&
            (best < 0 || g_defaultBufferPool[i].byteWidth < g_defaultBufferPool[best].byteWidth)) {
            best = i;
        }
    }
    if (best >= 0 && g_defaultBufferPool[best].byteWidth <= bucket * 2) {
        buffer.resource = std::move(g_defaultBufferPool[best].resource);
        buffer.byteWidth = g_defaultBufferPool[best].byteWidth;
        buffer.state = g_defaultBufferPool[best].state;
        g_defaultBufferPoolBytes -= g_defaultBufferPool[best].byteWidth;
        g_defaultBufferPool.erase(g_defaultBufferPool.begin() + best);
    } else {
        buffer.byteWidth = bucket;
        buffer.state = D3D12_RESOURCE_STATE_COMMON;
        D3D12_HEAP_PROPERTIES heap = {};
        heap.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC desc = {};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Width = buffer.byteWidth;
        desc.Height = 1;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.SampleDesc.Count = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        HRESULT hr = g_device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(buffer.resource.GetAddressOf()));
        if (FAILED(hr)) {
            LogHr("CreateCommittedResource(buffer)", hr);
            return 0;
        }
    }
    const int64_t handle = g_nextBufferHandle++;
    if (g_loggedBufferCreates < 32) {
        Log("nativeCreateBuffer handle=%lld size=%lld usage=%d", static_cast<long long>(handle), static_cast<long long>(size), static_cast<int>(usage));
        ++g_loggedBufferCreates;
    }
    g_buffers.emplace(handle, std::move(buffer));
    return static_cast<jlong>(handle);
}

extern "C" JNIEXPORT void JNICALL
Java_banditvault_d3d11mod_nativebridge_D3D11Native_nativeUpdateBuffer(JNIEnv* env, jclass, jlong handle, jlong offset, jobject source) {
    if (!g_device || handle == 0 || !source || !EnsureFrameOpen()) {
        return;
    }
    void* data = env->GetDirectBufferAddress(source);
    jlong size = env->GetDirectBufferCapacity(source);
    if (!data || size <= 0) {
        return;
    }
    NativeBuffer* buffer;
    {
        std::lock_guard<std::mutex> lock(g_resourceMutex);
        auto it = g_buffers.find(static_cast<int64_t>(handle));
        if (it == g_buffers.end()) {
            Log("nativeUpdateBuffer unknown handle=%lld", static_cast<long long>(handle));
            return;
        }
        buffer = &it->second;
    }
    if (offset < 0 || static_cast<UINT64>(offset + size) > buffer->byteWidth) {
        Log("nativeUpdateBuffer out of range handle=%lld", static_cast<long long>(handle));
        return;
    }
    UploadAlloc alloc;
    if (!AllocUpload(static_cast<UINT64>(size), 16, alloc)) {
        return;
    }
    std::memcpy(alloc.cpu, data, static_cast<size_t>(size));
    TransitionResource(buffer->resource.Get(), buffer->state, D3D12_RESOURCE_STATE_COPY_DEST);
    g_commandList->CopyBufferRegion(buffer->resource.Get(), static_cast<UINT64>(offset), alloc.resource, alloc.offset, static_cast<UINT64>(size));
    TransitionResource(buffer->resource.Get(), buffer->state, D3D12_RESOURCE_STATE_GENERIC_READ);
    if (g_loggedBufferUpdates < 32) {
        Log("nativeUpdateBuffer handle=%lld offset=%lld size=%lld", static_cast<long long>(handle), static_cast<long long>(offset), static_cast<long long>(size));
        ++g_loggedBufferUpdates;
    }
}

extern "C" JNIEXPORT void JNICALL
Java_banditvault_d3d11mod_nativebridge_D3D11Native_nativeDestroyBuffer(JNIEnv*, jclass, jlong handle) {
    std::lock_guard<std::mutex> lock(g_resourceMutex);
    auto it = g_buffers.find(static_cast<int64_t>(handle));
    if (it == g_buffers.end()) {
        return;
    }
    // Possibly still referenced by in-flight command lists: defer to the frame fence, then
    // recycle into the buffer pool (section meshing destroys hundreds of these per second).
    PooledBuffer returned;
    returned.resource = it->second.resource;
    returned.byteWidth = it->second.byteWidth;
    returned.state = it->second.state;
    g_frames[g_frameIndex].bufferReturns.push_back(std::move(returned));
    for (auto srv = g_bufferSrvSlots.begin(); srv != g_bufferSrvSlots.end();) {
        if ((srv->first >> 24) == static_cast<uint64_t>(handle)) {
            g_cpuSrvFree.push_back(srv->second);
            srv = g_bufferSrvSlots.erase(srv);
        } else {
            ++srv;
        }
    }
    g_buffers.erase(it);
}

extern "C" JNIEXPORT jlong JNICALL
Java_banditvault_d3d11mod_nativebridge_D3D11Native_nativeCreateTexture(JNIEnv*, jclass, jint width, jint height, jint layers, jboolean cube, jint mipLevels, jint pixelSize) {
    if (!g_device || width <= 0 || height <= 0 || mipLevels <= 0 || (pixelSize != 1 && pixelSize != 4)) {
        return 0;
    }
    if (layers != 1 && !(cube == JNI_TRUE && layers == 6)) {
        Log("nativeCreateTexture rejected layers=%d cube=%d", static_cast<int>(layers), cube == JNI_TRUE ? 1 : 0);
        return 0;
    }
    NativeTexture texture;
    texture.width = static_cast<UINT>(width);
    texture.height = static_cast<UINT>(height);
    UINT maxMips = 1;
    for (UINT size = texture.width > texture.height ? texture.width : texture.height; size > 1; size >>= 1) {
        ++maxMips;
    }
    texture.mipLevels = static_cast<UINT>(mipLevels) < maxMips ? static_cast<UINT>(mipLevels) : maxMips;
    texture.pixelSize = static_cast<UINT>(pixelSize);
    texture.layers = static_cast<UINT>(layers);
    texture.cube = cube == JNI_TRUE;
    texture.format = pixelSize == 4 ? DXGI_FORMAT_R8G8B8A8_UNORM : DXGI_FORMAT_R8_UNORM;
    texture.state = D3D12_RESOURCE_STATE_COPY_DEST;
    for (UINT i = 0; i < 16; ++i) {
        texture.rtvSlots[i] = UINT_MAX;
    }
    D3D12_HEAP_PROPERTIES heap = {};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = texture.width;
    desc.Height = texture.height;
    desc.DepthOrArraySize = static_cast<UINT16>(texture.layers);
    desc.MipLevels = static_cast<UINT16>(texture.mipLevels);
    desc.Format = texture.format;
    desc.SampleDesc.Count = 1;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    HRESULT hr = g_device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(texture.resource.GetAddressOf()));
    if (FAILED(hr)) {
        LogHr("CreateCommittedResource(texture)", hr);
        return 0;
    }
    texture.srvSlot = AllocCpuSrvSlot();
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = texture.format;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    if (texture.cube) {
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
        srvDesc.TextureCube.MostDetailedMip = 0;
        srvDesc.TextureCube.MipLevels = texture.mipLevels;
    } else {
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = texture.mipLevels;
    }
    g_device->CreateShaderResourceView(texture.resource.Get(), &srvDesc, CpuSrvHandle(texture.srvSlot));
    ++g_srvWriteGeneration;

    std::lock_guard<std::mutex> lock(g_resourceMutex);
    const int64_t handle = g_nextTextureHandle++;
    if (g_loggedTextureCreates < 32) {
        Log("nativeCreateTexture handle=%lld size=%dx%d layers=%d cube=%d mips=%u pixelSize=%d", static_cast<long long>(handle), width, height, static_cast<int>(layers), cube == JNI_TRUE ? 1 : 0, texture.mipLevels, pixelSize);
        ++g_loggedTextureCreates;
    }
    g_textures.emplace(handle, std::move(texture));
    return static_cast<jlong>(handle);
}

extern "C" JNIEXPORT void JNICALL
Java_banditvault_d3d11mod_nativebridge_D3D11Native_nativeUpdateTexture(JNIEnv* env, jclass, jlong handle, jint level, jint layer, jint xOffset, jint yOffset, jint width, jint height, jint rowPitch, jobject source) {
    if (!g_device || handle == 0 || !source || width <= 0 || height <= 0 || !EnsureFrameOpen()) {
        return;
    }
    void* data = env->GetDirectBufferAddress(source);
    jlong size = env->GetDirectBufferCapacity(source);
    if (!data || size <= 0) {
        return;
    }
    NativeTexture* texture;
    {
        std::lock_guard<std::mutex> lock(g_resourceMutex);
        auto it = g_textures.find(static_cast<int64_t>(handle));
        if (it == g_textures.end()) {
            Log("nativeUpdateTexture unknown handle=%lld", static_cast<long long>(handle));
            return;
        }
        texture = &it->second;
    }
    if (level < 0 || static_cast<UINT>(level) >= texture->mipLevels || layer < 0 || static_cast<UINT>(layer) >= texture->layers || rowPitch < width * static_cast<int>(texture->pixelSize)) {
        Log("nativeUpdateTexture bad args handle=%lld level=%d", static_cast<long long>(handle), static_cast<int>(level));
        return;
    }
    const UINT bytesPerRow = static_cast<UINT>(width) * texture->pixelSize;
    const UINT alignedPitch = (bytesPerRow + 255u) & ~255u;
    const UINT64 total = static_cast<UINT64>(alignedPitch) * static_cast<UINT64>(height);
    UploadAlloc alloc;
    if (!AllocUpload(total, 512, alloc)) {
        return;
    }
    const uint8_t* srcBytes = static_cast<const uint8_t*>(data);
    for (int row = 0; row < height; ++row) {
        std::memcpy(alloc.cpu + static_cast<size_t>(row) * alignedPitch, srcBytes + static_cast<size_t>(row) * rowPitch, bytesPerRow);
    }
    TransitionResource(texture->resource.Get(), texture->state, D3D12_RESOURCE_STATE_COPY_DEST);
    D3D12_TEXTURE_COPY_LOCATION dst = {};
    dst.pResource = texture->resource.Get();
    dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dst.SubresourceIndex = static_cast<UINT>(level) + static_cast<UINT>(layer) * texture->mipLevels;
    D3D12_TEXTURE_COPY_LOCATION src = {};
    src.pResource = alloc.resource;
    src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    src.PlacedFootprint.Offset = alloc.offset;
    src.PlacedFootprint.Footprint.Format = texture->format;
    src.PlacedFootprint.Footprint.Width = static_cast<UINT>(width);
    src.PlacedFootprint.Footprint.Height = static_cast<UINT>(height);
    src.PlacedFootprint.Footprint.Depth = 1;
    src.PlacedFootprint.Footprint.RowPitch = alignedPitch;
    g_commandList->CopyTextureRegion(&dst, static_cast<UINT>(xOffset), static_cast<UINT>(yOffset), 0, &src, nullptr);
    TransitionResource(texture->resource.Get(), texture->state, kTextureReadState);
    if (g_loggedTextureUpdates < 32) {
        Log("nativeUpdateTexture handle=%lld level=%d layer=%d dst=%dx%d size=%dx%d", static_cast<long long>(handle), static_cast<int>(level), static_cast<int>(layer), static_cast<int>(xOffset), static_cast<int>(yOffset), width, height);
        ++g_loggedTextureUpdates;
    }
}

extern "C" JNIEXPORT void JNICALL
Java_banditvault_d3d11mod_nativebridge_D3D11Native_nativeDestroyTexture(JNIEnv*, jclass, jlong handle) {
    std::lock_guard<std::mutex> lock(g_resourceMutex);
    auto it = g_textures.find(static_cast<int64_t>(handle));
    if (it == g_textures.end()) {
        return;
    }
    g_frames[g_frameIndex].garbage.push_back(it->second.resource);
    if (it->second.srvSlot != 0) {
        g_cpuSrvFree.push_back(it->second.srvSlot);
    }
    for (UINT i = 0; i < 16; ++i) {
        if (it->second.rtvSlots[i] != UINT_MAX) {
            g_rtvFree.push_back(it->second.rtvSlots[i]);
        }
    }
    g_textures.erase(it);
}

extern "C" JNIEXPORT void JNICALL
Java_banditvault_d3d11mod_nativebridge_D3D11Native_nativeClearTexture(JNIEnv*, jclass, jlong handle, jfloat r, jfloat g, jfloat b, jfloat a) {
    if (!g_device || handle == 0 || !EnsureFrameOpen()) {
        return;
    }
    NativeTexture* texture;
    {
        std::lock_guard<std::mutex> lock(g_resourceMutex);
        auto it = g_textures.find(static_cast<int64_t>(handle));
        if (it == g_textures.end()) {
            return;
        }
        texture = &it->second;
    }
    TransitionResource(texture->resource.Get(), texture->state, D3D12_RESOURCE_STATE_RENDER_TARGET);
    const float clear[] = { r, g, b, a };
    for (UINT mip = 0; mip < texture->mipLevels && mip < 16; ++mip) {
        UINT slot = GetOrCreateTextureRtvSlot(*texture, mip);
        if (slot != UINT_MAX) {
            g_commandList->ClearRenderTargetView(RtvHandle(slot), clear, 0, nullptr);
        }
    }
    TransitionResource(texture->resource.Get(), texture->state, kTextureReadState);
}

extern "C" JNIEXPORT void JNICALL
Java_banditvault_d3d11mod_nativebridge_D3D11Native_nativeClearDepth(JNIEnv*, jclass, jfloat depth) {
    if (!g_device || !EnsureFrameOpen() || !EnsureMainDepth()) {
        return;
    }
    g_commandList->ClearDepthStencilView(DsvHandle(g_mainDepthDsvSlot), D3D12_CLEAR_FLAG_DEPTH, depth, 0, 0, nullptr);
}

extern "C" JNIEXPORT void JNICALL
Java_banditvault_d3d11mod_nativebridge_D3D11Native_nativeClearOffscreenDepth(JNIEnv*, jclass, jint width, jint height, jfloat depth) {
    if (!g_device || width <= 0 || height <= 0 || !EnsureFrameOpen()) {
        return;
    }
    OffscreenDepth* entry = GetOrCreateOffscreenDepth(static_cast<UINT>(width), static_cast<UINT>(height));
    if (entry) {
        g_commandList->ClearDepthStencilView(DsvHandle(entry->dsvSlot), D3D12_CLEAR_FLAG_DEPTH, depth, 0, 0, nullptr);
    }
}
// ---- JNI: draws ------------------------------------------------------------------------------

static bool EnsureFanIndexBuffer(UINT vertexCount) {
    if (g_fanIndexBuffer && g_fanIndexVertexCapacity >= vertexCount) {
        return true;
    }
    UINT capacity = g_fanIndexVertexCapacity < 64 ? 64 : g_fanIndexVertexCapacity * 2;
    while (capacity < vertexCount) {
        capacity *= 2;
    }
    const UINT triangles = capacity - 2;
    std::vector<uint16_t> indices(static_cast<size_t>(triangles) * 3);
    for (UINT i = 0; i < triangles; ++i) {
        indices[i * 3 + 0] = 0;
        indices[i * 3 + 1] = static_cast<uint16_t>(i + 1);
        indices[i * 3 + 2] = static_cast<uint16_t>(i + 2);
    }
    if (g_fanIndexBuffer) {
        g_frames[g_frameIndex].garbage.push_back(g_fanIndexBuffer);
    }
    if (!CreateUploadBuffer(indices.size() * sizeof(uint16_t), g_fanIndexBuffer)) {
        return false;
    }
    void* mapped = nullptr;
    D3D12_RANGE readRange = {};
    if (FAILED(g_fanIndexBuffer->Map(0, &readRange, &mapped))) {
        return false;
    }
    std::memcpy(mapped, indices.data(), indices.size() * sizeof(uint16_t));
    g_fanIndexBuffer->Unmap(0, nullptr);
    g_fanIndexVertexCapacity = capacity;
    return true;
}

// GUI blend codes from Java: 0 translucent, 1 additive, 2 premultiplied, 3 none, 4 invert, 5 vignette.
static int TranslateGuiBlend(int blend) {
    switch (blend) {
        case 1: return BLEND_ADDITIVE;
        case 2: return BLEND_PREMULTIPLIED;
        case 3: return BLEND_NONE;
        case 4: return BLEND_INVERT;
        case 5: return BLEND_VIGNETTE;
        default: return BLEND_TRANSLUCENT;
    }
}

extern "C" JNIEXPORT jboolean JNICALL
Java_banditvault_d3d11mod_nativebridge_D3D11Native_nativeDrawGuiIndexed(
    JNIEnv* env, jclass,
    jlong vertexBufferHandle, jint vertexStride, jlong indexBufferHandle, jint indexBytes,
    jint vertexOffset, jint firstIndex, jint indexCount, jint instanceCount,
    jint uvOffset, jint colorOffset, jint psMode, jint blendMode,
    jint scissorX, jint scissorY, jint scissorWidth, jint scissorHeight,
    jlong textureHandle, jobject dynamicTransforms, jobject projection) {
    if (!g_device || vertexBufferHandle == 0 || indexBufferHandle == 0 || indexCount <= 0 || colorOffset < 0) {
        Log("nativeDrawGuiIndexed rejected vb=%lld ib=%lld count=%d", static_cast<long long>(vertexBufferHandle), static_cast<long long>(indexBufferHandle), static_cast<int>(indexCount));
        return JNI_FALSE;
    }
    void* dynData = env->GetDirectBufferAddress(dynamicTransforms);
    jlong dynSize = env->GetDirectBufferCapacity(dynamicTransforms);
    void* projData = env->GetDirectBufferAddress(projection);
    jlong projSize = env->GetDirectBufferCapacity(projection);
    if (!dynData || dynSize < 160 || !projData || projSize < 64) {
        return JNI_FALSE;
    }
    if (!EnsureFrameOpen() || !EnsurePipelines()) {
        return JNI_FALSE;
    }

    NativeBuffer vertexNative;
    NativeBuffer indexNative;
    UINT textureSrvSlot = 0;
    ID3D12Resource* textureResource = nullptr;
    D3D12_RESOURCE_STATES* textureState = nullptr;
    const bool hasTexture = textureHandle != 0;
    {
        std::lock_guard<std::mutex> lock(g_resourceMutex);
        auto vb = g_buffers.find(static_cast<int64_t>(vertexBufferHandle));
        auto ib = g_buffers.find(static_cast<int64_t>(indexBufferHandle));
        if (vb == g_buffers.end() || ib == g_buffers.end()) {
            return JNI_FALSE;
        }
        vertexNative = vb->second;
        indexNative = ib->second;
        if (hasTexture) {
            auto tex = g_textures.find(static_cast<int64_t>(textureHandle));
            if (tex == g_textures.end()) {
                return JNI_FALSE;
            }
            textureSrvSlot = tex->second.srvSlot;
            textureResource = tex->second.resource.Get();
            textureState = &tex->second.state;
        }
    }
    if (textureResource) {
        TransitionResource(textureResource, *textureState, kTextureReadState);
    }

    if (!g_loggedFirstGuiUniforms) {
        const float* dyn = static_cast<const float*>(dynData);
        Log("gui uniforms (D3D12) MV3=(%.3f,%.3f,%.3f,%.3f) colorMod=(%.3f,%.3f,%.3f,%.3f)",
            dyn[12], dyn[13], dyn[14], dyn[15], dyn[16], dyn[17], dyn[18], dyn[19]);
        g_loggedFirstGuiUniforms = true;
    }

    // psMode bit 0x100 = bound sampler is LINEAR; point default keeps magnified pixel art crisp.
    const bool guiLinearSampler = (psMode & 0x100) != 0;
    const int guiBaseMode = psMode & 0xFF;
    Program program = PROG_GUI;
    if (hasTexture) {
        if (guiBaseMode == 2) {
            program = guiLinearSampler ? PROG_GUI_TEXT : PROG_GUI_TEXT_POINT;
        } else {
            program = guiLinearSampler ? PROG_GUI_TEXTURED : PROG_GUI_TEXTURED_POINT;
        }
    }
    const UINT layoutKey = (static_cast<UINT>(uvOffset < 0 ? 0 : uvOffset) & 0xFFFu) | ((static_cast<UINT>(colorOffset) & 0xFFFu) << 12);
    ID3D12PipelineState* pso = GetPso(program, TranslateGuiBlend(blendMode), DEPTH_OFF, RASTER_NONE_CCW, layoutKey, DXGI_FORMAT_B8G8R8A8_UNORM);
    if (!pso) {
        return JNI_FALSE;
    }

    DrawTarget target;
    target.rtv = BackBufferRtv(g_frameIndex);
    target.width = static_cast<UINT>(g_width);
    target.height = static_cast<UINT>(g_height);
    BindTarget(target);
    if (scissorWidth > 0 && scissorHeight > 0) {
        // GL scissor (origin bottom-left) -> D3D top-left.
        D3D12_RECT rect = {};
        rect.left = scissorX < 0 ? 0 : scissorX;
        rect.right = scissorX + scissorWidth > g_width ? g_width : scissorX + scissorWidth;
        rect.top = g_height - (scissorY + scissorHeight) < 0 ? 0 : g_height - (scissorY + scissorHeight);
        rect.bottom = g_height - scissorY > g_height ? g_height : g_height - scissorY;
        g_commandList->RSSetScissorRects(1, &rect);
    }

    g_commandList->SetGraphicsRootSignature(g_rootSignature.Get());
    g_commandList->SetPipelineState(pso);
    g_commandList->SetGraphicsRootConstantBufferView(0, UploadCbuffer(dynData, 160, 256));
    g_commandList->SetGraphicsRootConstantBufferView(1, UploadCbuffer(projData, 64, 256));
    g_commandList->SetGraphicsRootDescriptorTable(5, AllocSrvTable(hasTexture ? textureSrvSlot : 0, 0, 0));
    g_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    SetVertexBuffer(vertexNative, static_cast<UINT>(vertexStride));
    SetIndexBuffer(indexNative, indexBytes);
    g_commandList->DrawIndexedInstanced(static_cast<UINT>(indexCount), instanceCount > 1 ? static_cast<UINT>(instanceCount) : 1, static_cast<UINT>(firstIndex), static_cast<INT>(vertexOffset), 0);

    if (!g_loggedFirstGuiDraw) {
        Log("nativeDrawGuiIndexed (D3D12) first draw stride=%d count=%d uv=%d color=%d blend=%d tex=%lld",
            static_cast<int>(vertexStride), static_cast<int>(indexCount), static_cast<int>(uvOffset), static_cast<int>(colorOffset), static_cast<int>(blendMode), static_cast<long long>(textureHandle));
        g_loggedFirstGuiDraw = true;
    }
    return JNI_TRUE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_banditvault_d3d11mod_nativebridge_D3D11Native_nativeDrawSpriteBlit(
    JNIEnv* env, jclass, jlong targetTexture, jint targetMip, jboolean interpolate,
    jlong spriteTexture, jlong nextSpriteTexture, jint vertexOffset, jint vertexCount, jobject spriteInfo) {
    if (!g_device || targetTexture == 0 || spriteTexture == 0 || vertexCount <= 0) {
        return JNI_FALSE;
    }
    void* infoData = env->GetDirectBufferAddress(spriteInfo);
    jlong infoSize = env->GetDirectBufferCapacity(spriteInfo);
    if (!infoData || infoSize < 140 || !EnsureFrameOpen() || !EnsurePipelines()) {
        return JNI_FALSE;
    }
    UINT targetRtvSlot = UINT_MAX;
    UINT targetWidth = 1;
    UINT targetHeight = 1;
    DXGI_FORMAT targetFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    ID3D12Resource* targetRes = nullptr;
    D3D12_RESOURCE_STATES* targetState = nullptr;
    UINT spriteSrv = 0;
    ID3D12Resource* spriteRes = nullptr;
    D3D12_RESOURCE_STATES* spriteState = nullptr;
    UINT nextSrv = 0;
    ID3D12Resource* nextRes = nullptr;
    D3D12_RESOURCE_STATES* nextState = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_resourceMutex);
        auto target = g_textures.find(static_cast<int64_t>(targetTexture));
        auto sprite = g_textures.find(static_cast<int64_t>(spriteTexture));
        if (target == g_textures.end() || sprite == g_textures.end()) {
            return JNI_FALSE;
        }
        targetRtvSlot = GetOrCreateTextureRtvSlot(target->second, static_cast<UINT>(targetMip));
        targetWidth = target->second.width >> targetMip;
        targetHeight = target->second.height >> targetMip;
        targetFormat = target->second.format;
        targetRes = target->second.resource.Get();
        targetState = &target->second.state;
        spriteSrv = sprite->second.srvSlot;
        spriteRes = sprite->second.resource.Get();
        spriteState = &sprite->second.state;
        if (interpolate) {
            auto next = g_textures.find(static_cast<int64_t>(nextSpriteTexture));
            if (next == g_textures.end()) {
                return JNI_FALSE;
            }
            nextSrv = next->second.srvSlot;
            nextRes = next->second.resource.Get();
            nextState = &next->second.state;
        }
    }
    if (targetRtvSlot == UINT_MAX) {
        return JNI_FALSE;
    }
    if (targetWidth == 0) targetWidth = 1;
    if (targetHeight == 0) targetHeight = 1;
    TransitionResource(targetRes, *targetState, D3D12_RESOURCE_STATE_RENDER_TARGET);
    TransitionResource(spriteRes, *spriteState, kTextureReadState);
    if (nextRes) {
        TransitionResource(nextRes, *nextState, kTextureReadState);
    }

    ID3D12PipelineState* pso = GetPso(interpolate ? PROG_SPRITE_INTERP : PROG_SPRITE_BLIT, BLEND_NONE, DEPTH_OFF, RASTER_NONE_CCW, 0, targetFormat);
    if (!pso) {
        return JNI_FALSE;
    }
    DrawTarget target;
    target.rtv = RtvHandle(targetRtvSlot);
    target.width = targetWidth;
    target.height = targetHeight;
    BindTarget(target);
    g_commandList->SetGraphicsRootSignature(g_rootSignature.Get());
    g_commandList->SetPipelineState(pso);
    g_commandList->SetGraphicsRootConstantBufferView(0, UploadCbuffer(infoData, infoSize < 144 ? infoSize : 144, 256));
    g_commandList->SetGraphicsRootDescriptorTable(5, AllocSrvTable(spriteSrv, interpolate ? nextSrv : 0, 0));
    g_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    g_commandList->DrawInstanced(static_cast<UINT>(vertexCount), 1, static_cast<UINT>(vertexOffset), 0);

    if (g_loggedSpriteBlits < 4) {
        Log("nativeDrawSpriteBlit (D3D12) target=%lld mip=%d interpolate=%d count=%d viewport=%ux%u",
            static_cast<long long>(targetTexture), static_cast<int>(targetMip), interpolate ? 1 : 0, static_cast<int>(vertexCount), targetWidth, targetHeight);
        ++g_loggedSpriteBlits;
    }
    return JNI_TRUE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_banditvault_d3d11mod_nativebridge_D3D11Native_nativeDrawPostPass(
    JNIEnv* env, jclass, jlong inputTexture, jlong targetTexture, jint targetMip, jint vertexOffset, jint vertexCount, jobject blurConfig, jobject globals) {
    if (!g_device || vertexCount <= 0 || !blurConfig || !EnsureFrameOpen() || !EnsurePipelines()) {
        return JNI_FALSE;
    }
    void* blurData = env->GetDirectBufferAddress(blurConfig);
    jlong blurSize = env->GetDirectBufferCapacity(blurConfig);
    if (!blurData || blurSize < 12) {
        return JNI_FALSE;
    }

    // Input: 0 = minecraft:main (the backbuffer) -> copy it into the reusable snapshot.
    UINT inputSrv = 0;
    float inWidth = 1.0f;
    float inHeight = 1.0f;
    if (inputTexture == 0) {
        if (!g_postSnapshot || g_postSnapshotWidth != static_cast<UINT>(g_width) || g_postSnapshotHeight != static_cast<UINT>(g_height)) {
            if (g_postSnapshot) {
                g_frames[g_frameIndex].garbage.push_back(g_postSnapshot);
                g_postSnapshot.Reset();
            }
            if (g_postSnapshotSrvSlot == 0) {
                g_postSnapshotSrvSlot = AllocCpuSrvSlot();
            }
            D3D12_HEAP_PROPERTIES heap = {};
            heap.Type = D3D12_HEAP_TYPE_DEFAULT;
            D3D12_RESOURCE_DESC desc = {};
            desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
            desc.Width = static_cast<UINT64>(g_width);
            desc.Height = static_cast<UINT>(g_height);
            desc.DepthOrArraySize = 1;
            desc.MipLevels = 1;
            desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;  // must match the swapchain for CopyResource
            desc.SampleDesc.Count = 1;
            HRESULT hr = g_device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(g_postSnapshot.ReleaseAndGetAddressOf()));
            if (FAILED(hr)) {
                LogHr("CreateCommittedResource(post snapshot)", hr);
                return JNI_FALSE;
            }
            g_postSnapshotState = D3D12_RESOURCE_STATE_COPY_DEST;
            g_postSnapshotWidth = static_cast<UINT>(g_width);
            g_postSnapshotHeight = static_cast<UINT>(g_height);
            D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
            srvDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
            srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srvDesc.Texture2D.MipLevels = 1;
            g_device->CreateShaderResourceView(g_postSnapshot.Get(), &srvDesc, CpuSrvHandle(g_postSnapshotSrvSlot));
            ++g_srvWriteGeneration;
        }
        // The backbuffer sits in RENDER_TARGET between beginFrame and present.
        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.pResource = g_backBuffers[g_frameIndex].Get();
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
        g_commandList->ResourceBarrier(1, &barrier);
        TransitionResource(g_postSnapshot.Get(), g_postSnapshotState, D3D12_RESOURCE_STATE_COPY_DEST);
        g_commandList->CopyResource(g_postSnapshot.Get(), g_backBuffers[g_frameIndex].Get());
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        g_commandList->ResourceBarrier(1, &barrier);
        TransitionResource(g_postSnapshot.Get(), g_postSnapshotState, kTextureReadState);
        inputSrv = g_postSnapshotSrvSlot;
        inWidth = static_cast<float>(g_width);
        inHeight = static_cast<float>(g_height);
    } else {
        ID3D12Resource* inputRes = nullptr;
        D3D12_RESOURCE_STATES* inputState = nullptr;
        {
            std::lock_guard<std::mutex> lock(g_resourceMutex);
            auto tex = g_textures.find(static_cast<int64_t>(inputTexture));
            if (tex == g_textures.end()) {
                return JNI_FALSE;
            }
            inputSrv = tex->second.srvSlot;
            inputRes = tex->second.resource.Get();
            inputState = &tex->second.state;
            inWidth = static_cast<float>(tex->second.width);
            inHeight = static_cast<float>(tex->second.height);
        }
        TransitionResource(inputRes, *inputState, kTextureReadState);
    }

    DrawTarget target;
    DXGI_FORMAT targetFormat = DXGI_FORMAT_B8G8R8A8_UNORM;
    if (targetTexture == 0) {
        target.rtv = BackBufferRtv(g_frameIndex);
        target.width = static_cast<UINT>(g_width);
        target.height = static_cast<UINT>(g_height);
    } else {
        UINT targetRtvSlot = UINT_MAX;
        ID3D12Resource* targetRes = nullptr;
        D3D12_RESOURCE_STATES* targetState = nullptr;
        UINT tw = 1;
        UINT th = 1;
        {
            std::lock_guard<std::mutex> lock(g_resourceMutex);
            auto tex = g_textures.find(static_cast<int64_t>(targetTexture));
            if (tex == g_textures.end()) {
                return JNI_FALSE;
            }
            targetRtvSlot = GetOrCreateTextureRtvSlot(tex->second, static_cast<UINT>(targetMip));
            tw = tex->second.width >> targetMip;
            th = tex->second.height >> targetMip;
            targetFormat = tex->second.format;
            targetRes = tex->second.resource.Get();
            targetState = &tex->second.state;
        }
        if (targetRtvSlot == UINT_MAX) {
            return JNI_FALSE;
        }
        TransitionResource(targetRes, *targetState, D3D12_RESOURCE_STATE_RENDER_TARGET);
        target.rtv = RtvHandle(targetRtvSlot);
        target.width = tw ? tw : 1;
        target.height = th ? th : 1;
    }

    float samplerInfo[4] = { static_cast<float>(target.width), static_cast<float>(target.height), inWidth, inHeight };
    unsigned char blurUpload[16] = {};
    std::memcpy(blurUpload, blurData, blurSize < 16 ? static_cast<size_t>(blurSize) : 16u);
    unsigned char globalsUpload[64] = {};
    if (globals) {
        void* globalsData = env->GetDirectBufferAddress(globals);
        jlong globalsSize = env->GetDirectBufferCapacity(globals);
        if (globalsData && globalsSize > 0) {
            std::memcpy(globalsUpload, globalsData, globalsSize < 64 ? static_cast<size_t>(globalsSize) : 64u);
        }
    }

    ID3D12PipelineState* pso = GetPso(PROG_POST_BLUR, BLEND_NONE, DEPTH_OFF, RASTER_NONE_CCW, 0, targetFormat);
    if (!pso) {
        return JNI_FALSE;
    }
    BindTarget(target);
    g_commandList->SetGraphicsRootSignature(g_rootSignature.Get());
    g_commandList->SetPipelineState(pso);
    g_commandList->SetGraphicsRootConstantBufferView(0, UploadCbuffer(samplerInfo, 16, 256));
    g_commandList->SetGraphicsRootConstantBufferView(1, UploadCbuffer(blurUpload, 16, 256));
    g_commandList->SetGraphicsRootConstantBufferView(2, UploadCbuffer(globalsUpload, 64, 256));
    g_commandList->SetGraphicsRootDescriptorTable(5, AllocSrvTable(inputSrv, 0, 0));
    g_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    g_commandList->DrawInstanced(static_cast<UINT>(vertexCount), 1, static_cast<UINT>(vertexOffset), 0);
    if (g_loggedPostPasses < 6) {
        Log("nativeDrawPostPass (D3D12) input=%lld target=%lld out=%ux%u count=%d", static_cast<long long>(inputTexture), static_cast<long long>(targetTexture), target.width, target.height, static_cast<int>(vertexCount));
        ++g_loggedPostPasses;
    }
    return JNI_TRUE;
}
extern "C" JNIEXPORT jboolean JNICALL
Java_banditvault_d3d11mod_nativebridge_D3D11Native_nativeDrawGlintToTexture(
    JNIEnv* env, jclass, jlong vertexBufferHandle, jint vertexStride, jlong indexBufferHandle, jint indexBytes,
    jint vertexOffset, jint firstIndex, jint count, jlong glintTexture,
    jlong targetTexture, jint targetMip, jboolean targetHasDepth,
    jobject dynamicTransforms, jobject projection, jobject fog, jobject globals) {
    if (!g_device || vertexBufferHandle == 0 || indexBufferHandle == 0 || count <= 0 || glintTexture == 0 || targetTexture == 0 || !EnsureFrameOpen() || !EnsurePipelines()) {
        return JNI_FALSE;
    }
    void* dynData = env->GetDirectBufferAddress(dynamicTransforms);
    jlong dynSize = env->GetDirectBufferCapacity(dynamicTransforms);
    void* projData = env->GetDirectBufferAddress(projection);
    jlong projSize = env->GetDirectBufferCapacity(projection);
    if (!dynData || dynSize < 160 || !projData || projSize < 64) {
        return JNI_FALSE;
    }
    NativeBuffer vertexNative;
    NativeBuffer indexNative;
    UINT glintSrv = 0;
    ID3D12Resource* glintRes = nullptr;
    D3D12_RESOURCE_STATES* glintState = nullptr;
    UINT targetRtvSlot = UINT_MAX;
    UINT targetWidth = 1;
    UINT targetHeight = 1;
    DXGI_FORMAT targetFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    ID3D12Resource* targetRes = nullptr;
    D3D12_RESOURCE_STATES* targetState = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_resourceMutex);
        auto vb = g_buffers.find(static_cast<int64_t>(vertexBufferHandle));
        auto ib = g_buffers.find(static_cast<int64_t>(indexBufferHandle));
        auto glint = g_textures.find(static_cast<int64_t>(glintTexture));
        auto target = g_textures.find(static_cast<int64_t>(targetTexture));
        if (vb == g_buffers.end() || ib == g_buffers.end() || glint == g_textures.end() || target == g_textures.end()) {
            return JNI_FALSE;
        }
        vertexNative = vb->second;
        indexNative = ib->second;
        glintSrv = glint->second.srvSlot;
        glintRes = glint->second.resource.Get();
        glintState = &glint->second.state;
        targetRtvSlot = GetOrCreateTextureRtvSlot(target->second, static_cast<UINT>(targetMip));
        targetWidth = target->second.width >> targetMip;
        targetHeight = target->second.height >> targetMip;
        targetFormat = target->second.format;
        targetRes = target->second.resource.Get();
        targetState = &target->second.state;
    }
    if (targetRtvSlot == UINT_MAX) {
        return JNI_FALSE;
    }
    if (targetWidth == 0) targetWidth = 1;
    if (targetHeight == 0) targetHeight = 1;
    TransitionResource(glintRes, *glintState, kTextureReadState);
    TransitionResource(targetRes, *targetState, D3D12_RESOURCE_STATE_RENDER_TARGET);
    DrawTarget target;
    target.rtv = RtvHandle(targetRtvSlot);
    target.width = targetWidth;
    target.height = targetHeight;
    if (targetHasDepth) {
        OffscreenDepth* offscreen = GetOrCreateOffscreenDepth(targetWidth, targetHeight);
        if (!offscreen) {
            return JNI_FALSE;
        }
        target.hasDsv = true;
        target.dsv = DsvHandle(offscreen->dsvSlot);
    }
    ID3D12PipelineState* pso = GetPso(PROG_GLINT_FLIP, BLEND_GLINT, targetHasDepth ? DEPTH_EQUAL_NO_WRITE : DEPTH_OFF, RASTER_NONE_CCW, 0, targetFormat);
    if (!pso) {
        return JNI_FALSE;
    }
    BindTarget(target);
    g_commandList->SetGraphicsRootSignature(g_rootSignature.Get());
    g_commandList->SetPipelineState(pso);
    g_commandList->SetGraphicsRootConstantBufferView(0, UploadCbuffer(dynData, 160, 256));
    g_commandList->SetGraphicsRootConstantBufferView(1, UploadCbuffer(projData, 64, 256));
    // Root bindings persist across draws: ALWAYS bind b2/b3 with safe defaults so a bake pass
    // without Fog/Globals uniforms cannot inherit stale data (no fog, GlintAlpha = 1).
    float fogUpload[12] = { 0.0f, 0.0f, 0.0f, 0.0f, 1.0e9f, 1.0e9f, 1.0e9f, 1.0e9f, 1.0e9f, 1.0e9f, 0.0f, 0.0f };
    if (fog) {
        void* fogData = env->GetDirectBufferAddress(fog);
        jlong fogSize = env->GetDirectBufferCapacity(fog);
        if (fogData && fogSize >= 40) {
            std::memcpy(fogUpload, fogData, fogSize < 48 ? static_cast<size_t>(fogSize) : 48u);
        }
    }
    g_commandList->SetGraphicsRootConstantBufferView(2, UploadCbuffer(fogUpload, 48, 256));
    float globalsUpload[16] = {};
    globalsUpload[10] = 1.0f;  // G_Misc2.z = GlintAlpha default
    if (globals) {
        void* globalsData = env->GetDirectBufferAddress(globals);
        jlong globalsSize = env->GetDirectBufferCapacity(globals);
        if (globalsData && globalsSize >= 56) {
            std::memcpy(globalsUpload, globalsData, globalsSize < 64 ? static_cast<size_t>(globalsSize) : 64u);
        }
    }
    g_commandList->SetGraphicsRootConstantBufferView(3, UploadCbuffer(globalsUpload, 64, 256));
    g_commandList->SetGraphicsRootDescriptorTable(5, AllocSrvTable(glintSrv, 0, 0));
    g_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    SetVertexBuffer(vertexNative, static_cast<UINT>(vertexStride));
    SetIndexBuffer(indexNative, indexBytes);
    g_commandList->DrawIndexedInstanced(static_cast<UINT>(count), 1, static_cast<UINT>(firstIndex), static_cast<INT>(vertexOffset), 0);
    if (g_loggedGlintBakes < 4) {
        Log("nativeDrawGlintToTexture (D3D12) target=%lld mip=%d count=%d hasDepth=%d", static_cast<long long>(targetTexture), static_cast<int>(targetMip), static_cast<int>(count), targetHasDepth ? 1 : 0);
        ++g_loggedGlintBakes;
    }
    return JNI_TRUE;
}
extern "C" JNIEXPORT jboolean JNICALL
Java_banditvault_d3d11mod_nativebridge_D3D11Native_nativeReadbackTexture(
    JNIEnv* env, jclass, jlong textureHandle, jint mip, jint x, jint y, jint width, jint height, jobject dst) {
    // Synchronous readback: FlushMidFrame stalls the GPU, but screenshots are rare.
    if (!g_device || width <= 0 || height <= 0 || mip < 0 || x < 0 || y < 0 || !dst || !EnsureFrameOpen()) {
        return JNI_FALSE;
    }
    uint8_t* dstBytes = static_cast<uint8_t*>(env->GetDirectBufferAddress(dst));
    jlong dstCapacity = env->GetDirectBufferCapacity(dst);
    if (!dstBytes || dstCapacity < static_cast<jlong>(width) * static_cast<jlong>(height) * 4) {
        return JNI_FALSE;
    }
    ID3D12Resource* srcRes = nullptr;
    D3D12_RESOURCE_STATES* srcState = nullptr;
    UINT subresource = 0;
    DXGI_FORMAT srcFormat = DXGI_FORMAT_B8G8R8A8_UNORM;
    const bool fromBackbuffer = textureHandle == 0;
    if (fromBackbuffer) {
        srcRes = g_backBuffers[g_frameIndex].Get();
    } else {
        std::lock_guard<std::mutex> lock(g_resourceMutex);
        auto it = g_textures.find(static_cast<int64_t>(textureHandle));
        if (it == g_textures.end() || it->second.pixelSize != 4 || static_cast<UINT>(mip) >= it->second.mipLevels || it->second.layers != 1) {
            return JNI_FALSE;
        }
        srcRes = it->second.resource.Get();
        srcState = &it->second.state;
        subresource = static_cast<UINT>(mip);
        srcFormat = it->second.format;
    }
    const bool swizzleBgra = srcFormat == DXGI_FORMAT_B8G8R8A8_UNORM;
    const UINT rowBytes = static_cast<UINT>(width) * 4u;
    const UINT alignedPitch = (rowBytes + 255u) & ~255u;
    const UINT64 total = static_cast<UINT64>(alignedPitch) * static_cast<UINT64>(height);

    D3D12_HEAP_PROPERTIES heap = {};
    heap.Type = D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = total;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ComPtr<ID3D12Resource> readback;
    HRESULT hr = g_device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(readback.GetAddressOf()));
    if (FAILED(hr)) {
        LogHr("CreateCommittedResource(readback)", hr);
        return JNI_FALSE;
    }
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    if (fromBackbuffer) {
        barrier.Transition.pResource = srcRes;
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
        g_commandList->ResourceBarrier(1, &barrier);
    } else {
        TransitionResource(srcRes, *srcState, D3D12_RESOURCE_STATE_COPY_SOURCE);
    }
    D3D12_TEXTURE_COPY_LOCATION srcLoc = {};
    srcLoc.pResource = srcRes;
    srcLoc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    srcLoc.SubresourceIndex = subresource;
    D3D12_TEXTURE_COPY_LOCATION dstLoc = {};
    dstLoc.pResource = readback.Get();
    dstLoc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dstLoc.PlacedFootprint.Offset = 0;
    dstLoc.PlacedFootprint.Footprint.Format = srcFormat;
    dstLoc.PlacedFootprint.Footprint.Width = static_cast<UINT>(width);
    dstLoc.PlacedFootprint.Footprint.Height = static_cast<UINT>(height);
    dstLoc.PlacedFootprint.Footprint.Depth = 1;
    dstLoc.PlacedFootprint.Footprint.RowPitch = alignedPitch;
    D3D12_BOX srcBox = {};
    srcBox.left = static_cast<UINT>(x);
    srcBox.top = static_cast<UINT>(y);
    srcBox.front = 0;
    srcBox.right = static_cast<UINT>(x) + static_cast<UINT>(width);
    srcBox.bottom = static_cast<UINT>(y) + static_cast<UINT>(height);
    srcBox.back = 1;
    g_commandList->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, &srcBox);
    if (fromBackbuffer) {
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        g_commandList->ResourceBarrier(1, &barrier);
    } else {
        TransitionResource(srcRes, *srcState, kTextureReadState);
    }
    FlushMidFrame();
    void* mapped = nullptr;
    D3D12_RANGE readRange = { 0, static_cast<SIZE_T>(total) };
    hr = readback->Map(0, &readRange, &mapped);
    if (FAILED(hr)) {
        LogHr("Map(readback)", hr);
        return JNI_FALSE;
    }
    const uint8_t* srcBytes = static_cast<const uint8_t*>(mapped);
    for (int row = 0; row < height; ++row) {
        // Vanilla assumes GL row order (framebuffer reads come back bottom-first), so flip
        // backbuffer reads; uploaded textures keep natural order.
        const uint8_t* srcRow = srcBytes + static_cast<size_t>(fromBackbuffer ? (height - 1 - row) : row) * alignedPitch;
        uint8_t* dstRow = dstBytes + static_cast<size_t>(row) * rowBytes;
        if (swizzleBgra) {
            for (int px = 0; px < width; ++px) {
                dstRow[px * 4 + 0] = srcRow[px * 4 + 2];
                dstRow[px * 4 + 1] = srcRow[px * 4 + 1];
                dstRow[px * 4 + 2] = srcRow[px * 4 + 0];
                dstRow[px * 4 + 3] = srcRow[px * 4 + 3];
            }
        } else {
            std::memcpy(dstRow, srcRow, rowBytes);
        }
    }
    D3D12_RANGE writtenRange = { 0, 0 };
    readback->Unmap(0, &writtenRange);
    if (g_loggedReadbacks < 4) {
        Log("nativeReadbackTexture (D3D12) handle=%lld mip=%d rect=%d,%d %dx%d backbuffer=%d", static_cast<long long>(textureHandle), static_cast<int>(mip), static_cast<int>(x), static_cast<int>(y), width, height, fromBackbuffer ? 1 : 0);
        ++g_loggedReadbacks;
    }
    return JNI_TRUE;
}
extern "C" JNIEXPORT jboolean JNICALL
Java_banditvault_d3d11mod_nativebridge_D3D11Native_nativeDrawText(
    JNIEnv* env, jclass, jlong vertexBufferHandle, jint vertexStride, jlong indexBufferHandle, jint indexBytes,
    jint vertexOffset, jint firstIndex, jint count, jlong fontTexture, jlong lightmapTexture, jint variant,
    jobject dynamicTransforms, jobject projection, jobject fog) {
    // In-world text. variant: 0 = text, 1 = text_polygon_offset (sign boards), 2 = see_through.
    if (!g_device || vertexBufferHandle == 0 || indexBufferHandle == 0 || count <= 0 || fontTexture == 0 || lightmapTexture == 0 || variant < 0 || variant > 2 || !EnsureFrameOpen() || !EnsurePipelines()) {
        return JNI_FALSE;
    }
    void* dynData = env->GetDirectBufferAddress(dynamicTransforms);
    jlong dynSize = env->GetDirectBufferCapacity(dynamicTransforms);
    void* projData = env->GetDirectBufferAddress(projection);
    jlong projSize = env->GetDirectBufferCapacity(projection);
    if (!dynData || dynSize < 160 || !projData || projSize < 64) {
        return JNI_FALSE;
    }
    NativeBuffer vertexNative;
    NativeBuffer indexNative;
    UINT fontSrv = 0;
    ID3D12Resource* fontRes = nullptr;
    D3D12_RESOURCE_STATES* fontState = nullptr;
    UINT lightmapSrv = 0;
    ID3D12Resource* lightmapRes = nullptr;
    D3D12_RESOURCE_STATES* lightmapState = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_resourceMutex);
        auto vb = g_buffers.find(static_cast<int64_t>(vertexBufferHandle));
        auto ib = g_buffers.find(static_cast<int64_t>(indexBufferHandle));
        auto font = g_textures.find(static_cast<int64_t>(fontTexture));
        auto lightmap = g_textures.find(static_cast<int64_t>(lightmapTexture));
        if (vb == g_buffers.end() || ib == g_buffers.end() || font == g_textures.end() || lightmap == g_textures.end()) {
            return JNI_FALSE;
        }
        vertexNative = vb->second;
        indexNative = ib->second;
        fontSrv = font->second.srvSlot;
        fontRes = font->second.resource.Get();
        fontState = &font->second.state;
        lightmapSrv = lightmap->second.srvSlot;
        lightmapRes = lightmap->second.resource.Get();
        lightmapState = &lightmap->second.state;
    }
    TransitionResource(fontRes, *fontState, kTextureReadState);
    TransitionResource(lightmapRes, *lightmapState, kTextureReadState);
    int depth = DEPTH_OFF;
    int raster = RASTER_BACK_CCW;
    if (variant != 2) {
        if (!EnsureMainDepth()) {
            return JNI_FALSE;
        }
        depth = DEPTH_LEQUAL_WRITE;
        if (variant == 1) {
            raster = RASTER_TEXT_OFFSET;
        }
    }
    ID3D12PipelineState* pso = GetPso(PROG_TEXT, BLEND_TRANSLUCENT, depth, raster, 0, DXGI_FORMAT_B8G8R8A8_UNORM);
    if (!pso) {
        return JNI_FALSE;
    }
    DrawTarget target;
    target.rtv = BackBufferRtv(g_frameIndex);
    target.width = static_cast<UINT>(g_width);
    target.height = static_cast<UINT>(g_height);
    if (depth != DEPTH_OFF) {
        target.hasDsv = true;
        target.dsv = DsvHandle(g_mainDepthDsvSlot);
    }
    BindTarget(target);
    g_commandList->SetGraphicsRootSignature(g_rootSignature.Get());
    g_commandList->SetPipelineState(pso);
    g_commandList->SetGraphicsRootConstantBufferView(0, UploadCbuffer(dynData, 160, 256));
    g_commandList->SetGraphicsRootConstantBufferView(1, UploadCbuffer(projData, 64, 256));
    // Always bind b2: a pass without Fog must not inherit a stale binding (no-fog defaults).
    float fogUpload[12] = { 0.0f, 0.0f, 0.0f, 0.0f, 1.0e9f, 1.0e9f, 1.0e9f, 1.0e9f, 1.0e9f, 1.0e9f, 0.0f, 0.0f };
    if (fog) {
        void* fogData = env->GetDirectBufferAddress(fog);
        jlong fogSize = env->GetDirectBufferCapacity(fog);
        if (fogData && fogSize >= 40) {
            std::memcpy(fogUpload, fogData, fogSize < 48 ? static_cast<size_t>(fogSize) : 48u);
        }
    }
    g_commandList->SetGraphicsRootConstantBufferView(2, UploadCbuffer(fogUpload, 48, 256));
    g_commandList->SetGraphicsRootDescriptorTable(5, AllocSrvTable(fontSrv, lightmapSrv, 0));
    g_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    SetVertexBuffer(vertexNative, static_cast<UINT>(vertexStride));
    SetIndexBuffer(indexNative, indexBytes);
    g_commandList->DrawIndexedInstanced(static_cast<UINT>(count), 1, static_cast<UINT>(firstIndex), static_cast<INT>(vertexOffset), 0);
    if (g_loggedTextDraws < 4) {
        Log("nativeDrawText (D3D12) variant=%d count=%d font=%lld", static_cast<int>(variant), static_cast<int>(count), static_cast<long long>(fontTexture));
        ++g_loggedTextDraws;
    }
    return JNI_TRUE;
}
extern "C" JNIEXPORT jboolean JNICALL
Java_banditvault_d3d11mod_nativebridge_D3D11Native_nativeDrawLightmap(
    JNIEnv* env, jclass, jlong targetTexture, jint targetMip, jint vertexOffset, jint vertexCount, jobject lightmapInfo) {
    if (!g_device || targetTexture == 0 || vertexCount <= 0) {
        return JNI_FALSE;
    }
    void* infoData = env->GetDirectBufferAddress(lightmapInfo);
    jlong infoSize = env->GetDirectBufferCapacity(lightmapInfo);
    if (!infoData || infoSize < 60 || !EnsureFrameOpen() || !EnsurePipelines()) {
        return JNI_FALSE;
    }
    UINT rtvSlot = UINT_MAX;
    UINT width = 1;
    UINT height = 1;
    DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM;
    ID3D12Resource* res = nullptr;
    D3D12_RESOURCE_STATES* state = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_resourceMutex);
        auto it = g_textures.find(static_cast<int64_t>(targetTexture));
        if (it == g_textures.end()) {
            return JNI_FALSE;
        }
        rtvSlot = GetOrCreateTextureRtvSlot(it->second, static_cast<UINT>(targetMip));
        width = it->second.width >> targetMip;
        height = it->second.height >> targetMip;
        format = it->second.format;
        res = it->second.resource.Get();
        state = &it->second.state;
    }
    if (rtvSlot == UINT_MAX) {
        return JNI_FALSE;
    }
    if (width == 0) width = 1;
    if (height == 0) height = 1;
    TransitionResource(res, *state, D3D12_RESOURCE_STATE_RENDER_TARGET);
    ID3D12PipelineState* pso = GetPso(PROG_LIGHTMAP, BLEND_NONE, DEPTH_OFF, RASTER_NONE_CCW, 0, format);
    if (!pso) {
        return JNI_FALSE;
    }
    DrawTarget target;
    target.rtv = RtvHandle(rtvSlot);
    target.width = width;
    target.height = height;
    BindTarget(target);
    g_commandList->SetGraphicsRootSignature(g_rootSignature.Get());
    g_commandList->SetPipelineState(pso);
    g_commandList->SetGraphicsRootConstantBufferView(0, UploadCbuffer(infoData, infoSize < 64 ? infoSize : 64, 256));
    g_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    g_commandList->DrawInstanced(static_cast<UINT>(vertexCount), 1, static_cast<UINT>(vertexOffset), 0);
    if (!g_loggedFirstLightmapDraw) {
        Log("nativeDrawLightmap (D3D12) target=%lld count=%d viewport=%ux%u", static_cast<long long>(targetTexture), static_cast<int>(vertexCount), width, height);
        g_loggedFirstLightmapDraw = true;
    }
    return JNI_TRUE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_banditvault_d3d11mod_nativebridge_D3D11Native_nativeBeginTerrainBatch(
    JNIEnv* env, jclass, jfloat alphaCutoff, jboolean translucent, jlong atlasTexture, jlong lightmapTexture,
    jobject projection, jobject fog, jobject globals) {
    g_terrainBatchActive = false;
    void* projData = env->GetDirectBufferAddress(projection);
    jlong projSize = env->GetDirectBufferCapacity(projection);
    void* fogData = env->GetDirectBufferAddress(fog);
    jlong fogSize = env->GetDirectBufferCapacity(fog);
    void* globalsData = env->GetDirectBufferAddress(globals);
    jlong globalsSize = env->GetDirectBufferCapacity(globals);
    if (!projData || projSize < 64 || !fogData || fogSize < 40 || !globalsData || globalsSize < 56) {
        return JNI_FALSE;
    }
    if (!EnsureFrameOpen() || !EnsurePipelines() || !EnsureMainDepth()) {
        return JNI_FALSE;
    }
    UINT atlasSrv = 0;
    UINT lightmapSrv = 0;
    ID3D12Resource* atlasRes = nullptr;
    D3D12_RESOURCE_STATES* atlasState = nullptr;
    ID3D12Resource* lightmapRes = nullptr;
    D3D12_RESOURCE_STATES* lightmapState = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_resourceMutex);
        auto atlas = g_textures.find(static_cast<int64_t>(atlasTexture));
        auto lightmap = g_textures.find(static_cast<int64_t>(lightmapTexture));
        if (atlas == g_textures.end() || lightmap == g_textures.end()) {
            return JNI_FALSE;
        }
        atlasSrv = atlas->second.srvSlot;
        atlasRes = atlas->second.resource.Get();
        atlasState = &atlas->second.state;
        lightmapSrv = lightmap->second.srvSlot;
        lightmapRes = lightmap->second.resource.Get();
        lightmapState = &lightmap->second.state;
    }
    TransitionResource(atlasRes, *atlasState, kTextureReadState);
    TransitionResource(lightmapRes, *lightmapState, kTextureReadState);

    unsigned char fogUpload[48] = {};
    std::memcpy(fogUpload, fogData, fogSize < 48 ? static_cast<size_t>(fogSize) : 48u);
    unsigned char globalsUpload[64] = {};
    std::memcpy(globalsUpload, globalsData, globalsSize < 64 ? static_cast<size_t>(globalsSize) : 64u);
    const float params[4] = { alphaCutoff, 0.0f, 0.0f, 0.0f };

    g_terrainProjVa = UploadCbuffer(projData, 64, 256);
    g_terrainFogVa = UploadCbuffer(fogUpload, 48, 256);
    g_terrainGlobalsVa = UploadCbuffer(globalsUpload, 64, 256);
    g_terrainParamsVa = UploadCbuffer(params, 16, 256);
    g_terrainSrvTable = AllocSrvTable(atlasSrv, 0, lightmapSrv);
    g_terrainPso = GetPso(PROG_TERRAIN, translucent ? BLEND_TRANSLUCENT : BLEND_NONE, DEPTH_LEQUAL_WRITE, RASTER_BACK_CCW, 0, DXGI_FORMAT_B8G8R8A8_UNORM);  // vanilla culls terrain; cull-off doubled glass panes
    if (!g_terrainPso) {
        return JNI_FALSE;
    }

    DrawTarget target;
    target.rtv = BackBufferRtv(g_frameIndex);
    target.hasDsv = true;
    target.dsv = DsvHandle(g_mainDepthDsvSlot);
    target.width = static_cast<UINT>(g_width);
    target.height = static_cast<UINT>(g_height);
    BindTarget(target);
    g_commandList->SetGraphicsRootSignature(g_rootSignature.Get());
    g_commandList->SetPipelineState(g_terrainPso);
    g_commandList->SetGraphicsRootConstantBufferView(1, g_terrainProjVa);
    g_commandList->SetGraphicsRootConstantBufferView(2, g_terrainGlobalsVa);
    g_commandList->SetGraphicsRootConstantBufferView(3, g_terrainFogVa);
    g_commandList->SetGraphicsRootConstantBufferView(4, g_terrainParamsVa);
    g_commandList->SetGraphicsRootDescriptorTable(5, g_terrainSrvTable);
    g_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    g_terrainBatchActive = true;
    return JNI_TRUE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_banditvault_d3d11mod_nativebridge_D3D11Native_nativeDrawTerrainSection(
    JNIEnv* env, jclass, jlong vertexBufferHandle, jint vertexStride, jlong indexBufferHandle, jint indexBytes,
    jint firstIndex, jint indexCount, jobject chunkSection) {
    if (!g_terrainBatchActive) {
        return JNI_FALSE;
    }
    void* csData = env->GetDirectBufferAddress(chunkSection);
    jlong csSize = env->GetDirectBufferCapacity(chunkSection);
    if (!csData || csSize < 92 || vertexBufferHandle == 0 || indexBufferHandle == 0 || indexCount <= 0) {
        return JNI_FALSE;
    }
    NativeBuffer vertexNative;
    NativeBuffer indexNative;
    {
        std::lock_guard<std::mutex> lock(g_resourceMutex);
        auto vb = g_buffers.find(static_cast<int64_t>(vertexBufferHandle));
        auto ib = g_buffers.find(static_cast<int64_t>(indexBufferHandle));
        if (vb == g_buffers.end() || ib == g_buffers.end()) {
            return JNI_FALSE;
        }
        vertexNative = vb->second;
        indexNative = ib->second;
    }
    unsigned char csUpload[96] = {};
    std::memcpy(csUpload, csData, csSize < 96 ? static_cast<size_t>(csSize) : 96u);
    g_commandList->SetGraphicsRootConstantBufferView(0, UploadCbuffer(csUpload, 96, 256));
    SetVertexBuffer(vertexNative, static_cast<UINT>(vertexStride));
    SetIndexBuffer(indexNative, indexBytes);
    g_commandList->DrawIndexedInstanced(static_cast<UINT>(indexCount), 1, static_cast<UINT>(firstIndex), 0, 0);
    if (!g_loggedFirstTerrainDraw) {
        Log("nativeDrawTerrainSection (D3D12) first draw stride=%d count=%d", static_cast<int>(vertexStride), static_cast<int>(indexCount));
        g_loggedFirstTerrainDraw = true;
    }
    return JNI_TRUE;
}

extern "C" JNIEXPORT void JNICALL
Java_banditvault_d3d11mod_nativebridge_D3D11Native_nativeEndTerrainBatch(JNIEnv*, jclass) {
    g_terrainBatchActive = false;
}

// worldSimple blend codes from Java: 0 none, 1 translucent, 2 overlay(additive), 3 crumbling.
static int TranslateWorldBlend(int blend) {
    switch (blend) {
        case 1: return BLEND_TRANSLUCENT;
        case 2: return BLEND_ADDITIVE;
        case 3: return BLEND_CRUMBLING;
        case 4: return BLEND_GLINT;
        default: return BLEND_NONE;
    }
}

extern "C" JNIEXPORT jboolean JNICALL
Java_banditvault_d3d11mod_nativebridge_D3D11Native_nativeDrawWorldSimple(
    JNIEnv* env, jclass, jint kind, jint blendMode, jboolean fan,
    jlong vertexBufferHandle, jint vertexStride, jlong indexBufferHandle, jint indexBytes,
    jint vertexOffset, jint firstIndex, jint count, jlong textureHandle,
    jobject dynamicTransforms, jobject projection, jobject fog,
    jobject globals, jobject cloudInfo, jlong cloudFacesBuffer, jint cloudFacesOffset, jint cloudFacesLength) {
    const bool needsVertexBuffer = kind != 7;
    if (!g_device || (needsVertexBuffer && vertexBufferHandle == 0) || count <= 0 || kind < 0 || kind > 10) {
        return JNI_FALSE;
    }
    void* dynData = env->GetDirectBufferAddress(dynamicTransforms);
    jlong dynSize = env->GetDirectBufferCapacity(dynamicTransforms);
    void* projData = env->GetDirectBufferAddress(projection);
    jlong projSize = env->GetDirectBufferCapacity(projection);
    if (!dynData || dynSize < 160 || !projData || projSize < 64) {
        return JNI_FALSE;
    }
    if (!EnsureFrameOpen() || !EnsurePipelines()) {
        return JNI_FALSE;
    }

    NativeBuffer vertexNative;
    NativeBuffer indexNative;
    UINT textureSrv = 0;
    ID3D12Resource* textureRes = nullptr;
    D3D12_RESOURCE_STATES* textureState = nullptr;
    UINT cloudSrvSlot = 0;
    UINT lineSrvSlot = 0;
    const bool hasTexture = textureHandle != 0;
    {
        std::lock_guard<std::mutex> lock(g_resourceMutex);
        if (needsVertexBuffer) {
            auto vb = g_buffers.find(static_cast<int64_t>(vertexBufferHandle));
            if (vb == g_buffers.end()) {
                return JNI_FALSE;
            }
            vertexNative = vb->second;
        }
        if (!fan && kind != 7) {
            auto ib = g_buffers.find(static_cast<int64_t>(indexBufferHandle));
            if (ib == g_buffers.end()) {
                return JNI_FALSE;
            }
            indexNative = ib->second;
        }
        if (kind == 7) {
            auto ib = g_buffers.find(static_cast<int64_t>(indexBufferHandle));
            if (ib == g_buffers.end()) {
                return JNI_FALSE;
            }
            indexNative = ib->second;
            auto faces = g_buffers.find(static_cast<int64_t>(cloudFacesBuffer));
            if (faces == g_buffers.end()) {
                return JNI_FALSE;
            }
            const uint64_t key = (static_cast<uint64_t>(cloudFacesBuffer) << 24) ^ (static_cast<uint64_t>(cloudFacesOffset) << 4) ^ static_cast<uint64_t>(cloudFacesLength);
            auto srvIt = g_bufferSrvSlots.find(key);
            if (srvIt != g_bufferSrvSlots.end()) {
                cloudSrvSlot = srvIt->second;
            } else {
                cloudSrvSlot = AllocCpuSrvSlot();
                D3D12_SHADER_RESOURCE_VIEW_DESC desc = {};
                desc.Format = DXGI_FORMAT_R8_SINT;
                desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
                desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                desc.Buffer.FirstElement = static_cast<UINT64>(cloudFacesOffset);
                desc.Buffer.NumElements = static_cast<UINT>(cloudFacesLength);
                g_device->CreateShaderResourceView(faces->second.resource.Get(), &desc, CpuSrvHandle(cloudSrvSlot));
                ++g_srvWriteGeneration;
                g_bufferSrvSlots.emplace(key, cloudSrvSlot);
            }
        }
        if (kind == 4) {
            // Raw (ByteAddressBuffer) view of the whole lines vertex buffer; the lines VS fetches
            // the unaligned 23-byte vertices itself. byteWidth is 256-padded, so /4 is exact and
            // the view covers every mesh byte. Marker keeps the destroy-time key sweep working
            // (it matches on key >> 24 == handle).
            const uint64_t key = (static_cast<uint64_t>(vertexBufferHandle) << 24) ^ 0xB17E5ull;
            auto srvIt = g_bufferSrvSlots.find(key);
            if (srvIt != g_bufferSrvSlots.end()) {
                lineSrvSlot = srvIt->second;
            } else {
                lineSrvSlot = AllocCpuSrvSlot();
                D3D12_SHADER_RESOURCE_VIEW_DESC desc = {};
                desc.Format = DXGI_FORMAT_R32_TYPELESS;
                desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
                desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                desc.Buffer.FirstElement = 0;
                desc.Buffer.NumElements = static_cast<UINT>(vertexNative.byteWidth / 4);
                desc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;
                g_device->CreateShaderResourceView(vertexNative.resource.Get(), &desc, CpuSrvHandle(lineSrvSlot));
                ++g_srvWriteGeneration;
                g_bufferSrvSlots.emplace(key, lineSrvSlot);
            }
        }
        if (hasTexture) {
            auto tex = g_textures.find(static_cast<int64_t>(textureHandle));
            if (tex == g_textures.end()) {
                return JNI_FALSE;
            }
            textureSrv = tex->second.srvSlot;
            textureRes = tex->second.resource.Get();
            textureState = &tex->second.state;
        }
    }
    if (fan && !EnsureFanIndexBuffer(static_cast<UINT>(count))) {
        return JNI_FALSE;
    }
    if (textureRes) {
        TransitionResource(textureRes, *textureState, kTextureReadState);
    }

    static const Program kKindPrograms[11] = { PROG_SKY, PROG_POS_COLOR, PROG_STARS, PROG_POS_TEX, PROG_LINES, PROG_SHADOW, PROG_CRUMBLING, PROG_CLOUDS, PROG_PANORAMA, PROG_LEASH, PROG_GLINT };
    int depth = DEPTH_OFF;
    int raster = RASTER_NONE_CCW;
    if (kind >= 4 && kind != 8) {  // panorama (8) draws depth-off like the sky family
        if (!EnsureMainDepth()) {
            return JNI_FALSE;
        }
        depth = (kind == 5 || kind == 6) ? DEPTH_LEQUAL_NO_WRITE : (kind == 10 ? DEPTH_EQUAL_NO_WRITE : DEPTH_LEQUAL_WRITE);
        if (kind == 6) {
            raster = RASTER_CRUMBLING;
        }
    }
    ID3D12PipelineState* pso = GetPso(kKindPrograms[kind], TranslateWorldBlend(blendMode), depth, raster, 0, DXGI_FORMAT_B8G8R8A8_UNORM);
    if (!pso) {
        return JNI_FALSE;
    }

    DrawTarget target;
    target.rtv = BackBufferRtv(g_frameIndex);
    target.width = static_cast<UINT>(g_width);
    target.height = static_cast<UINT>(g_height);
    if (depth != DEPTH_OFF) {
        target.hasDsv = true;
        target.dsv = DsvHandle(g_mainDepthDsvSlot);
    }
    BindTarget(target);
    g_commandList->SetGraphicsRootSignature(g_rootSignature.Get());
    g_commandList->SetPipelineState(pso);
    g_commandList->SetGraphicsRootConstantBufferView(0, UploadCbuffer(dynData, 160, 256));
    g_commandList->SetGraphicsRootConstantBufferView(1, UploadCbuffer(projData, 64, 256));
    if (fog) {
        void* fogData = env->GetDirectBufferAddress(fog);
        jlong fogSize = env->GetDirectBufferCapacity(fog);
        if (fogData && fogSize >= 40) {
            unsigned char fogUpload[48] = {};
            std::memcpy(fogUpload, fogData, fogSize < 48 ? static_cast<size_t>(fogSize) : 48u);
            g_commandList->SetGraphicsRootConstantBufferView(2, UploadCbuffer(fogUpload, 48, 256));
        }
    }
    if (globals) {
        void* globalsData = env->GetDirectBufferAddress(globals);
        jlong globalsSize = env->GetDirectBufferCapacity(globals);
        if (globalsData && globalsSize >= 56) {
            unsigned char globalsUpload[64] = {};
            std::memcpy(globalsUpload, globalsData, globalsSize < 64 ? static_cast<size_t>(globalsSize) : 64u);
            g_commandList->SetGraphicsRootConstantBufferView(3, UploadCbuffer(globalsUpload, 64, 256));
        }
    }
    if (cloudInfo) {
        void* cloudData = env->GetDirectBufferAddress(cloudInfo);
        jlong cloudSize = env->GetDirectBufferCapacity(cloudInfo);
        if (cloudData && cloudSize >= 44) {
            unsigned char cloudUpload[48] = {};
            std::memcpy(cloudUpload, cloudData, cloudSize < 48 ? static_cast<size_t>(cloudSize) : 48u);
            g_commandList->SetGraphicsRootConstantBufferView(4, UploadCbuffer(cloudUpload, 48, 256));
        }
    }
    g_commandList->SetGraphicsRootDescriptorTable(5, AllocSrvTable(kind == 7 ? cloudSrvSlot : (kind == 4 ? lineSrvSlot : (hasTexture ? textureSrv : 0)), 0, 0));
    // Leash (kind 9) is a strip with identity indices; everything else is triangle lists.
    g_commandList->IASetPrimitiveTopology(kind == 9 ? D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP : D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    if (needsVertexBuffer) {
        SetVertexBuffer(vertexNative, static_cast<UINT>(vertexStride));
    }
    if (fan) {
        D3D12_INDEX_BUFFER_VIEW view = {};
        view.BufferLocation = g_fanIndexBuffer->GetGPUVirtualAddress();
        view.SizeInBytes = static_cast<UINT>((g_fanIndexVertexCapacity - 2) * 3 * sizeof(uint16_t));
        view.Format = DXGI_FORMAT_R16_UINT;
        g_commandList->IASetIndexBuffer(&view);
        g_commandList->DrawIndexedInstanced(static_cast<UINT>((count - 2) * 3), 1, 0, static_cast<INT>(vertexOffset), 0);
    } else {
        SetIndexBuffer(indexNative, indexBytes);
        g_commandList->DrawIndexedInstanced(static_cast<UINT>(count), 1, static_cast<UINT>(firstIndex), static_cast<INT>(vertexOffset), 0);
    }
    if (kind >= 0 && kind < 11 && !g_loggedWorldSimpleKinds[kind]) {
        Log("nativeDrawWorldSimple (D3D12) first draw kind=%d blend=%d fan=%d count=%d", static_cast<int>(kind), static_cast<int>(blendMode), fan ? 1 : 0, static_cast<int>(count));
        g_loggedWorldSimpleKinds[kind] = true;
    }
    return JNI_TRUE;
}

static constexpr int ENTITY_FLAG_CULL_BACK = 16;
static constexpr int ENTITY_FLAG_FLIP_Y = 32;
static constexpr int ENTITY_FLAG_NO_DEPTH_WRITE = 64;

extern "C" JNIEXPORT jboolean JNICALL
Java_banditvault_d3d11mod_nativebridge_D3D11Native_nativeDrawEntity(
    JNIEnv* env, jclass, jint kind, jint blendMode, jfloat alphaCutoff, jint flags,
    jlong vertexBufferHandle, jint vertexStride, jlong indexBufferHandle, jint indexBytes,
    jint vertexOffset, jint firstIndex, jint indexCount,
    jlong texture0, jlong overlayTexture, jlong lightmapTexture,
    jlong targetTexture, jint targetMip, jboolean targetHasDepth,
    jobject dynamicTransforms, jobject projection, jobject fog, jobject lighting) {
    if (!g_device || vertexBufferHandle == 0 || indexBufferHandle == 0 || indexCount <= 0 || texture0 == 0) {
        return JNI_FALSE;
    }
    void* dynData = env->GetDirectBufferAddress(dynamicTransforms);
    jlong dynSize = env->GetDirectBufferCapacity(dynamicTransforms);
    void* projData = env->GetDirectBufferAddress(projection);
    jlong projSize = env->GetDirectBufferCapacity(projection);
    if (!dynData || dynSize < 160 || !projData || projSize < 64) {
        return JNI_FALSE;
    }
    if (!EnsureFrameOpen() || !EnsurePipelines()) {
        return JNI_FALSE;
    }

    NativeBuffer vertexNative;
    NativeBuffer indexNative;
    UINT tex0Srv = 0;
    ID3D12Resource* tex0Res = nullptr;
    D3D12_RESOURCE_STATES* tex0State = nullptr;
    UINT overlaySrv = 0;
    ID3D12Resource* overlayRes = nullptr;
    D3D12_RESOURCE_STATES* overlayState = nullptr;
    UINT lightmapSrv = 0;
    ID3D12Resource* lightmapRes = nullptr;
    D3D12_RESOURCE_STATES* lightmapState = nullptr;
    UINT targetRtvSlot = UINT_MAX;
    UINT targetWidth = 0;
    UINT targetHeight = 0;
    DXGI_FORMAT targetFormat = DXGI_FORMAT_B8G8R8A8_UNORM;
    ID3D12Resource* targetRes = nullptr;
    D3D12_RESOURCE_STATES* targetState = nullptr;
    const bool textureTarget = targetTexture != 0;
    {
        std::lock_guard<std::mutex> lock(g_resourceMutex);
        auto vb = g_buffers.find(static_cast<int64_t>(vertexBufferHandle));
        auto ib = g_buffers.find(static_cast<int64_t>(indexBufferHandle));
        auto tex = g_textures.find(static_cast<int64_t>(texture0));
        if (vb == g_buffers.end() || ib == g_buffers.end() || tex == g_textures.end()) {
            return JNI_FALSE;
        }
        vertexNative = vb->second;
        indexNative = ib->second;
        tex0Srv = tex->second.srvSlot;
        tex0Res = tex->second.resource.Get();
        tex0State = &tex->second.state;
        if (overlayTexture != 0) {
            auto overlay = g_textures.find(static_cast<int64_t>(overlayTexture));
            if (overlay != g_textures.end()) {
                overlaySrv = overlay->second.srvSlot;
                overlayRes = overlay->second.resource.Get();
                overlayState = &overlay->second.state;
            }
        }
        if (lightmapTexture != 0) {
            auto lightmap = g_textures.find(static_cast<int64_t>(lightmapTexture));
            if (lightmap != g_textures.end()) {
                lightmapSrv = lightmap->second.srvSlot;
                lightmapRes = lightmap->second.resource.Get();
                lightmapState = &lightmap->second.state;
            }
        }
        if (textureTarget) {
            auto target = g_textures.find(static_cast<int64_t>(targetTexture));
            if (target == g_textures.end()) {
                return JNI_FALSE;
            }
            targetRtvSlot = GetOrCreateTextureRtvSlot(target->second, static_cast<UINT>(targetMip));
            targetWidth = target->second.width >> targetMip;
            targetHeight = target->second.height >> targetMip;
            targetFormat = target->second.format;
            targetRes = target->second.resource.Get();
            targetState = &target->second.state;
        }
    }
    TransitionResource(tex0Res, *tex0State, kTextureReadState);
    if (overlayRes) TransitionResource(overlayRes, *overlayState, kTextureReadState);
    if (lightmapRes) TransitionResource(lightmapRes, *lightmapState, kTextureReadState);

    int effectiveFlags = static_cast<int>(flags);
    DrawTarget target;
    if (textureTarget) {
        if (targetRtvSlot == UINT_MAX) {
            return JNI_FALSE;
        }
        if (targetWidth == 0) targetWidth = 1;
        if (targetHeight == 0) targetHeight = 1;
        TransitionResource(targetRes, *targetState, D3D12_RESOURCE_STATE_RENDER_TARGET);
        target.rtv = RtvHandle(targetRtvSlot);
        target.width = targetWidth;
        target.height = targetHeight;
        target.format = targetFormat;
        effectiveFlags |= ENTITY_FLAG_FLIP_Y;
        if (targetHasDepth) {
            OffscreenDepth* offscreen = GetOrCreateOffscreenDepth(targetWidth, targetHeight);
            if (!offscreen) {
                return JNI_FALSE;
            }
            target.hasDsv = true;
            target.dsv = DsvHandle(offscreen->dsvSlot);
        }
    } else {
        if (!EnsureMainDepth()) {
            return JNI_FALSE;
        }
        target.rtv = BackBufferRtv(g_frameIndex);
        target.width = static_cast<UINT>(g_width);
        target.height = static_cast<UINT>(g_height);
        target.hasDsv = true;
        target.dsv = DsvHandle(g_mainDepthDsvSlot);
    }

    const bool noDepthWrite = (flags & ENTITY_FLAG_NO_DEPTH_WRITE) != 0;
    const bool cullBack = (flags & ENTITY_FLAG_CULL_BACK) != 0;
    const bool flip = (effectiveFlags & ENTITY_FLAG_FLIP_Y) != 0;
    int depth = target.hasDsv ? (noDepthWrite ? DEPTH_LEQUAL_NO_WRITE : DEPTH_LEQUAL_WRITE) : DEPTH_OFF;
    int raster = flip ? (cullBack ? RASTER_BACK_CW : RASTER_NONE_CW) : (cullBack ? RASTER_BACK_CCW : RASTER_NONE_CCW);
    ID3D12PipelineState* pso = GetPso(kind == 1 ? PROG_PARTICLE : PROG_ENTITY, blendMode == 1 ? BLEND_TRANSLUCENT : BLEND_NONE, depth, raster, 0, target.format);
    if (!pso) {
        return JNI_FALSE;
    }
    BindTarget(target);

    unsigned char lightingUpload[32] = {};
    if (lighting) {
        void* lightingData = env->GetDirectBufferAddress(lighting);
        jlong lightingSize = env->GetDirectBufferCapacity(lighting);
        if (lightingData && lightingSize >= 28) {
            std::memcpy(lightingUpload, lightingData, lightingSize < 32 ? static_cast<size_t>(lightingSize) : 32u);
        }
    }
    struct { float cutoff; int flags; float pad0; float pad1; } params = { alphaCutoff, effectiveFlags, 0.0f, 0.0f };

    g_commandList->SetGraphicsRootSignature(g_rootSignature.Get());
    g_commandList->SetPipelineState(pso);
    g_commandList->SetGraphicsRootConstantBufferView(0, UploadCbuffer(dynData, 160, 256));
    g_commandList->SetGraphicsRootConstantBufferView(1, UploadCbuffer(projData, 64, 256));
    if (fog) {
        void* fogData = env->GetDirectBufferAddress(fog);
        jlong fogSize = env->GetDirectBufferCapacity(fog);
        if (fogData && fogSize >= 40) {
            unsigned char fogUpload[48] = {};
            std::memcpy(fogUpload, fogData, fogSize < 48 ? static_cast<size_t>(fogSize) : 48u);
            g_commandList->SetGraphicsRootConstantBufferView(2, UploadCbuffer(fogUpload, 48, 256));
        }
    }
    g_commandList->SetGraphicsRootConstantBufferView(3, UploadCbuffer(lightingUpload, 32, 256));
    g_commandList->SetGraphicsRootConstantBufferView(4, UploadCbuffer(&params, 16, 256));
    g_commandList->SetGraphicsRootDescriptorTable(5, AllocSrvTable(tex0Srv, overlaySrv, lightmapSrv));
    g_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    SetVertexBuffer(vertexNative, static_cast<UINT>(vertexStride));
    SetIndexBuffer(indexNative, indexBytes);
    g_commandList->DrawIndexedInstanced(static_cast<UINT>(indexCount), 1, static_cast<UINT>(firstIndex), static_cast<INT>(vertexOffset), 0);

    if (kind == 1 ? !g_loggedFirstParticleDraw : !g_loggedFirstEntityDraw) {
        Log("nativeDrawEntity (D3D12) first draw kind=%d blend=%d cutoff=%.2f flags=0x%X count=%d target=%lld",
            static_cast<int>(kind), static_cast<int>(blendMode), alphaCutoff, effectiveFlags, static_cast<int>(indexCount), static_cast<long long>(targetTexture));
        if (kind == 1) { g_loggedFirstParticleDraw = true; } else { g_loggedFirstEntityDraw = true; }
    }
    return JNI_TRUE;
}

extern "C" JNIEXPORT void JNICALL
Java_banditvault_d3d11mod_nativebridge_D3D11Native_nativeShutdown(JNIEnv*, jclass) {
    Log("nativeShutdown (D3D12)");
    WaitForGpuIdle();
    {
        std::lock_guard<std::mutex> lock(g_resourceMutex);
        g_buffers.clear();
        g_textures.clear();
        g_bufferSrvSlots.clear();
        g_nextBufferHandle = 1;
        g_nextTextureHandle = 1;
    }
    g_uploadPool.clear();
    g_uploadPoolBytes = 0;
    g_defaultBufferPool.clear();
    g_defaultBufferPoolBytes = 0;
    for (UINT i = 0; i < kFrameCount; ++i) {
        g_frames[i].pooledInFlight.clear();
        g_frames[i].pooledOffset = 0;
        g_frames[i].bufferReturns.clear();
    }
    g_offscreenDepths.clear();
    g_mainDepth.Reset();
    g_mainDepthDsvSlot = UINT_MAX;
    g_psoCache.clear();
    for (int i = 0; i < PROG_COUNT; ++i) {
        g_vsBlobs[i].Reset();
        g_psBlobs[i].Reset();
    }
    g_shadersReady = false;
    g_rootSignature.Reset();
    g_fanIndexBuffer.Reset();
    g_fanIndexVertexCapacity = 0;
    g_triangleVertexBuffer.Reset();
    g_fence.Reset();
    if (g_fenceEvent) {
        CloseHandle(g_fenceEvent);
        g_fenceEvent = nullptr;
    }
    g_commandList.Reset();
    for (UINT i = 0; i < kFrameCount; ++i) {
        g_frames[i].garbage.clear();
        g_frames[i].uploadRing.Reset();
        g_frames[i].uploadRingCpu = nullptr;
        g_frames[i].allocator.Reset();
        g_frames[i].fenceValue = 0;
        g_backBuffers[i].Reset();
        g_gpuSrvHeaps[i].Reset();
    }
    g_cpuSrvHeap.Reset();
    g_rtvHeap.Reset();
    g_dsvHeap.Reset();
    g_backBufferRtvHeap.Reset();
    g_swapChain.Reset();
    g_commandQueue.Reset();
    g_device.Reset();
    g_window.Reset();
    g_frameOpen = false;
}
