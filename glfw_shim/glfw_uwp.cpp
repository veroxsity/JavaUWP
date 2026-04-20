// glfw_uwp.cpp - GLFW WinRT/EGL shim for Minecraft Java UWP (Xbox Series S)
// Replaces glfw.dll inside lwjgl-glfw-3.3.3-natives-windows.jar.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <roapi.h>
#include <wrl.h>
#include <wrl/wrappers/corewrappers.h>
#include <windows.applicationmodel.core.h>
#include <windows.foundation.h>
#include <windows.foundation.collections.h>
#include <windows.system.h>
#include <windows.ui.core.h>

using namespace Microsoft::WRL;
using namespace Microsoft::WRL::Wrappers;
using namespace ABI::Windows::ApplicationModel::Core;
using namespace ABI::Windows::Foundation;
using namespace ABI::Windows::Foundation::Collections;
using namespace ABI::Windows::System;
using namespace ABI::Windows::UI::Core;

// ---------------------------------------------------------------------------
// Minimal GLFW 3.3.x types
// ---------------------------------------------------------------------------
typedef struct GLFWwindow_  GLFWwindow;
typedef struct GLFWmonitor_ GLFWmonitor;
typedef struct GLFWcursor_  GLFWcursor;
typedef void (*GLFWerrorfun)             (int,const char*);
typedef void (*GLFWwindowclosefun)       (GLFWwindow*);
typedef void (*GLFWwindowsizefun)        (GLFWwindow*,int,int);
typedef void (*GLFWframebuffersizefun)   (GLFWwindow*,int,int);
typedef void (*GLFWwindowposfun)         (GLFWwindow*,int,int);
typedef void (*GLFWwindowfocusfun)       (GLFWwindow*,int);
typedef void (*GLFWwindowiconifyfun)     (GLFWwindow*,int);
typedef void (*GLFWwindowmaximizefun)    (GLFWwindow*,int);
typedef void (*GLFWwindowcontentscalefun)(GLFWwindow*,float,float);
typedef void (*GLFWwindowrefreshfun)     (GLFWwindow*);
typedef void (*GLFWkeyfun)               (GLFWwindow*,int,int,int,int);
typedef void (*GLFWcharfun)              (GLFWwindow*,unsigned int);
typedef void (*GLFWcharmodsfun)          (GLFWwindow*,unsigned int,int);
typedef void (*GLFWmousebuttonfun)       (GLFWwindow*,int,int,int);
typedef void (*GLFWcursorposfun)         (GLFWwindow*,double,double);
typedef void (*GLFWcursorenterfun)       (GLFWwindow*,int);
typedef void (*GLFWscrollfun)            (GLFWwindow*,double,double);
typedef void (*GLFWdropfun)              (GLFWwindow*,int,const char**);
typedef void (*GLFWjoystickfun)          (int,int);
typedef void (*GLFWmonitorfun)           (GLFWmonitor*,int);
typedef struct { int width,height,redBits,greenBits,blueBits,refreshRate; } GLFWvidmode;
typedef struct { unsigned short *red,*green,*blue; unsigned int size; } GLFWgammaramp;
typedef struct { unsigned char *pixels; int width,height; } GLFWimage;
typedef struct { unsigned char buttons[15]; float axes[6]; } GLFWgamepadstate;

#define GLFW_TRUE  1
#define GLFW_FALSE 0
#define GLFW_NO_ERROR 0
#define GLFW_ANY_PLATFORM 0x00060000
#define GLFW_PLATFORM_WIN32 0x00060001
#define GLFW_PRESS 1
#define GLFW_RELEASE 0
#define GLFW_REPEAT 2
#define GLFW_KEY_UNKNOWN            -1
#define GLFW_KEY_SPACE             32
#define GLFW_KEY_0                 48
#define GLFW_KEY_1                 49
#define GLFW_KEY_2                 50
#define GLFW_KEY_3                 51
#define GLFW_KEY_4                 52
#define GLFW_KEY_5                 53
#define GLFW_KEY_6                 54
#define GLFW_KEY_7                 55
#define GLFW_KEY_8                 56
#define GLFW_KEY_9                 57
#define GLFW_KEY_A                 65
#define GLFW_KEY_B                 66
#define GLFW_KEY_C                 67
#define GLFW_KEY_D                 68
#define GLFW_KEY_E                 69
#define GLFW_KEY_F                 70
#define GLFW_KEY_G                 71
#define GLFW_KEY_H                 72
#define GLFW_KEY_I                 73
#define GLFW_KEY_J                 74
#define GLFW_KEY_K                 75
#define GLFW_KEY_L                 76
#define GLFW_KEY_M                 77
#define GLFW_KEY_N                 78
#define GLFW_KEY_O                 79
#define GLFW_KEY_P                 80
#define GLFW_KEY_Q                 81
#define GLFW_KEY_R                 82
#define GLFW_KEY_S                 83
#define GLFW_KEY_T                 84
#define GLFW_KEY_U                 85
#define GLFW_KEY_V                 86
#define GLFW_KEY_W                 87
#define GLFW_KEY_X                 88
#define GLFW_KEY_Y                 89
#define GLFW_KEY_Z                 90
#define GLFW_KEY_ESCAPE           256
#define GLFW_KEY_ENTER            257
#define GLFW_KEY_TAB              258
#define GLFW_KEY_BACKSPACE        259
#define GLFW_KEY_INSERT           260
#define GLFW_KEY_DELETE           261
#define GLFW_KEY_RIGHT            262
#define GLFW_KEY_LEFT             263
#define GLFW_KEY_DOWN             264
#define GLFW_KEY_UP               265
#define GLFW_KEY_PAGE_UP          266
#define GLFW_KEY_PAGE_DOWN        267
#define GLFW_KEY_HOME             268
#define GLFW_KEY_END              269
#define GLFW_KEY_CAPS_LOCK        280
#define GLFW_KEY_SCROLL_LOCK      281
#define GLFW_KEY_NUM_LOCK         282
#define GLFW_KEY_PRINT_SCREEN     283
#define GLFW_KEY_PAUSE            284
#define GLFW_KEY_F1               290
#define GLFW_KEY_F2               291
#define GLFW_KEY_F3               292
#define GLFW_KEY_F4               293
#define GLFW_KEY_F5               294
#define GLFW_KEY_F6               295
#define GLFW_KEY_F7               296
#define GLFW_KEY_F8               297
#define GLFW_KEY_F9               298
#define GLFW_KEY_F10              299
#define GLFW_KEY_F11              300
#define GLFW_KEY_F12              301
#define GLFW_KEY_KP_0             320
#define GLFW_KEY_KP_1             321
#define GLFW_KEY_KP_2             322
#define GLFW_KEY_KP_3             323
#define GLFW_KEY_KP_4             324
#define GLFW_KEY_KP_5             325
#define GLFW_KEY_KP_6             326
#define GLFW_KEY_KP_7             327
#define GLFW_KEY_KP_8             328
#define GLFW_KEY_KP_9             329
#define GLFW_KEY_KP_DECIMAL       330
#define GLFW_KEY_KP_DIVIDE        331
#define GLFW_KEY_KP_MULTIPLY      332
#define GLFW_KEY_KP_SUBTRACT      333
#define GLFW_KEY_KP_ADD           334
#define GLFW_KEY_LEFT_SHIFT       340
#define GLFW_KEY_LEFT_CONTROL     341
#define GLFW_KEY_LEFT_ALT         342
#define GLFW_KEY_LEFT_SUPER       343
#define GLFW_KEY_RIGHT_SHIFT      344
#define GLFW_KEY_RIGHT_CONTROL    345
#define GLFW_KEY_RIGHT_ALT        346
#define GLFW_KEY_RIGHT_SUPER      347
#define GLFW_KEY_MENU             348
#define GLFW_MOD_SHIFT           0x0001
#define GLFW_MOD_CONTROL         0x0002
#define GLFW_MOD_ALT             0x0004
#define GLFW_MOD_SUPER           0x0008
#define GLFW_MOD_CAPS_LOCK       0x0010
#define GLFW_MOD_NUM_LOCK        0x0020
#define GLFW_CLIENT_API            0x00022001
#define GLFW_OPENGL_API            0x00030001
#define GLFW_CONTEXT_VERSION_MAJOR 0x00022002
#define GLFW_CONTEXT_VERSION_MINOR 0x00022003
#define GLFW_OPENGL_PROFILE        0x00022008
#define GLFW_CONTEXT_CREATION_API  0x0002200B
#define GLFW_VISIBLE   0x00020004
#define GLFW_FOCUSED   0x00020001
#define GLFW_ICONIFIED 0x00020002
#define GLFW_MAXIMIZED 0x00020008
#define GLFW_DECORATED 0x00020005
#define GLFW_RESIZABLE 0x00020003
#define GLFW_HOVERED   0x0002000B
#define GLFW_OPENGL_CORE_PROFILE   0x00032001
#define GLFW_NATIVE_CONTEXT_API    0x00036001
#define GLFW_EGL_CONTEXT_API       0x00036002
#define GLFW_CURSOR          0x00033001
#define GLFW_CURSOR_NORMAL   0x00034001
#define GLFW_CURSOR_DISABLED 0x00034003

#define GL_VENDOR   0x1F00
#define GL_RENDERER 0x1F01
#define GL_VERSION  0x1F02

// ---------------------------------------------------------------------------
// Minimal EGL types and constants
// ---------------------------------------------------------------------------
typedef void* EGLDisplay;
typedef void* EGLSurface;
typedef void* EGLContext;
typedef void* EGLConfig;
typedef void* EGLNativeDisplayType;
typedef IInspectable* EGLNativeWindowType;
typedef int32_t EGLint;
typedef int32_t EGLBoolean;
typedef uint32_t EGLenum;

