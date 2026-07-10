#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11_1.h>
#include <d3dcompiler.h>
#include <dxgi1_3.h>
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
#include <cwchar>
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

struct Vertex {
    float x, y, z;
    float r, g, b, a;
};

static ComPtr<ICoreWindow> g_window;
static ComPtr<ID3D11Device> g_device;
static ComPtr<ID3D11DeviceContext> g_context;
static ComPtr<IDXGISwapChain1> g_swapChain;
static ComPtr<ID3D11RenderTargetView> g_renderTargetView;
static ComPtr<ID3D11VertexShader> g_vertexShader;
static ComPtr<ID3D11PixelShader> g_pixelShader;
static ComPtr<ID3D11InputLayout> g_inputLayout;
static ComPtr<ID3D11Buffer> g_vertexBuffer;
static ComPtr<ID3D11VertexShader> g_guiVertexShader;
static ComPtr<ID3D11PixelShader> g_guiPixelShader;
static ComPtr<ID3D11VertexShader> g_guiTexturedVertexShader;
static ComPtr<ID3D11PixelShader> g_guiTexturedPixelShader;
static ComPtr<ID3D11PixelShader> g_guiTextPixelShader;
static ComPtr<ID3DBlob> g_guiVsBlob;
static ComPtr<ID3DBlob> g_guiTexturedVsBlob;
static std::unordered_map<uint32_t, ComPtr<ID3D11InputLayout>> g_guiInputLayouts;
static ComPtr<ID3D11Buffer> g_guiDynamicTransformsBuffer;
static ComPtr<ID3D11Buffer> g_guiProjectionBuffer;
static ComPtr<ID3D11BlendState> g_guiBlendState;
static ComPtr<ID3D11BlendState> g_guiAdditiveBlendState;
static ComPtr<ID3D11BlendState> g_guiPremultipliedBlendState;
static ComPtr<ID3D11BlendState> g_guiOpaqueBlendState;
static ComPtr<ID3D11BlendState> g_guiInvertBlendState;
static ComPtr<ID3D11BlendState> g_guiVignetteBlendState;
static ComPtr<ID3D11RasterizerState> g_guiRasterizerState;
static ComPtr<ID3D11RasterizerState> g_guiScissorRasterizerState;
static ComPtr<ID3D11SamplerState> g_guiSamplerState;
static ComPtr<ID3D11VertexShader> g_spriteVertexShader;
static ComPtr<ID3D11PixelShader> g_spriteBlitPixelShader;
static ComPtr<ID3D11PixelShader> g_spriteInterpolatePixelShader;
static ComPtr<ID3D11Buffer> g_spriteInfoBuffer;
static ComPtr<ID3D11SamplerState> g_spritePointSampler;
static std::unordered_map<uint64_t, ComPtr<ID3D11RenderTargetView>> g_textureRtvs;
static std::unordered_map<uint64_t, ComPtr<ID3D11ShaderResourceView>> g_bufferSrvs;
static int g_loggedSpriteBlits = 0;
static ComPtr<ID3D11Texture2D> g_depthTexture;
static ComPtr<ID3D11DepthStencilView> g_depthStencilView;
static ComPtr<ID3D11DepthStencilState> g_terrainDepthState;
static ComPtr<ID3D11VertexShader> g_terrainVertexShader;
static ComPtr<ID3D11PixelShader> g_terrainPixelShader;
static ComPtr<ID3D11InputLayout> g_terrainInputLayout;
static ComPtr<ID3D11Buffer> g_chunkSectionBuffer;   // 96 bytes (std140 ChunkSection)
static ComPtr<ID3D11Buffer> g_fogBuffer;            // 40 -> 48 bytes (std140 Fog)
static ComPtr<ID3D11Buffer> g_globalsBuffer;        // 56 -> 64 bytes (std140 Globals)
static ComPtr<ID3D11Buffer> g_terrainParamsBuffer;  // 16 bytes (alpha cutoff)
static ComPtr<ID3D11SamplerState> g_terrainAtlasSampler;
static ComPtr<ID3D11VertexShader> g_screenquadVertexShader;
static ComPtr<ID3D11PixelShader> g_lightmapPixelShader;
static ComPtr<ID3D11Buffer> g_lightmapInfoBuffer;   // 64 bytes (std140 LightmapInfo)
static bool g_loggedFirstTerrainDraw = false;
static bool g_loggedFirstLightmapDraw = false;
static wchar_t g_logPath[MAX_PATH] = {};
static wchar_t g_adapterDescription[256] = L"unknown adapter";
static int g_width = 0;
static int g_height = 0;
static bool g_loggedFirstBeginFrame = false;
static bool g_loggedFirstDraw = false;
static bool g_loggedFirstPresent = false;
static bool g_loggedFirstGuiDraw = false;
static bool g_loggedFirstGuiUniforms = false;

struct NativeBuffer {
    ComPtr<ID3D11Buffer> buffer;
    UINT byteWidth = 0;
    int usage = 0;
};

struct NativeTexture {
    ComPtr<ID3D11Texture2D> texture;
    ComPtr<ID3D11ShaderResourceView> srv;
    UINT width = 0;
    UINT height = 0;
    UINT mipLevels = 0;
    UINT pixelSize = 0;
};

// one global registry lock; split per resource type only if JNI contention shows up.
static std::mutex g_resourceMutex;
static std::unordered_map<int64_t, NativeBuffer> g_buffers;
static std::unordered_map<int64_t, NativeTexture> g_textures;
static int64_t g_nextBufferHandle = 1;
static int64_t g_nextTextureHandle = 1;
static int g_loggedBufferCreates = 0;
static int g_loggedBufferUpdates = 0;
static int g_loggedTextureCreates = 0;
static int g_loggedTextureUpdates = 0;

static constexpr int USAGE_MAP_READ = 1;
static constexpr int USAGE_MAP_WRITE = 2;
static constexpr int USAGE_COPY_DST = 8;
static constexpr int USAGE_COPY_SRC = 16;
static constexpr int USAGE_VERTEX = 32;
static constexpr int USAGE_INDEX = 64;
static constexpr int USAGE_UNIFORM = 128;
static constexpr int USAGE_UNIFORM_TEXEL_BUFFER = 256;

static void Log(const char* fmt, ...) {
    char line[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf_s(line, sizeof(line), _TRUNCATE, fmt, args);
    va_end(args);

    OutputDebugStringA("bandit_d3d11_backend: ");
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

static UINT AlignConstantBufferSize(UINT byteWidth, int usage) {
    if ((usage & USAGE_UNIFORM) == 0) {
        return byteWidth;
    }
    return (byteWidth + 15u) & ~15u;
}

static UINT BufferBindFlags(int usage) {
    UINT flags = 0;
    if ((usage & USAGE_VERTEX) != 0) {
        flags |= D3D11_BIND_VERTEX_BUFFER;
    }
    if ((usage & USAGE_INDEX) != 0) {
        flags |= D3D11_BIND_INDEX_BUFFER;
    }
    if ((usage & USAGE_UNIFORM) != 0) {
        flags |= D3D11_BIND_CONSTANT_BUFFER;
    }
    if ((usage & USAGE_UNIFORM_TEXEL_BUFFER) != 0) {
        flags |= D3D11_BIND_SHADER_RESOURCE;
    }
    return flags;
}

static UINT MaxMipLevels(UINT width, UINT height) {
    UINT levels = 1;
    UINT size = width > height ? width : height;
    while (size > 1) {
        size >>= 1;
        ++levels;
    }
    return levels;
}

static int ScaleDimension(float value, int fallback) {
    return value > 0.0f ? static_cast<int>(value + 0.5f) : fallback;
}

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

    if (width <= 0 || height <= 0) {
        Log("Invalid swapchain size %dx%d", width, height);
        return false;
    }
    return true;
}

static bool CreateRenderTarget() {
    g_renderTargetView.Reset();

    ComPtr<ID3D11Texture2D> backBuffer;
    HRESULT hr = g_swapChain->GetBuffer(0, IID_PPV_ARGS(backBuffer.GetAddressOf()));
    if (!SucceededOrLog("IDXGISwapChain1::GetBuffer", hr)) {
        return false;
    }

    hr = g_device->CreateRenderTargetView(backBuffer.Get(), nullptr, g_renderTargetView.GetAddressOf());
    if (!SucceededOrLog("ID3D11Device::CreateRenderTargetView", hr)) {
        return false;
    }

    g_context->OMSetRenderTargets(1, g_renderTargetView.GetAddressOf(), nullptr);

    D3D11_VIEWPORT viewport = {};
    viewport.Width = static_cast<float>(g_width);
    viewport.Height = static_cast<float>(g_height);
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    g_context->RSSetViewports(1, &viewport);
    return true;
}

static bool CompileShader(const char* name, const char* source, const char* target, ID3DBlob** outBlob) {
    UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#if defined(_DEBUG)
    flags |= D3DCOMPILE_DEBUG;
#endif
    ComPtr<ID3DBlob> errors;
    HRESULT hr = D3DCompile(source, strlen(source), name, nullptr, nullptr, "main", target, flags, 0, outBlob, errors.GetAddressOf());
    if (FAILED(hr)) {
        if (errors) {
            Log("%s compile errors: %.*s", name, static_cast<int>(errors->GetBufferSize()), static_cast<const char*>(errors->GetBufferPointer()));
        }
        LogHr(name, hr);
        return false;
    }
    Log("%s compiled", name);
    return true;
}

static bool CreateTriangleResources() {
    static const char* vertexShaderSource =
        "struct VSIn { float3 pos : POSITION; float4 color : COLOR0; };"
        "struct VSOut { float4 pos : SV_Position; float4 color : COLOR0; };"
        "VSOut main(VSIn input) {"
        "  VSOut output;"
        "  output.pos = float4(input.pos, 1.0);"
        "  output.color = input.color;"
        "  return output;"
        "}";
    static const char* pixelShaderSource =
        "struct PSIn { float4 pos : SV_Position; float4 color : COLOR0; };"
        "float4 main(PSIn input) : SV_Target { return input.color; }";

    ComPtr<ID3DBlob> vsBlob;
    ComPtr<ID3DBlob> psBlob;
    if (!CompileShader("first-pixel vertex shader", vertexShaderSource, "vs_4_0", vsBlob.GetAddressOf()) ||
        !CompileShader("first-pixel pixel shader", pixelShaderSource, "ps_4_0", psBlob.GetAddressOf())) {
        return false;
    }

    HRESULT hr = g_device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, g_vertexShader.GetAddressOf());
    if (!SucceededOrLog("CreateVertexShader", hr)) {
        return false;
    }

    hr = g_device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, g_pixelShader.GetAddressOf());
    if (!SucceededOrLog("CreatePixelShader", hr)) {
        return false;
    }

    D3D11_INPUT_ELEMENT_DESC layout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };
    hr = g_device->CreateInputLayout(layout, ARRAYSIZE(layout), vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), g_inputLayout.GetAddressOf());
    if (!SucceededOrLog("CreateInputLayout", hr)) {
        return false;
    }

    const Vertex vertices[] = {
        {  0.0f,  0.58f, 0.0f, 0.30f, 0.95f, 0.62f, 1.0f },
        {  0.62f, -0.50f, 0.0f, 0.30f, 0.55f, 1.0f, 1.0f },
        { -0.62f, -0.50f, 0.0f, 1.0f, 0.42f, 0.36f, 1.0f },
    };

    D3D11_BUFFER_DESC bufferDesc = {};
    bufferDesc.ByteWidth = sizeof(vertices);
    bufferDesc.Usage = D3D11_USAGE_DEFAULT;
    bufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;

    D3D11_SUBRESOURCE_DATA initialData = {};
    initialData.pSysMem = vertices;

    hr = g_device->CreateBuffer(&bufferDesc, &initialData, g_vertexBuffer.GetAddressOf());
    if (!SucceededOrLog("CreateBuffer(test triangle)", hr)) {
        return false;
    }

    return true;
}

// Shared HLSL uniform blocks mirroring vanilla's std140 DynamicTransforms/Projection
// (assets/minecraft/shaders/include/dynamictransforms.glsl + projection.glsl).
// JOML serializes matrices column-major, so MV0..MV3 / P0..P3 below are matrix COLUMNS
// and M * v is evaluated as sum(column_i * v_i).
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

static ID3D11InputLayout* GetGuiInputLayout(bool textured, int uvOffset, int colorOffset) {
    const uint32_t key = (textured ? 1u : 0u)
        | ((static_cast<uint32_t>(colorOffset) & 0x3FFu) << 1)
        | ((static_cast<uint32_t>(uvOffset < 0 ? 0x3FF : uvOffset) & 0x3FFu) << 11);
    auto it = g_guiInputLayouts.find(key);
    if (it != g_guiInputLayouts.end()) {
        return it->second.Get();
    }

    D3D11_INPUT_ELEMENT_DESC elements[3] = {};
    UINT elementCount = 0;
    elements[elementCount++] = { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 };
    if (textured) {
        elements[elementCount++] = { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, static_cast<UINT>(uvOffset), D3D11_INPUT_PER_VERTEX_DATA, 0 };
    }
    elements[elementCount++] = { "COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, static_cast<UINT>(colorOffset), D3D11_INPUT_PER_VERTEX_DATA, 0 };

    ID3DBlob* vsBlob = textured ? g_guiTexturedVsBlob.Get() : g_guiVsBlob.Get();
    if (!vsBlob) {
        Log("GetGuiInputLayout missing vs blob textured=%d", textured ? 1 : 0);
        return nullptr;
    }
    ComPtr<ID3D11InputLayout> layout;
    HRESULT hr = g_device->CreateInputLayout(elements, elementCount, vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), layout.GetAddressOf());
    if (FAILED(hr)) {
        Log("CreateInputLayout(gui textured=%d uv=%d color=%d) failed hr=0x%08X", textured ? 1 : 0, uvOffset, colorOffset, static_cast<unsigned>(hr));
        return nullptr;
    }
    Log("CreateInputLayout(gui textured=%d uv=%d color=%d) ok", textured ? 1 : 0, uvOffset, colorOffset);
    ID3D11InputLayout* raw = layout.Get();
    g_guiInputLayouts.emplace(key, std::move(layout));
    return raw;
}

