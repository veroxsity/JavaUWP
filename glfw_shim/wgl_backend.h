#pragma once
#include <windows.h>
#include <cstdio>
#include <cstdarg>

// Series S mesa 26.1.3 WGL backend. Binding contract proven in MesaTestApp:
// opengl32.dll is the WGL ICD, libgallium_wgl.dll is the megadriver and exports
// GetDC/SetPixelFormat, and the CoreWindow ABI pointer is cast straight to HWND
// for GetDC. LWJGL resolves GL entry points itself via wglGetProcAddress.

namespace wglb {

typedef HGLRC (WINAPI* PFN_wglCreateContext)(HDC);
typedef BOOL  (WINAPI* PFN_wglDeleteContext)(HGLRC);
typedef BOOL  (WINAPI* PFN_wglMakeCurrent)(HDC, HGLRC);
typedef BOOL  (WINAPI* PFN_wglSwapBuffers)(HDC);
typedef PROC  (WINAPI* PFN_wglGetProcAddress)(LPCSTR);
typedef int   (WINAPI* PFN_wglChoosePixelFormat)(HDC, const PIXELFORMATDESCRIPTOR*);
typedef HDC   (WINAPI* PFN_uwpGetDC)(HWND);
typedef BOOL  (WINAPI* PFN_uwpSetPixelFormat)(HDC, int, const PIXELFORMATDESCRIPTOR*);
typedef HGLRC (WINAPI* PFN_wglCreateContextAttribsARB)(HDC, HGLRC, const int*);
typedef BOOL  (WINAPI* PFN_wglSwapIntervalEXT)(int);

static HMODULE s_gl = nullptr;
static HMODULE s_gallium = nullptr;
static HDC s_dc = nullptr;
static HGLRC s_ctx = nullptr;
static bool s_active = false;
static void (*s_log)(const char*) = nullptr;

static PFN_wglCreateContext p_create = nullptr;
static PFN_wglDeleteContext p_delete = nullptr;
static PFN_wglMakeCurrent p_makecur = nullptr;
static PFN_wglSwapBuffers p_swap = nullptr;
static PFN_wglGetProcAddress p_getproc = nullptr;
static PFN_wglChoosePixelFormat p_choosepf = nullptr;
static PFN_uwpGetDC p_getdc = nullptr;
static PFN_uwpSetPixelFormat p_setpf = nullptr;
static PFN_wglCreateContextAttribsARB p_attribs = nullptr;
static PFN_wglSwapIntervalEXT p_swapinterval = nullptr;

static bool LegacyOpenGlContextRequested() {
    wchar_t value[16] = {};
    DWORD len = GetEnvironmentVariableW(L"MC_LEGACY_OPENGL_CONTEXT", value, ARRAYSIZE(value));
    return len > 0 && len < ARRAYSIZE(value) &&
        (value[0] == L'1' || value[0] == L'y' || value[0] == L'Y' ||
         value[0] == L't' || value[0] == L'T');
}

static void Log(const char* fmt, ...) {
    if (!s_log) return;
    char buf[512];
    va_list ap; va_start(ap, fmt);
    _vsnprintf_s(buf, sizeof(buf), _TRUNCATE, fmt, ap);
    va_end(ap);
    s_log(buf);
}

static bool Active() { return s_active; }
static HMODULE OpenGL32() { return s_gl; }

// loadfn is the shim's UWP-safe loader (LoadPackagedLibrary first, file fallback).
static bool Load(HMODULE (*loadfn)(const wchar_t*), void (*logfn)(const char*)) {
    s_log = logfn;
    loadfn(L"dxil.dll"); // DXIL signer; without it mesa emits unsigned DXIL and d3d12 rejects every shader at first draw
    loadfn(L"z-1.dll");
    loadfn(L"spirv_to_dxil.dll");
    loadfn(L"vulkan_dzn.dll");
    s_gallium = loadfn(L"libgallium_wgl.dll");
    s_gl = loadfn(L"opengl32.dll");
    if (!s_gallium || !s_gl) { Log("wgl FATAL gallium/opengl32 load failed"); return false; }

    p_create   = (PFN_wglCreateContext)::GetProcAddress(s_gl, "wglCreateContext");
    p_delete   = (PFN_wglDeleteContext)::GetProcAddress(s_gl, "wglDeleteContext");
    p_makecur  = (PFN_wglMakeCurrent)::GetProcAddress(s_gl, "wglMakeCurrent");
    p_swap     = (PFN_wglSwapBuffers)::GetProcAddress(s_gl, "wglSwapBuffers");
    p_getproc  = (PFN_wglGetProcAddress)::GetProcAddress(s_gl, "wglGetProcAddress");
    p_choosepf = (PFN_wglChoosePixelFormat)::GetProcAddress(s_gl, "wglChoosePixelFormat");
    p_getdc    = (PFN_uwpGetDC)::GetProcAddress(s_gallium, "GetDC");
    p_setpf    = (PFN_uwpSetPixelFormat)::GetProcAddress(s_gallium, "SetPixelFormat");
    if (!p_create || !p_makecur || !p_swap || !p_getproc || !p_getdc) {
        Log("wgl FATAL entry resolve failed");
        return false;
    }
    Log("wgl entries resolved");
    return true;
}

static bool CreateContext(HWND coreWindow) {
    s_dc = p_getdc(coreWindow);
    Log("wgl GetDC(%p) => %p", (void*)coreWindow, (void*)s_dc);
    if (!s_dc) { Log("wgl FATAL GetDC null err=%lu", GetLastError()); return false; }

    PIXELFORMATDESCRIPTOR pfd = {};
    pfd.nSize = sizeof(pfd); pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA; pfd.cColorBits = 32; pfd.cDepthBits = 24; pfd.cStencilBits = 8;
    pfd.iLayerType = PFD_MAIN_PLANE;
    int pf = p_choosepf ? p_choosepf(s_dc, &pfd) : 1;
    if (pf <= 0) pf = 1;
    Log("wgl pixel format = %d", pf);
    if (p_setpf && !p_setpf(s_dc, pf, &pfd)) Log("wgl SetPixelFormat failed err=%lu", GetLastError());

    HGLRC base = p_create(s_dc);
    Log("wgl wglCreateContext => %p err=%lu", (void*)base, base ? 0UL : GetLastError());
    if (!base) return false;
    if (!p_makecur(s_dc, base)) { Log("wgl makecurrent(base) failed err=%lu", GetLastError()); return false; }

    p_attribs = (PFN_wglCreateContextAttribsARB)p_getproc("wglCreateContextAttribsARB");
    s_ctx = base;
    if (p_attribs) {
        const bool legacyContext = LegacyOpenGlContextRequested();
        const int profile = legacyContext ? 0x00000002 : 0x00000001; // compatibility/core
        const int attribs[] = { 0x2091, 3, 0x2092, 2, 0x9126, profile, 0 };
        HGLRC requested = p_attribs(s_dc, nullptr, attribs);
        Log("wgl wglCreateContextAttribsARB(3.2 %s) => %p err=%lu",
            legacyContext ? "compatibility" : "core",
            (void*)requested,
            requested ? 0UL : GetLastError());
        if (requested) {
            p_makecur(nullptr, nullptr);
            if (p_delete) p_delete(base);
            if (!p_makecur(s_dc, requested)) {
                Log("wgl makecurrent(attribs) failed err=%lu", GetLastError());
                return false;
            }
            s_ctx = requested;
        }
    } else {
        Log("wgl wglCreateContextAttribsARB unavailable; keeping base context");
    }
    p_swapinterval = (PFN_wglSwapIntervalEXT)p_getproc("wglSwapIntervalEXT");
    Log("wgl wglSwapIntervalEXT => %p", (void*)p_swapinterval);
    s_active = true;
    return true;
}

static bool MakeCurrent(bool bind) {
    if (!p_makecur) return false;
    return bind ? (p_makecur(s_dc, s_ctx) != 0) : (p_makecur(nullptr, nullptr) != 0);
}
static void Swap() { if (p_swap && s_dc) p_swap(s_dc); }
static bool SetSwapInterval(int interval) {
    if (!p_swapinterval && p_getproc) p_swapinterval = (PFN_wglSwapIntervalEXT)p_getproc("wglSwapIntervalEXT");
    if (!p_swapinterval) { Log("wgl wglSwapIntervalEXT unavailable"); return false; }
    BOOL r = p_swapinterval(interval);
    Log("wgl swap interval set to %d (%s)", interval, r ? "ok" : "fail");
    return r != 0;
}
static void* GetProc(const char* name) {
    void* p = nullptr;
    if (p_getproc) p = (void*)p_getproc(name);
    if (!p && s_gl) p = (void*)::GetProcAddress(s_gl, name);
    return p;
}

} // namespace wglb