#define EGL_FALSE 0
#define EGL_TRUE 1
#define EGL_DEFAULT_DISPLAY ((EGLNativeDisplayType)0)
#define EGL_NO_DISPLAY ((EGLDisplay)0)
#define EGL_NO_SURFACE ((EGLSurface)0)
#define EGL_NO_CONTEXT ((EGLContext)0)
#define EGL_NONE 0x3038
#define EGL_RED_SIZE 0x3024
#define EGL_GREEN_SIZE 0x3023
#define EGL_BLUE_SIZE 0x3022
#define EGL_ALPHA_SIZE 0x3021
#define EGL_DEPTH_SIZE 0x3025
#define EGL_STENCIL_SIZE 0x3026
#define EGL_SURFACE_TYPE 0x3033
#define EGL_RENDERABLE_TYPE 0x3040
#define EGL_WINDOW_BIT 0x0004
#define EGL_OPENGL_BIT 0x0008
#define EGL_OPENGL_API 0x30A2
#define EGL_VENDOR 0x3053
#define EGL_VERSION 0x3054
#define EGL_CONTEXT_MAJOR_VERSION_KHR 0x3098
#define EGL_CONTEXT_MINOR_VERSION_KHR 0x30FB
#define EGL_CONTEXT_OPENGL_PROFILE_MASK_KHR 0x30FD
#define EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT_KHR 0x00000001

typedef EGLDisplay (WINAPI* PFN_eglGetDisplay)(EGLNativeDisplayType);
typedef EGLDisplay (WINAPI* PFN_eglGetPlatformDisplay)(EGLenum, void*, const EGLint*);
typedef EGLBoolean (WINAPI* PFN_eglInitialize)(EGLDisplay, EGLint*, EGLint*);
typedef EGLBoolean (WINAPI* PFN_eglTerminate)(EGLDisplay);
typedef EGLBoolean (WINAPI* PFN_eglBindAPI)(EGLenum);
typedef EGLBoolean (WINAPI* PFN_eglChooseConfig)(EGLDisplay, const EGLint*, EGLConfig*, EGLint, EGLint*);
typedef EGLSurface (WINAPI* PFN_eglCreateWindowSurface)(EGLDisplay, EGLConfig, EGLNativeWindowType, const EGLint*);
typedef EGLSurface (WINAPI* PFN_eglCreatePlatformWindowSurface)(EGLDisplay, EGLConfig, void*, const EGLint*);
typedef EGLContext (WINAPI* PFN_eglCreateContext)(EGLDisplay, EGLConfig, EGLContext, const EGLint*);
typedef EGLBoolean (WINAPI* PFN_eglDestroyContext)(EGLDisplay, EGLContext);
typedef EGLBoolean (WINAPI* PFN_eglDestroySurface)(EGLDisplay, EGLSurface);
typedef EGLBoolean (WINAPI* PFN_eglMakeCurrent)(EGLDisplay, EGLSurface, EGLSurface, EGLContext);
typedef EGLBoolean (WINAPI* PFN_eglSwapBuffers)(EGLDisplay, EGLSurface);
typedef EGLBoolean (WINAPI* PFN_eglSwapInterval)(EGLDisplay, EGLint);
typedef const char* (WINAPI* PFN_eglQueryString)(EGLDisplay, EGLint);
typedef void* (WINAPI* PFN_eglGetProcAddress)(const char*);
typedef EGLint (WINAPI* PFN_eglGetError)(void);
typedef EGLBoolean (WINAPI* PFN_eglGetConfigAttrib)(EGLDisplay, EGLConfig, EGLint, EGLint*);
typedef const unsigned char* (APIENTRY* PFN_glGetString)(unsigned int);

// ---------------------------------------------------------------------------
// Global state
// ---------------------------------------------------------------------------
static constexpr wchar_t kEGLNativeWindowTypeProperty[] = L"EGLNativeWindowTypeProperty";
static constexpr wchar_t kEGLRenderSurfaceSizeProperty[] = L"EGLRenderSurfaceSizeProperty";

static EGLDisplay g_eglDisplay = EGL_NO_DISPLAY;
static EGLSurface g_eglSurface = EGL_NO_SURFACE;
static EGLContext g_eglContext = EGL_NO_CONTEXT;
static EGLConfig  g_eglConfig = nullptr;
static HMODULE g_libEGL = NULL;
static HMODULE g_opengl32 = NULL;
static BOOL g_initialised = FALSE;
static BOOL g_should_close = FALSE;
static int g_width = 1920;
static int g_height = 1080;
static int g_swap_log_count = 0;
static int g_poll_log_count = 0;
static int g_proc_log_count = 0;
static int g_wait_log_count = 0;
static int g_key_log_count = 0;

static PFN_eglGetDisplay p_eglGetDisplay = nullptr;
static PFN_eglGetPlatformDisplay p_eglGetPlatformDisplay = nullptr;
static PFN_eglInitialize p_eglInitialize = nullptr;
static PFN_eglTerminate p_eglTerminate = nullptr;
static PFN_eglBindAPI p_eglBindAPI = nullptr;
static PFN_eglChooseConfig p_eglChooseConfig = nullptr;
static PFN_eglCreateWindowSurface p_eglCreateWindowSurface = nullptr;
static PFN_eglCreatePlatformWindowSurface p_eglCreatePlatformWindowSurface = nullptr;
static PFN_eglCreateContext p_eglCreateContext = nullptr;
static PFN_eglDestroyContext p_eglDestroyContext = nullptr;
static PFN_eglDestroySurface p_eglDestroySurface = nullptr;
static PFN_eglMakeCurrent p_eglMakeCurrent = nullptr;
static PFN_eglSwapBuffers p_eglSwapBuffers = nullptr;
static PFN_eglSwapInterval p_eglSwapInterval = nullptr;
static PFN_eglQueryString p_eglQueryString = nullptr;
static PFN_eglGetProcAddress p_eglGetProcAddress = nullptr;
static PFN_eglGetError p_eglGetError = nullptr;
static PFN_eglGetConfigAttrib p_eglGetConfigAttrib = nullptr;

static GLFWerrorfun           g_error_cb       = NULL;
static GLFWframebuffersizefun g_fbsize_cb      = NULL;
static GLFWwindowposfun       g_winpos_cb      = NULL;
static GLFWwindowsizefun      g_winsize_cb     = NULL;
static GLFWwindowclosefun     g_winclose_cb    = NULL;
static GLFWwindowrefreshfun   g_winrefresh_cb  = NULL;
static GLFWkeyfun             g_key_cb         = NULL;
static GLFWcharfun            g_char_cb        = NULL;
static GLFWcharmodsfun        g_charmods_cb    = NULL;
static GLFWmousebuttonfun     g_mousebutton_cb = NULL;
static GLFWcursorposfun       g_cursorpos_cb   = NULL;
static GLFWscrollfun          g_scroll_cb      = NULL;
static GLFWcursorenterfun     g_cursorenter_cb = NULL;
static GLFWwindowfocusfun     g_focus_cb       = NULL;
static GLFWwindowiconifyfun   g_iconify_cb     = NULL;
static GLFWwindowmaximizefun  g_maximize_cb    = NULL;
static GLFWwindowcontentscalefun g_contentscale_cb = NULL;
static GLFWdropfun            g_drop_cb        = NULL;
static GLFWjoystickfun        g_joystick_cb    = NULL;
static GLFWmonitorfun         g_monitor_cb     = NULL;
using CoreWindowKeyHandler = ABI::Windows::Foundation::__FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CKeyEventArgs_t;
using CoreWindowCharHandler = ABI::Windows::Foundation::__FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CCharacterReceivedEventArgs_t;
static ComPtr<CoreWindowKeyHandler> g_keyDownHandler;
static ComPtr<CoreWindowKeyHandler> g_keyUpHandler;
static ComPtr<CoreWindowCharHandler> g_charReceivedHandler;
static EventRegistrationToken g_keyDownToken = {};
static EventRegistrationToken g_keyUpToken = {};
static EventRegistrationToken g_charReceivedToken = {};
static bool g_keyboardHooksInstalled = false;
static unsigned char g_key_state[512] = {};

static ComPtr<ICoreWindow> g_coreWindow;
static ComPtr<ICoreDispatcher> g_dispatcher;
static ComPtr<IInspectable> g_nativeWindowPropertySet;

struct FakeWindow {
    DWORD magic;
    int width;
    int height;
    BOOL should_close;
    void* user_pointer;
};
static FakeWindow g_fake_window;
static GLFWvidmode g_vidmode = {1920,1080,8,8,8,60};

// ---------------------------------------------------------------------------
// Logging
// ---------------------------------------------------------------------------
static wchar_t g_log_path[MAX_PATH];

static void ShimLog(const char* fmt, ...) {
    if (!g_log_path[0]) return;
    FILE* f = NULL;
    _wfopen_s(&f, g_log_path, L"a");
    if (!f) return;
    SYSTEMTIME st;
    GetLocalTime(&st);
    fprintf(f,"[%02d:%02d:%02d.%03d] [glfw_uwp] ",st.wHour,st.wMinute,st.wSecond,st.wMilliseconds);
    va_list va;
    va_start(va,fmt);
    vfprintf(f,fmt,va);
    va_end(va);
    fprintf(f,"\n");
    fclose(f);
}

static void GetExeDir(wchar_t* out, int cch) {
    GetModuleFileNameW(NULL,out,cch);
    wchar_t* sl = wcsrchr(out,L'\\');
    if (sl) *sl = L'\0';
}

static void GetRuntimeDir(wchar_t* out, int cch) {
    DWORD len = GetEnvironmentVariableW(L"MC_RUNTIME_DIR", out, cch);
    if (len > 0 && len < (DWORD)cch) return;
    GetExeDir(out, cch);
}