static bool CreateGuiResources() {
    if (g_guiVertexShader && g_guiPixelShader && g_guiTexturedVertexShader && g_guiTexturedPixelShader && g_guiTextPixelShader && g_guiVsBlob && g_guiTexturedVsBlob && g_guiDynamicTransformsBuffer && g_guiProjectionBuffer && g_guiBlendState && g_guiAdditiveBlendState && g_guiPremultipliedBlendState && g_guiOpaqueBlendState && g_guiInvertBlendState && g_guiVignetteBlendState && g_guiRasterizerState && g_guiScissorRasterizerState && g_guiSamplerState) {
        return true;
    }

    // Mirrors core/gui.vsh: clip = Proj * ModelView * pos, vertex color passthrough.
    const std::string vertexShaderSource = std::string(kGuiUniformBlocks) +
        "struct VSIn { float3 pos : POSITION; float4 color : COLOR0; };"
        "struct VSOut { float4 pos : SV_Position; float4 color : COLOR0; };"
        "VSOut main(VSIn input) {"
        "  VSOut output;"
        "  output.pos = gui_transform(input.pos);"
        "  output.color = input.color;"
        "  return output;"
        "}";
    // Mirrors core/gui.fsh: discard on zero vertex alpha, then multiply by ColorModulator.
    const std::string pixelShaderSource = std::string(kGuiUniformBlocks) +
        "struct PSIn { float4 pos : SV_Position; float4 color : COLOR0; };"
        "float4 main(PSIn input) : SV_Target {"
        "  float4 color = input.color;"
        "  if (color.a == 0.0) discard;"
        "  return color * ColorModulator;"
        "}";
    // Mirrors core/position_tex_color.vsh and core/rendertype_text.vsh: UV0 passes through
    // untransformed (no TextureMat). Text lightmap UV2 is ignored; GUI text always samples a
    // white lightmap texel.
    const std::string texturedVertexShaderSource = std::string(kGuiUniformBlocks) +
        "struct VSIn { float3 pos : POSITION; float2 uv : TEXCOORD0; float4 color : COLOR0; };"
        "struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; float4 color : COLOR0; };"
        "VSOut main(VSIn input) {"
        "  VSOut output;"
        "  output.pos = gui_transform(input.pos);"
        "  output.uv = input.uv;"
        "  output.color = input.color;"
        "  return output;"
        "}";
    // Mirrors core/position_tex_color.fsh.
    const std::string texturedPixelShaderSource = std::string(kGuiUniformBlocks) +
        "Texture2D GuiTexture : register(t0);"
        "SamplerState GuiSampler : register(s0);"
        "struct PSIn { float4 pos : SV_Position; float2 uv : TEXCOORD0; float4 color : COLOR0; };"
        "float4 main(PSIn input) : SV_Target {"
        "  float4 color = GuiTexture.Sample(GuiSampler, input.uv) * input.color;"
        "  if (color.a == 0.0) discard;"
        "  return color * ColorModulator;"
        "}";
    // Mirrors core/rendertype_text.fsh (fog omitted for GUI): modulate first, cut at a<0.1.
    const std::string textPixelShaderSource = std::string(kGuiUniformBlocks) +
        "Texture2D GuiTexture : register(t0);"
        "SamplerState GuiSampler : register(s0);"
        "struct PSIn { float4 pos : SV_Position; float2 uv : TEXCOORD0; float4 color : COLOR0; };"
        "float4 main(PSIn input) : SV_Target {"
        "  float4 color = GuiTexture.Sample(GuiSampler, input.uv) * input.color * ColorModulator;"
        "  if (color.a < 0.1) discard;"
        "  return color;"
        "}";

    ComPtr<ID3DBlob> psBlob;
    ComPtr<ID3DBlob> texturedPsBlob;
    ComPtr<ID3DBlob> textPsBlob;
    if (!CompileShader("gui vertex shader", vertexShaderSource.c_str(), "vs_4_0", g_guiVsBlob.ReleaseAndGetAddressOf()) ||
        !CompileShader("gui pixel shader", pixelShaderSource.c_str(), "ps_4_0", psBlob.GetAddressOf()) ||
        !CompileShader("gui textured vertex shader", texturedVertexShaderSource.c_str(), "vs_4_0", g_guiTexturedVsBlob.ReleaseAndGetAddressOf()) ||
        !CompileShader("gui textured pixel shader", texturedPixelShaderSource.c_str(), "ps_4_0", texturedPsBlob.GetAddressOf()) ||
        !CompileShader("gui text pixel shader", textPixelShaderSource.c_str(), "ps_4_0", textPsBlob.GetAddressOf())) {
        return false;
    }

    HRESULT hr = g_device->CreateVertexShader(g_guiVsBlob->GetBufferPointer(), g_guiVsBlob->GetBufferSize(), nullptr, g_guiVertexShader.ReleaseAndGetAddressOf());
    if (!SucceededOrLog("CreateVertexShader(gui)", hr)) {
        return false;
    }

    hr = g_device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, g_guiPixelShader.ReleaseAndGetAddressOf());
    if (!SucceededOrLog("CreatePixelShader(gui)", hr)) {
        return false;
    }
    hr = g_device->CreateVertexShader(g_guiTexturedVsBlob->GetBufferPointer(), g_guiTexturedVsBlob->GetBufferSize(), nullptr, g_guiTexturedVertexShader.ReleaseAndGetAddressOf());
    if (!SucceededOrLog("CreateVertexShader(gui textured)", hr)) {
        return false;
    }
    hr = g_device->CreatePixelShader(texturedPsBlob->GetBufferPointer(), texturedPsBlob->GetBufferSize(), nullptr, g_guiTexturedPixelShader.ReleaseAndGetAddressOf());
    if (!SucceededOrLog("CreatePixelShader(gui textured)", hr)) {
        return false;
    }
    hr = g_device->CreatePixelShader(textPsBlob->GetBufferPointer(), textPsBlob->GetBufferSize(), nullptr, g_guiTextPixelShader.ReleaseAndGetAddressOf());
    if (!SucceededOrLog("CreatePixelShader(gui text)", hr)) {
        return false;
    }

    D3D11_BUFFER_DESC cbDesc = {};
    cbDesc.Usage = D3D11_USAGE_DEFAULT;
    cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cbDesc.ByteWidth = 160;
    hr = g_device->CreateBuffer(&cbDesc, nullptr, g_guiDynamicTransformsBuffer.ReleaseAndGetAddressOf());
    if (!SucceededOrLog("CreateBuffer(gui DynamicTransforms)", hr)) {
        return false;
    }
    cbDesc.ByteWidth = 64;
    hr = g_device->CreateBuffer(&cbDesc, nullptr, g_guiProjectionBuffer.ReleaseAndGetAddressOf());
    if (!SucceededOrLog("CreateBuffer(gui Projection)", hr)) {
        return false;
    }

    D3D11_BLEND_DESC blendDesc = {};
    blendDesc.RenderTarget[0].BlendEnable = TRUE;
    blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
    blendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    hr = g_device->CreateBlendState(&blendDesc, g_guiBlendState.ReleaseAndGetAddressOf());
    if (!SucceededOrLog("CreateBlendState(gui)", hr)) {
        return false;
    }

    // mojang_logo: (SRC_ALPHA, ONE)
    blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_ONE;
    hr = g_device->CreateBlendState(&blendDesc, g_guiAdditiveBlendState.ReleaseAndGetAddressOf());
    if (!SucceededOrLog("CreateBlendState(gui additive)", hr)) {
        return false;
    }

    // gui_textured_premultiplied_alpha: (ONE, ONE_MINUS_SRC_ALPHA)
    blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
    blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    hr = g_device->CreateBlendState(&blendDesc, g_guiPremultipliedBlendState.ReleaseAndGetAddressOf());
    if (!SucceededOrLog("CreateBlendState(gui premultiplied)", hr)) {
        return false;
    }

    // gui_opaque_textured_background: blending disabled
    blendDesc.RenderTarget[0].BlendEnable = FALSE;
    hr = g_device->CreateBlendState(&blendDesc, g_guiOpaqueBlendState.ReleaseAndGetAddressOf());
    if (!SucceededOrLog("CreateBlendState(gui opaque)", hr)) {
        return false;
    }

    // crosshair: BlendFunction.INVERT = (ONE_MINUS_DST_COLOR, ONE_MINUS_SRC_COLOR)
    blendDesc.RenderTarget[0].BlendEnable = TRUE;
    blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_INV_DEST_COLOR;
    blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_COLOR;
    hr = g_device->CreateBlendState(&blendDesc, g_guiInvertBlendState.ReleaseAndGetAddressOf());
    if (!SucceededOrLog("CreateBlendState(gui invert)", hr)) {
        return false;
    }

    // vignette: (ZERO, ONE_MINUS_SRC_COLOR)
    blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_ZERO;
    blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_COLOR;
    hr = g_device->CreateBlendState(&blendDesc, g_guiVignetteBlendState.ReleaseAndGetAddressOf());
    if (!SucceededOrLog("CreateBlendState(gui vignette)", hr)) {
        return false;
    }

    D3D11_RASTERIZER_DESC rasterizerDesc = {};
    rasterizerDesc.FillMode = D3D11_FILL_SOLID;
    rasterizerDesc.CullMode = D3D11_CULL_NONE;
    rasterizerDesc.DepthClipEnable = TRUE;
    hr = g_device->CreateRasterizerState(&rasterizerDesc, g_guiRasterizerState.ReleaseAndGetAddressOf());
    if (!SucceededOrLog("CreateRasterizerState(gui)", hr)) {
        return false;
    }

    rasterizerDesc.ScissorEnable = TRUE;
    hr = g_device->CreateRasterizerState(&rasterizerDesc, g_guiScissorRasterizerState.ReleaseAndGetAddressOf());
    if (!SucceededOrLog("CreateRasterizerState(gui scissor)", hr)) {
        return false;
    }

    D3D11_SAMPLER_DESC samplerDesc = {};
    samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    // ponytail: only mip 0 is uploaded right now; enable mips after upload/generation exists.
    samplerDesc.MaxLOD = 0.0f;
    hr = g_device->CreateSamplerState(&samplerDesc, g_guiSamplerState.ReleaseAndGetAddressOf());
    if (!SucceededOrLog("CreateSamplerState(gui)", hr)) {
        return false;
    }

    Log("D3D11 gui pipeline resources initialized");
    return true;
}

static bool CreateDeviceAndSwapchain(int requestedWidth, int requestedHeight) {
    if (!ResolveCoreWindow() || !GetWindowSize(requestedWidth, requestedHeight, g_width, g_height)) {
        return false;
    }

    const UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    D3D_FEATURE_LEVEL levels[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0,
    };
    D3D_FEATURE_LEVEL selectedLevel = D3D_FEATURE_LEVEL_11_0;

    HRESULT hr = D3D11CreateDevice(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        flags,
        levels,
        ARRAYSIZE(levels),
        D3D11_SDK_VERSION,
        g_device.GetAddressOf(),
        &selectedLevel,
        g_context.GetAddressOf());
    if (!SucceededOrLog("D3D11CreateDevice(HARDWARE)", hr)) {
        hr = D3D11CreateDevice(
            nullptr,
            D3D_DRIVER_TYPE_WARP,
            nullptr,
            flags,
            levels,
            ARRAYSIZE(levels),
            D3D11_SDK_VERSION,
            g_device.GetAddressOf(),
            &selectedLevel,
            g_context.GetAddressOf());
        if (!SucceededOrLog("D3D11CreateDevice(WARP)", hr)) {
            return false;
        }
    }

    ComPtr<IDXGIDevice1> dxgiDevice;
    hr = g_device.As(&dxgiDevice);
    if (!SucceededOrLog("Query IDXGIDevice1", hr)) {
        return false;
    }

    ComPtr<IDXGIAdapter> adapter;
    hr = dxgiDevice->GetAdapter(adapter.GetAddressOf());
    if (!SucceededOrLog("IDXGIDevice1::GetAdapter", hr) || !adapter) {
        return false;
    }

    DXGI_ADAPTER_DESC desc = {};
    if (SUCCEEDED(adapter->GetDesc(&desc))) {
        wcsncpy_s(g_adapterDescription, desc.Description, _TRUNCATE);
        Log("Adapter: %S", g_adapterDescription);
    }

    ComPtr<IDXGIFactory2> factory;
    hr = adapter->GetParent(IID_PPV_ARGS(factory.GetAddressOf()));
    if (!SucceededOrLog("Get IDXGIFactory2", hr)) {
        return false;
    }

    DXGI_SWAP_CHAIN_DESC1 swapDesc = {};
    swapDesc.Width = g_width;
    swapDesc.Height = g_height;
    swapDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    swapDesc.SampleDesc.Count = 1;
    swapDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapDesc.BufferCount = 2;
    swapDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    swapDesc.Scaling = DXGI_SCALING_STRETCH;
    swapDesc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;

    hr = factory->CreateSwapChainForCoreWindow(g_device.Get(), reinterpret_cast<IUnknown*>(g_window.Get()), &swapDesc, nullptr, g_swapChain.GetAddressOf());
    if (!SucceededOrLog("CreateSwapChainForCoreWindow", hr)) {
        return false;
    }

    if (!CreateRenderTarget() || !CreateTriangleResources()) {
        return false;
    }

    Log("D3D11 first-pixel backend initialized %dx%d featureLevel=0x%04X", g_width, g_height, selectedLevel);
    return true;
}

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

    Log("nativeInit requested size=%dx%d", static_cast<int>(width), static_cast<int>(height));
    return CreateDeviceAndSwapchain(width, height) ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_banditvault_d3d11mod_nativebridge_D3D11Native_nativeResize(JNIEnv*, jclass, jint width, jint height) {
    if (!g_swapChain || width <= 0 || height <= 0) {
        Log("nativeResize rejected swapChain=%p size=%dx%d", g_swapChain.Get(), static_cast<int>(width), static_cast<int>(height));
        return JNI_FALSE;
    }

    if (width == g_width && height == g_height) {
        return JNI_TRUE;
    }

    g_context->OMSetRenderTargets(0, nullptr, nullptr);
    g_renderTargetView.Reset();
    g_width = width;
    g_height = height;

    HRESULT hr = g_swapChain->ResizeBuffers(0, g_width, g_height, DXGI_FORMAT_UNKNOWN, 0);
    if (!SucceededOrLog("ResizeBuffers", hr)) {
        return JNI_FALSE;
    }

    g_depthStencilView.Reset();
    g_depthTexture.Reset();
    return CreateRenderTarget() ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT void JNICALL
Java_banditvault_d3d11mod_nativebridge_D3D11Native_nativeBeginFrame(JNIEnv*, jclass, jfloat r, jfloat g, jfloat b, jfloat a) {
    if (!g_context || !g_renderTargetView) {
        if (!g_loggedFirstBeginFrame) {
            Log("nativeBeginFrame skipped context=%p renderTarget=%p", g_context.Get(), g_renderTargetView.Get());
            g_loggedFirstBeginFrame = true;
        }
        return;
    }
    if (!g_loggedFirstBeginFrame) {
        Log("nativeBeginFrame first clear %.3f %.3f %.3f %.3f", r, g, b, a);
        g_loggedFirstBeginFrame = true;
    }
    const float clear[] = { r, g, b, a };
    g_context->ClearRenderTargetView(g_renderTargetView.Get(), clear);
}

extern "C" JNIEXPORT void JNICALL
Java_banditvault_d3d11mod_nativebridge_D3D11Native_nativeDrawTestTriangle(JNIEnv*, jclass) {
    if (!g_context || !g_vertexBuffer || !g_inputLayout || !g_vertexShader || !g_pixelShader) {
        if (!g_loggedFirstDraw) {
            Log("nativeDrawTestTriangle skipped context=%p vb=%p layout=%p vs=%p ps=%p",
                g_context.Get(), g_vertexBuffer.Get(), g_inputLayout.Get(), g_vertexShader.Get(), g_pixelShader.Get());
            g_loggedFirstDraw = true;
        }
        return;
    }
    if (!g_loggedFirstDraw) {
        Log("nativeDrawTestTriangle first draw");
        g_loggedFirstDraw = true;
    }

    UINT stride = sizeof(Vertex);
    UINT offset = 0;
    ID3D11Buffer* vertexBuffers[] = { g_vertexBuffer.Get() };
    g_context->IASetInputLayout(g_inputLayout.Get());
    g_context->IASetVertexBuffers(0, 1, vertexBuffers, &stride, &offset);
    g_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    g_context->VSSetShader(g_vertexShader.Get(), nullptr, 0);
    g_context->PSSetShader(g_pixelShader.Get(), nullptr, 0);
    g_context->Draw(3, 0);
}

extern "C" JNIEXPORT jboolean JNICALL
Java_banditvault_d3d11mod_nativebridge_D3D11Native_nativePresent(JNIEnv*, jclass) {
    if (!g_swapChain) {
        if (!g_loggedFirstPresent) {
            Log("nativePresent skipped: no swapchain");
            g_loggedFirstPresent = true;
        }
        return JNI_FALSE;
    }
    HRESULT hr = g_swapChain->Present(1, 0);
    if (FAILED(hr)) {
        LogHr("Present", hr);
        return JNI_FALSE;
    }
    if (!g_loggedFirstPresent) {
        Log("nativePresent first frame ok");
        g_loggedFirstPresent = true;
    }
    return JNI_TRUE;
}

extern "C" JNIEXPORT jstring JNICALL
Java_banditvault_d3d11mod_nativebridge_D3D11Native_nativeGetAdapterDescription(JNIEnv* env, jclass) {
    return env->NewString(reinterpret_cast<const jchar*>(g_adapterDescription), static_cast<jsize>(wcslen(g_adapterDescription)));
}

extern "C" JNIEXPORT jlong JNICALL
Java_banditvault_d3d11mod_nativebridge_D3D11Native_nativeCreateBuffer(JNIEnv*, jclass, jint usage, jlong size) {
    if (!g_device || size <= 0 || size > UINT32_MAX) {
        Log("nativeCreateBuffer rejected device=%p size=%lld usage=%d", g_device.Get(), static_cast<long long>(size), static_cast<int>(usage));
        return 0;
    }

    D3D11_BUFFER_DESC desc = {};
    desc.ByteWidth = AlignConstantBufferSize(static_cast<UINT>(size), usage);
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = BufferBindFlags(usage);
    desc.CPUAccessFlags = 0;

    NativeBuffer nativeBuffer;
    nativeBuffer.byteWidth = desc.ByteWidth;
    nativeBuffer.usage = usage;

    HRESULT hr = g_device->CreateBuffer(&desc, nullptr, nativeBuffer.buffer.GetAddressOf());
    if (FAILED(hr)) {
        Log("nativeCreateBuffer failed hr=0x%08X size=%lld byteWidth=%u usage=%d bindFlags=0x%X",
            static_cast<unsigned>(hr), static_cast<long long>(size), desc.ByteWidth, static_cast<int>(usage), desc.BindFlags);
        return 0;
    }

    std::lock_guard<std::mutex> lock(g_resourceMutex);
    const int64_t handle = g_nextBufferHandle++;
    g_buffers.emplace(handle, std::move(nativeBuffer));
    if (g_loggedBufferCreates < 64) {
        Log("nativeCreateBuffer handle=%lld size=%lld byteWidth=%u usage=%d bindFlags=0x%X",
            static_cast<long long>(handle), static_cast<long long>(size), desc.ByteWidth, static_cast<int>(usage), desc.BindFlags);
        ++g_loggedBufferCreates;
    }
    return static_cast<jlong>(handle);
}

extern "C" JNIEXPORT void JNICALL
Java_banditvault_d3d11mod_nativebridge_D3D11Native_nativeUpdateBuffer(JNIEnv* env, jclass, jlong handle, jlong offset, jobject source) {
    if (!g_context || handle == 0 || !source) {
        Log("nativeUpdateBuffer rejected context=%p handle=%lld source=%p", g_context.Get(), static_cast<long long>(handle), source);
        return;
    }

    void* data = env->GetDirectBufferAddress(source);
    jlong size = env->GetDirectBufferCapacity(source);
    if (!data || size <= 0) {
        Log("nativeUpdateBuffer rejected non-direct/empty source handle=%lld size=%lld", static_cast<long long>(handle), static_cast<long long>(size));
        return;
    }

    NativeBuffer nativeBuffer;
    {
        std::lock_guard<std::mutex> lock(g_resourceMutex);
        auto it = g_buffers.find(static_cast<int64_t>(handle));
        if (it == g_buffers.end()) {
            Log("nativeUpdateBuffer unknown handle=%lld", static_cast<long long>(handle));
            return;
        }
        nativeBuffer = it->second;
    }

    if (offset < 0 || offset + size > nativeBuffer.byteWidth) {
        Log("nativeUpdateBuffer out of range handle=%lld offset=%lld size=%lld byteWidth=%u",
            static_cast<long long>(handle), static_cast<long long>(offset), static_cast<long long>(size), nativeBuffer.byteWidth);
        return;
    }

    D3D11_BOX box = {};
    box.left = static_cast<UINT>(offset);
    box.right = static_cast<UINT>(offset + size);
    box.top = 0;
    box.bottom = 1;
    box.front = 0;
    box.back = 1;
    g_context->UpdateSubresource(nativeBuffer.buffer.Get(), 0, &box, data, 0, 0);

    if (g_loggedBufferUpdates < 64) {
        Log("nativeUpdateBuffer handle=%lld offset=%lld size=%lld", static_cast<long long>(handle), static_cast<long long>(offset), static_cast<long long>(size));
        ++g_loggedBufferUpdates;
    }
}

extern "C" JNIEXPORT void JNICALL
Java_banditvault_d3d11mod_nativebridge_D3D11Native_nativeDestroyBuffer(JNIEnv*, jclass, jlong handle) {
    std::lock_guard<std::mutex> lock(g_resourceMutex);
    for (auto it = g_bufferSrvs.begin(); it != g_bufferSrvs.end();) {
        if ((it->first >> 24) == static_cast<uint64_t>(handle)) {
            it = g_bufferSrvs.erase(it);
        } else {
            ++it;
        }
    }
    const size_t erased = g_buffers.erase(static_cast<int64_t>(handle));
    if (erased && g_loggedBufferCreates < 80) {
        Log("nativeDestroyBuffer handle=%lld", static_cast<long long>(handle));
    }
}

extern "C" JNIEXPORT jlong JNICALL
Java_banditvault_d3d11mod_nativebridge_D3D11Native_nativeCreateTexture(JNIEnv*, jclass, jint width, jint height, jint layers, jboolean cube, jint mipLevels, jint pixelSize) {
    // Cubemaps are D3D12-only; metadata-only here keeps the shared jar compatible.
    if (layers != 1 || cube == JNI_TRUE) {
        return 0;
    }
    if (!g_device || width <= 0 || height <= 0 || mipLevels <= 0 || (pixelSize != 1 && pixelSize != 4)) {
        Log("nativeCreateTexture rejected device=%p size=%dx%d mips=%d pixelSize=%d", g_device.Get(), static_cast<int>(width), static_cast<int>(height), static_cast<int>(mipLevels), static_cast<int>(pixelSize));
        return 0;
    }

    DXGI_FORMAT format = pixelSize == 4 ? DXGI_FORMAT_R8G8B8A8_UNORM : DXGI_FORMAT_R8_UNORM;
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = static_cast<UINT>(width);
    desc.Height = static_cast<UINT>(height);
    const UINT requestedMipLevels = static_cast<UINT>(mipLevels);
    const UINT nativeMipLevels = min(requestedMipLevels, MaxMipLevels(desc.Width, desc.Height));
    desc.MipLevels = nativeMipLevels;
    desc.ArraySize = 1;
    desc.Format = format;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    // Render-target bind allows sprite blits / atlas animation / future post passes to draw
    // into any texture; harmless for plain sampled textures.
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;

    NativeTexture nativeTexture;
    nativeTexture.width = desc.Width;
    nativeTexture.height = desc.Height;
    nativeTexture.mipLevels = desc.MipLevels;
    nativeTexture.pixelSize = static_cast<UINT>(pixelSize);

    HRESULT hr = g_device->CreateTexture2D(&desc, nullptr, nativeTexture.texture.GetAddressOf());
    if (FAILED(hr)) {
        Log("nativeCreateTexture CreateTexture2D failed hr=0x%08X size=%dx%d mips=%u/%u pixelSize=%d", static_cast<unsigned>(hr), static_cast<int>(width), static_cast<int>(height), nativeMipLevels, requestedMipLevels, static_cast<int>(pixelSize));
        return 0;
    }

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = format;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = desc.MipLevels;
    hr = g_device->CreateShaderResourceView(nativeTexture.texture.Get(), &srvDesc, nativeTexture.srv.GetAddressOf());
    if (FAILED(hr)) {
        LogHr("nativeCreateTexture CreateShaderResourceView", hr);
        return 0;
    }

    std::lock_guard<std::mutex> lock(g_resourceMutex);
    const int64_t handle = g_nextTextureHandle++;
    g_textures.emplace(handle, std::move(nativeTexture));
    if (g_loggedTextureCreates < 64) {
        Log("nativeCreateTexture handle=%lld size=%dx%d mips=%u/%u pixelSize=%d", static_cast<long long>(handle), static_cast<int>(width), static_cast<int>(height), nativeMipLevels, requestedMipLevels, static_cast<int>(pixelSize));
        ++g_loggedTextureCreates;
    }
    return static_cast<jlong>(handle);
}

extern "C" JNIEXPORT void JNICALL
Java_banditvault_d3d11mod_nativebridge_D3D11Native_nativeUpdateTexture(JNIEnv* env, jclass, jlong handle, jint level, jint layer, jint xOffset, jint yOffset, jint width, jint height, jint rowPitch, jobject source) {
    if (layer != 0) {
        return;  // array layers unsupported on the legacy D3D11 backend
    }
    if (!g_context || handle == 0 || !source) {
        Log("nativeUpdateTexture rejected context=%p handle=%lld source=%p", g_context.Get(), static_cast<long long>(handle), source);
        return;
    }

    void* data = env->GetDirectBufferAddress(source);
    jlong size = env->GetDirectBufferCapacity(source);
    if (!data || size <= 0) {
        Log("nativeUpdateTexture rejected non-direct/empty source handle=%lld size=%lld", static_cast<long long>(handle), static_cast<long long>(size));
        return;
    }

    NativeTexture nativeTexture;
    {
        std::lock_guard<std::mutex> lock(g_resourceMutex);
        auto it = g_textures.find(static_cast<int64_t>(handle));
        if (it == g_textures.end()) {
            Log("nativeUpdateTexture unknown handle=%lld", static_cast<long long>(handle));
            return;
        }
        nativeTexture = it->second;
    }

    if (level < 0 || static_cast<UINT>(level) >= nativeTexture.mipLevels || width <= 0 || height <= 0 || rowPitch < width * static_cast<int>(nativeTexture.pixelSize)) {
        Log("nativeUpdateTexture bad args handle=%lld level=%d size=%dx%d rowPitch=%d", static_cast<long long>(handle), static_cast<int>(level), static_cast<int>(width), static_cast<int>(height), static_cast<int>(rowPitch));
        return;
    }

    D3D11_BOX box = {};
    box.left = static_cast<UINT>(xOffset);
    box.top = static_cast<UINT>(yOffset);
    box.front = 0;
    box.right = static_cast<UINT>(xOffset + width);
    box.bottom = static_cast<UINT>(yOffset + height);
    box.back = 1;
    g_context->UpdateSubresource(nativeTexture.texture.Get(), static_cast<UINT>(level), &box, data, static_cast<UINT>(rowPitch), 0);

    if (g_loggedTextureUpdates < 64) {
        Log("nativeUpdateTexture handle=%lld level=%d dst=%dx%d size=%dx%d rowPitch=%d bytes=%lld",
            static_cast<long long>(handle), static_cast<int>(level), static_cast<int>(xOffset), static_cast<int>(yOffset),
            static_cast<int>(width), static_cast<int>(height), static_cast<int>(rowPitch), static_cast<long long>(size));
        ++g_loggedTextureUpdates;
    }
}

extern "C" JNIEXPORT void JNICALL
Java_banditvault_d3d11mod_nativebridge_D3D11Native_nativeDestroyTexture(JNIEnv*, jclass, jlong handle) {
    std::lock_guard<std::mutex> lock(g_resourceMutex);
    for (auto it = g_textureRtvs.begin(); it != g_textureRtvs.end();) {
        if ((it->first >> 8) == static_cast<uint64_t>(handle)) {
            it = g_textureRtvs.erase(it);
        } else {
            ++it;
        }
    }
    const size_t erased = g_textures.erase(static_cast<int64_t>(handle));
    if (erased && g_loggedTextureCreates < 80) {
        Log("nativeDestroyTexture handle=%lld", static_cast<long long>(handle));
    }
}

// ---- Sprite blit / render-to-texture support ----------------------------------------------
// Vanilla populates and animates texture atlases by drawing sprite quads into the atlas with
// the animate_sprite_blit / animate_sprite_interpolate pipelines (see core/animate_sprite.vsh:
// vertex-buffer-less unit quad from gl_VertexID, transformed by SpriteAnimationInfo's
// ProjectionMatrix * SpriteMatrix, sampling the per-sprite source texture with textureLod).


static ID3D11RenderTargetView* GetOrCreateTextureRtv(int64_t handle, const NativeTexture& texture, UINT mip) {
    const uint64_t key = (static_cast<uint64_t>(handle) << 8) | (mip & 0xFFu);
    std::lock_guard<std::mutex> lock(g_resourceMutex);
    auto it = g_textureRtvs.find(key);
    if (it != g_textureRtvs.end()) {
        return it->second.Get();
    }
    D3D11_RENDER_TARGET_VIEW_DESC desc = {};
    desc.Format = texture.pixelSize == 4 ? DXGI_FORMAT_R8G8B8A8_UNORM : DXGI_FORMAT_R8_UNORM;
    desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
    desc.Texture2D.MipSlice = mip;
    ComPtr<ID3D11RenderTargetView> rtv;
    HRESULT hr = g_device->CreateRenderTargetView(texture.texture.Get(), &desc, rtv.GetAddressOf());
    if (FAILED(hr)) {
        Log("CreateRenderTargetView(texture=%lld mip=%u) failed hr=0x%08X", static_cast<long long>(handle), mip, static_cast<unsigned>(hr));
        return nullptr;
    }
    ID3D11RenderTargetView* raw = rtv.Get();
    g_textureRtvs.emplace(key, std::move(rtv));
    return raw;
}

static bool CreateSpriteResources() {
    if (g_spriteVertexShader && g_spriteBlitPixelShader && g_spriteInterpolatePixelShader && g_spriteInfoBuffer && g_spritePointSampler) {
        return true;
    }

    // Mirrors assets/minecraft/shaders/include/animation_sprite.glsl (std140):
    // mat4 ProjectionMatrix @0, mat4 SpriteMatrix @64, float UPadding @128, float VPadding @132,
    // int MipMapLevel @136. Matrices are JOML column-major, so PM0..PM3 / SM0..SM3 are columns.
    static const char* spriteCommon =
        "cbuffer SpriteAnimationInfo : register(b0) {"
        "  float4 PM0; float4 PM1; float4 PM2; float4 PM3;"
        "  float4 SM0; float4 SM1; float4 SM2; float4 SM3;"
        "  float UPadding; float VPadding; int MipMapLevel; float SpritePad0;"
        "};"
        "struct SpriteVSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; float progress : TEXCOORD1; };";
    const std::string spriteVertexSource = std::string(spriteCommon) +
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
    const std::string spriteBlitSource = std::string(spriteCommon) +
        "Texture2D Sprite : register(t0);"
        "SamplerState SpriteSampler : register(s0);"
        "float4 main(SpriteVSOut input) : SV_Target {"
        "  return Sprite.SampleLevel(SpriteSampler, input.uv, MipMapLevel);"
        "}";
    const std::string spriteInterpolateSource = std::string(spriteCommon) +
        "Texture2D CurrentSprite : register(t0);"
        "Texture2D NextSprite : register(t1);"
        "SamplerState SpriteSampler : register(s0);"
        "float4 main(SpriteVSOut input) : SV_Target {"
        "  float4 currentColor = CurrentSprite.SampleLevel(SpriteSampler, input.uv, MipMapLevel);"
        "  float4 nextColor = NextSprite.SampleLevel(SpriteSampler, input.uv, MipMapLevel);"
        "  return lerp(currentColor, nextColor, input.progress);"
        "}";

    ComPtr<ID3DBlob> vsBlob;
    ComPtr<ID3DBlob> blitBlob;
    ComPtr<ID3DBlob> interpolateBlob;
    if (!CompileShader("sprite vertex shader", spriteVertexSource.c_str(), "vs_4_0", vsBlob.GetAddressOf()) ||
        !CompileShader("sprite blit pixel shader", spriteBlitSource.c_str(), "ps_4_0", blitBlob.GetAddressOf()) ||
        !CompileShader("sprite interpolate pixel shader", spriteInterpolateSource.c_str(), "ps_4_0", interpolateBlob.GetAddressOf())) {
        return false;
    }

    HRESULT hr = g_device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, g_spriteVertexShader.ReleaseAndGetAddressOf());
    if (!SucceededOrLog("CreateVertexShader(sprite)", hr)) {
        return false;
    }
    hr = g_device->CreatePixelShader(blitBlob->GetBufferPointer(), blitBlob->GetBufferSize(), nullptr, g_spriteBlitPixelShader.ReleaseAndGetAddressOf());
    if (!SucceededOrLog("CreatePixelShader(sprite blit)", hr)) {
        return false;
    }
    hr = g_device->CreatePixelShader(interpolateBlob->GetBufferPointer(), interpolateBlob->GetBufferSize(), nullptr, g_spriteInterpolatePixelShader.ReleaseAndGetAddressOf());
    if (!SucceededOrLog("CreatePixelShader(sprite interpolate)", hr)) {
        return false;
    }

    D3D11_BUFFER_DESC cbDesc = {};
    cbDesc.Usage = D3D11_USAGE_DEFAULT;
    cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cbDesc.ByteWidth = 144;
    hr = g_device->CreateBuffer(&cbDesc, nullptr, g_spriteInfoBuffer.ReleaseAndGetAddressOf());
    if (!SucceededOrLog("CreateBuffer(sprite info)", hr)) {
        return false;
    }

    D3D11_SAMPLER_DESC samplerDesc = {};
    samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
    hr = g_device->CreateSamplerState(&samplerDesc, g_spritePointSampler.ReleaseAndGetAddressOf());
    if (!SucceededOrLog("CreateSamplerState(sprite point)", hr)) {
        return false;
    }

    Log("D3D11 sprite blit resources initialized");
    return true;
}