static HMODULE LoadPackagedOrFile(const wchar_t* packagedPath, const wchar_t* absolutePath, const char* label) {
    HMODULE module = nullptr;
    if (packagedPath && *packagedPath) {
        module = LoadPackagedLibrary(packagedPath, 0);
        if (module) {
            ShimLog("LoadPackagedLibrary(%s) => %p", label, (void*)module);
            return module;
        }
        ShimLog("LoadPackagedLibrary(%s) failed err=%u", label, GetLastError());
    }

    if (absolutePath && *absolutePath) {
        module = LoadLibraryW(absolutePath);
        if (module) {
            ShimLog("LoadLibraryW(%s) => %p", label, (void*)module);
            return module;
        }
        ShimLog("LoadLibraryW(%s) failed err=%u", label, GetLastError());
    }

    return nullptr;
}

static FARPROC ResolveProc(HMODULE module, const char* name) {
    FARPROC proc = module ? GetProcAddress(module, name) : NULL;
    ShimLog("  %s=%p", name, (void*)proc);
    return proc;
}

static void ReportEglError(const char* label) {
    const EGLint err = p_eglGetError ? p_eglGetError() : 0;
    ShimLog("%s failed eglError=0x%04X", label, err);
}

static int MapVirtualKeyToGlfw(VirtualKey key) {
    switch (key) {
    case VirtualKey_Space: return GLFW_KEY_SPACE;
    case VirtualKey_Number0: return GLFW_KEY_0;
    case VirtualKey_Number1: return GLFW_KEY_1;
    case VirtualKey_Number2: return GLFW_KEY_2;
    case VirtualKey_Number3: return GLFW_KEY_3;
    case VirtualKey_Number4: return GLFW_KEY_4;
    case VirtualKey_Number5: return GLFW_KEY_5;
    case VirtualKey_Number6: return GLFW_KEY_6;
    case VirtualKey_Number7: return GLFW_KEY_7;
    case VirtualKey_Number8: return GLFW_KEY_8;
    case VirtualKey_Number9: return GLFW_KEY_9;
    case VirtualKey_A: return GLFW_KEY_A;
    case VirtualKey_B: return GLFW_KEY_B;
    case VirtualKey_C: return GLFW_KEY_C;
    case VirtualKey_D: return GLFW_KEY_D;
    case VirtualKey_E: return GLFW_KEY_E;
    case VirtualKey_F: return GLFW_KEY_F;
    case VirtualKey_G: return GLFW_KEY_G;
    case VirtualKey_H: return GLFW_KEY_H;
    case VirtualKey_I: return GLFW_KEY_I;
    case VirtualKey_J: return GLFW_KEY_J;
    case VirtualKey_K: return GLFW_KEY_K;
    case VirtualKey_L: return GLFW_KEY_L;
    case VirtualKey_M: return GLFW_KEY_M;
    case VirtualKey_N: return GLFW_KEY_N;
    case VirtualKey_O: return GLFW_KEY_O;
    case VirtualKey_P: return GLFW_KEY_P;
    case VirtualKey_Q: return GLFW_KEY_Q;
    case VirtualKey_R: return GLFW_KEY_R;
    case VirtualKey_S: return GLFW_KEY_S;
    case VirtualKey_T: return GLFW_KEY_T;
    case VirtualKey_U: return GLFW_KEY_U;
    case VirtualKey_V: return GLFW_KEY_V;
    case VirtualKey_W: return GLFW_KEY_W;
    case VirtualKey_X: return GLFW_KEY_X;
    case VirtualKey_Y: return GLFW_KEY_Y;
    case VirtualKey_Z: return GLFW_KEY_Z;
    case VirtualKey_Left: return GLFW_KEY_LEFT;
    case VirtualKey_Right: return GLFW_KEY_RIGHT;
    case VirtualKey_Up: return GLFW_KEY_UP;
    case VirtualKey_Down: return GLFW_KEY_DOWN;
    case VirtualKey_Tab: return GLFW_KEY_TAB;
    case VirtualKey_Enter: return GLFW_KEY_ENTER;
    case VirtualKey_Back: return GLFW_KEY_BACKSPACE;
    case VirtualKey_Escape: return GLFW_KEY_ESCAPE;
    case VirtualKey_PageUp: return GLFW_KEY_PAGE_UP;
    case VirtualKey_PageDown: return GLFW_KEY_PAGE_DOWN;
    case VirtualKey_Home: return GLFW_KEY_HOME;
    case VirtualKey_End: return GLFW_KEY_END;
    case VirtualKey_Insert: return GLFW_KEY_INSERT;
    case VirtualKey_Delete: return GLFW_KEY_DELETE;
    case VirtualKey_CapitalLock: return GLFW_KEY_CAPS_LOCK;
    case VirtualKey_Scroll: return GLFW_KEY_SCROLL_LOCK;
    case VirtualKey_NumberKeyLock: return GLFW_KEY_NUM_LOCK;
    case VirtualKey_Snapshot: return GLFW_KEY_PRINT_SCREEN;
    case VirtualKey_Pause: return GLFW_KEY_PAUSE;
    case VirtualKey_F1: return GLFW_KEY_F1;
    case VirtualKey_F2: return GLFW_KEY_F2;
    case VirtualKey_F3: return GLFW_KEY_F3;
    case VirtualKey_F4: return GLFW_KEY_F4;
    case VirtualKey_F5: return GLFW_KEY_F5;
    case VirtualKey_F6: return GLFW_KEY_F6;
    case VirtualKey_F7: return GLFW_KEY_F7;
    case VirtualKey_F8: return GLFW_KEY_F8;
    case VirtualKey_F9: return GLFW_KEY_F9;
    case VirtualKey_F10: return GLFW_KEY_F10;
    case VirtualKey_F11: return GLFW_KEY_F11;
    case VirtualKey_F12: return GLFW_KEY_F12;
    case VirtualKey_NumberPad0: return GLFW_KEY_KP_0;
    case VirtualKey_NumberPad1: return GLFW_KEY_KP_1;
    case VirtualKey_NumberPad2: return GLFW_KEY_KP_2;
    case VirtualKey_NumberPad3: return GLFW_KEY_KP_3;
    case VirtualKey_NumberPad4: return GLFW_KEY_KP_4;
    case VirtualKey_NumberPad5: return GLFW_KEY_KP_5;
    case VirtualKey_NumberPad6: return GLFW_KEY_KP_6;
    case VirtualKey_NumberPad7: return GLFW_KEY_KP_7;
    case VirtualKey_NumberPad8: return GLFW_KEY_KP_8;
    case VirtualKey_NumberPad9: return GLFW_KEY_KP_9;
    case VirtualKey_Decimal: return GLFW_KEY_KP_DECIMAL;
    case VirtualKey_Divide: return GLFW_KEY_KP_DIVIDE;
    case VirtualKey_Multiply: return GLFW_KEY_KP_MULTIPLY;
    case VirtualKey_Subtract: return GLFW_KEY_KP_SUBTRACT;
    case VirtualKey_Add: return GLFW_KEY_KP_ADD;
    case VirtualKey_LeftShift: return GLFW_KEY_LEFT_SHIFT;
    case VirtualKey_RightShift: return GLFW_KEY_RIGHT_SHIFT;
    case VirtualKey_LeftControl: return GLFW_KEY_LEFT_CONTROL;
    case VirtualKey_RightControl: return GLFW_KEY_RIGHT_CONTROL;
    case VirtualKey_LeftMenu: return GLFW_KEY_LEFT_ALT;
    case VirtualKey_RightMenu: return GLFW_KEY_RIGHT_ALT;
    case VirtualKey_LeftWindows: return GLFW_KEY_LEFT_SUPER;
    case VirtualKey_RightWindows: return GLFW_KEY_RIGHT_SUPER;
    case VirtualKey_Menu: return GLFW_KEY_MENU;
    default:
        return GLFW_KEY_UNKNOWN;
    }
}

static int CurrentGlfwMods() {
    int mods = 0;
    if (g_key_state[GLFW_KEY_LEFT_SHIFT] || g_key_state[GLFW_KEY_RIGHT_SHIFT]) mods |= GLFW_MOD_SHIFT;
    if (g_key_state[GLFW_KEY_LEFT_CONTROL] || g_key_state[GLFW_KEY_RIGHT_CONTROL]) mods |= GLFW_MOD_CONTROL;
    if (g_key_state[GLFW_KEY_LEFT_ALT] || g_key_state[GLFW_KEY_RIGHT_ALT]) mods |= GLFW_MOD_ALT;
    if (g_key_state[GLFW_KEY_LEFT_SUPER] || g_key_state[GLFW_KEY_RIGHT_SUPER]) mods |= GLFW_MOD_SUPER;
    if (g_key_state[GLFW_KEY_CAPS_LOCK]) mods |= GLFW_MOD_CAPS_LOCK;
    if (g_key_state[GLFW_KEY_NUM_LOCK]) mods |= GLFW_MOD_NUM_LOCK;
    return mods;
}

static void UpdateKeyState(int key, int action) {
    if (key < 0 || key >= (int)sizeof(g_key_state)) return;
    g_key_state[key] = (action == GLFW_RELEASE) ? GLFW_RELEASE : GLFW_PRESS;
}

static void DispatchKeyEvent(VirtualKey virtualKey, const CorePhysicalKeyStatus& status, int action) {
    const int glfwKey = MapVirtualKeyToGlfw(virtualKey);
    if (glfwKey == GLFW_KEY_UNKNOWN) return;

    const int glfwAction = (action == GLFW_PRESS && status.WasKeyDown) ? GLFW_REPEAT : action;
    UpdateKeyState(glfwKey, glfwAction);
    const int mods = CurrentGlfwMods();
    if (g_key_log_count < 24) {
        ++g_key_log_count;
        ShimLog("Key event vk=%d glfw=%d action=%d scancode=%u mods=0x%X repeat=%u",
            (int)virtualKey, glfwKey, glfwAction, status.ScanCode, mods, status.RepeatCount);
    }
    if (g_key_cb) {
        g_key_cb((GLFWwindow*)&g_fake_window, glfwKey, (int)status.ScanCode, glfwAction, mods);
    }
}

static void DispatchCharEvent(unsigned int codepoint) {
    if (codepoint == 0) return;
    if (g_char_cb) {
        g_char_cb((GLFWwindow*)&g_fake_window, codepoint);
    }
    if (g_charmods_cb) {
        g_charmods_cb((GLFWwindow*)&g_fake_window, codepoint, CurrentGlfwMods());
    }
}

static bool InstallKeyboardHooks() {
    if (g_keyboardHooksInstalled) return true;
    if (!g_coreWindow) return false;

    g_keyDownHandler = Callback<CoreWindowKeyHandler>(
        [](ICoreWindow*, IKeyEventArgs* args) -> HRESULT {
            if (!args) return S_OK;
            VirtualKey virtualKey = VirtualKey_None;
            CorePhysicalKeyStatus status = {};
            args->get_VirtualKey(&virtualKey);
            args->get_KeyStatus(&status);
            DispatchKeyEvent(virtualKey, status, GLFW_PRESS);
            return S_OK;
        });
    g_keyUpHandler = Callback<CoreWindowKeyHandler>(
        [](ICoreWindow*, IKeyEventArgs* args) -> HRESULT {
            if (!args) return S_OK;
            VirtualKey virtualKey = VirtualKey_None;
            CorePhysicalKeyStatus status = {};
            args->get_VirtualKey(&virtualKey);
            args->get_KeyStatus(&status);
            DispatchKeyEvent(virtualKey, status, GLFW_RELEASE);
            return S_OK;
        });
    g_charReceivedHandler = Callback<CoreWindowCharHandler>(
        [](ICoreWindow*, ICharacterReceivedEventArgs* args) -> HRESULT {
            if (!args) return S_OK;
            UINT32 codepoint = 0;
            args->get_KeyCode(&codepoint);
            DispatchCharEvent(codepoint);
            return S_OK;
        });

    HRESULT hr = g_coreWindow->add_KeyDown(g_keyDownHandler.Get(), &g_keyDownToken);
    if (FAILED(hr)) {
        ShimLog("add_KeyDown failed hr=0x%08X", hr);
        return false;
    }
    hr = g_coreWindow->add_KeyUp(g_keyUpHandler.Get(), &g_keyUpToken);
    if (FAILED(hr)) {
        ShimLog("add_KeyUp failed hr=0x%08X", hr);
        g_coreWindow->remove_KeyDown(g_keyDownToken);
        ZeroMemory(&g_keyDownToken, sizeof(g_keyDownToken));
        return false;
    }
    hr = g_coreWindow->add_CharacterReceived(g_charReceivedHandler.Get(), &g_charReceivedToken);
    if (FAILED(hr)) {
        ShimLog("add_CharacterReceived failed hr=0x%08X", hr);
        g_coreWindow->remove_KeyDown(g_keyDownToken);
        g_coreWindow->remove_KeyUp(g_keyUpToken);
        ZeroMemory(&g_keyDownToken, sizeof(g_keyDownToken));
        ZeroMemory(&g_keyUpToken, sizeof(g_keyUpToken));
        return false;
    }

    g_keyboardHooksInstalled = true;
    ShimLog("CoreWindow keyboard hooks installed");
    return true;
}

// ---------------------------------------------------------------------------
// CoreWindow access
// ---------------------------------------------------------------------------
static bool AcquireCoreWindow() {
    if (g_coreWindow) return true;

    ComPtr<ICoreApplication> coreApp;
    HRESULT hr = GetActivationFactory(
        HStringReference(RuntimeClass_Windows_ApplicationModel_Core_CoreApplication).Get(),
        &coreApp);
    if (SUCCEEDED(hr)) {
        ComPtr<IPropertySet> props;
        hr = coreApp->get_Properties(props.GetAddressOf());
        if (SUCCEEDED(hr)) {
            ComPtr<IMap<HSTRING, IInspectable*>> propMap;
            hr = props.As(&propMap);
            if (SUCCEEDED(hr)) {
                boolean hasWindow = false;
                hr = propMap->HasKey(HStringReference(kEGLNativeWindowTypeProperty).Get(), &hasWindow);
                if (SUCCEEDED(hr) && hasWindow) {
                    ComPtr<IInspectable> inspectable;
                    hr = propMap->Lookup(HStringReference(kEGLNativeWindowTypeProperty).Get(), inspectable.GetAddressOf());
                    if (SUCCEEDED(hr) && inspectable) {
                        hr = inspectable.As(&g_coreWindow);
                        if (SUCCEEDED(hr) && g_coreWindow) {
                            ShimLog("CoreWindow acquired from CoreApplication properties");
                        }
                    }
                }
            }
        }
    }

    if (!g_coreWindow) {
        ComPtr<ICoreWindowStatic> coreWindowStatic;
        hr = GetActivationFactory(
            HStringReference(RuntimeClass_Windows_UI_Core_CoreWindow).Get(),
            &coreWindowStatic);
        if (SUCCEEDED(hr)) {
            coreWindowStatic->GetForCurrentThread(g_coreWindow.GetAddressOf());
            if (g_coreWindow) {
                ShimLog("CoreWindow acquired from GetForCurrentThread");
            }
        }
    }

    if (!g_coreWindow) {
        ShimLog("No CoreWindow available");
        return false;
    }

    g_coreWindow->get_Dispatcher(g_dispatcher.GetAddressOf());
    InstallKeyboardHooks();
    return true;
}

static void RefreshWindowMetrics(bool fireCallbacks) {
    if (!AcquireCoreWindow()) return;

    Rect bounds = {};
    if (FAILED(g_coreWindow->get_Bounds(&bounds))) return;

    const int newWidth = bounds.Width > 0 ? (int)bounds.Width : g_width;
    const int newHeight = bounds.Height > 0 ? (int)bounds.Height : g_height;
    if (newWidth == g_width && newHeight == g_height) return;

    g_width = newWidth;
    g_height = newHeight;
    g_vidmode.width = g_width;
    g_vidmode.height = g_height;
    g_fake_window.width = g_width;
    g_fake_window.height = g_height;
    ShimLog("Window size %dx%d", g_width, g_height);

    if (fireCallbacks) {
        if (g_winsize_cb) g_winsize_cb((GLFWwindow*)&g_fake_window, g_width, g_height);
        if (g_fbsize_cb) g_fbsize_cb((GLFWwindow*)&g_fake_window, g_width, g_height);
    }
}

static bool BuildNativeWindowPropertySet() {
    if (g_nativeWindowPropertySet) return true;
    if (!AcquireCoreWindow()) return false;

    ComPtr<IActivationFactory> propSetFactory;
    HRESULT hr = GetActivationFactory(
        HStringReference(RuntimeClass_Windows_Foundation_Collections_PropertySet).Get(),
        &propSetFactory);
    if (FAILED(hr)) {
        ShimLog("PropertySet factory failed hr=0x%08X", hr);
        return false;
    }

    ComPtr<IInspectable> propertySetInspectable;
    hr = propSetFactory->ActivateInstance(propertySetInspectable.GetAddressOf());
    if (FAILED(hr)) {
        ShimLog("PropertySet ActivateInstance failed hr=0x%08X", hr);
        return false;
    }

    ComPtr<IPropertySet> propertySet;
    hr = propertySetInspectable.As(&propertySet);
    if (FAILED(hr)) {
        ShimLog("PropertySet As(IPropertySet) failed hr=0x%08X", hr);
        return false;
    }

    ComPtr<IMap<HSTRING, IInspectable*>> propMap;
    hr = propertySet.As(&propMap);
    if (FAILED(hr)) {
        ShimLog("PropertySet As(IMap) failed hr=0x%08X", hr);
        return false;
    }

    RefreshWindowMetrics(false);

    boolean replaced = false;
    hr = propMap->Insert(HStringReference(kEGLNativeWindowTypeProperty).Get(), g_coreWindow.Get(), &replaced);
    if (FAILED(hr)) {
        ShimLog("Insert(EGLNativeWindowTypeProperty) failed hr=0x%08X", hr);
        return false;
    }

    ComPtr<IPropertyValueStatics> propertyValueStatics;
    hr = GetActivationFactory(
        HStringReference(RuntimeClass_Windows_Foundation_PropertyValue).Get(),
        &propertyValueStatics);
    if (FAILED(hr)) {
        ShimLog("PropertyValue factory failed hr=0x%08X", hr);
        return false;
    }

    Size size = {};
    size.Width = (FLOAT)g_width;
    size.Height = (FLOAT)g_height;

    ComPtr<IInspectable> sizeInspectable;
    hr = propertyValueStatics->CreateSize(size, sizeInspectable.GetAddressOf());
    if (FAILED(hr)) {
        ShimLog("CreateSize failed hr=0x%08X", hr);
        return false;
    }

    hr = propMap->Insert(HStringReference(kEGLRenderSurfaceSizeProperty).Get(), sizeInspectable.Get(), &replaced);
    if (FAILED(hr)) {
        ShimLog("Insert(EGLRenderSurfaceSizeProperty) failed hr=0x%08X", hr);
        return false;
    }

    propertySet.As(&g_nativeWindowPropertySet);
    ShimLog("Created EGL PropertySet surface descriptor (%dx%d)", g_width, g_height);
    return true;
}