extern "C" JNIEXPORT void JNICALL
Java_banditvault_d3d11mod_nativebridge_D3D11Native_nativeClearTexture(JNIEnv*, jclass, jlong handle, jfloat r, jfloat g, jfloat b, jfloat a) {
    if (!g_context || handle == 0) {
        return;
    }
    NativeTexture nativeTexture;
    {
        std::lock_guard<std::mutex> lock(g_resourceMutex);
        auto it = g_textures.find(static_cast<int64_t>(handle));
        if (it == g_textures.end()) {
            Log("nativeClearTexture unknown handle=%lld", static_cast<long long>(handle));
            return;
        }
        nativeTexture = it->second;
    }
    const float clear[] = { r, g, b, a };
    for (UINT mip = 0; mip < nativeTexture.mipLevels; ++mip) {
        ID3D11RenderTargetView* rtv = GetOrCreateTextureRtv(static_cast<int64_t>(handle), nativeTexture, mip);
        if (rtv) {
            g_context->ClearRenderTargetView(rtv, clear);
        }
    }
}

extern "C" JNIEXPORT jboolean JNICALL
Java_banditvault_d3d11mod_nativebridge_D3D11Native_nativeDrawSpriteBlit(
    JNIEnv* env,
    jclass,
    jlong targetTexture,
    jint targetMip,
    jboolean interpolate,
    jlong spriteTexture,
    jlong nextSpriteTexture,
    jint vertexOffset,
    jint vertexCount,
    jobject spriteInfo) {
    if (!g_context || targetTexture == 0 || spriteTexture == 0 || vertexCount <= 0 || vertexOffset < 0 || targetMip < 0) {
        Log("nativeDrawSpriteBlit rejected target=%lld sprite=%lld count=%d", static_cast<long long>(targetTexture), static_cast<long long>(spriteTexture), static_cast<int>(vertexCount));
        return JNI_FALSE;
    }
    if (interpolate && nextSpriteTexture == 0) {
        Log("nativeDrawSpriteBlit rejected interpolate without next sprite");
        return JNI_FALSE;
    }
    void* infoData = env->GetDirectBufferAddress(spriteInfo);
    jlong infoSize = env->GetDirectBufferCapacity(spriteInfo);
    if (!infoData || infoSize < 140) {
        Log("nativeDrawSpriteBlit rejected info=%p/%lld", infoData, static_cast<long long>(infoSize));
        return JNI_FALSE;
    }
    if (!CreateGuiResources() || !CreateSpriteResources()) {
        return JNI_FALSE;
    }

    NativeTexture targetNative;
    NativeTexture spriteNative;
    NativeTexture nextNative;
    {
        std::lock_guard<std::mutex> lock(g_resourceMutex);
        auto target = g_textures.find(static_cast<int64_t>(targetTexture));
        auto sprite = g_textures.find(static_cast<int64_t>(spriteTexture));
        if (target == g_textures.end() || sprite == g_textures.end()) {
            Log("nativeDrawSpriteBlit unknown texture target=%lld sprite=%lld", static_cast<long long>(targetTexture), static_cast<long long>(spriteTexture));
            return JNI_FALSE;
        }
        targetNative = target->second;
        spriteNative = sprite->second;
        if (interpolate) {
            auto next = g_textures.find(static_cast<int64_t>(nextSpriteTexture));
            if (next == g_textures.end()) {
                Log("nativeDrawSpriteBlit unknown next sprite=%lld", static_cast<long long>(nextSpriteTexture));
                return JNI_FALSE;
            }
            nextNative = next->second;
        }
    }
    if (static_cast<UINT>(targetMip) >= targetNative.mipLevels) {
        Log("nativeDrawSpriteBlit rejected mip=%d levels=%u", static_cast<int>(targetMip), targetNative.mipLevels);
        return JNI_FALSE;
    }

    ID3D11RenderTargetView* rtv = GetOrCreateTextureRtv(static_cast<int64_t>(targetTexture), targetNative, static_cast<UINT>(targetMip));
    if (!rtv) {
        return JNI_FALSE;
    }

    g_context->UpdateSubresource(g_spriteInfoBuffer.Get(), 0, nullptr, infoData, 0, 0);

    UINT mipWidth = targetNative.width >> targetMip;
    UINT mipHeight = targetNative.height >> targetMip;
    if (mipWidth == 0) mipWidth = 1;
    if (mipHeight == 0) mipHeight = 1;

    g_context->OMSetRenderTargets(1, &rtv, nullptr);
    D3D11_VIEWPORT viewport = {};
    viewport.Width = static_cast<float>(mipWidth);
    viewport.Height = static_cast<float>(mipHeight);
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    g_context->RSSetViewports(1, &viewport);

    const float blendFactor[] = { 0.0f, 0.0f, 0.0f, 0.0f };
    g_context->OMSetBlendState(g_guiOpaqueBlendState.Get(), blendFactor, 0xFFFFFFFF);
    g_context->RSSetState(g_guiRasterizerState.Get());
    g_context->IASetInputLayout(nullptr);
    ID3D11Buffer* nullVb = nullptr;
    UINT zero = 0;
    g_context->IASetVertexBuffers(0, 1, &nullVb, &zero, &zero);
    g_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D11Buffer* cbs[] = { g_spriteInfoBuffer.Get() };
    g_context->VSSetConstantBuffers(0, 1, cbs);
    g_context->PSSetConstantBuffers(0, 1, cbs);
    g_context->VSSetShader(g_spriteVertexShader.Get(), nullptr, 0);
    g_context->PSSetShader(interpolate ? g_spriteInterpolatePixelShader.Get() : g_spriteBlitPixelShader.Get(), nullptr, 0);
    ID3D11ShaderResourceView* srvs[] = { spriteNative.srv.Get(), interpolate ? nextNative.srv.Get() : nullptr };
    ID3D11SamplerState* samplers[] = { g_spritePointSampler.Get() };
    g_context->PSSetShaderResources(0, 2, srvs);
    g_context->PSSetSamplers(0, 1, samplers);

    g_context->Draw(static_cast<UINT>(vertexCount), static_cast<UINT>(vertexOffset));

    // Unbind the target so it can be sampled by subsequent draws without hazard warnings.
    ID3D11RenderTargetView* nullRtv = nullptr;
    g_context->OMSetRenderTargets(1, &nullRtv, nullptr);
    ID3D11ShaderResourceView* nullSrvs[] = { nullptr, nullptr };
    g_context->PSSetShaderResources(0, 2, nullSrvs);

    if (g_loggedSpriteBlits < 8) {
        const float* info = static_cast<const float*>(infoData);
        Log("nativeDrawSpriteBlit target=%lld mip=%d interpolate=%d sprite=%lld next=%lld first=%d count=%d viewport=%ux%u PM0.x=%.5f PM3=(%.3f,%.3f) SM3=(%.1f,%.1f)",
            static_cast<long long>(targetTexture), static_cast<int>(targetMip), interpolate ? 1 : 0,
            static_cast<long long>(spriteTexture), static_cast<long long>(nextSpriteTexture),
            static_cast<int>(vertexOffset), static_cast<int>(vertexCount), mipWidth, mipHeight,
            info[0], info[12], info[13], info[28], info[29]);
        ++g_loggedSpriteBlits;
    }
    return JNI_TRUE;
}

// ---- World rendering: depth buffer, terrain, lightmap --------------------------------------

static bool EnsureDepthBuffer() {
    if (g_depthStencilView && g_depthTexture) {
        D3D11_TEXTURE2D_DESC desc = {};
        g_depthTexture->GetDesc(&desc);
        if (desc.Width == static_cast<UINT>(g_width) && desc.Height == static_cast<UINT>(g_height)) {
            return true;
        }
        g_depthStencilView.Reset();
        g_depthTexture.Reset();
    }

    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = static_cast<UINT>(g_width);
    desc.Height = static_cast<UINT>(g_height);
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_D32_FLOAT;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    HRESULT hr = g_device->CreateTexture2D(&desc, nullptr, g_depthTexture.ReleaseAndGetAddressOf());
    if (FAILED(hr)) {
        LogHr("CreateTexture2D(depth)", hr);
        return false;
    }
    hr = g_device->CreateDepthStencilView(g_depthTexture.Get(), nullptr, g_depthStencilView.ReleaseAndGetAddressOf());
    if (FAILED(hr)) {
        LogHr("CreateDepthStencilView", hr);
        return false;
    }
    Log("Depth buffer created %dx%d", g_width, g_height);
    return true;
}

static bool CreateWorldResources() {
    if (g_terrainVertexShader && g_terrainPixelShader && g_terrainInputLayout && g_terrainDepthState && g_chunkSectionBuffer && g_fogBuffer && g_globalsBuffer && g_terrainParamsBuffer && g_terrainAtlasSampler && g_screenquadVertexShader && g_lightmapPixelShader && g_lightmapInfoBuffer) {
        return true;
    }

    // std140 mirrors. Ints are read with asint() because cbuffer loads are typeless 32-bit.
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

    // Mirrors core/terrain.vsh. Lightmap is sampled in the vertex shader.
    const std::string terrainVertexSource = std::string(worldUniformBlocks) +
        "Texture2D Lightmap : register(t2);"
        "SamplerState LightmapSampler : register(s2);"
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

    // Mirrors core/terrain.fsh (sampleNearest path; RGSS path preserved behind UseRgss).
    const std::string terrainPixelSource = std::string(worldUniformBlocks) +
        "Texture2D BlockAtlas : register(t0);"
        "SamplerState AtlasSampler : register(s0);"
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

    // Mirrors core/screenquad.vsh (fullscreen triangle from SV_VertexID) with the GL->D3D
    // render-to-texture Y flip.
    static const char* screenquadVertexSource =
        "struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };"
        "VSOut main(uint vertexId : SV_VertexID) {"
        "  float2 uv = float2((vertexId << 1) & 2, vertexId & 2);"
        "  VSOut output;"
        "  output.pos = float4(uv.x * 2.0 - 1.0, -(uv.y * 2.0 - 1.0), 0.0, 1.0);"
        "  output.uv = uv;"
        "  return output;"
        "}";

    // Mirrors core/lightmap.fsh. std140 LightmapInfo: 7 floats @0..27, vec3 SkyLightColor @32,
    // vec3 AmbientColor @48 (total 64).
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

    ComPtr<ID3DBlob> terrainVsBlob;
    ComPtr<ID3DBlob> terrainPsBlob;
    ComPtr<ID3DBlob> screenquadVsBlob;
    ComPtr<ID3DBlob> lightmapPsBlob;
    if (!CompileShader("terrain vertex shader", terrainVertexSource.c_str(), "vs_4_0", terrainVsBlob.GetAddressOf()) ||
        !CompileShader("terrain pixel shader", terrainPixelSource.c_str(), "ps_4_0", terrainPsBlob.GetAddressOf()) ||
        !CompileShader("screenquad vertex shader", screenquadVertexSource, "vs_4_0", screenquadVsBlob.GetAddressOf()) ||
        !CompileShader("lightmap pixel shader", lightmapPixelSource, "ps_4_0", lightmapPsBlob.GetAddressOf())) {
        return false;
    }

    HRESULT hr = g_device->CreateVertexShader(terrainVsBlob->GetBufferPointer(), terrainVsBlob->GetBufferSize(), nullptr, g_terrainVertexShader.ReleaseAndGetAddressOf());
    if (!SucceededOrLog("CreateVertexShader(terrain)", hr)) return false;
    hr = g_device->CreatePixelShader(terrainPsBlob->GetBufferPointer(), terrainPsBlob->GetBufferSize(), nullptr, g_terrainPixelShader.ReleaseAndGetAddressOf());
    if (!SucceededOrLog("CreatePixelShader(terrain)", hr)) return false;
    hr = g_device->CreateVertexShader(screenquadVsBlob->GetBufferPointer(), screenquadVsBlob->GetBufferSize(), nullptr, g_screenquadVertexShader.ReleaseAndGetAddressOf());
    if (!SucceededOrLog("CreateVertexShader(screenquad)", hr)) return false;
    hr = g_device->CreatePixelShader(lightmapPsBlob->GetBufferPointer(), lightmapPsBlob->GetBufferSize(), nullptr, g_lightmapPixelShader.ReleaseAndGetAddressOf());
    if (!SucceededOrLog("CreatePixelShader(lightmap)", hr)) return false;

    // BLOCK vertex format: POSITION float3 @0, COLOR unorm4 @12, UV0 float2 @16,
    // UV2 short2 @24 (lightmap coords), NORMAL @28 (unused by the shader). Stride 32.
    D3D11_INPUT_ELEMENT_DESC terrainLayout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 1, DXGI_FORMAT_R16G16_SINT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };
    hr = g_device->CreateInputLayout(terrainLayout, ARRAYSIZE(terrainLayout), terrainVsBlob->GetBufferPointer(), terrainVsBlob->GetBufferSize(), g_terrainInputLayout.ReleaseAndGetAddressOf());
    if (!SucceededOrLog("CreateInputLayout(terrain)", hr)) return false;

    D3D11_DEPTH_STENCIL_DESC depthDesc = {};
    depthDesc.DepthEnable = TRUE;
    depthDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    depthDesc.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
    hr = g_device->CreateDepthStencilState(&depthDesc, g_terrainDepthState.ReleaseAndGetAddressOf());
    if (!SucceededOrLog("CreateDepthStencilState(terrain)", hr)) return false;

    D3D11_BUFFER_DESC cbDesc = {};
    cbDesc.Usage = D3D11_USAGE_DEFAULT;
    cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cbDesc.ByteWidth = 96;
    hr = g_device->CreateBuffer(&cbDesc, nullptr, g_chunkSectionBuffer.ReleaseAndGetAddressOf());
    if (!SucceededOrLog("CreateBuffer(ChunkSection)", hr)) return false;
    cbDesc.ByteWidth = 48;
    hr = g_device->CreateBuffer(&cbDesc, nullptr, g_fogBuffer.ReleaseAndGetAddressOf());
    if (!SucceededOrLog("CreateBuffer(Fog)", hr)) return false;
    cbDesc.ByteWidth = 64;
    hr = g_device->CreateBuffer(&cbDesc, nullptr, g_globalsBuffer.ReleaseAndGetAddressOf());
    if (!SucceededOrLog("CreateBuffer(Globals)", hr)) return false;
    cbDesc.ByteWidth = 16;
    hr = g_device->CreateBuffer(&cbDesc, nullptr, g_terrainParamsBuffer.ReleaseAndGetAddressOf());
    if (!SucceededOrLog("CreateBuffer(TerrainParams)", hr)) return false;
    cbDesc.ByteWidth = 64;
    hr = g_device->CreateBuffer(&cbDesc, nullptr, g_lightmapInfoBuffer.ReleaseAndGetAddressOf());
    if (!SucceededOrLog("CreateBuffer(LightmapInfo)", hr)) return false;

    // The terrain shader does its own nearest/RGSS filtering with explicit gradients, so the
    // underlying sampler is linear across the mip chain.
    D3D11_SAMPLER_DESC samplerDesc = {};
    samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
    hr = g_device->CreateSamplerState(&samplerDesc, g_terrainAtlasSampler.ReleaseAndGetAddressOf());
    if (!SucceededOrLog("CreateSamplerState(terrain atlas)", hr)) return false;

    Log("D3D11 world resources initialized");
    return true;
}

extern "C" JNIEXPORT void JNICALL
Java_banditvault_d3d11mod_nativebridge_D3D11Native_nativeClearDepth(JNIEnv*, jclass, jfloat depth) {
    if (!g_context) {
        return;
    }
    if (!EnsureDepthBuffer()) {
        return;
    }
    g_context->ClearDepthStencilView(g_depthStencilView.Get(), D3D11_CLEAR_DEPTH, depth, 0);
}

extern "C" JNIEXPORT jboolean JNICALL
Java_banditvault_d3d11mod_nativebridge_D3D11Native_nativeDrawLightmap(
    JNIEnv* env,
    jclass,
    jlong targetTexture,
    jint targetMip,
    jint vertexOffset,
    jint vertexCount,
    jobject lightmapInfo) {
    if (!g_context || targetTexture == 0 || vertexCount <= 0) {
        Log("nativeDrawLightmap rejected target=%lld count=%d", static_cast<long long>(targetTexture), static_cast<int>(vertexCount));
        return JNI_FALSE;
    }
    void* infoData = env->GetDirectBufferAddress(lightmapInfo);
    jlong infoSize = env->GetDirectBufferCapacity(lightmapInfo);
    if (!infoData || infoSize < 60) {
        Log("nativeDrawLightmap rejected info=%p/%lld", infoData, static_cast<long long>(infoSize));
        return JNI_FALSE;
    }
    if (!CreateGuiResources() || !CreateWorldResources()) {
        return JNI_FALSE;
    }
    NativeTexture targetNative;
    {
        std::lock_guard<std::mutex> lock(g_resourceMutex);
        auto it = g_textures.find(static_cast<int64_t>(targetTexture));
        if (it == g_textures.end()) {
            Log("nativeDrawLightmap unknown target=%lld", static_cast<long long>(targetTexture));
            return JNI_FALSE;
        }
        targetNative = it->second;
    }
    ID3D11RenderTargetView* rtv = GetOrCreateTextureRtv(static_cast<int64_t>(targetTexture), targetNative, static_cast<UINT>(targetMip));
    if (!rtv) {
        return JNI_FALSE;
    }

    float upload[16] = {};
    std::memcpy(upload, infoData, infoSize < 64 ? static_cast<size_t>(infoSize) : 64u);
    g_context->UpdateSubresource(g_lightmapInfoBuffer.Get(), 0, nullptr, upload, 0, 0);

    UINT mipWidth = targetNative.width >> targetMip;
    UINT mipHeight = targetNative.height >> targetMip;
    if (mipWidth == 0) mipWidth = 1;
    if (mipHeight == 0) mipHeight = 1;

    g_context->OMSetRenderTargets(1, &rtv, nullptr);
    D3D11_VIEWPORT viewport = {};
    viewport.Width = static_cast<float>(mipWidth);
    viewport.Height = static_cast<float>(mipHeight);
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    g_context->RSSetViewports(1, &viewport);

    const float blendFactor[] = { 0.0f, 0.0f, 0.0f, 0.0f };
    g_context->OMSetBlendState(g_guiOpaqueBlendState.Get(), blendFactor, 0xFFFFFFFF);
    g_context->RSSetState(g_guiRasterizerState.Get());
    g_context->OMSetDepthStencilState(nullptr, 0);
    g_context->IASetInputLayout(nullptr);
    g_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D11Buffer* cbs[] = { g_lightmapInfoBuffer.Get() };
    g_context->PSSetConstantBuffers(0, 1, cbs);
    g_context->VSSetShader(g_screenquadVertexShader.Get(), nullptr, 0);
    g_context->PSSetShader(g_lightmapPixelShader.Get(), nullptr, 0);
    ID3D11ShaderResourceView* nullSrvs[] = { nullptr, nullptr, nullptr };
    g_context->PSSetShaderResources(0, 3, nullSrvs);

    g_context->Draw(static_cast<UINT>(vertexCount), static_cast<UINT>(vertexOffset));

    ID3D11RenderTargetView* nullRtv = nullptr;
    g_context->OMSetRenderTargets(1, &nullRtv, nullptr);

    if (!g_loggedFirstLightmapDraw) {
        const float* info = static_cast<const float*>(infoData);
        Log("nativeDrawLightmap target=%lld mip=%d count=%d viewport=%ux%u ambient=%.3f sky=%.3f block=%.3f",
            static_cast<long long>(targetTexture), static_cast<int>(targetMip), static_cast<int>(vertexCount), mipWidth, mipHeight, info[0], info[1], info[2]);
        g_loggedFirstLightmapDraw = true;
    }
    return JNI_TRUE;
}


// ---- Sky family + terrain batching ----------------------------------------------------------

static ComPtr<ID3D11VertexShader> g_skyVertexShader;
static ComPtr<ID3D11PixelShader> g_skyPixelShader;
static ComPtr<ID3D11VertexShader> g_posColorVertexShader;
static ComPtr<ID3D11PixelShader> g_posColorPixelShader;
static ComPtr<ID3D11VertexShader> g_starsVertexShader;
static ComPtr<ID3D11PixelShader> g_starsPixelShader;
static ComPtr<ID3D11VertexShader> g_posTexVertexShader;
static ComPtr<ID3D11PixelShader> g_posTexPixelShader;
static ComPtr<ID3D11InputLayout> g_positionInputLayout;
static ComPtr<ID3D11InputLayout> g_positionColorInputLayout;
static ComPtr<ID3D11InputLayout> g_positionTexInputLayout;
static ComPtr<ID3D11Buffer> g_fanIndexBuffer;
static UINT g_fanIndexVertexCapacity = 0;
static bool g_loggedWorldSimpleKinds[8] = {};
static ComPtr<ID3D11VertexShader> g_linesVertexShader;
static ComPtr<ID3D11PixelShader> g_linesPixelShader;
static ComPtr<ID3D11InputLayout> g_linesInputLayout;
static ComPtr<ID3D11VertexShader> g_shadowVertexShader;
static ComPtr<ID3D11PixelShader> g_shadowPixelShader;
static ComPtr<ID3D11InputLayout> g_shadowInputLayout;
static ComPtr<ID3D11VertexShader> g_cloudsVertexShader;
static ComPtr<ID3D11PixelShader> g_cloudsPixelShader;
static ComPtr<ID3D11Buffer> g_cloudInfoBuffer;      // 48 bytes (std140 CloudInfo)
static ComPtr<ID3D11DepthStencilState> g_lequalNoWriteDepthState;
static ComPtr<ID3D11RasterizerState> g_crumblingRasterizerState;
static ComPtr<ID3D11BlendState> g_crumblingBlendState;  // (DST_COLOR, SRC_COLOR)