// ---------------------------------------------------------------------------
// Mesa EGL loader
// ---------------------------------------------------------------------------
static bool LoadMesaEGL() {
    if (g_libEGL && g_opengl32) return true;

    wchar_t exeDir[MAX_PATH];
    GetExeDir(exeDir, MAX_PATH);

    wchar_t eglPath[MAX_PATH];
    wchar_t glPath[MAX_PATH];
    swprintf_s(eglPath, L"%s\\libEGL.dll", exeDir);
    swprintf_s(glPath, L"%s\\opengl32.dll", exeDir);
    const wchar_t* eglPackaged = L"libEGL.dll";
    const wchar_t* glPackaged = L"opengl32.dll";
    if (GetFileAttributesW(eglPath) == INVALID_FILE_ATTRIBUTES) {
        swprintf_s(eglPath, L"%s\\natives\\libEGL.dll", exeDir);
        eglPackaged = L"natives\\libEGL.dll";
    }
    if (GetFileAttributesW(glPath) == INVALID_FILE_ATTRIBUTES) {
        swprintf_s(glPath, L"%s\\natives\\opengl32.dll", exeDir);
        glPackaged = L"natives\\opengl32.dll";
    }

    ShimLog("libEGL=%S", eglPath);
    ShimLog("opengl32=%S", glPath);

    wchar_t glapiPath[MAX_PATH];
    wchar_t galliumPath[MAX_PATH];
    wchar_t glesPath[MAX_PATH];
    wchar_t dxilPath[MAX_PATH];
    wchar_t zlibPath[MAX_PATH];
    swprintf_s(glapiPath, L"%s\\libglapi.dll", exeDir);
    swprintf_s(galliumPath, L"%s\\libgallium_wgl.dll", exeDir);
    swprintf_s(glesPath, L"%s\\libGLESv2.dll", exeDir);
    swprintf_s(dxilPath, L"%s\\dxil.dll", exeDir);
    swprintf_s(zlibPath, L"%s\\z-1.dll", exeDir);
    const wchar_t* glapiPackaged = L"libglapi.dll";
    const wchar_t* galliumPackaged = L"libgallium_wgl.dll";
    const wchar_t* glesPackaged = L"libGLESv2.dll";
    const wchar_t* dxilPackaged = L"dxil.dll";
    const wchar_t* zlibPackaged = L"z-1.dll";
    if (GetFileAttributesW(glapiPath) == INVALID_FILE_ATTRIBUTES) {
        swprintf_s(glapiPath, L"%s\\natives\\libglapi.dll", exeDir);
        glapiPackaged = L"natives\\libglapi.dll";
    }
    if (GetFileAttributesW(galliumPath) == INVALID_FILE_ATTRIBUTES) {
        swprintf_s(galliumPath, L"%s\\natives\\libgallium_wgl.dll", exeDir);
        galliumPackaged = L"natives\\libgallium_wgl.dll";
    }
    if (GetFileAttributesW(glesPath) == INVALID_FILE_ATTRIBUTES) {
        swprintf_s(glesPath, L"%s\\natives\\libGLESv2.dll", exeDir);
        glesPackaged = L"natives\\libGLESv2.dll";
    }
    if (GetFileAttributesW(dxilPath) == INVALID_FILE_ATTRIBUTES) {
        swprintf_s(dxilPath, L"%s\\natives\\dxil.dll", exeDir);
        dxilPackaged = L"natives\\dxil.dll";
    }
    if (GetFileAttributesW(zlibPath) == INVALID_FILE_ATTRIBUTES) {
        swprintf_s(zlibPath, L"%s\\natives\\z-1.dll", exeDir);
        zlibPackaged = L"natives\\z-1.dll";
    }

    // Preload Mesa siblings so package-local dependency resolution works.
    LoadPackagedOrFile(glapiPackaged, glapiPath, "libglapi.dll");
    LoadPackagedOrFile(zlibPackaged, zlibPath, "z-1.dll");
    LoadPackagedOrFile(galliumPackaged, galliumPath, "libgallium_wgl.dll");
    LoadPackagedOrFile(glesPackaged, glesPath, "libGLESv2.dll");
    LoadPackagedOrFile(dxilPackaged, dxilPath, "dxil.dll");

    g_opengl32 = LoadPackagedOrFile(glPackaged, glPath, "opengl32.dll");
    g_libEGL = LoadPackagedOrFile(eglPackaged, eglPath, "libEGL.dll");
    if (!g_opengl32 || !g_libEGL) {
        ShimLog("Graphics loader failed gl=%p egl=%p err=%u", g_opengl32, g_libEGL, GetLastError());
        return false;
    }

    p_eglGetDisplay = (PFN_eglGetDisplay)ResolveProc(g_libEGL, "eglGetDisplay");
    p_eglGetPlatformDisplay = (PFN_eglGetPlatformDisplay)ResolveProc(g_libEGL, "eglGetPlatformDisplay");
    p_eglInitialize = (PFN_eglInitialize)ResolveProc(g_libEGL, "eglInitialize");
    p_eglTerminate = (PFN_eglTerminate)ResolveProc(g_libEGL, "eglTerminate");
    p_eglBindAPI = (PFN_eglBindAPI)ResolveProc(g_libEGL, "eglBindAPI");
    p_eglChooseConfig = (PFN_eglChooseConfig)ResolveProc(g_libEGL, "eglChooseConfig");
    p_eglCreateWindowSurface = (PFN_eglCreateWindowSurface)ResolveProc(g_libEGL, "eglCreateWindowSurface");
    p_eglCreatePlatformWindowSurface = (PFN_eglCreatePlatformWindowSurface)ResolveProc(g_libEGL, "eglCreatePlatformWindowSurface");
    p_eglCreateContext = (PFN_eglCreateContext)ResolveProc(g_libEGL, "eglCreateContext");
    p_eglDestroyContext = (PFN_eglDestroyContext)ResolveProc(g_libEGL, "eglDestroyContext");
    p_eglDestroySurface = (PFN_eglDestroySurface)ResolveProc(g_libEGL, "eglDestroySurface");
    p_eglMakeCurrent = (PFN_eglMakeCurrent)ResolveProc(g_libEGL, "eglMakeCurrent");
    p_eglSwapBuffers = (PFN_eglSwapBuffers)ResolveProc(g_libEGL, "eglSwapBuffers");
    p_eglSwapInterval = (PFN_eglSwapInterval)ResolveProc(g_libEGL, "eglSwapInterval");
    p_eglQueryString = (PFN_eglQueryString)ResolveProc(g_libEGL, "eglQueryString");
    p_eglGetProcAddress = (PFN_eglGetProcAddress)ResolveProc(g_libEGL, "eglGetProcAddress");
    p_eglGetError = (PFN_eglGetError)ResolveProc(g_libEGL, "eglGetError");
    p_eglGetConfigAttrib = (PFN_eglGetConfigAttrib)ResolveProc(g_libEGL, "eglGetConfigAttrib");

    if (!p_eglGetDisplay || !p_eglInitialize || !p_eglBindAPI || !p_eglChooseConfig ||
        !p_eglCreateWindowSurface || !p_eglCreateContext || !p_eglMakeCurrent ||
        !p_eglSwapBuffers || !p_eglGetProcAddress) {
        ShimLog("Critical EGL exports missing");
        return false;
    }

    return true;
}

static bool CreateEglContext() {
    if (g_eglContext != EGL_NO_CONTEXT) return true;
    if (!LoadMesaEGL() || !AcquireCoreWindow()) return false;

    g_eglDisplay = p_eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (g_eglDisplay == EGL_NO_DISPLAY && p_eglGetPlatformDisplay) {
        g_eglDisplay = p_eglGetPlatformDisplay(0, EGL_DEFAULT_DISPLAY, nullptr);
    }
    if (g_eglDisplay == EGL_NO_DISPLAY) {
        ReportEglError("eglGetDisplay");
        return false;
    }

    EGLint major = 0;
    EGLint minor = 0;
    if (!p_eglInitialize(g_eglDisplay, &major, &minor)) {
        ReportEglError("eglInitialize");
        return false;
    }

    if (!p_eglBindAPI(EGL_OPENGL_API)) {
        ReportEglError("eglBindAPI(EGL_OPENGL_API)");
        return false;
    }

    const EGLint configAttrs[] = {
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_DEPTH_SIZE, 24,
        EGL_STENCIL_SIZE, 8,
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
        EGL_NONE
    };

    EGLint numConfigs = 0;
    if (!p_eglChooseConfig(g_eglDisplay, configAttrs, &g_eglConfig, 1, &numConfigs) || numConfigs < 1) {
        ReportEglError("eglChooseConfig");
        return false;
    }

    // Prefer the raw CoreWindow for Mesa's UWP EGL backend.
    g_eglSurface = p_eglCreateWindowSurface(g_eglDisplay, g_eglConfig,
        reinterpret_cast<EGLNativeWindowType>(g_coreWindow.Get()), nullptr);
    if (g_eglSurface != EGL_NO_SURFACE) {
        ShimLog("eglCreateWindowSurface(CoreWindow) succeeded");
    } else {
        ReportEglError("eglCreateWindowSurface(CoreWindow)");
    }

    if (g_eglSurface == EGL_NO_SURFACE && p_eglCreatePlatformWindowSurface) {
        g_eglSurface = p_eglCreatePlatformWindowSurface(g_eglDisplay, g_eglConfig,
            g_coreWindow.Get(), nullptr);
        if (g_eglSurface != EGL_NO_SURFACE) {
            ShimLog("eglCreatePlatformWindowSurface(CoreWindow) succeeded");
        } else {
            ReportEglError("eglCreatePlatformWindowSurface(CoreWindow)");
        }
    }

    if (g_eglSurface == EGL_NO_SURFACE) {
        if (!BuildNativeWindowPropertySet()) return false;

        g_eglSurface = p_eglCreateWindowSurface(g_eglDisplay, g_eglConfig,
            reinterpret_cast<EGLNativeWindowType>(g_nativeWindowPropertySet.Get()), nullptr);
        if (g_eglSurface != EGL_NO_SURFACE) {
            ShimLog("eglCreateWindowSurface(PropertySet) succeeded");
        } else {
            ReportEglError("eglCreateWindowSurface(PropertySet)");
        }
    }
    if (g_eglSurface == EGL_NO_SURFACE && p_eglCreatePlatformWindowSurface) {
        g_eglSurface = p_eglCreatePlatformWindowSurface(g_eglDisplay, g_eglConfig,
            g_nativeWindowPropertySet.Get(), nullptr);
        if (g_eglSurface != EGL_NO_SURFACE) {
            ShimLog("eglCreatePlatformWindowSurface(PropertySet) succeeded");
        } else {
            ReportEglError("eglCreatePlatformWindowSurface(PropertySet)");
        }
    }
    if (g_eglSurface == EGL_NO_SURFACE) {
        return false;
    }

    const EGLint contextAttrs[] = {
        EGL_CONTEXT_MAJOR_VERSION_KHR, 3,
        EGL_CONTEXT_MINOR_VERSION_KHR, 2,
        EGL_CONTEXT_OPENGL_PROFILE_MASK_KHR, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT_KHR,
        EGL_NONE
    };
    g_eglContext = p_eglCreateContext(g_eglDisplay, g_eglConfig, EGL_NO_CONTEXT, contextAttrs);
    if (g_eglContext == EGL_NO_CONTEXT) {
        ReportEglError("eglCreateContext(3.2 core)");
        g_eglContext = p_eglCreateContext(g_eglDisplay, g_eglConfig, EGL_NO_CONTEXT, nullptr);
    }
    if (g_eglContext == EGL_NO_CONTEXT) {
        ReportEglError("eglCreateContext(fallback)");
        return false;
    }

    if (!p_eglMakeCurrent(g_eglDisplay, g_eglSurface, g_eglSurface, g_eglContext)) {
        ReportEglError("eglMakeCurrent");
        return false;
    }

    const char* vendor = p_eglQueryString ? p_eglQueryString(g_eglDisplay, EGL_VENDOR) : nullptr;
    const char* version = p_eglQueryString ? p_eglQueryString(g_eglDisplay, EGL_VERSION) : nullptr;
    ShimLog("EGL initialized %d.%d vendor=%s version=%s",
        major, minor, vendor ? vendor : "?", version ? version : "?");
    return true;
}