static bool CreateSkyResources() {
    if (g_skyVertexShader && g_skyPixelShader && g_posColorVertexShader && g_posColorPixelShader && g_starsVertexShader && g_starsPixelShader && g_posTexVertexShader && g_posTexPixelShader && g_positionInputLayout && g_positionColorInputLayout && g_positionTexInputLayout && g_linesVertexShader && g_linesPixelShader && g_linesInputLayout && g_shadowVertexShader && g_shadowPixelShader && g_shadowInputLayout && g_cloudsVertexShader && g_cloudsPixelShader && g_cloudInfoBuffer && g_lequalNoWriteDepthState && g_crumblingRasterizerState && g_crumblingBlendState) {
        return true;
    }

    // Same DynamicTransforms/Projection/Fog mirrors as the GUI/world shaders; ModelViewMat
    // comes from DynamicTransforms for the sky family (unlike terrain's ChunkSection).
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

    // Mirrors core/sky.vsh/.fsh: fog distances from the raw Position, sky fog band from FogSkyEnd.
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
    // Mirrors core/position_color.vsh/.fsh (sunrise/sunset fan).
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
    // Mirrors core/stars.vsh/.fsh.
    const std::string starsVsSource = std::string(skyCommon) +
        "float4 main(float3 pos : POSITION) : SV_Position {"
        "  return world_transform(pos);"
        "}";
    const std::string starsPsSource = std::string(skyCommon) +
        "float4 main(float4 pos : SV_Position) : SV_Target {"
        "  return ColorModulator;"
        "}";
    // Mirrors core/position_tex.vsh/.fsh (celestial sun/moon quads).
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

    // Mirrors core/rendertype_lines.vsh: screen-space quad expansion from per-vertex Normal
    // (line direction) and LineWidth, alternating sides by SV_VertexID parity (D3D includes
    // base vertex like gl_VertexID for indexed draws).
    const std::string linesVsSource = std::string(skyCommon) +
        // POSITION_COLOR_NORMAL_LINE_WIDTH has no padding: normal snorm8x3 @16 and LineWidth
        // float @19 (stride 23). D3D can't express an unaligned float element, so bytes 16..23
        // are read as two uints and decoded manually.
        "float snorm8(uint b) { int v = int(b & 0xFFu); if (v > 127) v -= 256; return max(float(v) / 127.0, -1.0); }"
        "struct VSIn { float3 pos : POSITION; float4 color : COLOR0; uint2 packed : TEXCOORD6; };"
        "struct VSOut { float4 pos : SV_Position; float4 color : COLOR0; float2 dist : TEXCOORD3; };"
        "VSOut main(VSIn input, uint vertexId : SV_VertexID) {"
        "  float3 normal = float3(snorm8(input.packed.x), snorm8(input.packed.x >> 8), snorm8(input.packed.x >> 16));"
        "  float lineWidth = asfloat((input.packed.x >> 24) | (input.packed.y << 8));"
        "  const float VIEW_SHRINK = 1.0 - (1.0 / 256.0);"
        "  float3 shrunkStart = input.pos * VIEW_SHRINK;"
        "  float3 shrunkEnd = (input.pos + normal) * VIEW_SHRINK;"
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
        "  output.color = input.color;"
        "  output.dist = float2(length(input.pos), max(length(input.pos.xz), abs(input.pos.y)));"
        "  return output;"
        "}";
    const std::string linesPsSource = std::string(skyCommon) +
        "struct PSIn { float4 pos : SV_Position; float4 color : COLOR0; float2 dist : TEXCOORD3; };"
        "float4 main(PSIn input) : SV_Target {"
        "  float4 color = input.color * ColorModulator;"
        "  float fogValue = max(linear_fog_value(input.dist.x, Fog_Misc0.x, Fog_Misc0.y), linear_fog_value(input.dist.y, Fog_Misc0.z, Fog_Misc0.w));"
        "  return float4(lerp(color.rgb, FogColor.rgb, fogValue * FogColor.a), color.a);"
        "}";
    // Mirrors core/rendertype_entity_shadow.vsh/.fsh (also reused for crumbling with its own
    // blend/bias; crumbling's UV2 is unused by the fragment shader).
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
    // Mirrors core/rendertype_clouds.vsh/.fsh: vertex-buffer-less cells decoded from the
    // CloudFaces texel buffer.
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

    ComPtr<ID3DBlob> skyVsBlob, skyPsBlob, posColorVsBlob, posColorPsBlob, starsVsBlob, starsPsBlob, posTexVsBlob, posTexPsBlob;
    ComPtr<ID3DBlob> linesVsBlob, linesPsBlob, shadowVsBlob, shadowPsBlob, cloudsVsBlob, cloudsPsBlob;
    if (!CompileShader("lines vertex shader", linesVsSource.c_str(), "vs_4_0", linesVsBlob.GetAddressOf()) ||
        !CompileShader("lines pixel shader", linesPsSource.c_str(), "ps_4_0", linesPsBlob.GetAddressOf()) ||
        !CompileShader("shadow vertex shader", shadowVsSource.c_str(), "vs_4_0", shadowVsBlob.GetAddressOf()) ||
        !CompileShader("shadow pixel shader", shadowPsSource.c_str(), "ps_4_0", shadowPsBlob.GetAddressOf()) ||
        !CompileShader("clouds vertex shader", cloudsVsSource.c_str(), "vs_4_0", cloudsVsBlob.GetAddressOf()) ||
        !CompileShader("clouds pixel shader", cloudsPsSource.c_str(), "ps_4_0", cloudsPsBlob.GetAddressOf())) {
        return false;
    }
    if (!CompileShader("sky vertex shader", skyVsSource.c_str(), "vs_4_0", skyVsBlob.GetAddressOf()) ||
        !CompileShader("sky pixel shader", skyPsSource.c_str(), "ps_4_0", skyPsBlob.GetAddressOf()) ||
        !CompileShader("position_color vertex shader", posColorVsSource.c_str(), "vs_4_0", posColorVsBlob.GetAddressOf()) ||
        !CompileShader("position_color pixel shader", posColorPsSource.c_str(), "ps_4_0", posColorPsBlob.GetAddressOf()) ||
        !CompileShader("stars vertex shader", starsVsSource.c_str(), "vs_4_0", starsVsBlob.GetAddressOf()) ||
        !CompileShader("stars pixel shader", starsPsSource.c_str(), "ps_4_0", starsPsBlob.GetAddressOf()) ||
        !CompileShader("position_tex vertex shader", posTexVsSource.c_str(), "vs_4_0", posTexVsBlob.GetAddressOf()) ||
        !CompileShader("position_tex pixel shader", posTexPsSource.c_str(), "ps_4_0", posTexPsBlob.GetAddressOf())) {
        return false;
    }

    HRESULT hr = g_device->CreateVertexShader(skyVsBlob->GetBufferPointer(), skyVsBlob->GetBufferSize(), nullptr, g_skyVertexShader.ReleaseAndGetAddressOf());
    if (!SucceededOrLog("CreateVertexShader(sky)", hr)) return false;
    hr = g_device->CreatePixelShader(skyPsBlob->GetBufferPointer(), skyPsBlob->GetBufferSize(), nullptr, g_skyPixelShader.ReleaseAndGetAddressOf());
    if (!SucceededOrLog("CreatePixelShader(sky)", hr)) return false;
    hr = g_device->CreateVertexShader(posColorVsBlob->GetBufferPointer(), posColorVsBlob->GetBufferSize(), nullptr, g_posColorVertexShader.ReleaseAndGetAddressOf());
    if (!SucceededOrLog("CreateVertexShader(position_color)", hr)) return false;
    hr = g_device->CreatePixelShader(posColorPsBlob->GetBufferPointer(), posColorPsBlob->GetBufferSize(), nullptr, g_posColorPixelShader.ReleaseAndGetAddressOf());
    if (!SucceededOrLog("CreatePixelShader(position_color)", hr)) return false;
    hr = g_device->CreateVertexShader(starsVsBlob->GetBufferPointer(), starsVsBlob->GetBufferSize(), nullptr, g_starsVertexShader.ReleaseAndGetAddressOf());
    if (!SucceededOrLog("CreateVertexShader(stars)", hr)) return false;
    hr = g_device->CreatePixelShader(starsPsBlob->GetBufferPointer(), starsPsBlob->GetBufferSize(), nullptr, g_starsPixelShader.ReleaseAndGetAddressOf());
    if (!SucceededOrLog("CreatePixelShader(stars)", hr)) return false;
    hr = g_device->CreateVertexShader(posTexVsBlob->GetBufferPointer(), posTexVsBlob->GetBufferSize(), nullptr, g_posTexVertexShader.ReleaseAndGetAddressOf());
    if (!SucceededOrLog("CreateVertexShader(position_tex)", hr)) return false;
    hr = g_device->CreatePixelShader(posTexPsBlob->GetBufferPointer(), posTexPsBlob->GetBufferSize(), nullptr, g_posTexPixelShader.ReleaseAndGetAddressOf());
    if (!SucceededOrLog("CreatePixelShader(position_tex)", hr)) return false;

    D3D11_INPUT_ELEMENT_DESC positionLayout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };
    hr = g_device->CreateInputLayout(positionLayout, 1, skyVsBlob->GetBufferPointer(), skyVsBlob->GetBufferSize(), g_positionInputLayout.ReleaseAndGetAddressOf());
    if (!SucceededOrLog("CreateInputLayout(position)", hr)) return false;
    D3D11_INPUT_ELEMENT_DESC positionColorLayout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };
    hr = g_device->CreateInputLayout(positionColorLayout, 2, posColorVsBlob->GetBufferPointer(), posColorVsBlob->GetBufferSize(), g_positionColorInputLayout.ReleaseAndGetAddressOf());
    if (!SucceededOrLog("CreateInputLayout(position_color)", hr)) return false;
    D3D11_INPUT_ELEMENT_DESC positionTexLayout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };
    hr = g_device->CreateInputLayout(positionTexLayout, 2, posTexVsBlob->GetBufferPointer(), posTexVsBlob->GetBufferSize(), g_positionTexInputLayout.ReleaseAndGetAddressOf());
    if (!SucceededOrLog("CreateInputLayout(position_tex)", hr)) return false;

    hr = g_device->CreateVertexShader(linesVsBlob->GetBufferPointer(), linesVsBlob->GetBufferSize(), nullptr, g_linesVertexShader.ReleaseAndGetAddressOf());
    if (!SucceededOrLog("CreateVertexShader(lines)", hr)) return false;
    hr = g_device->CreatePixelShader(linesPsBlob->GetBufferPointer(), linesPsBlob->GetBufferSize(), nullptr, g_linesPixelShader.ReleaseAndGetAddressOf());
    if (!SucceededOrLog("CreatePixelShader(lines)", hr)) return false;
    hr = g_device->CreateVertexShader(shadowVsBlob->GetBufferPointer(), shadowVsBlob->GetBufferSize(), nullptr, g_shadowVertexShader.ReleaseAndGetAddressOf());
    if (!SucceededOrLog("CreateVertexShader(shadow)", hr)) return false;
    hr = g_device->CreatePixelShader(shadowPsBlob->GetBufferPointer(), shadowPsBlob->GetBufferSize(), nullptr, g_shadowPixelShader.ReleaseAndGetAddressOf());
    if (!SucceededOrLog("CreatePixelShader(shadow)", hr)) return false;
    hr = g_device->CreateVertexShader(cloudsVsBlob->GetBufferPointer(), cloudsVsBlob->GetBufferSize(), nullptr, g_cloudsVertexShader.ReleaseAndGetAddressOf());
    if (!SucceededOrLog("CreateVertexShader(clouds)", hr)) return false;
    hr = g_device->CreatePixelShader(cloudsPsBlob->GetBufferPointer(), cloudsPsBlob->GetBufferSize(), nullptr, g_cloudsPixelShader.ReleaseAndGetAddressOf());
    if (!SucceededOrLog("CreatePixelShader(clouds)", hr)) return false;

    // LINES format: POSITION @0, COLOR unorm4 @12, NORMAL snorm byte3 @16 (+pad @19),
    // LINE_WIDTH float @20. Stride 24.
    D3D11_INPUT_ELEMENT_DESC linesLayout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 6, DXGI_FORMAT_R32G32_UINT, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };
    hr = g_device->CreateInputLayout(linesLayout, ARRAYSIZE(linesLayout), linesVsBlob->GetBufferPointer(), linesVsBlob->GetBufferSize(), g_linesInputLayout.ReleaseAndGetAddressOf());
    if (!SucceededOrLog("CreateInputLayout(lines)", hr)) return false;

    // POSITION_COLOR_TEX (shadow) / BLOCK prefix (crumbling): pos @0, color @12, uv0 @16.
    D3D11_INPUT_ELEMENT_DESC shadowLayout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };
    hr = g_device->CreateInputLayout(shadowLayout, ARRAYSIZE(shadowLayout), shadowVsBlob->GetBufferPointer(), shadowVsBlob->GetBufferSize(), g_shadowInputLayout.ReleaseAndGetAddressOf());
    if (!SucceededOrLog("CreateInputLayout(shadow)", hr)) return false;

    D3D11_BUFFER_DESC cloudCbDesc = {};
    cloudCbDesc.Usage = D3D11_USAGE_DEFAULT;
    cloudCbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cloudCbDesc.ByteWidth = 48;
    hr = g_device->CreateBuffer(&cloudCbDesc, nullptr, g_cloudInfoBuffer.ReleaseAndGetAddressOf());
    if (!SucceededOrLog("CreateBuffer(CloudInfo)", hr)) return false;

    D3D11_DEPTH_STENCIL_DESC noWriteDesc = {};
    noWriteDesc.DepthEnable = TRUE;
    noWriteDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    noWriteDesc.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
    hr = g_device->CreateDepthStencilState(&noWriteDesc, g_lequalNoWriteDepthState.ReleaseAndGetAddressOf());
    if (!SucceededOrLog("CreateDepthStencilState(lequal no-write)", hr)) return false;

    // Crumbling overlays the block mesh: GL polygonOffset(-3,-3) equivalent bias.
    D3D11_RASTERIZER_DESC crumblingDesc = {};
    crumblingDesc.FillMode = D3D11_FILL_SOLID;
    crumblingDesc.CullMode = D3D11_CULL_BACK;
    crumblingDesc.FrontCounterClockwise = TRUE;
    crumblingDesc.DepthClipEnable = TRUE;
    crumblingDesc.DepthBias = -4;
    crumblingDesc.SlopeScaledDepthBias = -3.0f;
    hr = g_device->CreateRasterizerState(&crumblingDesc, g_crumblingRasterizerState.ReleaseAndGetAddressOf());
    if (!SucceededOrLog("CreateRasterizerState(crumbling)", hr)) return false;

    D3D11_BLEND_DESC crumblingBlend = {};
    crumblingBlend.RenderTarget[0].BlendEnable = TRUE;
    crumblingBlend.RenderTarget[0].SrcBlend = D3D11_BLEND_DEST_COLOR;
    crumblingBlend.RenderTarget[0].DestBlend = D3D11_BLEND_SRC_COLOR;
    crumblingBlend.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    crumblingBlend.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    crumblingBlend.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
    crumblingBlend.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    crumblingBlend.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    hr = g_device->CreateBlendState(&crumblingBlend, g_crumblingBlendState.ReleaseAndGetAddressOf());
    if (!SucceededOrLog("CreateBlendState(crumbling)", hr)) return false;

    Log("D3D11 sky resources initialized");
    return true;
}

static ID3D11ShaderResourceView* GetOrCreateBufferSrv(int64_t handle, const NativeBuffer& buffer, UINT offsetBytes, UINT lengthBytes) {
    const uint64_t key = (static_cast<uint64_t>(handle) << 24) ^ (static_cast<uint64_t>(offsetBytes) << 4) ^ lengthBytes;
    std::lock_guard<std::mutex> lock(g_resourceMutex);
    auto it = g_bufferSrvs.find(key);
    if (it != g_bufferSrvs.end()) {
        return it->second.Get();
    }
    D3D11_SHADER_RESOURCE_VIEW_DESC desc = {};
    desc.Format = DXGI_FORMAT_R8_SINT;
    desc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
    desc.Buffer.FirstElement = offsetBytes;
    desc.Buffer.NumElements = lengthBytes;
    ComPtr<ID3D11ShaderResourceView> srv;
    HRESULT hr = g_device->CreateShaderResourceView(buffer.buffer.Get(), &desc, srv.GetAddressOf());
    if (FAILED(hr)) {
        Log("CreateShaderResourceView(buffer=%lld offset=%u len=%u) failed hr=0x%08X", static_cast<long long>(handle), offsetBytes, lengthBytes, static_cast<unsigned>(hr));
        return nullptr;
    }
    ID3D11ShaderResourceView* raw = srv.Get();
    g_bufferSrvs.emplace(key, std::move(srv));
    return raw;
}

static bool EnsureFanIndexBuffer(UINT vertexCount) {
    if (g_fanIndexBuffer && g_fanIndexVertexCapacity >= vertexCount) {
        return true;
    }
    UINT capacity = g_fanIndexVertexCapacity < 64 ? 64 : g_fanIndexVertexCapacity * 2;
    while (capacity < vertexCount) {
        capacity *= 2;
    }
    if (capacity > 0xFFFF) {
        Log("EnsureFanIndexBuffer capacity too large %u", capacity);
        return false;
    }
    const UINT triangles = capacity - 2;
    std::vector<uint16_t> indices(static_cast<size_t>(triangles) * 3);
    for (UINT i = 0; i < triangles; ++i) {
        indices[i * 3 + 0] = 0;
        indices[i * 3 + 1] = static_cast<uint16_t>(i + 1);
        indices[i * 3 + 2] = static_cast<uint16_t>(i + 2);
    }
    D3D11_BUFFER_DESC desc = {};
    desc.ByteWidth = static_cast<UINT>(indices.size() * sizeof(uint16_t));
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_INDEX_BUFFER;
    D3D11_SUBRESOURCE_DATA initial = {};
    initial.pSysMem = indices.data();
    HRESULT hr = g_device->CreateBuffer(&desc, &initial, g_fanIndexBuffer.ReleaseAndGetAddressOf());
    if (FAILED(hr)) {
        LogHr("CreateBuffer(fan indices)", hr);
        return false;
    }
    g_fanIndexVertexCapacity = capacity;
    return true;
}

// kind: 0 = sky (POSITION, fan), 1 = position_color (sunrise fan), 2 = stars (POSITION quads),
// 3 = celestial (POSITION_TEX quads). blendMode: 0 none, 1 translucent, 2 overlay(SRC_ALPHA, ONE).
extern "C" JNIEXPORT jboolean JNICALL
Java_banditvault_d3d11mod_nativebridge_D3D11Native_nativeDrawWorldSimple(
    JNIEnv* env,
    jclass,
    jint kind,
    jint blendMode,
    jboolean fan,
    jlong vertexBufferHandle,
    jint vertexStride,
    jlong indexBufferHandle,
    jint indexBytes,
    jint vertexOffset,
    jint firstIndex,
    jint count,
    jlong textureHandle,
    jobject dynamicTransforms,
    jobject projection,
    jobject fog,
    jobject globals,
    jobject cloudInfo,
    jlong cloudFacesBuffer,
    jint cloudFacesOffset,
    jint cloudFacesLength) {
    const bool needsVertexBuffer = kind != 7;
    if (kind > 7) {
        // D3D12-only kinds; no-op success so the shared jar does not throw here.
        return JNI_TRUE;
    }
    if (!g_context || !g_renderTargetView || (needsVertexBuffer && vertexBufferHandle == 0) || count <= 0 || kind < 0 || kind > 7) {
        Log("nativeDrawWorldSimple rejected kind=%d vb=%lld count=%d", static_cast<int>(kind), static_cast<long long>(vertexBufferHandle), static_cast<int>(count));
        return JNI_FALSE;
    }
    void* dynamicData = env->GetDirectBufferAddress(dynamicTransforms);
    jlong dynamicSize = env->GetDirectBufferCapacity(dynamicTransforms);
    void* projectionData = env->GetDirectBufferAddress(projection);
    jlong projectionSize = env->GetDirectBufferCapacity(projection);
    if (!dynamicData || dynamicSize < 160 || !projectionData || projectionSize < 64) {
        Log("nativeDrawWorldSimple rejected uniforms dyn=%lld proj=%lld", static_cast<long long>(dynamicSize), static_cast<long long>(projectionSize));
        return JNI_FALSE;
    }
    void* fogData = fog ? env->GetDirectBufferAddress(fog) : nullptr;
    jlong fogSize = fog ? env->GetDirectBufferCapacity(fog) : 0;
    if (!CreateGuiResources() || !CreateWorldResources() || !CreateSkyResources()) {
        return JNI_FALSE;
    }

    NativeBuffer vertexNative;
    NativeBuffer indexNative;
    NativeTexture textureNative;
    NativeBuffer cloudFacesNative;
    const bool hasTexture = textureHandle != 0;
    {
        std::lock_guard<std::mutex> lock(g_resourceMutex);
        if (needsVertexBuffer) {
            auto vb = g_buffers.find(static_cast<int64_t>(vertexBufferHandle));
            if (vb == g_buffers.end()) {
                Log("nativeDrawWorldSimple unknown vb=%lld", static_cast<long long>(vertexBufferHandle));
                return JNI_FALSE;
            }
            vertexNative = vb->second;
        }
        if (kind == 7) {
            auto faces = g_buffers.find(static_cast<int64_t>(cloudFacesBuffer));
            if (faces == g_buffers.end()) {
                Log("nativeDrawWorldSimple unknown cloud faces=%lld", static_cast<long long>(cloudFacesBuffer));
                return JNI_FALSE;
            }
            cloudFacesNative = faces->second;
        }
        if (!fan) {
            auto ib = g_buffers.find(static_cast<int64_t>(indexBufferHandle));
            if (ib == g_buffers.end()) {
                Log("nativeDrawWorldSimple unknown ib=%lld", static_cast<long long>(indexBufferHandle));
                return JNI_FALSE;
            }
            indexNative = ib->second;
        }
        if (hasTexture) {
            auto texture = g_textures.find(static_cast<int64_t>(textureHandle));
            if (texture == g_textures.end()) {
                Log("nativeDrawWorldSimple unknown texture=%lld", static_cast<long long>(textureHandle));
                return JNI_FALSE;
            }
            textureNative = texture->second;
        }
    }
    if (fan && !EnsureFanIndexBuffer(static_cast<UINT>(count))) {
        return JNI_FALSE;
    }

    g_context->UpdateSubresource(g_guiDynamicTransformsBuffer.Get(), 0, nullptr, dynamicData, 0, 0);
    g_context->UpdateSubresource(g_guiProjectionBuffer.Get(), 0, nullptr, projectionData, 0, 0);
    if (fogData && fogSize >= 40) {
        unsigned char fogUpload[48] = {};
        std::memcpy(fogUpload, fogData, fogSize < 48 ? static_cast<size_t>(fogSize) : 48u);
        g_context->UpdateSubresource(g_fogBuffer.Get(), 0, nullptr, fogUpload, 0, 0);
    }
    if (globals) {
        void* globalsData = env->GetDirectBufferAddress(globals);
        jlong globalsSize = env->GetDirectBufferCapacity(globals);
        if (globalsData && globalsSize >= 56) {
            unsigned char globalsUpload[64] = {};
            std::memcpy(globalsUpload, globalsData, globalsSize < 64 ? static_cast<size_t>(globalsSize) : 64u);
            g_context->UpdateSubresource(g_globalsBuffer.Get(), 0, nullptr, globalsUpload, 0, 0);
        }
    }
    if (cloudInfo) {
        void* cloudData = env->GetDirectBufferAddress(cloudInfo);
        jlong cloudSize = env->GetDirectBufferCapacity(cloudInfo);
        if (cloudData && cloudSize >= 44) {
            unsigned char cloudUpload[48] = {};
            std::memcpy(cloudUpload, cloudData, cloudSize < 48 ? static_cast<size_t>(cloudSize) : 48u);
            g_context->UpdateSubresource(g_cloudInfoBuffer.Get(), 0, nullptr, cloudUpload, 0, 0);
        }
    }

    // Sky family (kinds 0-3) renders before terrain without depth. Lines/clouds test+write;
    // shadow/crumbling test without write.
    ID3D11DepthStencilView* dsv = nullptr;
    ID3D11DepthStencilState* depthState = nullptr;
    if (kind >= 4) {
        if (!EnsureDepthBuffer()) {
            return JNI_FALSE;
        }
        dsv = g_depthStencilView.Get();
        depthState = (kind == 5 || kind == 6) ? g_lequalNoWriteDepthState.Get() : g_terrainDepthState.Get();
    }
    g_context->OMSetRenderTargets(1, g_renderTargetView.GetAddressOf(), dsv);
    D3D11_VIEWPORT viewport = {};
    viewport.Width = static_cast<float>(g_width);
    viewport.Height = static_cast<float>(g_height);
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    g_context->RSSetViewports(1, &viewport);

    ID3D11BlendState* blendState = g_guiOpaqueBlendState.Get();
    if (blendMode == 1) {
        blendState = g_guiBlendState.Get();
    } else if (blendMode == 2) {
        blendState = g_guiAdditiveBlendState.Get();
    } else if (blendMode == 3) {
        blendState = g_crumblingBlendState.Get();
    }
    const float blendFactor[] = { 0.0f, 0.0f, 0.0f, 0.0f };
    g_context->OMSetBlendState(blendState, blendFactor, 0xFFFFFFFF);
    g_context->RSSetState(kind == 6 ? g_crumblingRasterizerState.Get() : g_guiRasterizerState.Get());
    g_context->OMSetDepthStencilState(depthState, 0);

    ID3D11InputLayout* inputLayout = g_positionInputLayout.Get();
    ID3D11VertexShader* vertexShader = g_skyVertexShader.Get();
    ID3D11PixelShader* pixelShader = g_skyPixelShader.Get();
    if (kind == 1) {
        inputLayout = g_positionColorInputLayout.Get();
        vertexShader = g_posColorVertexShader.Get();
        pixelShader = g_posColorPixelShader.Get();
    } else if (kind == 2) {
        inputLayout = g_positionInputLayout.Get();
        vertexShader = g_starsVertexShader.Get();
        pixelShader = g_starsPixelShader.Get();
    } else if (kind == 3) {
        inputLayout = g_positionTexInputLayout.Get();
        vertexShader = g_posTexVertexShader.Get();
        pixelShader = g_posTexPixelShader.Get();
    } else if (kind == 4) {
        inputLayout = g_linesInputLayout.Get();
        vertexShader = g_linesVertexShader.Get();
        pixelShader = g_linesPixelShader.Get();
    } else if (kind == 5 || kind == 6) {
        inputLayout = g_shadowInputLayout.Get();
        vertexShader = g_shadowVertexShader.Get();
        pixelShader = g_shadowPixelShader.Get();
    } else if (kind == 7) {
        inputLayout = nullptr;
        vertexShader = g_cloudsVertexShader.Get();
        pixelShader = g_cloudsPixelShader.Get();
    }
    g_context->IASetInputLayout(inputLayout);
    UINT stride = static_cast<UINT>(vertexStride);
    UINT offset = 0;
    ID3D11Buffer* vertexBuffers[] = { needsVertexBuffer ? vertexNative.buffer.Get() : nullptr };
    g_context->IASetVertexBuffers(0, 1, vertexBuffers, &stride, &offset);
    g_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D11Buffer* cbs[] = { g_guiDynamicTransformsBuffer.Get(), g_guiProjectionBuffer.Get(), g_fogBuffer.Get(), g_globalsBuffer.Get(), g_cloudInfoBuffer.Get() };
    g_context->VSSetConstantBuffers(0, 5, cbs);
    g_context->PSSetConstantBuffers(0, 5, cbs);
    g_context->VSSetShader(vertexShader, nullptr, 0);
    g_context->PSSetShader(pixelShader, nullptr, 0);
    if (kind == 7) {
        ID3D11ShaderResourceView* facesSrv = GetOrCreateBufferSrv(static_cast<int64_t>(cloudFacesBuffer), cloudFacesNative, static_cast<UINT>(cloudFacesOffset), static_cast<UINT>(cloudFacesLength));
        if (!facesSrv) {
            return JNI_FALSE;
        }
        ID3D11ShaderResourceView* vsSrvs[] = { facesSrv };
        g_context->VSSetShaderResources(0, 1, vsSrvs);
    }
    if (hasTexture) {
        ID3D11ShaderResourceView* srvs[] = { textureNative.srv.Get() };
        ID3D11SamplerState* samplers[] = { g_guiSamplerState.Get() };
        g_context->PSSetShaderResources(0, 1, srvs);
        g_context->PSSetSamplers(0, 1, samplers);
    } else {
        ID3D11ShaderResourceView* srvs[] = { nullptr };
        g_context->PSSetShaderResources(0, 1, srvs);
    }

    if (fan) {
        g_context->IASetIndexBuffer(g_fanIndexBuffer.Get(), DXGI_FORMAT_R16_UINT, 0);
        g_context->DrawIndexed(static_cast<UINT>((count - 2) * 3), 0, static_cast<INT>(vertexOffset));
    } else {
        g_context->IASetIndexBuffer(indexNative.buffer.Get(), indexBytes == 2 ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT, 0);
        g_context->DrawIndexed(static_cast<UINT>(count), static_cast<UINT>(firstIndex), static_cast<INT>(vertexOffset));
    }

    if (kind >= 4) {
        g_context->OMSetDepthStencilState(nullptr, 0);
    }
    if (kind == 7) {
        ID3D11ShaderResourceView* nullVsSrvs[] = { nullptr };
        g_context->VSSetShaderResources(0, 1, nullVsSrvs);
    }
    if (!g_loggedWorldSimpleKinds[kind]) {
        Log("nativeDrawWorldSimple first draw kind=%d blend=%d fan=%d vb=%lld stride=%d first=%d count=%d texture=%lld",
            static_cast<int>(kind), static_cast<int>(blendMode), fan ? 1 : 0, static_cast<long long>(vertexBufferHandle), static_cast<int>(vertexStride), static_cast<int>(firstIndex), static_cast<int>(count), static_cast<long long>(textureHandle));
        g_loggedWorldSimpleKinds[kind] = true;
    }
    return JNI_TRUE;
}

// ---- Terrain batching: shared state/uniforms set once, per-section draws stay minimal -------

static bool g_terrainBatchActive = false;

extern "C" JNIEXPORT jboolean JNICALL
Java_banditvault_d3d11mod_nativebridge_D3D11Native_nativeBeginTerrainBatch(
    JNIEnv* env,
    jclass,
    jfloat alphaCutoff,
    jboolean translucent,
    jlong atlasTexture,
    jlong lightmapTexture,
    jobject projection,
    jobject fog,
    jobject globals) {
    g_terrainBatchActive = false;
    if (!g_context || !g_renderTargetView) {
        return JNI_FALSE;
    }
    void* projData = env->GetDirectBufferAddress(projection);
    jlong projSize = env->GetDirectBufferCapacity(projection);
    void* fogData = env->GetDirectBufferAddress(fog);
    jlong fogSize = env->GetDirectBufferCapacity(fog);
    void* globalsData = env->GetDirectBufferAddress(globals);
    jlong globalsSize = env->GetDirectBufferCapacity(globals);
    if (!projData || projSize < 64 || !fogData || fogSize < 40 || !globalsData || globalsSize < 56) {
        Log("nativeBeginTerrainBatch rejected uniforms proj=%lld fog=%lld globals=%lld", static_cast<long long>(projSize), static_cast<long long>(fogSize), static_cast<long long>(globalsSize));
        return JNI_FALSE;
    }
    if (!CreateGuiResources() || !CreateWorldResources() || !EnsureDepthBuffer()) {
        return JNI_FALSE;
    }
    NativeTexture atlasNative;
    NativeTexture lightmapNative;
    {
        std::lock_guard<std::mutex> lock(g_resourceMutex);
        auto atlas = g_textures.find(static_cast<int64_t>(atlasTexture));
        auto lightmap = g_textures.find(static_cast<int64_t>(lightmapTexture));
        if (atlas == g_textures.end() || lightmap == g_textures.end()) {
            Log("nativeBeginTerrainBatch unknown texture atlas=%lld lightmap=%lld", static_cast<long long>(atlasTexture), static_cast<long long>(lightmapTexture));
            return JNI_FALSE;
        }
        atlasNative = atlas->second;
        lightmapNative = lightmap->second;
    }

    g_context->UpdateSubresource(g_guiProjectionBuffer.Get(), 0, nullptr, projData, 0, 0);
    unsigned char fogUpload[48] = {};
    std::memcpy(fogUpload, fogData, fogSize < 48 ? static_cast<size_t>(fogSize) : 48u);
    g_context->UpdateSubresource(g_fogBuffer.Get(), 0, nullptr, fogUpload, 0, 0);
    unsigned char globalsUpload[64] = {};
    std::memcpy(globalsUpload, globalsData, globalsSize < 64 ? static_cast<size_t>(globalsSize) : 64u);
    g_context->UpdateSubresource(g_globalsBuffer.Get(), 0, nullptr, globalsUpload, 0, 0);
    const float params[4] = { alphaCutoff, 0.0f, 0.0f, 0.0f };
    g_context->UpdateSubresource(g_terrainParamsBuffer.Get(), 0, nullptr, params, 0, 0);

    g_context->OMSetRenderTargets(1, g_renderTargetView.GetAddressOf(), g_depthStencilView.Get());
    D3D11_VIEWPORT viewport = {};
    viewport.Width = static_cast<float>(g_width);
    viewport.Height = static_cast<float>(g_height);
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    g_context->RSSetViewports(1, &viewport);
    const float blendFactor[] = { 0.0f, 0.0f, 0.0f, 0.0f };
    g_context->OMSetBlendState(translucent ? g_guiBlendState.Get() : g_guiOpaqueBlendState.Get(), blendFactor, 0xFFFFFFFF);
    g_context->RSSetState(g_guiRasterizerState.Get());
    g_context->OMSetDepthStencilState(g_terrainDepthState.Get(), 0);
    g_context->IASetInputLayout(g_terrainInputLayout.Get());
    g_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D11Buffer* cbs[] = { g_chunkSectionBuffer.Get(), g_guiProjectionBuffer.Get(), g_globalsBuffer.Get(), g_fogBuffer.Get(), g_terrainParamsBuffer.Get() };
    g_context->VSSetConstantBuffers(0, 5, cbs);
    g_context->PSSetConstantBuffers(0, 5, cbs);
    g_context->VSSetShader(g_terrainVertexShader.Get(), nullptr, 0);
    g_context->PSSetShader(g_terrainPixelShader.Get(), nullptr, 0);
    ID3D11ShaderResourceView* psSrvs[] = { atlasNative.srv.Get() };
    ID3D11SamplerState* psSamplers[] = { g_terrainAtlasSampler.Get() };
    g_context->PSSetShaderResources(0, 1, psSrvs);
    g_context->PSSetSamplers(0, 1, psSamplers);
    ID3D11ShaderResourceView* vsSrvs[] = { nullptr, nullptr, lightmapNative.srv.Get() };
    ID3D11SamplerState* vsSamplers[] = { nullptr, nullptr, g_guiSamplerState.Get() };
    g_context->VSSetShaderResources(0, 3, vsSrvs);
    g_context->VSSetSamplers(0, 3, vsSamplers);

    g_terrainBatchActive = true;
    return JNI_TRUE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_banditvault_d3d11mod_nativebridge_D3D11Native_nativeDrawTerrainSection(
    JNIEnv* env,
    jclass,
    jlong vertexBufferHandle,
    jint vertexStride,
    jlong indexBufferHandle,
    jint indexBytes,
    jint firstIndex,
    jint indexCount,
    jobject chunkSection) {
    if (!g_terrainBatchActive || !g_context) {
        Log("nativeDrawTerrainSection rejected: no active batch");
        return JNI_FALSE;
    }
    void* csData = env->GetDirectBufferAddress(chunkSection);
    jlong csSize = env->GetDirectBufferCapacity(chunkSection);
    if (!csData || csSize < 92 || vertexBufferHandle == 0 || indexBufferHandle == 0 || indexCount <= 0 || (indexBytes != 2 && indexBytes != 4)) {
        Log("nativeDrawTerrainSection rejected vb=%lld ib=%lld count=%d cs=%lld", static_cast<long long>(vertexBufferHandle), static_cast<long long>(indexBufferHandle), static_cast<int>(indexCount), static_cast<long long>(csSize));
        return JNI_FALSE;
    }
    NativeBuffer vertexNative;
    NativeBuffer indexNative;
    {
        std::lock_guard<std::mutex> lock(g_resourceMutex);
        auto vb = g_buffers.find(static_cast<int64_t>(vertexBufferHandle));
        auto ib = g_buffers.find(static_cast<int64_t>(indexBufferHandle));
        if (vb == g_buffers.end() || ib == g_buffers.end()) {
            Log("nativeDrawTerrainSection unknown buffer vb=%lld ib=%lld", static_cast<long long>(vertexBufferHandle), static_cast<long long>(indexBufferHandle));
            return JNI_FALSE;
        }
        vertexNative = vb->second;
        indexNative = ib->second;
    }
    unsigned char csUpload[96] = {};
    std::memcpy(csUpload, csData, csSize < 96 ? static_cast<size_t>(csSize) : 96u);
    g_context->UpdateSubresource(g_chunkSectionBuffer.Get(), 0, nullptr, csUpload, 0, 0);
    UINT stride = static_cast<UINT>(vertexStride);
    UINT offset = 0;
    ID3D11Buffer* vertexBuffers[] = { vertexNative.buffer.Get() };
    g_context->IASetVertexBuffers(0, 1, vertexBuffers, &stride, &offset);
    g_context->IASetIndexBuffer(indexNative.buffer.Get(), indexBytes == 2 ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT, 0);
    g_context->DrawIndexed(static_cast<UINT>(indexCount), static_cast<UINT>(firstIndex), 0);

    if (!g_loggedFirstTerrainDraw) {
        Log("nativeDrawTerrainSection first draw vb=%lld ib=%lld stride=%d indexBytes=%d firstIndex=%d indexCount=%d",
            static_cast<long long>(vertexBufferHandle), static_cast<long long>(indexBufferHandle), static_cast<int>(vertexStride), static_cast<int>(indexBytes), static_cast<int>(firstIndex), static_cast<int>(indexCount));
        g_loggedFirstTerrainDraw = true;
    }
    return JNI_TRUE;
}

extern "C" JNIEXPORT void JNICALL
Java_banditvault_d3d11mod_nativebridge_D3D11Native_nativeEndTerrainBatch(JNIEnv*, jclass) {
    if (!g_context) {
        return;
    }
    g_terrainBatchActive = false;
    g_context->OMSetDepthStencilState(nullptr, 0);
    ID3D11ShaderResourceView* nullVsSrvs[] = { nullptr, nullptr, nullptr };
    g_context->VSSetShaderResources(0, 3, nullVsSrvs);
}

// ---- Entity + particle rendering ------------------------------------------------------------

static ComPtr<ID3D11VertexShader> g_entityVertexShader;
static ComPtr<ID3D11PixelShader> g_entityPixelShader;
static ComPtr<ID3D11InputLayout> g_entityInputLayout;
static ComPtr<ID3D11VertexShader> g_particleVertexShader;
static ComPtr<ID3D11PixelShader> g_particlePixelShader;
static ComPtr<ID3D11InputLayout> g_particleInputLayout;
static ComPtr<ID3D11Buffer> g_lightingBuffer;       // 32 bytes (std140 Lighting)
static ComPtr<ID3D11Buffer> g_entityParamsBuffer;   // 16 bytes (cutoff + flags)
static ComPtr<ID3D11RasterizerState> g_worldCullBackRasterizerState;  // GL winding (FrontCounterClockwise)
static ComPtr<ID3D11RasterizerState> g_worldCullNoneRasterizerState;
static ComPtr<ID3D11RasterizerState> g_worldCullBackCwRasterizerState;  // flipped-Y render-to-texture winding
static ComPtr<ID3D11RasterizerState> g_worldCullNoneCwRasterizerState;
struct OffscreenDepth {
    ComPtr<ID3D11Texture2D> texture;
    ComPtr<ID3D11DepthStencilView> dsv;
};
static std::unordered_map<uint64_t, OffscreenDepth> g_offscreenDepths;
static bool g_loggedFirstEntityDraw = false;
static bool g_loggedFirstParticleDraw = false;

// Entity flag bits, mirrored in D3D11RenderPass.java.
static constexpr int ENTITY_FLAG_PER_FACE_LIGHTING = 1;
static constexpr int ENTITY_FLAG_NO_OVERLAY = 2;
static constexpr int ENTITY_FLAG_EMISSIVE = 4;
static constexpr int ENTITY_FLAG_NO_CARDINAL_LIGHTING = 8;
static constexpr int ENTITY_FLAG_CULL_BACK = 16;
static constexpr int ENTITY_FLAG_FLIP_Y = 32;  // set natively for texture render targets
static constexpr int ENTITY_FLAG_NO_DEPTH_WRITE = 64;

static bool CreateEntityResources() {
    if (g_entityVertexShader && g_entityPixelShader && g_entityInputLayout && g_particleVertexShader && g_particlePixelShader && g_particleInputLayout && g_lightingBuffer && g_entityParamsBuffer && g_worldCullBackRasterizerState && g_worldCullNoneRasterizerState) {
        return true;
    }

    // Mirrors include/light.glsl, include/fog.glsl, dynamictransforms/projection plus a params
    // block replacing the compile-time defines (PER_FACE_LIGHTING / NO_OVERLAY / EMISSIVE /
    // NO_CARDINAL_LIGHTING / ALPHA_CUTOUT) with runtime flags.
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

    // Mirrors core/entity.vsh: cardinal lighting, overlay/lightmap texel fetches in the VS.
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
    // Mirrors core/entity.fsh.
    const std::string entityPsSource = std::string(entityCommon) +
        "Texture2D EntityTex : register(t0);"
        "SamplerState EntitySampler : register(s0);"
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
    // Mirrors core/particle.vsh/.fsh.
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
        "SamplerState ParticleSampler : register(s0);"
        "struct PSIn { float4 pos : SV_Position; float4 color : COLOR0; float2 uv : TEXCOORD0; float2 dist : TEXCOORD3; };"
        "float4 main(PSIn input) : SV_Target {"
        "  float4 color = ParticleTex.Sample(ParticleSampler, input.uv) * input.color * ColorModulator;"
        "  if (color.a < 0.1) discard;"
        "  return apply_fog(color, input.dist.x, input.dist.y);"
        "}";

    ComPtr<ID3DBlob> entityVsBlob, entityPsBlob, particleVsBlob, particlePsBlob;
    if (!CompileShader("entity vertex shader", entityVsSource.c_str(), "vs_4_0", entityVsBlob.GetAddressOf()) ||
        !CompileShader("entity pixel shader", entityPsSource.c_str(), "ps_4_0", entityPsBlob.GetAddressOf()) ||
        !CompileShader("particle vertex shader", particleVsSource.c_str(), "vs_4_0", particleVsBlob.GetAddressOf()) ||
        !CompileShader("particle pixel shader", particlePsSource.c_str(), "ps_4_0", particlePsBlob.GetAddressOf())) {
        return false;
    }

    HRESULT hr = g_device->CreateVertexShader(entityVsBlob->GetBufferPointer(), entityVsBlob->GetBufferSize(), nullptr, g_entityVertexShader.ReleaseAndGetAddressOf());
    if (!SucceededOrLog("CreateVertexShader(entity)", hr)) return false;
    hr = g_device->CreatePixelShader(entityPsBlob->GetBufferPointer(), entityPsBlob->GetBufferSize(), nullptr, g_entityPixelShader.ReleaseAndGetAddressOf());
    if (!SucceededOrLog("CreatePixelShader(entity)", hr)) return false;
    hr = g_device->CreateVertexShader(particleVsBlob->GetBufferPointer(), particleVsBlob->GetBufferSize(), nullptr, g_particleVertexShader.ReleaseAndGetAddressOf());
    if (!SucceededOrLog("CreateVertexShader(particle)", hr)) return false;
    hr = g_device->CreatePixelShader(particlePsBlob->GetBufferPointer(), particlePsBlob->GetBufferSize(), nullptr, g_particlePixelShader.ReleaseAndGetAddressOf());
    if (!SucceededOrLog("CreatePixelShader(particle)", hr)) return false;

    // NEW_ENTITY: POSITION @0, COLOR unorm4 @12, UV0 float2 @16, UV1 short2 @24 (overlay),
    // UV2 short2 @28 (lightmap), NORMAL snorm byte3+pad @32. Stride 36.
    D3D11_INPUT_ELEMENT_DESC entityLayout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 1, DXGI_FORMAT_R16G16_SINT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 2, DXGI_FORMAT_R16G16_SINT, 0, 28, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "NORMAL", 0, DXGI_FORMAT_R8G8B8A8_SNORM, 0, 32, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };
    hr = g_device->CreateInputLayout(entityLayout, ARRAYSIZE(entityLayout), entityVsBlob->GetBufferPointer(), entityVsBlob->GetBufferSize(), g_entityInputLayout.ReleaseAndGetAddressOf());
    if (!SucceededOrLog("CreateInputLayout(entity)", hr)) return false;

    // PARTICLE: POSITION @0, UV0 float2 @12, COLOR unorm4 @20, UV2 short2 @24. Stride 28.
    D3D11_INPUT_ELEMENT_DESC particleLayout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 20, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 2, DXGI_FORMAT_R16G16_SINT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };
    hr = g_device->CreateInputLayout(particleLayout, ARRAYSIZE(particleLayout), particleVsBlob->GetBufferPointer(), particleVsBlob->GetBufferSize(), g_particleInputLayout.ReleaseAndGetAddressOf());
    if (!SucceededOrLog("CreateInputLayout(particle)", hr)) return false;

    D3D11_BUFFER_DESC cbDesc = {};
    cbDesc.Usage = D3D11_USAGE_DEFAULT;
    cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cbDesc.ByteWidth = 32;
    hr = g_device->CreateBuffer(&cbDesc, nullptr, g_lightingBuffer.ReleaseAndGetAddressOf());
    if (!SucceededOrLog("CreateBuffer(Lighting)", hr)) return false;
    cbDesc.ByteWidth = 16;
    hr = g_device->CreateBuffer(&cbDesc, nullptr, g_entityParamsBuffer.ReleaseAndGetAddressOf());
    if (!SucceededOrLog("CreateBuffer(EntityParams)", hr)) return false;

    // GL-matching winding: front faces are counter-clockwise. Needed both for BACK culling and
    // for correct SV_IsFrontFace with per-face lighting on double-sided geometry.
    D3D11_RASTERIZER_DESC rasterizerDesc = {};
    rasterizerDesc.FillMode = D3D11_FILL_SOLID;
    rasterizerDesc.CullMode = D3D11_CULL_BACK;
    rasterizerDesc.FrontCounterClockwise = TRUE;
    rasterizerDesc.DepthClipEnable = TRUE;
    hr = g_device->CreateRasterizerState(&rasterizerDesc, g_worldCullBackRasterizerState.ReleaseAndGetAddressOf());
    if (!SucceededOrLog("CreateRasterizerState(world cull back)", hr)) return false;
    rasterizerDesc.CullMode = D3D11_CULL_NONE;
    hr = g_device->CreateRasterizerState(&rasterizerDesc, g_worldCullNoneRasterizerState.ReleaseAndGetAddressOf());
    if (!SucceededOrLog("CreateRasterizerState(world cull none)", hr)) return false;

    // Rendering into textures flips Y, which inverts on-screen winding; use CW-front variants
    // there so culling and SV_IsFrontFace still match GL.
    rasterizerDesc.FrontCounterClockwise = FALSE;
    rasterizerDesc.CullMode = D3D11_CULL_BACK;
    hr = g_device->CreateRasterizerState(&rasterizerDesc, g_worldCullBackCwRasterizerState.ReleaseAndGetAddressOf());
    if (!SucceededOrLog("CreateRasterizerState(world cull back cw)", hr)) return false;
    rasterizerDesc.CullMode = D3D11_CULL_NONE;
    hr = g_device->CreateRasterizerState(&rasterizerDesc, g_worldCullNoneCwRasterizerState.ReleaseAndGetAddressOf());
    if (!SucceededOrLog("CreateRasterizerState(world cull none cw)", hr)) return false;

    Log("D3D11 entity/particle resources initialized");
    return true;
}