// ---------------------------------------------------------------------------
// DLL entry
// ---------------------------------------------------------------------------
BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        wchar_t dir[MAX_PATH];
        GetRuntimeDir(dir, MAX_PATH);
        swprintf_s(g_log_path, L"%s\\glfw_uwp.log", dir);
        FILE* f = NULL;
        _wfopen_s(&f, g_log_path, L"w");
        if (f) fclose(f);
        ShimLog("DllMain attached");
        DisableThreadLibraryCalls(h);
    }
    return TRUE;
}

// ===========================================================================
// GLFW API
// ===========================================================================
extern "C" __declspec(dllexport) int glfwInit(void) {
    ShimLog("glfwInit");
    if (g_initialised) return GLFW_TRUE;
    if (!AcquireCoreWindow()) return GLFW_FALSE;
    RefreshWindowMetrics(false);
    g_fake_window = {0x58574C47u, g_width, g_height, FALSE, NULL};
    g_initialised = TRUE;
    ShimLog("glfwInit OK %dx%d", g_width, g_height);
    return GLFW_TRUE;
}

extern "C" __declspec(dllexport) void glfwTerminate(void) {
    if (g_coreWindow && g_keyboardHooksInstalled) {
        g_coreWindow->remove_KeyDown(g_keyDownToken);
        g_coreWindow->remove_KeyUp(g_keyUpToken);
        g_coreWindow->remove_CharacterReceived(g_charReceivedToken);
    }
    ZeroMemory(&g_keyDownToken, sizeof(g_keyDownToken));
    ZeroMemory(&g_keyUpToken, sizeof(g_keyUpToken));
    ZeroMemory(&g_charReceivedToken, sizeof(g_charReceivedToken));
    g_keyDownHandler.Reset();
    g_keyUpHandler.Reset();
    g_charReceivedHandler.Reset();
    g_keyboardHooksInstalled = false;
    ZeroMemory(g_key_state, sizeof(g_key_state));
    if (p_eglMakeCurrent && g_eglDisplay != EGL_NO_DISPLAY) {
        p_eglMakeCurrent(g_eglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    }
    if (p_eglDestroyContext && g_eglDisplay != EGL_NO_DISPLAY && g_eglContext != EGL_NO_CONTEXT) {
        p_eglDestroyContext(g_eglDisplay, g_eglContext);
    }
    if (p_eglDestroySurface && g_eglDisplay != EGL_NO_DISPLAY && g_eglSurface != EGL_NO_SURFACE) {
        p_eglDestroySurface(g_eglDisplay, g_eglSurface);
    }
    if (p_eglTerminate && g_eglDisplay != EGL_NO_DISPLAY) {
        p_eglTerminate(g_eglDisplay);
    }
    g_eglContext = EGL_NO_CONTEXT;
    g_eglSurface = EGL_NO_SURFACE;
    g_eglDisplay = EGL_NO_DISPLAY;
    g_eglConfig = nullptr;
    g_nativeWindowPropertySet.Reset();
    g_initialised = FALSE;
}

extern "C" __declspec(dllexport) void glfwInitHint(int,int) {}
extern "C" __declspec(dllexport) int  glfwGetPlatform(void) { return GLFW_PLATFORM_WIN32; }
extern "C" __declspec(dllexport) int  glfwPlatformSupported(int platform) {
    return platform == GLFW_ANY_PLATFORM || platform == GLFW_PLATFORM_WIN32;
}
extern "C" __declspec(dllexport) void glfwGetVersion(int*a,int*b,int*c) { if(a)*a=3; if(b)*b=3; if(c)*c=3; }
extern "C" __declspec(dllexport) const char* glfwGetVersionString(void) { return "3.3.3 UWP-EGL"; }
extern "C" __declspec(dllexport) int  glfwGetError(const char**d) { if(d)*d=NULL; return GLFW_NO_ERROR; }
extern "C" __declspec(dllexport) GLFWerrorfun glfwSetErrorCallback(GLFWerrorfun cb) {
    GLFWerrorfun p = g_error_cb;
    g_error_cb = cb;
    return p;
}
extern "C" __declspec(dllexport) void glfwDefaultWindowHints(void) {}
extern "C" __declspec(dllexport) void glfwWindowHint(int,int) {}
extern "C" __declspec(dllexport) void glfwWindowHintString(int,const char*) {}

extern "C" __declspec(dllexport)
GLFWwindow* glfwCreateWindow(int w, int h, const char* title, GLFWmonitor*, GLFWwindow*) {
    ShimLog("glfwCreateWindow %dx%d '%s'", w, h, title ? title : "");
    if (!g_initialised && !glfwInit()) return NULL;

    if (w > 0) g_width = w;
    if (h > 0) g_height = h;
    RefreshWindowMetrics(false);
    if (!CreateEglContext()) {
        ShimLog("CreateEglContext FAILED");
        return NULL;
    }

    g_fake_window.width = g_width;
    g_fake_window.height = g_height;
    g_vidmode.width = g_width;
    g_vidmode.height = g_height;
    if (g_fbsize_cb) g_fbsize_cb((GLFWwindow*)&g_fake_window, g_width, g_height);
    ShimLog("glfwCreateWindow OK");
    return (GLFWwindow*)&g_fake_window;
}

extern "C" __declspec(dllexport) void glfwDestroyWindow(GLFWwindow*) { g_should_close = TRUE; }
extern "C" __declspec(dllexport) int  glfwWindowShouldClose(GLFWwindow*) { return g_should_close ? GLFW_TRUE : GLFW_FALSE; }
extern "C" __declspec(dllexport) void glfwSetWindowShouldClose(GLFWwindow*, int v) { g_should_close = (v != 0); }
extern "C" __declspec(dllexport) void glfwSetWindowTitle(GLFWwindow*, const char*) {}
extern "C" __declspec(dllexport) void glfwSetWindowIcon(GLFWwindow*, int, const GLFWimage*) {}
extern "C" __declspec(dllexport) void glfwGetWindowPos(GLFWwindow*, int*x, int*y) { if(x)*x=0; if(y)*y=0; }
extern "C" __declspec(dllexport) void glfwSetWindowPos(GLFWwindow*, int, int) {}
extern "C" __declspec(dllexport) void glfwGetWindowSize(GLFWwindow*, int*w, int*h) { RefreshWindowMetrics(false); if(w)*w=g_width; if(h)*h=g_height; }
extern "C" __declspec(dllexport) void glfwSetWindowSizeLimits(GLFWwindow*, int, int, int, int) {}
extern "C" __declspec(dllexport) void glfwSetWindowAspectRatio(GLFWwindow*, int, int) {}
extern "C" __declspec(dllexport) void glfwSetWindowSize(GLFWwindow*, int, int) {}
extern "C" __declspec(dllexport) void glfwGetFramebufferSize(GLFWwindow*, int*w, int*h) { RefreshWindowMetrics(false); if(w)*w=g_width; if(h)*h=g_height; }
extern "C" __declspec(dllexport) void glfwGetWindowFrameSize(GLFWwindow*, int*l, int*t, int*r, int*b) {
    if(l)*l=0; if(t)*t=0; if(r)*r=0; if(b)*b=0;
}
extern "C" __declspec(dllexport) void glfwGetWindowContentScale(GLFWwindow*, float*x, float*y) {
    if(x)*x=1.f; if(y)*y=1.f;
}
extern "C" __declspec(dllexport) float glfwGetWindowOpacity(GLFWwindow*) { return 1.f; }
extern "C" __declspec(dllexport) void  glfwSetWindowOpacity(GLFWwindow*, float) {}
extern "C" __declspec(dllexport) void glfwIconifyWindow(GLFWwindow*) {}
extern "C" __declspec(dllexport) void glfwRestoreWindow(GLFWwindow*) {}
extern "C" __declspec(dllexport) void glfwMaximizeWindow(GLFWwindow*) {}
extern "C" __declspec(dllexport) void glfwShowWindow(GLFWwindow*) {
    ShimLog("glfwShowWindow");
}
extern "C" __declspec(dllexport) void glfwHideWindow(GLFWwindow*) {}
extern "C" __declspec(dllexport) void glfwFocusWindow(GLFWwindow*) {}
extern "C" __declspec(dllexport) void glfwRequestWindowAttention(GLFWwindow*) {}
extern "C" __declspec(dllexport) GLFWmonitor* glfwGetWindowMonitor(GLFWwindow*) { return NULL; }
extern "C" __declspec(dllexport) void glfwSetWindowMonitor(GLFWwindow*, GLFWmonitor*, int, int, int, int, int) {}
extern "C" __declspec(dllexport) int glfwGetWindowAttrib(GLFWwindow*, int a) {
    switch (a) {
    case GLFW_VISIBLE:
    case GLFW_FOCUSED:
    case GLFW_HOVERED:
        return GLFW_TRUE;
    case GLFW_MAXIMIZED:
        return GLFW_FALSE;
    case GLFW_CLIENT_API:
        return GLFW_OPENGL_API;
    case GLFW_CONTEXT_VERSION_MAJOR:
        return 3;
    case GLFW_CONTEXT_VERSION_MINOR:
        return 2;
    case GLFW_OPENGL_PROFILE:
        return GLFW_OPENGL_CORE_PROFILE;
    case GLFW_CONTEXT_CREATION_API:
        return GLFW_EGL_CONTEXT_API;
    default:
        return 0;
    }
}
extern "C" __declspec(dllexport) void  glfwSetWindowAttrib(GLFWwindow*, int, int) {}
extern "C" __declspec(dllexport) void glfwSetWindowUserPointer(GLFWwindow* window, void* userPointer) {
    FakeWindow* fake = reinterpret_cast<FakeWindow*>(window);
    if (fake && fake->magic == 0x58574C47u) {
        fake->user_pointer = userPointer;
    }
}
extern "C" __declspec(dllexport) void* glfwGetWindowUserPointer(GLFWwindow* window) {
    FakeWindow* fake = reinterpret_cast<FakeWindow*>(window);
    if (fake && fake->magic == 0x58574C47u) {
        return fake->user_pointer;
    }
    return NULL;
}

template <typename T>
static T SwapCallback(T& slot, T cb) {
    T previous = slot;
    slot = cb;
    return previous;
}

extern "C" __declspec(dllexport) GLFWwindowposfun glfwSetWindowPosCallback(GLFWwindow*, GLFWwindowposfun cb) {
    return SwapCallback(g_winpos_cb, cb);
}
extern "C" __declspec(dllexport) GLFWwindowsizefun glfwSetWindowSizeCallback(GLFWwindow*, GLFWwindowsizefun cb) {
    return SwapCallback(g_winsize_cb, cb);
}
extern "C" __declspec(dllexport) GLFWwindowclosefun glfwSetWindowCloseCallback(GLFWwindow*, GLFWwindowclosefun cb) {
    return SwapCallback(g_winclose_cb, cb);
}
extern "C" __declspec(dllexport) GLFWwindowrefreshfun glfwSetWindowRefreshCallback(GLFWwindow*, GLFWwindowrefreshfun cb) {
    return SwapCallback(g_winrefresh_cb, cb);
}
extern "C" __declspec(dllexport) GLFWwindowfocusfun glfwSetWindowFocusCallback(GLFWwindow*, GLFWwindowfocusfun cb) {
    return SwapCallback(g_focus_cb, cb);
}
extern "C" __declspec(dllexport) GLFWwindowiconifyfun glfwSetWindowIconifyCallback(GLFWwindow*, GLFWwindowiconifyfun cb) {
    return SwapCallback(g_iconify_cb, cb);
}
extern "C" __declspec(dllexport) GLFWwindowmaximizefun glfwSetWindowMaximizeCallback(GLFWwindow*, GLFWwindowmaximizefun cb) {
    return SwapCallback(g_maximize_cb, cb);
}
extern "C" __declspec(dllexport) GLFWframebuffersizefun glfwSetFramebufferSizeCallback(GLFWwindow*, GLFWframebuffersizefun cb) {
    return SwapCallback(g_fbsize_cb, cb);
}
extern "C" __declspec(dllexport) GLFWwindowcontentscalefun glfwSetWindowContentScaleCallback(GLFWwindow*, GLFWwindowcontentscalefun cb) {
    return SwapCallback(g_contentscale_cb, cb);
}
extern "C" __declspec(dllexport) GLFWkeyfun glfwSetKeyCallback(GLFWwindow*, GLFWkeyfun cb) {
    return SwapCallback(g_key_cb, cb);
}
extern "C" __declspec(dllexport) GLFWcharfun glfwSetCharCallback(GLFWwindow*, GLFWcharfun cb) {
    return SwapCallback(g_char_cb, cb);
}
extern "C" __declspec(dllexport) GLFWcharmodsfun glfwSetCharModsCallback(GLFWwindow*, GLFWcharmodsfun cb) {
    return SwapCallback(g_charmods_cb, cb);
}
extern "C" __declspec(dllexport) GLFWmousebuttonfun glfwSetMouseButtonCallback(GLFWwindow*, GLFWmousebuttonfun cb) {
    return SwapCallback(g_mousebutton_cb, cb);
}
extern "C" __declspec(dllexport) GLFWcursorposfun glfwSetCursorPosCallback(GLFWwindow*, GLFWcursorposfun cb) {
    return SwapCallback(g_cursorpos_cb, cb);
}
extern "C" __declspec(dllexport) GLFWcursorenterfun glfwSetCursorEnterCallback(GLFWwindow*, GLFWcursorenterfun cb) {
    return SwapCallback(g_cursorenter_cb, cb);
}
extern "C" __declspec(dllexport) GLFWscrollfun glfwSetScrollCallback(GLFWwindow*, GLFWscrollfun cb) {
    return SwapCallback(g_scroll_cb, cb);
}
extern "C" __declspec(dllexport) GLFWdropfun glfwSetDropCallback(GLFWwindow*, GLFWdropfun cb) {
    return SwapCallback(g_drop_cb, cb);
}

extern "C" __declspec(dllexport) void glfwPollEvents(void) {
    if (g_poll_log_count < 8) {
        ++g_poll_log_count;
        ShimLog("glfwPollEvents #%d", g_poll_log_count);
    }
    if (g_dispatcher) {
        g_dispatcher->ProcessEvents(CoreProcessEventsOption_ProcessAllIfPresent);
    }
    RefreshWindowMetrics(true);
}
extern "C" __declspec(dllexport) void glfwWaitEvents(void) {
    if (g_wait_log_count < 8) {
        ++g_wait_log_count;
        ShimLog("glfwWaitEvents #%d", g_wait_log_count);
    }
    // UWP does not expose GLFW's native wait semantics cleanly here.
    // Blocking on the dispatcher can stall Minecraft on a black screen,
    // so emulate a brief wait and then poll.
    Sleep(1);
    glfwPollEvents();
}
extern "C" __declspec(dllexport) void glfwWaitEventsTimeout(double) {
    if (g_wait_log_count < 8) {
        ++g_wait_log_count;
        ShimLog("glfwWaitEventsTimeout #%d", g_wait_log_count);
    }
    Sleep(1);
    glfwPollEvents();
}
extern "C" __declspec(dllexport) void glfwPostEmptyEvent(void) {}
extern "C" __declspec(dllexport) int  glfwGetInputMode(GLFWwindow*, int m) { return m == GLFW_CURSOR ? GLFW_CURSOR_NORMAL : 0; }
extern "C" __declspec(dllexport) void glfwSetInputMode(GLFWwindow*, int, int) {}
extern "C" __declspec(dllexport) int  glfwRawMouseMotionSupported(void) { return GLFW_FALSE; }
extern "C" __declspec(dllexport) const char* glfwGetKeyName(int, int) { return ""; }
extern "C" __declspec(dllexport) int  glfwGetKeyScancode(int) { return 0; }
extern "C" __declspec(dllexport) int  glfwGetKey(GLFWwindow*, int key) {
    if (key < 0 || key >= (int)sizeof(g_key_state)) return GLFW_RELEASE;
    return g_key_state[key] ? GLFW_PRESS : GLFW_RELEASE;
}
extern "C" __declspec(dllexport) int  glfwGetMouseButton(GLFWwindow*, int) { return GLFW_RELEASE; }
extern "C" __declspec(dllexport) void glfwGetCursorPos(GLFWwindow*, double*x, double*y) { if(x)*x=0.; if(y)*y=0.; }
extern "C" __declspec(dllexport) void glfwSetCursorPos(GLFWwindow*, double, double) {}
extern "C" __declspec(dllexport) GLFWcursor* glfwCreateCursor(const GLFWimage*, int, int) { return (GLFWcursor*)1; }
extern "C" __declspec(dllexport) GLFWcursor* glfwCreateStandardCursor(int) { return (GLFWcursor*)1; }
extern "C" __declspec(dllexport) void glfwDestroyCursor(GLFWcursor*) {}
extern "C" __declspec(dllexport) void glfwSetCursor(GLFWwindow*, GLFWcursor*) {}
extern "C" __declspec(dllexport) const char* glfwGetClipboardString(GLFWwindow*) { return ""; }
extern "C" __declspec(dllexport) void glfwSetClipboardString(GLFWwindow*, const char*) {}

static LARGE_INTEGER g_freq, g_start;
static BOOL g_time_init = FALSE;
static void TimeInit() {
    if (!g_time_init) {
        QueryPerformanceFrequency(&g_freq);
        QueryPerformanceCounter(&g_start);
        g_time_init = TRUE;
    }
}
extern "C" __declspec(dllexport) double glfwGetTime(void) {
    TimeInit();
    LARGE_INTEGER n;
    QueryPerformanceCounter(&n);
    return (double)(n.QuadPart - g_start.QuadPart) / (double)g_freq.QuadPart;
}
extern "C" __declspec(dllexport) void glfwSetTime(double) {}
extern "C" __declspec(dllexport) uint64_t glfwGetTimerValue(void) {
    LARGE_INTEGER v;
    QueryPerformanceCounter(&v);
    return (uint64_t)v.QuadPart;
}
extern "C" __declspec(dllexport) uint64_t glfwGetTimerFrequency(void) {
    LARGE_INTEGER f;
    QueryPerformanceFrequency(&f);
    return (uint64_t)f.QuadPart;
}

extern "C" __declspec(dllexport) void glfwMakeContextCurrent(GLFWwindow* w) {
    ShimLog("MakeContextCurrent %p", (void*)w);
    if (!w) {
        if (p_eglMakeCurrent && g_eglDisplay != EGL_NO_DISPLAY) {
            p_eglMakeCurrent(g_eglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        }
        return;
    }
    if (!CreateEglContext()) return;
    if (!p_eglMakeCurrent(g_eglDisplay, g_eglSurface, g_eglSurface, g_eglContext)) {
        ReportEglError("eglMakeCurrent");
        return;
    }
    ShimLog("eglMakeCurrent OK");

    PFN_glGetString p_glGetString = nullptr;
    if (g_opengl32) {
        p_glGetString = reinterpret_cast<PFN_glGetString>(GetProcAddress(g_opengl32, "glGetString"));
    }
    if (!p_glGetString && p_eglGetProcAddress) {
        p_glGetString = reinterpret_cast<PFN_glGetString>(p_eglGetProcAddress("glGetString"));
    }
    if (p_glGetString) {
        const unsigned char* vendor = p_glGetString(GL_VENDOR);
        const unsigned char* renderer = p_glGetString(GL_RENDERER);
        const unsigned char* version = p_glGetString(GL_VERSION);
        ShimLog("GL vendor=%s renderer=%s version=%s",
            vendor ? reinterpret_cast<const char*>(vendor) : "?",
            renderer ? reinterpret_cast<const char*>(renderer) : "?",
            version ? reinterpret_cast<const char*>(version) : "?");
    } else {
        ShimLog("glGetString unresolved");
    }
}
extern "C" __declspec(dllexport) GLFWwindow* glfwGetCurrentContext(void) {
    return (g_eglContext != EGL_NO_CONTEXT) ? (GLFWwindow*)&g_fake_window : NULL;
}
extern "C" __declspec(dllexport) void glfwSwapBuffers(GLFWwindow*) {
    if (g_swap_log_count < 12) {
        ++g_swap_log_count;
        ShimLog("glfwSwapBuffers #%d", g_swap_log_count);
    }
    if (!p_eglSwapBuffers || g_eglDisplay == EGL_NO_DISPLAY || g_eglSurface == EGL_NO_SURFACE) return;
    if (!p_eglSwapBuffers(g_eglDisplay, g_eglSurface)) {
        ReportEglError("eglSwapBuffers");
    }
}
extern "C" __declspec(dllexport) void glfwSwapInterval(int i) {
    if (p_eglSwapInterval && g_eglDisplay != EGL_NO_DISPLAY) {
        p_eglSwapInterval(g_eglDisplay, i);
    }
}
extern "C" __declspec(dllexport) int  glfwExtensionSupported(const char*) { return GLFW_FALSE; }
extern "C" __declspec(dllexport) void* glfwGetProcAddress(const char* name) {
    void* p = p_eglGetProcAddress ? p_eglGetProcAddress(name) : NULL;
    if (!p && g_opengl32) p = (void*)GetProcAddress(g_opengl32, name);
    if (!p && g_libEGL) p = (void*)GetProcAddress(g_libEGL, name);
    if (g_proc_log_count < 40) {
        ++g_proc_log_count;
        ShimLog("glfwGetProcAddress #%d %s => %p", g_proc_log_count, name ? name : "(null)", p);
    }
    return p;
}

extern "C" __declspec(dllexport) GLFWmonitor** glfwGetMonitors(int*c) {
    static GLFWmonitor*m[] = {(GLFWmonitor*)1};
    if(c)*c=1;
    return m;
}
extern "C" __declspec(dllexport) GLFWmonitor* glfwGetPrimaryMonitor(void) { return (GLFWmonitor*)1; }
extern "C" __declspec(dllexport) void glfwGetMonitorPos(GLFWmonitor*, int*x, int*y) { if(x)*x=0; if(y)*y=0; }
extern "C" __declspec(dllexport) void glfwGetMonitorWorkarea(GLFWmonitor*, int*x, int*y, int*w, int*h) {
    if(x)*x=0; if(y)*y=0; if(w)*w=g_width; if(h)*h=g_height;
}
extern "C" __declspec(dllexport) void glfwGetMonitorPhysicalSize(GLFWmonitor*, int*w, int*h) { if(w)*w=527; if(h)*h=296; }
extern "C" __declspec(dllexport) void glfwGetMonitorContentScale(GLFWmonitor*, float*x, float*y) { if(x)*x=1.f; if(y)*y=1.f; }
extern "C" __declspec(dllexport) const char* glfwGetMonitorName(GLFWmonitor*) { return "CoreWindow Display"; }
extern "C" __declspec(dllexport) void  glfwSetMonitorUserPointer(GLFWmonitor*, void*) {}
extern "C" __declspec(dllexport) void* glfwGetMonitorUserPointer(GLFWmonitor*) { return NULL; }
extern "C" __declspec(dllexport) GLFWmonitorfun glfwSetMonitorCallback(GLFWmonitorfun cb) { GLFWmonitorfun p=g_monitor_cb; g_monitor_cb=cb; return p; }
extern "C" __declspec(dllexport) const GLFWvidmode* glfwGetVideoModes(GLFWmonitor*, int*c) { if(c)*c=1; return &g_vidmode; }
extern "C" __declspec(dllexport) const GLFWvidmode* glfwGetVideoMode(GLFWmonitor*) { return &g_vidmode; }
extern "C" __declspec(dllexport) void glfwSetGamma(GLFWmonitor*, float) {}
extern "C" __declspec(dllexport) const GLFWgammaramp* glfwGetGammaRamp(GLFWmonitor*) { return NULL; }
extern "C" __declspec(dllexport) void glfwSetGammaRamp(GLFWmonitor*, const GLFWgammaramp*) {}

extern "C" __declspec(dllexport) int glfwJoystickPresent(int) { return GLFW_FALSE; }
extern "C" __declspec(dllexport) const float* glfwGetJoystickAxes(int, int*c) { if(c)*c=0; return NULL; }
extern "C" __declspec(dllexport) const unsigned char* glfwGetJoystickButtons(int, int*c) { if(c)*c=0; return NULL; }
extern "C" __declspec(dllexport) const unsigned char* glfwGetJoystickHats(int, int*c) { if(c)*c=0; return NULL; }
extern "C" __declspec(dllexport) const char* glfwGetJoystickName(int) { return NULL; }
extern "C" __declspec(dllexport) const char* glfwGetJoystickGUID(int) { return NULL; }
extern "C" __declspec(dllexport) void  glfwSetJoystickUserPointer(int, void*) {}
extern "C" __declspec(dllexport) void* glfwGetJoystickUserPointer(int) { return NULL; }
extern "C" __declspec(dllexport) int   glfwJoystickIsGamepad(int) { return GLFW_FALSE; }
extern "C" __declspec(dllexport) GLFWjoystickfun glfwSetJoystickCallback(GLFWjoystickfun cb) { GLFWjoystickfun p=g_joystick_cb; g_joystick_cb=cb; return p; }
extern "C" __declspec(dllexport) int  glfwUpdateGamepadMappings(const char*) { return GLFW_FALSE; }
extern "C" __declspec(dllexport) const char* glfwGetGamepadName(int) { return NULL; }
extern "C" __declspec(dllexport) int  glfwGetGamepadState(int, GLFWgamepadstate*) { return GLFW_FALSE; }

extern "C" __declspec(dllexport) HWND  glfwGetWin32Window(GLFWwindow*) { return NULL; }
extern "C" __declspec(dllexport) void* glfwGetWGLContext(GLFWwindow*) { return NULL; }

typedef struct { void* allocate; void* reallocate; void* deallocate; void* user; } GLFWallocator;
extern "C" __declspec(dllexport) void glfwInitAllocator(const GLFWallocator*) {}

extern "C" __declspec(dllexport) int   glfwVulkanSupported(void) { return GLFW_FALSE; }
extern "C" __declspec(dllexport) void* glfwGetInstanceProcAddress(void*, const char*) { return NULL; }
extern "C" __declspec(dllexport) int   glfwGetPhysicalDevicePresentationSupport(void*, void*, unsigned int) { return GLFW_FALSE; }
extern "C" __declspec(dllexport) int   glfwCreateWindowSurface(void*, GLFWwindow*, const void*, void*) { return 1; }
extern "C" __declspec(dllexport) const char** glfwGetRequiredInstanceExtensions(unsigned int* c) { if(c)*c=0; return NULL; }

extern "C" __declspec(dllexport) void* glfwGetEGLDisplay(void) { return g_eglDisplay; }
extern "C" __declspec(dllexport) void* glfwGetEGLContext(GLFWwindow*) { return g_eglContext; }
extern "C" __declspec(dllexport) void* glfwGetEGLSurface(GLFWwindow*) { return g_eglSurface; }
extern "C" __declspec(dllexport) void* glfwGetEGLConfig(GLFWwindow*) { return g_eglConfig; }

extern "C" __declspec(dllexport) int glfwGetOSMesaColorBuffer(GLFWwindow*, int*, int*, int*, void**) { return GLFW_FALSE; }
extern "C" __declspec(dllexport) int glfwGetOSMesaDepthBuffer(GLFWwindow*, int*, int*, int*, void**) { return GLFW_FALSE; }
extern "C" __declspec(dllexport) void* glfwGetOSMesaContext(GLFWwindow*) { return NULL; }

extern "C" __declspec(dllexport) const char* glfwGetWin32Adapter(GLFWmonitor*) { return NULL; }
extern "C" __declspec(dllexport) const char* glfwGetWin32Monitor(GLFWmonitor*) { return NULL; }