static OffscreenDepth* GetOrCreateOffscreenDepth(UINT width, UINT height) {
    const uint64_t key = (static_cast<uint64_t>(width) << 20) | height;
    auto it = g_offscreenDepths.find(key);
    if (it != g_offscreenDepths.end()) {
        return &it->second;
    }
    OffscreenDepth entry;
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_D32_FLOAT;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    HRESULT hr = g_device->CreateTexture2D(&desc, nullptr, entry.texture.GetAddressOf());
    if (FAILED(hr)) {
        LogHr("CreateTexture2D(offscreen depth)", hr);
        return nullptr;
    }
    hr = g_device->CreateDepthStencilView(entry.texture.Get(), nullptr, entry.dsv.GetAddressOf());
    if (FAILED(hr)) {
        LogHr("CreateDepthStencilView(offscreen)", hr);
        return nullptr;
    }
    // Fresh D3D textures are zero-filled; a 0.0 depth would reject every LESS_EQUAL fragment,
    // so start cleared to the far plane.
    g_context->ClearDepthStencilView(entry.dsv.Get(), D3D11_CLEAR_DEPTH, 1.0f, 0);
    Log("Offscreen depth created %ux%u", width, height);
    auto inserted = g_offscreenDepths.emplace(key, std::move(entry));
    return &inserted.first->second;
}

extern "C" JNIEXPORT void JNICALL
Java_banditvault_d3d11mod_nativebridge_D3D11Native_nativeClearOffscreenDepth(JNIEnv*, jclass, jint width, jint height, jfloat depth) {
    if (!g_context || width <= 0 || height <= 0) {
        return;
    }
    OffscreenDepth* entry = GetOrCreateOffscreenDepth(static_cast<UINT>(width), static_cast<UINT>(height));
    if (entry) {
        g_context->ClearDepthStencilView(entry->dsv.Get(), D3D11_CLEAR_DEPTH, depth, 0);
    }
}

// blendMode: 0 none, 1 translucent. kind: 0 entity (NEW_ENTITY), 1 particle (PARTICLE).
extern "C" JNIEXPORT jboolean JNICALL
Java_banditvault_d3d11mod_nativebridge_D3D11Native_nativeDrawEntity(
    JNIEnv* env,
    jclass,
    jint kind,
    jint blendMode,
    jfloat alphaCutoff,
    jint flags,
    jlong vertexBufferHandle,
    jint vertexStride,
    jlong indexBufferHandle,
    jint indexBytes,
    jint vertexOffset,
    jint firstIndex,
    jint indexCount,
    jlong texture0,
    jlong overlayTexture,
    jlong lightmapTexture,
    jlong targetTexture,
    jint targetMip,
    jboolean targetHasDepth,
    jobject dynamicTransforms,
    jobject projection,
    jobject fog,
    jobject lighting) {
    if (!g_context || !g_renderTargetView || vertexBufferHandle == 0 || indexBufferHandle == 0 || indexCount <= 0 || texture0 == 0) {
        Log("nativeDrawEntity rejected kind=%d vb=%lld ib=%lld tex=%lld count=%d", static_cast<int>(kind), static_cast<long long>(vertexBufferHandle), static_cast<long long>(indexBufferHandle), static_cast<long long>(texture0), static_cast<int>(indexCount));
        return JNI_FALSE;
    }
    void* dynData = env->GetDirectBufferAddress(dynamicTransforms);
    jlong dynSize = env->GetDirectBufferCapacity(dynamicTransforms);
    void* projData = env->GetDirectBufferAddress(projection);
    jlong projSize = env->GetDirectBufferCapacity(projection);
    if (!dynData || dynSize < 160 || !projData || projSize < 64) {
        Log("nativeDrawEntity rejected uniforms dyn=%lld proj=%lld", static_cast<long long>(dynSize), static_cast<long long>(projSize));
        return JNI_FALSE;
    }
    void* fogData = fog ? env->GetDirectBufferAddress(fog) : nullptr;
    jlong fogSize = fog ? env->GetDirectBufferCapacity(fog) : 0;
    void* lightingData = lighting ? env->GetDirectBufferAddress(lighting) : nullptr;
    jlong lightingSize = lighting ? env->GetDirectBufferCapacity(lighting) : 0;
    if (!CreateGuiResources() || !CreateWorldResources() || !CreateSkyResources() || !CreateEntityResources() || !EnsureDepthBuffer()) {
        return JNI_FALSE;
    }

    NativeBuffer vertexNative;
    NativeBuffer indexNative;
    NativeTexture textureNative;
    NativeTexture overlayNative;
    NativeTexture lightmapNative;
    NativeTexture targetNative;
    const bool hasOverlay = overlayTexture != 0;
    const bool hasLightmap = lightmapTexture != 0;
    const bool textureTarget = targetTexture != 0;
    {
        std::lock_guard<std::mutex> lock(g_resourceMutex);
        auto vb = g_buffers.find(static_cast<int64_t>(vertexBufferHandle));
        auto ib = g_buffers.find(static_cast<int64_t>(indexBufferHandle));
        auto tex = g_textures.find(static_cast<int64_t>(texture0));
        if (vb == g_buffers.end() || ib == g_buffers.end() || tex == g_textures.end()) {
            Log("nativeDrawEntity unknown resource vb=%lld ib=%lld tex=%lld", static_cast<long long>(vertexBufferHandle), static_cast<long long>(indexBufferHandle), static_cast<long long>(texture0));
            return JNI_FALSE;
        }
        vertexNative = vb->second;
        indexNative = ib->second;
        textureNative = tex->second;
        if (hasOverlay) {
            auto overlay = g_textures.find(static_cast<int64_t>(overlayTexture));
            if (overlay == g_textures.end()) {
                Log("nativeDrawEntity unknown overlay=%lld", static_cast<long long>(overlayTexture));
                return JNI_FALSE;
            }
            overlayNative = overlay->second;
        }
        if (hasLightmap) {
            auto lightmap = g_textures.find(static_cast<int64_t>(lightmapTexture));
            if (lightmap == g_textures.end()) {
                Log("nativeDrawEntity unknown lightmap=%lld", static_cast<long long>(lightmapTexture));
                return JNI_FALSE;
            }
            lightmapNative = lightmap->second;
        }
        if (textureTarget) {
            auto target = g_textures.find(static_cast<int64_t>(targetTexture));
            if (target == g_textures.end()) {
                Log("nativeDrawEntity unknown target=%lld", static_cast<long long>(targetTexture));
                return JNI_FALSE;
            }
            targetNative = target->second;
        }
    }

    g_context->UpdateSubresource(g_guiDynamicTransformsBuffer.Get(), 0, nullptr, dynData, 0, 0);
    g_context->UpdateSubresource(g_guiProjectionBuffer.Get(), 0, nullptr, projData, 0, 0);
    if (fogData && fogSize >= 40) {
        unsigned char fogUpload[48] = {};
        std::memcpy(fogUpload, fogData, fogSize < 48 ? static_cast<size_t>(fogSize) : 48u);
        g_context->UpdateSubresource(g_fogBuffer.Get(), 0, nullptr, fogUpload, 0, 0);
    }
    unsigned char lightingUpload[32] = {};
    if (lightingData && lightingSize >= 28) {
        std::memcpy(lightingUpload, lightingData, lightingSize < 32 ? static_cast<size_t>(lightingSize) : 32u);
    }
    g_context->UpdateSubresource(g_lightingBuffer.Get(), 0, nullptr, lightingUpload, 0, 0);
    int effectiveFlags = static_cast<int>(flags);
    ID3D11RenderTargetView* rtv = g_renderTargetView.Get();
    ID3D11DepthStencilView* dsv = g_depthStencilView.Get();
    UINT viewportWidth = static_cast<UINT>(g_width);
    UINT viewportHeight = static_cast<UINT>(g_height);
    if (textureTarget) {
        rtv = GetOrCreateTextureRtv(static_cast<int64_t>(targetTexture), targetNative, static_cast<UINT>(targetMip));
        if (!rtv) {
            return JNI_FALSE;
        }
        viewportWidth = targetNative.width >> targetMip;
        viewportHeight = targetNative.height >> targetMip;
        if (viewportWidth == 0) viewportWidth = 1;
        if (viewportHeight == 0) viewportHeight = 1;
        if (targetHasDepth) {
            OffscreenDepth* offscreenDepth = GetOrCreateOffscreenDepth(viewportWidth, viewportHeight);
            if (!offscreenDepth) {
                return JNI_FALSE;
            }
            dsv = offscreenDepth->dsv.Get();
        } else {
            dsv = nullptr;
        }
        // Render-to-texture output is sampled with GL row order later: flip Y in clip space.
        effectiveFlags |= ENTITY_FLAG_FLIP_Y;
    }
    struct { float cutoff; int flags; float pad0; float pad1; } params = { alphaCutoff, effectiveFlags, 0.0f, 0.0f };
    g_context->UpdateSubresource(g_entityParamsBuffer.Get(), 0, nullptr, &params, 0, 0);

    g_context->OMSetRenderTargets(1, &rtv, dsv);
    D3D11_VIEWPORT viewport = {};
    viewport.Width = static_cast<float>(viewportWidth);
    viewport.Height = static_cast<float>(viewportHeight);
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    g_context->RSSetViewports(1, &viewport);
    const float blendFactor[] = { 0.0f, 0.0f, 0.0f, 0.0f };
    g_context->OMSetBlendState(blendMode == 1 ? g_guiBlendState.Get() : g_guiOpaqueBlendState.Get(), blendFactor, 0xFFFFFFFF);
    const bool noDepthWrite = (flags & ENTITY_FLAG_NO_DEPTH_WRITE) != 0;
    const bool cullBack = (flags & ENTITY_FLAG_CULL_BACK) != 0;
    if ((effectiveFlags & ENTITY_FLAG_FLIP_Y) != 0) {
        g_context->RSSetState(cullBack ? g_worldCullBackCwRasterizerState.Get() : g_worldCullNoneCwRasterizerState.Get());
    } else {
        g_context->RSSetState(cullBack ? g_worldCullBackRasterizerState.Get() : g_worldCullNoneRasterizerState.Get());
    }
    g_context->OMSetDepthStencilState(noDepthWrite ? g_lequalNoWriteDepthState.Get() : g_terrainDepthState.Get(), 0);
    g_context->IASetInputLayout(kind == 1 ? g_particleInputLayout.Get() : g_entityInputLayout.Get());
    UINT stride = static_cast<UINT>(vertexStride);
    UINT offset = 0;
    ID3D11Buffer* vertexBuffers[] = { vertexNative.buffer.Get() };
    g_context->IASetVertexBuffers(0, 1, vertexBuffers, &stride, &offset);
    g_context->IASetIndexBuffer(indexNative.buffer.Get(), indexBytes == 2 ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT, 0);
    g_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D11Buffer* cbs[] = { g_guiDynamicTransformsBuffer.Get(), g_guiProjectionBuffer.Get(), g_fogBuffer.Get(), g_lightingBuffer.Get(), g_entityParamsBuffer.Get() };
    g_context->VSSetConstantBuffers(0, 5, cbs);
    g_context->PSSetConstantBuffers(0, 5, cbs);
    g_context->VSSetShader(kind == 1 ? g_particleVertexShader.Get() : g_entityVertexShader.Get(), nullptr, 0);
    g_context->PSSetShader(kind == 1 ? g_particlePixelShader.Get() : g_entityPixelShader.Get(), nullptr, 0);
    // Entity textures are pixel art: point sampling. Overlay/lightmap are Load()-only in the VS.
    ID3D11ShaderResourceView* psSrvs[] = { textureNative.srv.Get() };
    ID3D11SamplerState* psSamplers[] = { g_spritePointSampler.Get() };
    g_context->PSSetShaderResources(0, 1, psSrvs);
    g_context->PSSetSamplers(0, 1, psSamplers);
    ID3D11ShaderResourceView* vsSrvs[] = { nullptr, hasOverlay ? overlayNative.srv.Get() : nullptr, hasLightmap ? lightmapNative.srv.Get() : nullptr };
    g_context->VSSetShaderResources(0, 3, vsSrvs);

    g_context->DrawIndexed(static_cast<UINT>(indexCount), static_cast<UINT>(firstIndex), static_cast<INT>(vertexOffset));

    g_context->OMSetDepthStencilState(nullptr, 0);
    ID3D11ShaderResourceView* nullVsSrvs[] = { nullptr, nullptr, nullptr };
    g_context->VSSetShaderResources(0, 3, nullVsSrvs);
    if (textureTarget) {
        ID3D11RenderTargetView* nullRtv = nullptr;
        g_context->OMSetRenderTargets(1, &nullRtv, nullptr);
    }

    static bool loggedFirstTargetBake = false;
    if (textureTarget && !loggedFirstTargetBake) {
        Log("nativeDrawEntity texture-target bake target=%lld mip=%d hasDepth=%d viewport=%ux%u flags=0x%X count=%d tex=%lld",
            static_cast<long long>(targetTexture), static_cast<int>(targetMip), targetHasDepth ? 1 : 0,
            viewportWidth, viewportHeight, effectiveFlags, static_cast<int>(indexCount), static_cast<long long>(texture0));
        loggedFirstTargetBake = true;
    }
    if (kind == 1 ? !g_loggedFirstParticleDraw : !g_loggedFirstEntityDraw) {
        Log("nativeDrawEntity first draw kind=%d blend=%d cutoff=%.2f flags=0x%X vb=%lld stride=%d count=%d tex=%lld overlay=%lld lightmap=%lld",
            static_cast<int>(kind), static_cast<int>(blendMode), alphaCutoff, static_cast<int>(flags),
            static_cast<long long>(vertexBufferHandle), static_cast<int>(vertexStride), static_cast<int>(indexCount),
            static_cast<long long>(texture0), static_cast<long long>(overlayTexture), static_cast<long long>(lightmapTexture));
        if (kind == 1) { g_loggedFirstParticleDraw = true; } else { g_loggedFirstEntityDraw = true; }
    }
    return JNI_TRUE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_banditvault_d3d11mod_nativebridge_D3D11Native_nativeDrawGuiIndexed(
    JNIEnv* env,
    jclass,
    jlong vertexBufferHandle,
    jint vertexStride,
    jlong indexBufferHandle,
    jint indexBytes,
    jint vertexOffset,
    jint firstIndex,
    jint indexCount,
    jint instanceCount,
    jint uvOffset,
    jint colorOffset,
    jint psMode,
    jint blendMode,
    jint scissorX,
    jint scissorY,
    jint scissorWidth,
    jint scissorHeight,
    jlong textureHandle,
    jobject dynamicTransforms,
    jobject projection) {
    if (!g_context || !g_renderTargetView || vertexBufferHandle == 0 || indexBufferHandle == 0 || indexCount <= 0) {
        Log("nativeDrawGuiIndexed rejected context=%p rtv=%p vb=%lld ib=%lld indexCount=%d",
            g_context.Get(), g_renderTargetView.Get(), static_cast<long long>(vertexBufferHandle), static_cast<long long>(indexBufferHandle), static_cast<int>(indexCount));
        return JNI_FALSE;
    }
    if (vertexStride <= 0 || (indexBytes != 2 && indexBytes != 4)) {
        Log("nativeDrawGuiIndexed rejected stride=%d indexBytes=%d", static_cast<int>(vertexStride), static_cast<int>(indexBytes));
        return JNI_FALSE;
    }
    if (colorOffset < 0 || colorOffset + 4 > vertexStride) {
        Log("nativeDrawGuiIndexed rejected colorOffset=%d stride=%d", static_cast<int>(colorOffset), static_cast<int>(vertexStride));
        return JNI_FALSE;
    }
    const bool hasTexture = textureHandle != 0;
    if (hasTexture && (uvOffset < 0 || uvOffset + 8 > vertexStride)) {
        Log("nativeDrawGuiIndexed rejected textured uvOffset=%d stride=%d", static_cast<int>(uvOffset), static_cast<int>(vertexStride));
        return JNI_FALSE;
    }
    if (!CreateGuiResources()) {
        return JNI_FALSE;
    }

    void* dynamicData = env->GetDirectBufferAddress(dynamicTransforms);
    jlong dynamicSize = env->GetDirectBufferCapacity(dynamicTransforms);
    void* projectionData = env->GetDirectBufferAddress(projection);
    jlong projectionSize = env->GetDirectBufferCapacity(projection);
    if (!dynamicData || dynamicSize < 160 || !projectionData || projectionSize < 64) {
        Log("nativeDrawGuiIndexed rejected uniforms dynamic=%p/%lld projection=%p/%lld", dynamicData, static_cast<long long>(dynamicSize), projectionData, static_cast<long long>(projectionSize));
        return JNI_FALSE;
    }

    NativeBuffer vertexNative;
    NativeBuffer indexNative;
    NativeTexture textureNative;
    {
        std::lock_guard<std::mutex> lock(g_resourceMutex);
        auto vb = g_buffers.find(static_cast<int64_t>(vertexBufferHandle));
        auto ib = g_buffers.find(static_cast<int64_t>(indexBufferHandle));
        if (vb == g_buffers.end() || ib == g_buffers.end()) {
            Log("nativeDrawGuiIndexed unknown buffer vb=%lld ib=%lld", static_cast<long long>(vertexBufferHandle), static_cast<long long>(indexBufferHandle));
            return JNI_FALSE;
        }
        vertexNative = vb->second;
        indexNative = ib->second;
        if (hasTexture) {
            auto texture = g_textures.find(static_cast<int64_t>(textureHandle));
            if (texture == g_textures.end()) {
                Log("nativeDrawGuiIndexed unknown texture=%lld", static_cast<long long>(textureHandle));
                return JNI_FALSE;
            }
            textureNative = texture->second;
        }
    }

    ID3D11InputLayout* inputLayout = GetGuiInputLayout(hasTexture, uvOffset, colorOffset);
    if (!inputLayout) {
        return JNI_FALSE;
    }

    if (!g_loggedFirstGuiUniforms) {
        // One-shot sanity snapshot: after the mapBuffer endianness fix these must read as real
        // values (colorMod ~1,1,1,1 and a large negative MV z translation), not denormal ~0.
        const float* dyn = static_cast<const float*>(dynamicData);
        const float* proj = static_cast<const float*>(projectionData);
        Log("gui uniforms MV3=(%.3f,%.3f,%.3f,%.3f) colorMod=(%.3f,%.3f,%.3f,%.3f) P0.x=%.5f P1.y=%.5f P3=(%.3f,%.3f,%.3f,%.3f)",
            dyn[12], dyn[13], dyn[14], dyn[15], dyn[16], dyn[17], dyn[18], dyn[19],
            proj[0], proj[5], proj[12], proj[13], proj[14], proj[15]);
        g_loggedFirstGuiUniforms = true;
    }

    g_context->UpdateSubresource(g_guiDynamicTransformsBuffer.Get(), 0, nullptr, dynamicData, 0, 0);
    g_context->UpdateSubresource(g_guiProjectionBuffer.Get(), 0, nullptr, projectionData, 0, 0);

    g_context->OMSetRenderTargets(1, g_renderTargetView.GetAddressOf(), nullptr);
    D3D11_VIEWPORT viewport = {};
    viewport.Width = static_cast<float>(g_width);
    viewport.Height = static_cast<float>(g_height);
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    g_context->RSSetViewports(1, &viewport);

    ID3D11BlendState* blendState = g_guiBlendState.Get();
    if (blendMode == 1) {
        blendState = g_guiAdditiveBlendState.Get();
    } else if (blendMode == 2) {
        blendState = g_guiPremultipliedBlendState.Get();
    } else if (blendMode == 3) {
        blendState = g_guiOpaqueBlendState.Get();
    } else if (blendMode == 4) {
        blendState = g_guiInvertBlendState.Get();
    } else if (blendMode == 5) {
        blendState = g_guiVignetteBlendState.Get();
    }
    const float blendFactor[] = { 0.0f, 0.0f, 0.0f, 0.0f };
    g_context->OMSetBlendState(blendState, blendFactor, 0xFFFFFFFF);
    const bool scissorEnabled = scissorWidth > 0 && scissorHeight > 0;
    if (scissorEnabled) {
        // Vanilla passes GL-style scissor rects (origin bottom-left, framebuffer pixels);
        // D3D wants top-left, so flip vertically against the current backbuffer height.
        D3D11_RECT scissorRect = {};
        scissorRect.left = scissorX < 0 ? 0 : scissorX;
        scissorRect.right = scissorX + scissorWidth > g_width ? g_width : scissorX + scissorWidth;
        scissorRect.top = g_height - (scissorY + scissorHeight) < 0 ? 0 : g_height - (scissorY + scissorHeight);
        scissorRect.bottom = g_height - scissorY > g_height ? g_height : g_height - scissorY;
        g_context->RSSetScissorRects(1, &scissorRect);
    }
    g_context->RSSetState(scissorEnabled ? g_guiScissorRasterizerState.Get() : g_guiRasterizerState.Get());
    g_context->IASetInputLayout(inputLayout);
    UINT stride = static_cast<UINT>(vertexStride);
    UINT offset = 0;
    ID3D11Buffer* vertexBuffers[] = { vertexNative.buffer.Get() };
    g_context->IASetVertexBuffers(0, 1, vertexBuffers, &stride, &offset);
    g_context->IASetIndexBuffer(indexNative.buffer.Get(), indexBytes == 2 ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT, 0);
    g_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D11Buffer* vsBuffers[] = { g_guiDynamicTransformsBuffer.Get(), g_guiProjectionBuffer.Get() };
    g_context->VSSetConstantBuffers(0, 2, vsBuffers);
    ID3D11Buffer* psBuffers[] = { g_guiDynamicTransformsBuffer.Get() };
    g_context->PSSetConstantBuffers(0, 1, psBuffers);
    g_context->VSSetShader(hasTexture ? g_guiTexturedVertexShader.Get() : g_guiVertexShader.Get(), nullptr, 0);
    ID3D11PixelShader* pixelShader = g_guiPixelShader.Get();
    if (hasTexture) {
        pixelShader = (psMode & 0xFF) == 2 ? g_guiTextPixelShader.Get() : g_guiTexturedPixelShader.Get();  // bit 0x100 = D3D12 linear-sampler hint
    }
    g_context->PSSetShader(pixelShader, nullptr, 0);
    if (hasTexture) {
        ID3D11ShaderResourceView* srvs[] = { textureNative.srv.Get() };
        ID3D11SamplerState* samplers[] = { g_guiSamplerState.Get() };
        g_context->PSSetShaderResources(0, 1, srvs);
        g_context->PSSetSamplers(0, 1, samplers);
    } else {
        ID3D11ShaderResourceView* srvs[] = { nullptr };
        g_context->PSSetShaderResources(0, 1, srvs);
    }

    if (instanceCount > 1) {
        g_context->DrawIndexedInstanced(static_cast<UINT>(indexCount), static_cast<UINT>(instanceCount), static_cast<UINT>(firstIndex), static_cast<INT>(vertexOffset), 0);
    } else {
        g_context->DrawIndexed(static_cast<UINT>(indexCount), static_cast<UINT>(firstIndex), static_cast<INT>(vertexOffset));
    }

    if (!g_loggedFirstGuiDraw) {
        Log("nativeDrawGuiIndexed first draw vb=%lld ib=%lld stride=%d indexBytes=%d firstIndex=%d indexCount=%d vertexOffset=%d instances=%d uvOffset=%d colorOffset=%d psMode=%d blendMode=%d texture=%lld",
            static_cast<long long>(vertexBufferHandle), static_cast<long long>(indexBufferHandle), static_cast<int>(vertexStride), static_cast<int>(indexBytes),
            static_cast<int>(firstIndex), static_cast<int>(indexCount), static_cast<int>(vertexOffset), static_cast<int>(instanceCount),
            static_cast<int>(uvOffset), static_cast<int>(colorOffset), static_cast<int>(psMode), static_cast<int>(blendMode), static_cast<long long>(textureHandle));
        g_loggedFirstGuiDraw = true;
    }
    return JNI_TRUE;
}

extern "C" JNIEXPORT void JNICALL
Java_banditvault_d3d11mod_nativebridge_D3D11Native_nativeShutdown(JNIEnv*, jclass) {
    Log("nativeShutdown");
    {
        std::lock_guard<std::mutex> lock(g_resourceMutex);
        g_buffers.clear();
        g_textures.clear();
        g_nextBufferHandle = 1;
        g_nextTextureHandle = 1;
        g_loggedBufferCreates = 0;
        g_loggedBufferUpdates = 0;
        g_loggedTextureCreates = 0;
        g_loggedTextureUpdates = 0;
    }
    g_textureRtvs.clear();
    g_offscreenDepths.clear();
    g_worldCullNoneCwRasterizerState.Reset();
    g_worldCullBackCwRasterizerState.Reset();
    g_worldCullNoneRasterizerState.Reset();
    g_worldCullBackRasterizerState.Reset();
    g_entityParamsBuffer.Reset();
    g_lightingBuffer.Reset();
    g_particleInputLayout.Reset();
    g_particlePixelShader.Reset();
    g_particleVertexShader.Reset();
    g_entityInputLayout.Reset();
    g_entityPixelShader.Reset();
    g_entityVertexShader.Reset();
    g_loggedFirstEntityDraw = false;
    g_loggedFirstParticleDraw = false;
    g_fanIndexBuffer.Reset();
    g_fanIndexVertexCapacity = 0;
    g_positionTexInputLayout.Reset();
    g_positionColorInputLayout.Reset();
    g_positionInputLayout.Reset();
    g_posTexPixelShader.Reset();
    g_posTexVertexShader.Reset();
    g_starsPixelShader.Reset();
    g_starsVertexShader.Reset();
    g_posColorPixelShader.Reset();
    g_posColorVertexShader.Reset();
    g_skyPixelShader.Reset();
    g_skyVertexShader.Reset();
    g_terrainBatchActive = false;
    std::memset(g_loggedWorldSimpleKinds, 0, sizeof(g_loggedWorldSimpleKinds));
    g_lightmapInfoBuffer.Reset();
    g_lightmapPixelShader.Reset();
    g_screenquadVertexShader.Reset();
    g_terrainAtlasSampler.Reset();
    g_terrainParamsBuffer.Reset();
    g_globalsBuffer.Reset();
    g_fogBuffer.Reset();
    g_chunkSectionBuffer.Reset();
    g_terrainInputLayout.Reset();
    g_terrainPixelShader.Reset();
    g_terrainVertexShader.Reset();
    g_terrainDepthState.Reset();
    g_depthStencilView.Reset();
    g_depthTexture.Reset();
    g_loggedFirstTerrainDraw = false;
    g_loggedFirstLightmapDraw = false;
    g_spritePointSampler.Reset();
    g_spriteInfoBuffer.Reset();
    g_spriteInterpolatePixelShader.Reset();
    g_spriteBlitPixelShader.Reset();
    g_spriteVertexShader.Reset();
    g_loggedSpriteBlits = 0;
    g_guiSamplerState.Reset();
    g_guiScissorRasterizerState.Reset();
    g_guiRasterizerState.Reset();
    g_guiVignetteBlendState.Reset();
    g_guiInvertBlendState.Reset();
    g_guiOpaqueBlendState.Reset();
    g_guiPremultipliedBlendState.Reset();
    g_guiAdditiveBlendState.Reset();
    g_guiBlendState.Reset();
    g_guiProjectionBuffer.Reset();
    g_guiDynamicTransformsBuffer.Reset();
    g_guiInputLayouts.clear();
    g_guiTexturedVsBlob.Reset();
    g_guiVsBlob.Reset();
    g_guiTextPixelShader.Reset();
    g_guiTexturedPixelShader.Reset();
    g_guiTexturedVertexShader.Reset();
    g_guiPixelShader.Reset();
    g_guiVertexShader.Reset();
    g_vertexBuffer.Reset();
    g_inputLayout.Reset();
    g_pixelShader.Reset();
    g_vertexShader.Reset();
    g_renderTargetView.Reset();
    g_swapChain.Reset();
    g_context.Reset();
    g_device.Reset();
    g_window.Reset();
    g_loggedFirstBeginFrame = false;
    g_loggedFirstDraw = false;
    g_loggedFirstPresent = false;
    g_loggedFirstGuiDraw = false;
    g_loggedFirstGuiUniforms = false;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_banditvault_d3d11mod_nativebridge_D3D11Native_nativeDrawPostPass(
    JNIEnv*, jclass, jlong, jlong, jint, jint, jint, jobject, jobject) {
    // D3D12-only; menus simply are not blurred here.
    return JNI_TRUE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_banditvault_d3d11mod_nativebridge_D3D11Native_nativeDrawGlintToTexture(
    JNIEnv*, jclass, jlong, jint, jlong, jint, jint, jint, jint, jlong, jlong, jint, jboolean, jobject, jobject, jobject, jobject) {
    // Glint icon re-bakes are a D3D12-backend feature; no-op success keeps the shared jar safe.
    return JNI_TRUE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_banditvault_d3d11mod_nativebridge_D3D11Native_nativeReadbackTexture(
    JNIEnv*, jclass, jlong, jint, jint, jint, jint, jint, jobject) {
    // D3D12-only; false keeps the legacy warn-once path.
    return JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_banditvault_d3d11mod_nativebridge_D3D11Native_nativeDrawText(
    JNIEnv*, jclass, jlong, jint, jlong, jint, jint, jint, jint, jlong, jlong, jint, jobject, jobject, jobject) {
    // In-world text is a D3D12-backend feature; no-op success keeps the shared jar safe.
    return JNI_TRUE;
}
