// glfw_uwp.cpp - GLFW WinRT/EGL shim for Minecraft Java UWP (Xbox Series S)
// Replaces glfw.dll inside lwjgl-glfw-3.3.3-natives-windows.jar.

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
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
#include <GameInput.h>
#include <windows.system.h>
#include <windows.ui.core.h>
#include <windows.graphics.display.h>
#include <vector>
#include <windows.devices.input.h>
#include <windows.ui.input.h>
#include <windows.applicationmodel.datatransfer.h>
#pragma comment(lib, "ws2_32.lib")

using namespace Microsoft::WRL;
using namespace Microsoft::WRL::Wrappers;
using namespace ABI::Windows::ApplicationModel::Core;
using namespace ABI::Windows::Foundation;
using namespace ABI::Windows::Foundation::Collections;
using namespace ABI::Windows::Graphics::Display;
using namespace ABI::Windows::System;
using namespace ABI::Windows::UI::Core;
using namespace ABI::Windows::UI::Input;
using namespace ABI::Windows::ApplicationModel::DataTransfer;

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
#define GLFW_MOUSE_BUTTON_LEFT    0
#define GLFW_MOUSE_BUTTON_RIGHT   1
#define GLFW_MOUSE_BUTTON_MIDDLE  2
#define GLFW_MOUSE_BUTTON_4       3
#define GLFW_MOUSE_BUTTON_5       4
#define GLFW_CURSOR_HIDDEN   0x00034002
#define GLFW_RAW_MOUSE_MOTION 0x00033005
#define GLFW_KEY_APOSTROPHE        39
#define GLFW_KEY_COMMA             44
#define GLFW_KEY_MINUS             45
#define GLFW_KEY_PERIOD            46
#define GLFW_KEY_SLASH             47
#define GLFW_KEY_SEMICOLON         59
#define GLFW_KEY_EQUAL             61
#define GLFW_KEY_LEFT_BRACKET      91
#define GLFW_KEY_BACKSLASH         92
#define GLFW_KEY_RIGHT_BRACKET     93
#define GLFW_KEY_GRAVE_ACCENT      96
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
#define GLFW_OPENGL_COMPAT_PROFILE 0x00032002
#define GLFW_NATIVE_CONTEXT_API    0x00036001
#define GLFW_EGL_CONTEXT_API       0x00036002
#define GLFW_CURSOR          0x00033001
#define GLFW_CURSOR_NORMAL   0x00034001
#define GLFW_CURSOR_DISABLED 0x00034003
#define GLFW_CONNECTED       0x00040001
#define GLFW_DISCONNECTED    0x00040002
#define GLFW_JOYSTICK_1      0
#define GLFW_GAMEPAD_BUTTON_A             0
#define GLFW_GAMEPAD_BUTTON_B             1
#define GLFW_GAMEPAD_BUTTON_X             2
#define GLFW_GAMEPAD_BUTTON_Y             3
#define GLFW_GAMEPAD_BUTTON_LEFT_BUMPER   4
#define GLFW_GAMEPAD_BUTTON_RIGHT_BUMPER  5
#define GLFW_GAMEPAD_BUTTON_BACK          6
#define GLFW_GAMEPAD_BUTTON_START         7
#define GLFW_GAMEPAD_BUTTON_GUIDE         8
#define GLFW_GAMEPAD_BUTTON_LEFT_THUMB    9
#define GLFW_GAMEPAD_BUTTON_RIGHT_THUMB   10
#define GLFW_GAMEPAD_BUTTON_DPAD_UP       11
#define GLFW_GAMEPAD_BUTTON_DPAD_RIGHT    12
#define GLFW_GAMEPAD_BUTTON_DPAD_DOWN     13
#define GLFW_GAMEPAD_BUTTON_DPAD_LEFT     14
#define GLFW_GAMEPAD_AXIS_LEFT_X          0
#define GLFW_GAMEPAD_AXIS_LEFT_Y          1
#define GLFW_GAMEPAD_AXIS_RIGHT_X         2
#define GLFW_GAMEPAD_AXIS_RIGHT_Y         3
#define GLFW_GAMEPAD_AXIS_LEFT_TRIGGER    4
#define GLFW_GAMEPAD_AXIS_RIGHT_TRIGGER   5

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
#define EGL_OPENGL_ES2_BIT 0x0004
#define EGL_OPENGL_ES3_BIT_KHR 0x00000040
#define EGL_OPENGL_API 0x30A2
#define EGL_OPENGL_ES_API 0x30A0
#define EGL_VENDOR 0x3053
#define EGL_VERSION 0x3054
#define EGL_CONTEXT_CLIENT_VERSION 0x3098
#define EGL_CONTEXT_MAJOR_VERSION_KHR 0x3098
#define EGL_CONTEXT_MINOR_VERSION_KHR 0x30FB
#define EGL_CONTEXT_OPENGL_PROFILE_MASK_KHR 0x30FD
#define EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT_KHR 0x00000001
#define EGL_CONTEXT_OPENGL_COMPATIBILITY_PROFILE_BIT_KHR 0x00000002

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
typedef void (*PFN_proc_init)(void);

// ---------------------------------------------------------------------------
// Global state
// ---------------------------------------------------------------------------
static constexpr wchar_t kEGLNativeWindowTypeProperty[] = L"EGLNativeWindowTypeProperty";
static constexpr wchar_t kEGLRenderSurfaceSizeProperty[] = L"EGLRenderSurfaceSizeProperty";

static EGLDisplay g_eglDisplay = EGL_NO_DISPLAY;
static EGLSurface g_eglSurface = EGL_NO_SURFACE;
static EGLContext g_eglContext = EGL_NO_CONTEXT;
static EGLConfig  g_eglConfig = nullptr;
static DWORD g_eglContextThreadId = 0;
static HMODULE g_libEGL = NULL;
static HMODULE g_opengl32 = NULL;
static HMODULE g_libGLESv2 = NULL;
static BOOL g_graphicsRuntimeUsesGles = FALSE;
static BOOL g_initialised = FALSE;
static BOOL g_should_close = FALSE;
static int g_window_width = 1920;
static int g_window_height = 1080;
static int g_framebuffer_width = 1920;
static int g_framebuffer_height = 1080;
static int g_menu_window_width = 960;
static int g_menu_window_height = 540;
static float g_content_scale_x = 1.0f;
static float g_content_scale_y = 1.0f;
static int g_swap_log_count = 0;
static int g_poll_log_count = 0;
static int g_proc_log_count = 0;
static int g_wait_log_count = 0;
static int g_key_log_count = 0;
static int g_controller_log_count = 0;
static int g_gamepad_query_log_count = 0;
static int g_current_context_log_count = 0;
static int g_window_attrib_log_count = 0;
static int g_extension_log_count = 0;

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
using CoreWindowVisibilityHandler = ABI::Windows::Foundation::__FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CVisibilityChangedEventArgs_t;
using CoreWindowActivatedHandler = ABI::Windows::Foundation::__FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CWindowActivatedEventArgs_t;
static ComPtr<CoreWindowKeyHandler> g_keyDownHandler;
static ComPtr<CoreWindowKeyHandler> g_keyUpHandler;
static ComPtr<CoreWindowCharHandler> g_charReceivedHandler;
static ComPtr<CoreWindowVisibilityHandler> g_visibilityHandler;
static ComPtr<CoreWindowActivatedHandler> g_activatedHandler;
static ComPtr<IGameInput> g_gameInput;
static EventRegistrationToken g_keyDownToken = {};
static EventRegistrationToken g_keyUpToken = {};
static EventRegistrationToken g_charReceivedToken = {};
static EventRegistrationToken g_visibilityToken = {};
static EventRegistrationToken g_activatedToken = {};
static bool g_keyboardHooksInstalled = false;
static bool g_lifecycleHooksInstalled = false;
static bool g_gameInputCreateTried = false;
static bool g_gamepad_present = false;
static bool g_haveGameInputGamepadState = false;
static bool g_haveGameInputPollCache = false;
static ULONGLONG g_lastGameInputPollMs = 0;
static volatile LONG g_coreWindowVisibleForInput = 1;
static volatile LONG g_coreWindowActivatedForInput = 1;
static volatile LONGLONG g_coreWindowInputStateChangedMs = 0;
static bool g_legacyControllerModModeLogged = false;
static bool g_lastLegacyControllerModMode = false;
static unsigned char g_key_state[512] = {};
static GameInputGamepadState g_lastGameInputGamepadState = {};
static GLFWgamepadstate g_gamepad_state = {};
static float g_joystick_axes[6] = {};
static unsigned char g_joystick_buttons[15] = {};
static void* g_joystick_user_pointer = NULL;

static ComPtr<ICoreWindow> g_coreWindow;
static ComPtr<ICoreDispatcher> g_dispatcher;
static ComPtr<IInspectable> g_nativeWindowPropertySet;

enum PointerDispatchKind {
    PointerDispatchMove,
    PointerDispatchPress,
    PointerDispatchRelease,
    PointerDispatchWheel,
    PointerDispatchEnter,
    PointerDispatchExit,
    PointerDispatchCaptureLost
};
static constexpr double kProtocolWidth = 1920.0;
static constexpr double kProtocolHeight = 1080.0;

using CoreWindowPointerHandler = ABI::Windows::Foundation::__FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CPointerEventArgs_t;
using MouseDeviceMovedHandler = ABI::Windows::Foundation::__FITypedEventHandler_2_Windows__CDevices__CInput__CMouseDevice_Windows__CDevices__CInput__CMouseEventArgs_t;
static char g_clipboard_buf[65536] = {};
static float g_content_scale = 1.f;
static unsigned char g_controller_key_state[512] = {};
static bool g_controller_lb_down = false;
static unsigned char g_controller_mouse_state[8] = {};
static bool g_controller_rb_down = false;
static bool g_cursorDisabled = true;
static int g_cursorMode = GLFW_CURSOR_DISABLED;
static bool g_cursor_inside = false;
static SOCKET g_cursor_overlay_socket = INVALID_SOCKET;
static double g_cursor_x = 960.0;
static double g_cursor_y = 540.0;
static int g_gameinput_log_count = 0;
static bool g_haveGameInputMouseState = false;
static bool g_have_mouse_relay_status_addr = false;
static int g_height = 1080;
static GameInputMouseState g_lastGameInputMouseState = {};
static double g_menu_abs_x = 960.0;
static double g_menu_abs_y = 540.0;
static ULONGLONG g_modeChangeTime = 0;
static ComPtr<ABI::Windows::Devices::Input::IMouseDevice> g_mouseDevice;
static bool g_mouseDeviceHooksInstalled = false;
static ComPtr<MouseDeviceMovedHandler> g_mouseMovedHandler;
static EventRegistrationToken g_mouseMovedToken = {};
static int g_mouse_log_count = 0;
static sockaddr_in g_mouse_relay_status_addr = {};
static SRWLOCK g_mouse_relay_status_lock = SRWLOCK_INIT;
static SOCKET g_mouse_relay_status_socket = INVALID_SOCKET;
static unsigned char g_mouse_state[8] = {};
static int g_pendingMode = -1;
static ComPtr<CoreWindowPointerHandler> g_pointerCaptureLostHandler;
static EventRegistrationToken g_pointerCaptureLostToken = {};
static ComPtr<CoreWindowPointerHandler> g_pointerEnteredHandler;
static EventRegistrationToken g_pointerEnteredToken = {};
static ComPtr<CoreWindowPointerHandler> g_pointerExitedHandler;
static EventRegistrationToken g_pointerExitedToken = {};
static bool g_pointerHooksInstalled = false;
static ComPtr<CoreWindowPointerHandler> g_pointerMovedHandler;
static EventRegistrationToken g_pointerMovedToken = {};
static ComPtr<CoreWindowPointerHandler> g_pointerPressedHandler;
static EventRegistrationToken g_pointerPressedToken = {};
static ComPtr<CoreWindowPointerHandler> g_pointerReleasedHandler;
static EventRegistrationToken g_pointerReleasedToken = {};
static ComPtr<CoreWindowPointerHandler> g_pointerWheelHandler;
static EventRegistrationToken g_pointerWheelToken = {};
static bool g_raw_mouse_motion = false;
static bool g_remote_mouse_abs_pending = false;
static bool g_remote_mouse_abs_window = false;
static double g_remote_mouse_abs_x = 960.0;
static double g_remote_mouse_abs_y = 540.0;
static int g_remote_mouse_button_changes = 0;
static int g_remote_mouse_buttons = 0;
static double g_remote_mouse_dx = 0.0;
static double g_remote_mouse_dy = 0.0;
static SRWLOCK g_remote_mouse_lock = SRWLOCK_INIT;
static int g_remote_mouse_log_count = 0;
static volatile LONG g_remote_mouse_seen = 0;
static volatile LONG g_remote_mouse_server_started = 0;
static double g_remote_mouse_wheel_y = 0.0;
static volatile LONG g_last_companion_tick = 0;     
static bool g_mouse_active_latched = false;         
static const DWORD kMouseCompanionTimeoutMs = 3000; 
static int g_reportedMode = GLFW_CURSOR_DISABLED;
static int g_width = 1920;
static volatile LONG g_processing_events = 0;
static int g_process_events_error_log_count = 0;
static int g_nested_process_log_count = 0;
static int g_process_events_offthread_log_count = 0;
static bool g_controller_bridge_enabled = false;

static double CurrentPointerScaleX();
static double CurrentPointerScaleY();
static void DispatchCursorEnter(bool entered);
static void DispatchCursorPos(double x, double y);
static double ClampDouble(double value, double minValue, double maxValue);
static double CursorMaxX();
static double CursorMaxY();
static double ProtocolToWindowX(double x);
static double ProtocolToWindowY(double y);
static double WindowToProtocolX(double x);
static double WindowToProtocolY(double y);
static void SendCursorOverlayState();
static void RememberMouseRelayStatusAddress(const sockaddr_in& remote);
static void SendMouseRelayStatusText(const char* text);
static void SendMouseRelayCursorSync(double x, double y);
static void DispatchMouseDelta(double dx, double dy);
static void DispatchMouseAbsolute(double x, double y);
static void DispatchMouseWindowAbsolute(double x, double y);
static void FireRemoteMouseButtonCallback(int button, int action);
static void SetRemoteMouseButtonState(int button, int action);
static void SetMouseButtonState(int button, int action, bool fireCallback);
static bool AnyMouseButtonDown();
static void QueueRemoteMouseButton(int buttonBit, int value, int& buttons, int& changes);
static void DrainRemoteMouseInput();
static void StartRemoteMouseServer();
static void MarkMouseCompanionSeen();
static bool MouseCompanionActive();
static void FlushMouseButtonsForDeactivate();
static int SyntheticScancodeForKey(int key);
static void SetControllerKeyState(int key, bool down);
static void ReleaseControllerKeys();
static void SetControllerMouseButtonState(int button, bool down);
static void ReleaseControllerMouseButtons();
static int ClampInt64ToInt(int64_t value);
static bool SyncMouseButtonsFromProperties(ABI::Windows::UI::Input::IPointerPointProperties* props, bool fireCallbacks);
static bool ButtonActionFromUpdateKind(
    ABI::Windows::UI::Input::PointerUpdateKind kind,
    int* button,
    int* action);
static bool ReadPointerEvent(
    IPointerEventArgs* args,
    double* x,
    double* y,
    ComPtr<ABI::Windows::UI::Input::IPointerPointProperties>* propsOut);
static void HandlePointerEvent(IPointerEventArgs* args, PointerDispatchKind kind);
static void HandleMouseDeviceMoved(ABI::Windows::Devices::Input::IMouseEventArgs* args);
static void PollCoreWindowPointerPosition();
static void SyncGameInputMouseButtons(GameInputMouseButtons buttons);
static void PollGameInputMouse();
static float ApplyDeadzone(float value, float deadzone);
static bool IsGamepadButtonDown(const GameInputGamepadState& state, GameInputGamepadButtons button);
static void UpdateControllerBridge(const GameInputGamepadState& state);
static void ReleaseControllerBridge();
static void RemoveMouseDeviceHooks();
static bool InstallMouseDeviceHooks();
static void RemovePointerHooks();
static bool InstallPointerHooks();
static IClipboardStatics* GetClipboardStatics();

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

static void JoinPath(wchar_t* out, int cch, const wchar_t* dir, const wchar_t* name) {
    if (!dir || !*dir) {
        swprintf_s(out, cch, L"%s", name ? name : L"");
        return;
    }
    if (!name || !*name) {
        swprintf_s(out, cch, L"%s", dir);
        return;
    }
    swprintf_s(out, cch, L"%s\\%s", dir, name);
}

static void GetGraphicsRuntimeName(wchar_t* out, int cch) {
    DWORD len = GetEnvironmentVariableW(L"MC_GRAPHICS_RUNTIME", out, cch);
    if (len == 0 || len >= (DWORD)cch) {
        swprintf_s(out, cch, L"mesa");
        return;
    }

    if (_wcsicmp(out, L"series") == 0 || _wcsicmp(out, L"seriesx") == 0 ||
        _wcsicmp(out, L"seriess") == 0 || _wcsicmp(out, L"auto") == 0) {
        swprintf_s(out, cch, L"mesa");
    }
}

static bool EnvFlagEnabled(const wchar_t* name) {
    wchar_t value[32] = {};
    DWORD len = GetEnvironmentVariableW(name, value, ARRAYSIZE(value));
    if (len == 0 || len >= ARRAYSIZE(value)) return false;
    return _wcsicmp(value, L"1") == 0 ||
        _wcsicmp(value, L"true") == 0 ||
        _wcsicmp(value, L"yes") == 0 ||
        _wcsicmp(value, L"on") == 0;
}

static bool EnvFlagDisabled(const wchar_t* name) {
    wchar_t value[32] = {};
    DWORD len = GetEnvironmentVariableW(name, value, ARRAYSIZE(value));
    if (len == 0 || len >= ARRAYSIZE(value)) return false;
    return _wcsicmp(value, L"0") == 0 ||
        _wcsicmp(value, L"false") == 0 ||
        _wcsicmp(value, L"no") == 0 ||
        _wcsicmp(value, L"off") == 0;
}

static bool DirectoryExists(const wchar_t* path) {
    const DWORD attrs = GetFileAttributesW(path);
    return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY);
}

static bool GraphicsRuntimeReady(const wchar_t* path) {
    if (!DirectoryExists(path)) return false;

    wchar_t glPath[MAX_PATH];
    wchar_t eglPath[MAX_PATH];
    JoinPath(glPath, MAX_PATH, path, L"opengl32.dll");
    JoinPath(eglPath, MAX_PATH, path, L"libEGL.dll");
    return GetFileAttributesW(glPath) != INVALID_FILE_ATTRIBUTES &&
        GetFileAttributesW(eglPath) != INVALID_FILE_ATTRIBUTES;
}

static bool SelectGraphicsRuntimeDir(
    wchar_t* runtimeDir,
    int runtimeDirCch,
    wchar_t* packagePrefix,
    int packagePrefixCch) {
    wchar_t exeDir[MAX_PATH];
    GetExeDir(exeDir, MAX_PATH);

    wchar_t requested[32];
    GetGraphicsRuntimeName(requested, (int)(sizeof(requested) / sizeof(requested[0])));
    g_graphicsRuntimeUsesGles = (_wcsicmp(requested, L"xboxone") == 0);

    wchar_t candidate[MAX_PATH];
    swprintf_s(candidate, L"%s\\graphics\\%s", exeDir, requested);
    if (GraphicsRuntimeReady(candidate)) {
        swprintf_s(runtimeDir, runtimeDirCch, L"%s", candidate);
        swprintf_s(packagePrefix, packagePrefixCch, L"graphics\\%s", requested);
        ShimLog("Graphics runtime selected: %S (%S)", requested, runtimeDir);
        return true;
    }

    swprintf_s(candidate, L"%s\\natives\\graphics\\%s", exeDir, requested);
    if (GraphicsRuntimeReady(candidate)) {
        swprintf_s(runtimeDir, runtimeDirCch, L"%s", candidate);
        swprintf_s(packagePrefix, packagePrefixCch, L"natives\\graphics\\%s", requested);
        ShimLog("Graphics runtime selected: %S (%S)", requested, runtimeDir);
        return true;
    }

    // Legacy layout: older packages copied Mesa DLLs directly beside the exe
    // or under natives. Keep this so the Series path remains compatible.
    swprintf_s(runtimeDir, runtimeDirCch, L"%s", exeDir);
    swprintf_s(packagePrefix, packagePrefixCch, L"");
    g_graphicsRuntimeUsesGles = FALSE;
    ShimLog("Graphics runtime folder missing for %S; using legacy Mesa lookup", requested);
    return true;
}

static void RuntimeDllPath(
    const wchar_t* runtimeDir,
    const wchar_t* packagePrefix,
    const wchar_t* dll,
    wchar_t* absolutePath,
    int absolutePathCch,
    wchar_t* packagedPath,
    int packagedPathCch) {
    JoinPath(absolutePath, absolutePathCch, runtimeDir, dll);
    if (packagePrefix && *packagePrefix) {
        swprintf_s(packagedPath, packagedPathCch, L"%s\\%s", packagePrefix, dll);
    } else {
        swprintf_s(packagedPath, packagedPathCch, L"%s", dll);
    }
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
    case VirtualKey_Shift: return GLFW_KEY_LEFT_SHIFT;
    case VirtualKey_RightShift: return GLFW_KEY_RIGHT_SHIFT;
    case VirtualKey_LeftControl: return GLFW_KEY_LEFT_CONTROL;
    case VirtualKey_Control: return GLFW_KEY_LEFT_CONTROL;
    case VirtualKey_RightControl: return GLFW_KEY_RIGHT_CONTROL;
    case VirtualKey_LeftMenu: return GLFW_KEY_LEFT_ALT;
    case VirtualKey_RightMenu: return GLFW_KEY_RIGHT_ALT;
    case VirtualKey_LeftWindows: return GLFW_KEY_LEFT_SUPER;
    case VirtualKey_RightWindows: return GLFW_KEY_RIGHT_SUPER;
    case VirtualKey_Menu: return GLFW_KEY_MENU;
    case (VirtualKey)188: return GLFW_KEY_COMMA;
    case (VirtualKey)190: return GLFW_KEY_PERIOD;
    case (VirtualKey)191: return GLFW_KEY_SLASH;
    case (VirtualKey)186: return GLFW_KEY_SEMICOLON;
    case (VirtualKey)222: return GLFW_KEY_APOSTROPHE;
    case (VirtualKey)219: return GLFW_KEY_LEFT_BRACKET;
    case (VirtualKey)221: return GLFW_KEY_RIGHT_BRACKET;
    case (VirtualKey)220: return GLFW_KEY_BACKSLASH;
    case (VirtualKey)189: return GLFW_KEY_MINUS;
    case (VirtualKey)187: return GLFW_KEY_EQUAL;
    case (VirtualKey)192: return GLFW_KEY_GRAVE_ACCENT;
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

static void ClearKeyboardState() {
    ZeroMemory(g_key_state, sizeof(g_key_state));
}

static void ClearGamepadState() {
    ZeroMemory(&g_gamepad_state, sizeof(g_gamepad_state));
    ZeroMemory(g_joystick_axes, sizeof(g_joystick_axes));
    ZeroMemory(g_joystick_buttons, sizeof(g_joystick_buttons));
}

static void MarkCoreWindowInputStateChanged() {
    InterlockedExchange64(&g_coreWindowInputStateChangedMs, (LONGLONG)GetTickCount64());
}

static bool CoreWindowAcceptsInput() {
    if (InterlockedCompareExchange(&g_coreWindowVisibleForInput, 1, 1) == 0 ||
        InterlockedCompareExchange(&g_coreWindowActivatedForInput, 1, 1) == 0) {
        return false;
    }
    const LONGLONG changed = InterlockedCompareExchange64(&g_coreWindowInputStateChangedMs, 0, 0);
    return changed == 0 || ((LONGLONG)GetTickCount64() - changed) >= 250;
}

static int DisambiguateLeftRightKey(VirtualKey virtualKey, const CorePhysicalKeyStatus& status, int glfwKey) {
    switch ((int)virtualKey) {
    case 16:  
    case 160: 
    case 161: 
        return (status.ScanCode == 0x36) ? GLFW_KEY_RIGHT_SHIFT : GLFW_KEY_LEFT_SHIFT;
    case 17:  
    case 162: 
    case 163: 
        return status.IsExtendedKey ? GLFW_KEY_RIGHT_CONTROL : GLFW_KEY_LEFT_CONTROL;
    case 18:  
    case 164: 
    case 165: 
        return status.IsExtendedKey ? GLFW_KEY_RIGHT_ALT : GLFW_KEY_LEFT_ALT;
    default:
        return glfwKey;
    }
}

static void DispatchKeyEvent(VirtualKey virtualKey, const CorePhysicalKeyStatus& status, int action) {
    if (!CoreWindowAcceptsInput()) {
        ClearKeyboardState();
        return;
    }

    int glfwKey = MapVirtualKeyToGlfw(virtualKey);
    if (glfwKey == GLFW_KEY_UNKNOWN) return;
    glfwKey = DisambiguateLeftRightKey(virtualKey, status, glfwKey);

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
    if (!CoreWindowAcceptsInput()) return;
    if (codepoint == 0) return;
    if (g_char_cb) {
        g_char_cb((GLFWwindow*)&g_fake_window, codepoint);
    }
    if (g_charmods_cb) {
        g_charmods_cb((GLFWwindow*)&g_fake_window, codepoint, CurrentGlfwMods());
    }
}

static bool EnsureGameInput() {
    if (g_gameInput) return true;
    if (g_gameInputCreateTried) return false;

    g_gameInputCreateTried = true;
    HRESULT hr = GameInputCreate(g_gameInput.GetAddressOf());
    if (FAILED(hr) || !g_gameInput) {
        ShimLog("GameInputCreate failed hr=0x%08X", hr);
        return false;
    }

    g_gameInput->SetFocusPolicy(GameInputExclusiveForegroundInput);
    ShimLog("GameInput initialized");
    return true;
}

static float ClampGamepadAxis(float value) {
    if (value < -1.0f) return -1.0f;
    if (value > 1.0f) return 1.0f;
    return value;
}

static float AbsGamepadAxis(float value) {
    return value < 0.0f ? -value : value;
}

static bool LegacyControllerModMode() {
    const bool enabled = EnvFlagEnabled(L"MC_LEGACY_CONTROLLER_MOD");
    if (!g_legacyControllerModModeLogged || enabled != g_lastLegacyControllerModMode) {
        g_legacyControllerModModeLogged = true;
        g_lastLegacyControllerModMode = enabled;
        ShimLog("Legacy controller mod input mode: %s", enabled ? "enabled" : "disabled");
    }
    return enabled;
}

static float ShapeStickAxisForLegacyControllerMods(float value) {
    value = ClampGamepadAxis(value);
    if (!LegacyControllerModMode()) return value;

    if (AbsGamepadAxis(value) < 0.08f) {
        return 0.0f;
    }
    return ClampGamepadAxis(value * 1.12f);
}

static unsigned char GamepadButton(GameInputGamepadButtons buttons, GameInputGamepadButtons mask) {
    return (buttons & mask) ? GLFW_PRESS : GLFW_RELEASE;
}

static void ConvertGameInputGamepadState(const GameInputGamepadState& state) {
    ClearGamepadState();

    g_gamepad_state.buttons[GLFW_GAMEPAD_BUTTON_A] = GamepadButton(state.buttons, GameInputGamepadA);
    g_gamepad_state.buttons[GLFW_GAMEPAD_BUTTON_B] = GamepadButton(state.buttons, GameInputGamepadB);
    g_gamepad_state.buttons[GLFW_GAMEPAD_BUTTON_X] = GamepadButton(state.buttons, GameInputGamepadX);
    g_gamepad_state.buttons[GLFW_GAMEPAD_BUTTON_Y] = GamepadButton(state.buttons, GameInputGamepadY);
    g_gamepad_state.buttons[GLFW_GAMEPAD_BUTTON_LEFT_BUMPER] = GamepadButton(state.buttons, GameInputGamepadLeftShoulder);
    g_gamepad_state.buttons[GLFW_GAMEPAD_BUTTON_RIGHT_BUMPER] = GamepadButton(state.buttons, GameInputGamepadRightShoulder);
    g_gamepad_state.buttons[GLFW_GAMEPAD_BUTTON_BACK] = GamepadButton(state.buttons, GameInputGamepadView);
    g_gamepad_state.buttons[GLFW_GAMEPAD_BUTTON_START] = GamepadButton(state.buttons, GameInputGamepadMenu);
    g_gamepad_state.buttons[GLFW_GAMEPAD_BUTTON_GUIDE] = GLFW_RELEASE;
    g_gamepad_state.buttons[GLFW_GAMEPAD_BUTTON_LEFT_THUMB] = GamepadButton(state.buttons, GameInputGamepadLeftThumbstick);
    g_gamepad_state.buttons[GLFW_GAMEPAD_BUTTON_RIGHT_THUMB] = GamepadButton(state.buttons, GameInputGamepadRightThumbstick);
    g_gamepad_state.buttons[GLFW_GAMEPAD_BUTTON_DPAD_UP] = GamepadButton(state.buttons, GameInputGamepadDPadUp);
    g_gamepad_state.buttons[GLFW_GAMEPAD_BUTTON_DPAD_RIGHT] = GamepadButton(state.buttons, GameInputGamepadDPadRight);
    g_gamepad_state.buttons[GLFW_GAMEPAD_BUTTON_DPAD_DOWN] = GamepadButton(state.buttons, GameInputGamepadDPadDown);
    g_gamepad_state.buttons[GLFW_GAMEPAD_BUTTON_DPAD_LEFT] = GamepadButton(state.buttons, GameInputGamepadDPadLeft);

    g_gamepad_state.axes[GLFW_GAMEPAD_AXIS_LEFT_X] = ShapeStickAxisForLegacyControllerMods(state.leftThumbstickX);
    g_gamepad_state.axes[GLFW_GAMEPAD_AXIS_LEFT_Y] = ShapeStickAxisForLegacyControllerMods(-state.leftThumbstickY);
    g_gamepad_state.axes[GLFW_GAMEPAD_AXIS_RIGHT_X] = ShapeStickAxisForLegacyControllerMods(state.rightThumbstickX);
    g_gamepad_state.axes[GLFW_GAMEPAD_AXIS_RIGHT_Y] = ShapeStickAxisForLegacyControllerMods(-state.rightThumbstickY);
    g_gamepad_state.axes[GLFW_GAMEPAD_AXIS_LEFT_TRIGGER] = ClampGamepadAxis(state.leftTrigger * 2.0f - 1.0f);
    g_gamepad_state.axes[GLFW_GAMEPAD_AXIS_RIGHT_TRIGGER] = ClampGamepadAxis(state.rightTrigger * 2.0f - 1.0f);

    memcpy(g_joystick_axes, g_gamepad_state.axes, sizeof(g_joystick_axes));
    memcpy(g_joystick_buttons, g_gamepad_state.buttons, sizeof(g_joystick_buttons));
}

static bool PollGameInputGamepad(bool fireCallbacks) {
    if (!EnsureGameInput()) return false;

    if (!CoreWindowAcceptsInput()) {
        ClearGamepadState();
        return g_gamepad_present;
    }

    const ULONGLONG nowMs = GetTickCount64();
    if (g_haveGameInputPollCache && nowMs - g_lastGameInputPollMs <= 4) {
        return g_gamepad_present;
    }
    g_haveGameInputPollCache = true;
    g_lastGameInputPollMs = nowMs;

    ComPtr<IGameInputReading> reading;
    HRESULT hr = g_gameInput->GetCurrentReading(GameInputKindGamepad, nullptr, reading.GetAddressOf());

    GameInputGamepadState state = {};
    const bool present = SUCCEEDED(hr) && reading && reading->GetGamepadState(&state);
    const bool previousPresent = g_gamepad_present;
    g_gamepad_present = present;

    if (!present) {
        ClearGamepadState();
        g_haveGameInputGamepadState = false;
        if (fireCallbacks) {
            ReleaseControllerBridge();
        }

        if (previousPresent && fireCallbacks && g_joystick_cb) {
            g_joystick_cb(GLFW_JOYSTICK_1, GLFW_DISCONNECTED);
        }
        if (g_controller_log_count < 6) {
            ++g_controller_log_count;
            ShimLog("GameInput gamepad unavailable hr=0x%08X", hr);
        }
        return false;
    }

    ConvertGameInputGamepadState(state);
    if (fireCallbacks) {
        UpdateControllerBridge(state);
    }

    if (!g_haveGameInputGamepadState) {
        g_haveGameInputGamepadState = true;
        g_lastGameInputGamepadState = state;
        ShimLog("GameInput gamepad ready kind=0x%X buttons=0x%X lt=%.2f rt=%.2f lx=%.2f ly=%.2f rx=%.2f ry=%.2f",
            reading->GetInputKind(), (unsigned)state.buttons,
            state.leftTrigger, state.rightTrigger,
            state.leftThumbstickX, state.leftThumbstickY,
            state.rightThumbstickX, state.rightThumbstickY);
    } else if (g_controller_log_count < 24 &&
        (state.buttons != g_lastGameInputGamepadState.buttons ||
         state.leftTrigger != g_lastGameInputGamepadState.leftTrigger ||
         state.rightTrigger != g_lastGameInputGamepadState.rightTrigger ||
         state.leftThumbstickX != g_lastGameInputGamepadState.leftThumbstickX ||
         state.leftThumbstickY != g_lastGameInputGamepadState.leftThumbstickY ||
         state.rightThumbstickX != g_lastGameInputGamepadState.rightThumbstickX ||
         state.rightThumbstickY != g_lastGameInputGamepadState.rightThumbstickY)) {
        ++g_controller_log_count;
        ShimLog("GameInput gamepad state buttons=0x%X lt=%.2f rt=%.2f lx=%.2f ly=%.2f rx=%.2f ry=%.2f",
            (unsigned)state.buttons, state.leftTrigger, state.rightTrigger,
            state.leftThumbstickX, state.leftThumbstickY,
            state.rightThumbstickX, state.rightThumbstickY);
        g_lastGameInputGamepadState = state;
    }

    if (!previousPresent && fireCallbacks && g_joystick_cb) {
        ShimLog("GameInput gamepad callback GLFW_CONNECTED");
        g_joystick_cb(GLFW_JOYSTICK_1, GLFW_CONNECTED);
    }
    return true;
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

static void InstallCoreWindowLifecycleHooks() {
    if (g_lifecycleHooksInstalled || !g_coreWindow) return;

    g_visibilityHandler = Callback<CoreWindowVisibilityHandler>(
        [](ICoreWindow*, IVisibilityChangedEventArgs* args) -> HRESULT {
            boolean visible = true;
            if (args) {
                args->get_Visible(&visible);
            }
            const LONG next = visible ? 1 : 0;
            const LONG old = InterlockedExchange(&g_coreWindowVisibleForInput, next);
            if (old != next) {
                MarkCoreWindowInputStateChanged();
                ClearKeyboardState();
                ClearGamepadState();
                ShimLog("CoreWindow VisibilityChanged visible=%d", next);
            }
            return S_OK;
        });

    HRESULT hr = g_coreWindow->add_VisibilityChanged(g_visibilityHandler.Get(), &g_visibilityToken);
    if (FAILED(hr)) {
        ShimLog("add_VisibilityChanged failed hr=0x%08X", hr);
    }

    g_activatedHandler = Callback<CoreWindowActivatedHandler>(
        [](ICoreWindow*, IWindowActivatedEventArgs* args) -> HRESULT {
            CoreWindowActivationState state = CoreWindowActivationState_CodeActivated;
            if (args) {
                args->get_WindowActivationState(&state);
            }
            const LONG next = state == CoreWindowActivationState_Deactivated ? 0 : 1;
            const LONG old = InterlockedExchange(&g_coreWindowActivatedForInput, next);
            if (old != next) {
                MarkCoreWindowInputStateChanged();
                ClearKeyboardState();
                ClearGamepadState();
                ShimLog("CoreWindow Activated state=%d active=%d", (int)state, next);
            }
            if (g_focus_cb) {
                g_focus_cb((GLFWwindow*)&g_fake_window, next ? GLFW_TRUE : GLFW_FALSE);
            }
            return S_OK;
        });

    hr = g_coreWindow->add_Activated(g_activatedHandler.Get(), &g_activatedToken);
    if (FAILED(hr)) {
        ShimLog("add_Activated failed hr=0x%08X", hr);
    }

    g_lifecycleHooksInstalled = true;
    ShimLog("CoreWindow lifecycle hooks installed");
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
    InstallCoreWindowLifecycleHooks();
    InstallKeyboardHooks();
    InstallPointerHooks();
    InstallMouseDeviceHooks();
    StartRemoteMouseServer();
    return true;
}

static int ScaleDimensionToPixels(FLOAT value, double scale, int fallback) {
    if (value <= 0.0f) return fallback;
    if (scale <= 0.0) scale = 1.0;
    const double scaled = (double)value * scale;
    return scaled > 0.0 ? (int)(scaled + 0.5) : fallback;
}

static bool UseRawScaledFramebuffer() {
    return !EnvFlagDisabled(L"MC_USE_RAW_SCALED_FRAMEBUFFER");
}

static void GetDisplayScale(double& scaleX, double& scaleY) {
    scaleX = 1.0;
    scaleY = 1.0;

    wchar_t envScale[32] = {};
    DWORD envLen = GetEnvironmentVariableW(
        L"MC_RAW_PIXELS_PER_VIEW_PIXEL",
        envScale,
        (DWORD)(sizeof(envScale) / sizeof(envScale[0])));
    if (envLen > 0 && envLen < (DWORD)(sizeof(envScale) / sizeof(envScale[0]))) {
        wchar_t* end = nullptr;
        const double value = wcstod(envScale, &end);
        if (value >= 0.5 && value <= 8.0) {
            scaleX = value;
            scaleY = value;
            return;
        }
    }

    ComPtr<IDisplayInformationStatics> displayStatics;
    HRESULT hr = GetActivationFactory(
        HStringReference(RuntimeClass_Windows_Graphics_Display_DisplayInformation).Get(),
        &displayStatics);
    if (FAILED(hr)) return;

    ComPtr<IDisplayInformation> displayInfo;
    hr = displayStatics->GetForCurrentView(displayInfo.GetAddressOf());
    if (FAILED(hr) || !displayInfo) return;

    ComPtr<IDisplayInformation2> displayInfo2;
    if (SUCCEEDED(displayInfo.As(&displayInfo2)) && displayInfo2) {
        DOUBLE rawPixelsPerViewPixel = 1.0;
        if (SUCCEEDED(displayInfo2->get_RawPixelsPerViewPixel(&rawPixelsPerViewPixel)) &&
            rawPixelsPerViewPixel > 0.0) {
            scaleX = rawPixelsPerViewPixel;
            scaleY = rawPixelsPerViewPixel;
            return;
        }
    }

    FLOAT logicalDpi = 96.0f;
    if (SUCCEEDED(displayInfo->get_LogicalDpi(&logicalDpi)) && logicalDpi > 0.0f) {
        scaleX = logicalDpi / 96.0;
        scaleY = logicalDpi / 96.0;
    }
}

static void RefreshWindowMetrics(bool fireCallbacks) {
    if (!AcquireCoreWindow()) return;

    Rect bounds = {};
    if (FAILED(g_coreWindow->get_Bounds(&bounds))) return;

    double scaleX = 1.0;
    double scaleY = 1.0;
    GetDisplayScale(scaleX, scaleY);

    const int newWindowWidth = bounds.Width > 0 ? (int)(bounds.Width + 0.5f) : g_window_width;
    const int newWindowHeight = bounds.Height > 0 ? (int)(bounds.Height + 0.5f) : g_window_height;
    // CoreWindow bounds are UWP view pixels, while Mesa presents a raw-pixel
    // surface. GLFW must expose framebuffer/video-mode sizes in raw pixels or
    // Minecraft renders into only part of a 4K surface and overflows 720p.
    const bool rawScaledFramebuffer = UseRawScaledFramebuffer();
    const int newFramebufferWidth = rawScaledFramebuffer
        ? ScaleDimensionToPixels(bounds.Width, scaleX, g_framebuffer_width)
        : newWindowWidth;
    const int newFramebufferHeight = rawScaledFramebuffer
        ? ScaleDimensionToPixels(bounds.Height, scaleY, g_framebuffer_height)
        : newWindowHeight;
    const float newContentScaleX = (float)scaleX;
    const float newContentScaleY = (float)scaleY;

    if (newWindowWidth == g_window_width &&
        newWindowHeight == g_window_height &&
        newFramebufferWidth == g_framebuffer_width &&
        newFramebufferHeight == g_framebuffer_height &&
        newContentScaleX == g_content_scale_x &&
        newContentScaleY == g_content_scale_y) {
        return;
    }

    g_window_width = newWindowWidth;
    g_window_height = newWindowHeight;
    g_framebuffer_width = newFramebufferWidth;
    g_framebuffer_height = newFramebufferHeight;
    g_content_scale_x = newContentScaleX;
    g_content_scale_y = newContentScaleY;
    g_vidmode.width = g_framebuffer_width;
    g_vidmode.height = g_framebuffer_height;
    g_fake_window.width = g_window_width;
    g_fake_window.height = g_window_height;
    ShimLog("Window size %dx%d, framebuffer %dx%d, scale %.3fx%.3f%s",
        g_window_width, g_window_height,
        g_framebuffer_width, g_framebuffer_height,
        g_content_scale_x, g_content_scale_y,
        rawScaledFramebuffer ? " raw-scaled" : "");

    if (fireCallbacks) {
        if (g_winsize_cb) g_winsize_cb((GLFWwindow*)&g_fake_window, g_window_width, g_window_height);
        if (g_fbsize_cb) g_fbsize_cb((GLFWwindow*)&g_fake_window, g_framebuffer_width, g_framebuffer_height);
        if (g_contentscale_cb) g_contentscale_cb((GLFWwindow*)&g_fake_window, g_content_scale_x, g_content_scale_y);
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
    size.Width = (FLOAT)g_framebuffer_width;
    size.Height = (FLOAT)g_framebuffer_height;

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
    ShimLog("Created EGL PropertySet surface descriptor (%dx%d)",
        g_framebuffer_width, g_framebuffer_height);
    return true;
}

// ---------------------------------------------------------------------------
// Graphics runtime loader
// ---------------------------------------------------------------------------
static bool LoadMesaEGL() {
    if (g_libEGL && g_opengl32) return true;

    wchar_t runtimeDir[MAX_PATH];
    wchar_t packagePrefix[MAX_PATH];
    SelectGraphicsRuntimeDir(runtimeDir, MAX_PATH, packagePrefix, MAX_PATH);

    struct RuntimeDll {
        const wchar_t* file;
        const char* label;
        bool required;
    };

    const RuntimeDll siblings[] = {
        { L"libglapi.dll", "libglapi.dll", false },
        { L"z-1.dll", "z-1.dll", false },
        { L"libgallium_wgl.dll", "libgallium_wgl.dll", false },
        { L"libGLESv2.dll", "libGLESv2.dll", false },
        { L"libGLESv1_CM.dll", "libGLESv1_CM.dll", false },
        { L"glu32.dll", "glu32.dll", false },
        { L"dxil.dll", "dxil.dll", false },
    };

    for (const RuntimeDll& dll : siblings) {
        wchar_t absolutePath[MAX_PATH];
        wchar_t packagedPath[MAX_PATH];
        RuntimeDllPath(runtimeDir, packagePrefix, dll.file,
            absolutePath, MAX_PATH, packagedPath, MAX_PATH);
        if (GetFileAttributesW(absolutePath) != INVALID_FILE_ATTRIBUTES) {
            HMODULE loaded = LoadPackagedOrFile(packagedPath, absolutePath, dll.label);
            if (_wcsicmp(dll.file, L"libGLESv2.dll") == 0) {
                g_libGLESv2 = loaded;
            }
        }
    }

    wchar_t glPath[MAX_PATH];
    wchar_t glPackaged[MAX_PATH];
    wchar_t eglPath[MAX_PATH];
    wchar_t eglPackaged[MAX_PATH];
    RuntimeDllPath(runtimeDir, packagePrefix, L"opengl32.dll", glPath, MAX_PATH, glPackaged, MAX_PATH);
    RuntimeDllPath(runtimeDir, packagePrefix, L"libEGL.dll", eglPath, MAX_PATH, eglPackaged, MAX_PATH);

    ShimLog("libEGL=%S", eglPath);
    ShimLog("opengl32=%S", glPath);

    g_opengl32 = LoadPackagedOrFile(glPackaged, glPath, "opengl32.dll");
    g_libEGL = LoadPackagedOrFile(eglPackaged, eglPath, "libEGL.dll");
    if (!g_opengl32 || !g_libEGL) {
        ShimLog("Graphics loader failed gl=%p egl=%p err=%u", g_opengl32, g_libEGL, GetLastError());
        return false;
    }

    PFN_proc_init procInit = (PFN_proc_init)GetProcAddress(g_opengl32, "proc_init");
    if (procInit) {
        ShimLog("Calling opengl32!proc_init");
        procInit();
        ShimLog("opengl32!proc_init returned");
    } else if (g_graphicsRuntimeUsesGles) {
        ShimLog("opengl32!proc_init not exported");
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

    const EGLenum eglApi = g_graphicsRuntimeUsesGles ? EGL_OPENGL_ES_API : EGL_OPENGL_API;
    if (!p_eglBindAPI(eglApi)) {
        ReportEglError(g_graphicsRuntimeUsesGles ? "eglBindAPI(EGL_OPENGL_ES_API)" : "eglBindAPI(EGL_OPENGL_API)");
        return false;
    }

    const EGLint renderableType = g_graphicsRuntimeUsesGles ? EGL_OPENGL_ES3_BIT_KHR : EGL_OPENGL_BIT;
    const EGLint configAttrs[] = {
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_DEPTH_SIZE, 24,
        EGL_STENCIL_SIZE, 8,
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE, renderableType,
        EGL_NONE
    };

    EGLint numConfigs = 0;
    if (!p_eglChooseConfig(g_eglDisplay, configAttrs, &g_eglConfig, 1, &numConfigs) || numConfigs < 1) {
        ReportEglError("eglChooseConfig");
        return false;
    }

    // The raw CoreWindow path is the stable Mesa UWP path. The PropertySet path
    // can report success but present a black surface on Xbox.
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

    if (g_eglSurface == EGL_NO_SURFACE && g_nativeWindowPropertySet && p_eglCreatePlatformWindowSurface) {
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

    const bool legacyOpenGlContext = EnvFlagEnabled(L"MC_LEGACY_OPENGL_CONTEXT");
    const EGLint desktopContextAttrs[] = {
        EGL_CONTEXT_MAJOR_VERSION_KHR, 3,
        EGL_CONTEXT_MINOR_VERSION_KHR, 2,
        EGL_CONTEXT_OPENGL_PROFILE_MASK_KHR, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT_KHR,
        EGL_NONE
    };
    const EGLint legacyDesktopContextAttrs[] = {
        EGL_CONTEXT_MAJOR_VERSION_KHR, 3,
        EGL_CONTEXT_MINOR_VERSION_KHR, 2,
        EGL_CONTEXT_OPENGL_PROFILE_MASK_KHR, EGL_CONTEXT_OPENGL_COMPATIBILITY_PROFILE_BIT_KHR,
        EGL_NONE
    };
    const EGLint glesContextAttrs[] = {
        EGL_CONTEXT_CLIENT_VERSION, 3,
        EGL_NONE
    };
    const EGLint* contextAttrs = g_graphicsRuntimeUsesGles ? glesContextAttrs : (legacyOpenGlContext ? legacyDesktopContextAttrs : desktopContextAttrs);
    ShimLog("OpenGL context request: %s", g_graphicsRuntimeUsesGles ? "GLES3" : (legacyOpenGlContext ? "3.2 compatibility" : "3.2 core"));
    g_eglContext = p_eglCreateContext(g_eglDisplay, g_eglConfig, EGL_NO_CONTEXT, contextAttrs);
    if (g_eglContext == EGL_NO_CONTEXT) {
        ReportEglError(g_graphicsRuntimeUsesGles ? "eglCreateContext(GLES3)" : (legacyOpenGlContext ? "eglCreateContext(3.2 compatibility)" : "eglCreateContext(3.2 core)"));
        g_eglContext = p_eglCreateContext(g_eglDisplay, g_eglConfig, EGL_NO_CONTEXT, nullptr);
    }
    if (g_eglContext == EGL_NO_CONTEXT) {
        ReportEglError("eglCreateContext(fallback)");
        return false;
    }

    const char* vendor = p_eglQueryString ? p_eglQueryString(g_eglDisplay, EGL_VENDOR) : nullptr;
    const char* version = p_eglQueryString ? p_eglQueryString(g_eglDisplay, EGL_VERSION) : nullptr;
    ShimLog("EGL initialized %d.%d vendor=%s version=%s context=%p unbound creatorTid=%lu",
        major, minor, vendor ? vendor : "?", version ? version : "?",
        g_eglContext, GetCurrentThreadId());
    return true;
}

// ---------------------------------------------------------------------------
// DLL entry
// ---------------------------------------------------------------------------
BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        wchar_t dir[MAX_PATH];
        DWORD logLen = GetEnvironmentVariableW(L"MC_LOG_DIR", dir, MAX_PATH);
        if (logLen == 0 || logLen >= MAX_PATH) {
            GetRuntimeDir(dir, MAX_PATH);
        }
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
    g_fake_window = {0x58574C47u, g_window_width, g_window_height, FALSE, NULL};
    g_initialised = TRUE;
    ShimLog("glfwInit OK window %dx%d framebuffer %dx%d",
        g_window_width, g_window_height,
        g_framebuffer_width, g_framebuffer_height);
    return GLFW_TRUE;
}

static double CurrentPointerScaleX() {
    Rect bounds = {};
    if (g_coreWindow && SUCCEEDED(g_coreWindow->get_Bounds(&bounds)) && bounds.Width > 0) {
        return g_window_width > 0 ? (double)g_window_width / (double)bounds.Width : 1.0;
    }
    return 1.0;
}
static double CurrentPointerScaleY() {
    Rect bounds = {};
    if (g_coreWindow && SUCCEEDED(g_coreWindow->get_Bounds(&bounds)) && bounds.Height > 0) {
        return g_window_height > 0 ? (double)g_window_height / (double)bounds.Height : 1.0;
    }
    return 1.0;
}
static void DispatchCursorEnter(bool entered) {
    if (g_cursor_inside == entered) return;
    g_cursor_inside = entered;
    if (g_cursorenter_cb && MouseCompanionActive()) {
        g_cursorenter_cb((GLFWwindow*)&g_fake_window, entered ? GLFW_TRUE : GLFW_FALSE);
    }
}
static void DispatchCursorPos(double x, double y) {
    if (g_cursorMode != GLFW_CURSOR_DISABLED) {
        if (x < 0.0) x = 0.0;
        if (y < 0.0) y = 0.0;
        if (g_window_width > 0 && x > (double)g_window_width) x = (double)g_window_width;
        if (g_window_height > 0 && y > (double)g_window_height) y = (double)g_window_height;
    }

    g_cursor_x = x;
    g_cursor_y = y;

    
    
    
    
    
    
    
    if (g_cursorMode != GLFW_CURSOR_DISABLED) {
        g_menu_abs_x = g_cursor_x;
        g_menu_abs_y = g_cursor_y;
        SendCursorOverlayState();
    }

    DispatchCursorEnter(true);
    if (g_cursorpos_cb && MouseCompanionActive()) {
        g_cursorpos_cb((GLFWwindow*)&g_fake_window, g_cursor_x, g_cursor_y);
    }
}
static double ClampDouble(double value, double minValue, double maxValue) {
    if (value < minValue) return minValue;
    if (value > maxValue) return maxValue;
    return value;
}
static double CursorMaxX() {
    return g_window_width > 1 ? (double)(g_window_width - 1) : 0.0;
}
static double CursorMaxY() {
    return g_window_height > 1 ? (double)(g_window_height - 1) : 0.0;
}
static double ProtocolToWindowX(double x) {
    return g_window_width > 0 ? x * ((double)g_window_width / kProtocolWidth) : x;
}
static double ProtocolToWindowY(double y) {
    return g_window_height > 0 ? y * ((double)g_window_height / kProtocolHeight) : y;
}
static double WindowToProtocolX(double x) {
    return g_window_width > 0 ? x * (kProtocolWidth / (double)g_window_width) : x;
}
static double WindowToProtocolY(double y) {
    return g_window_height > 0 ? y * (kProtocolHeight / (double)g_window_height) : y;
}
static void SendCursorOverlayState() {
    WSADATA wsa{};
    WSAStartup(MAKEWORD(2, 2), &wsa);

    if (g_cursor_overlay_socket == INVALID_SOCKET) {
        g_cursor_overlay_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (g_cursor_overlay_socket == INVALID_SOCKET) return;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(7333);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    char packet[64] = {};
    const int visible = (g_cursorMode == GLFW_CURSOR_NORMAL) ? 1 : 0;
    
    sprintf_s(packet, "CURSOR:%.0f,%.0f,%d",
        WindowToProtocolX(g_menu_abs_x), WindowToProtocolY(g_menu_abs_y), visible);
    sendto(g_cursor_overlay_socket, packet, (int)strlen(packet), 0,
        reinterpret_cast<const sockaddr*>(&addr), sizeof(addr));
}
static void RememberMouseRelayStatusAddress(const sockaddr_in& remote) {
    sockaddr_in statusTo = remote;
    statusTo.sin_port = htons(7332);
    AcquireSRWLockExclusive(&g_mouse_relay_status_lock);
    g_mouse_relay_status_addr = statusTo;
    g_have_mouse_relay_status_addr = true;
    ReleaseSRWLockExclusive(&g_mouse_relay_status_lock);
}
static void SendMouseRelayStatusText(const char* text) {
    WSADATA wsa{};
    WSAStartup(MAKEWORD(2, 2), &wsa);

    if (g_mouse_relay_status_socket == INVALID_SOCKET) {
        g_mouse_relay_status_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (g_mouse_relay_status_socket == INVALID_SOCKET) return;
    }

    sockaddr_in statusTo{};
    bool haveAddress = false;
    AcquireSRWLockShared(&g_mouse_relay_status_lock);
    if (g_have_mouse_relay_status_addr) {
        statusTo = g_mouse_relay_status_addr;
        haveAddress = true;
    }
    ReleaseSRWLockShared(&g_mouse_relay_status_lock);

    if (!haveAddress) return;
    sendto(g_mouse_relay_status_socket, text, (int)strlen(text), 0,
        reinterpret_cast<const sockaddr*>(&statusTo), sizeof(statusTo));
}
static void SendMouseRelayCursorSync(double x, double y) {
    char packet[64] = {};
    sprintf_s(packet, "SYNC:%.0f,%.0f", x, y);
    SendMouseRelayStatusText(packet);
}
static void SendMouseRelayWindowCursorSync(double x, double y) {
    char packet[64] = {};
    sprintf_s(packet, "SYNCW:%.0f,%.0f", x, y);
    SendMouseRelayStatusText(packet);
}
static void DispatchMouseDelta(double dx, double dy) {
    if (dx == 0.0 && dy == 0.0) return;

    if (!g_cursorDisabled) {
        g_menu_abs_x = ClampDouble(g_menu_abs_x + dx, 0.0, CursorMaxX());
        g_menu_abs_y = ClampDouble(g_menu_abs_y + dy, 0.0, CursorMaxY());
        DispatchCursorPos(g_menu_abs_x, g_menu_abs_y);
    } else {
        DispatchCursorPos(g_cursor_x + dx, g_cursor_y + dy);
    }

    if (g_mouse_log_count < 24) {
        ++g_mouse_log_count;
        ShimLog("Mouse delta dx=%.3f dy=%.3f cursor=%.1f,%.1f mode=%d",
            dx, dy, g_cursor_x, g_cursor_y, g_cursorMode);
    }
}
static void DispatchMouseAbsolute(double x, double y) {
    
    g_menu_abs_x = ClampDouble(ProtocolToWindowX(x), 0.0, CursorMaxX());
    g_menu_abs_y = ClampDouble(ProtocolToWindowY(y), 0.0, CursorMaxY());
    DispatchCursorPos(g_menu_abs_x, g_menu_abs_y);
    SendCursorOverlayState();
    if (g_remote_mouse_log_count < 12) {
        ++g_remote_mouse_log_count;
        ShimLog("RemoteMouse ABS protocol=%.1f,%.1f -> window=%.1f,%.1f (win %dx%d fb %dx%d scale %.3f)",
            x, y, g_menu_abs_x, g_menu_abs_y,
            g_window_width, g_window_height, g_width, g_height, g_content_scale);
    }
}
static void DispatchMouseWindowAbsolute(double x, double y) {
    g_menu_abs_x = ClampDouble(x, 0.0, CursorMaxX());
    g_menu_abs_y = ClampDouble(y, 0.0, CursorMaxY());
    DispatchCursorPos(g_menu_abs_x, g_menu_abs_y);
    SendCursorOverlayState();
    if (g_remote_mouse_log_count < 12) {
        ++g_remote_mouse_log_count;
        ShimLog("RemoteMouse ABSW window=%.1f,%.1f (win %dx%d fb %dx%d scale %.3f)",
            g_menu_abs_x, g_menu_abs_y,
            g_window_width, g_window_height, g_width, g_height, g_content_scale);
    }
}
static void FireRemoteMouseButtonCallback(int button, int action) {
    if (g_mousebutton_cb && MouseCompanionActive()) {
        const int mods = CurrentGlfwMods();
        g_mousebutton_cb((GLFWwindow*)&g_fake_window, button, action, mods);
    }
}
static void SetRemoteMouseButtonState(int button, int action) {
    if (button < 0 || button >= (int)sizeof(g_mouse_state)) return;
    const unsigned char state = action == GLFW_RELEASE ? GLFW_RELEASE : GLFW_PRESS;
    if (g_mouse_state[button] == state) return;

    g_mouse_state[button] = state;
    FireRemoteMouseButtonCallback(button, action);
}
static void SetMouseButtonState(int button, int action, bool fireCallback) {
    if (button < 0 || button >= (int)sizeof(g_mouse_state)) return;
    const unsigned char state = action == GLFW_RELEASE ? GLFW_RELEASE : GLFW_PRESS;
    if (g_mouse_state[button] == state) return;

    g_mouse_state[button] = state;
    if (fireCallback) {
        FireRemoteMouseButtonCallback(button, state);
    }
}
static bool AnyMouseButtonDown() {
    for (int i = 0; i < (int)sizeof(g_mouse_state); ++i) {
        if (g_mouse_state[i]) return true;
    }
    return false;
}
static void MarkMouseCompanionSeen() {
    InterlockedExchange(&g_last_companion_tick, (LONG)GetTickCount());
}
static bool MouseCompanionActive() {
    const LONG last = InterlockedCompareExchange(&g_last_companion_tick, 0, 0);
    if (last == 0) return false;
    return (DWORD)(GetTickCount() - (DWORD)last) <= kMouseCompanionTimeoutMs;
}
static void FlushMouseButtonsForDeactivate() {
    for (int i = 0; i < (int)sizeof(g_mouse_state); ++i) {
        if (g_mouse_state[i]) {
            g_mouse_state[i] = GLFW_RELEASE;
            if (g_mousebutton_cb) {
                g_mousebutton_cb((GLFWwindow*)&g_fake_window, i, GLFW_RELEASE, CurrentGlfwMods());
            }
        }
    }
}
static void QueueRemoteMouseButton(int buttonBit, int value, int& buttons, int& changes) {
    if (value < 0) return;
    changes |= buttonBit;
    if (value) buttons |= buttonBit;
    else buttons &= ~buttonBit;
}
static void DrainRemoteMouseInput() {
    double dx = 0.0;
    double dy = 0.0;
    bool absPending = false;
    double absX = 0.0;
    double absY = 0.0;
    double wheelY = 0.0;
    int buttons = 0;
    int buttonChanges = 0;

    AcquireSRWLockExclusive(&g_remote_mouse_lock);
    dx = g_remote_mouse_dx;
    dy = g_remote_mouse_dy;
    absPending = g_remote_mouse_abs_pending;
    absX = g_remote_mouse_abs_x;
    absY = g_remote_mouse_abs_y;
    const bool absWindow = g_remote_mouse_abs_window;
    wheelY = g_remote_mouse_wheel_y;
    buttons = g_remote_mouse_buttons;
    buttonChanges = g_remote_mouse_button_changes;
    g_remote_mouse_dx = 0.0;
    g_remote_mouse_dy = 0.0;
    g_remote_mouse_abs_pending = false;
    g_remote_mouse_abs_window = false;
    g_remote_mouse_wheel_y = 0.0;
    g_remote_mouse_button_changes = 0;
    ReleaseSRWLockExclusive(&g_remote_mouse_lock);

    if (absPending) {
        if (absWindow) {
            DispatchMouseWindowAbsolute(absX, absY);
        } else {
            DispatchMouseAbsolute(absX, absY);
        }
    } else if (dx != 0.0 || dy != 0.0) {
        DispatchMouseDelta(dx, dy);
    }
    if (wheelY != 0.0 && g_scroll_cb && MouseCompanionActive()) {
        g_scroll_cb((GLFWwindow*)&g_fake_window, 0.0, wheelY);
    }

    if (buttonChanges) {
        if (buttonChanges & 1) {
        SetRemoteMouseButtonState(GLFW_MOUSE_BUTTON_LEFT,
            (buttons & 1) ? GLFW_PRESS : GLFW_RELEASE);
        }
        if (buttonChanges & 2) {
        SetRemoteMouseButtonState(GLFW_MOUSE_BUTTON_RIGHT,
            (buttons & 2) ? GLFW_PRESS : GLFW_RELEASE);
        }
        if (buttonChanges & 4) {
        SetRemoteMouseButtonState(GLFW_MOUSE_BUTTON_MIDDLE,
            (buttons & 4) ? GLFW_PRESS : GLFW_RELEASE);
        }
        if (buttonChanges & 8) {
        SetRemoteMouseButtonState(GLFW_MOUSE_BUTTON_4,
            (buttons & 8) ? GLFW_PRESS : GLFW_RELEASE);
        }
        if (buttonChanges & 16) {
        SetRemoteMouseButtonState(GLFW_MOUSE_BUTTON_5,
            (buttons & 16) ? GLFW_PRESS : GLFW_RELEASE);
        }
    }
}
static void StartRemoteMouseServer() {
    if (InterlockedExchange(&g_remote_mouse_server_started, 1) != 0) {
        return;
    }

    HANDLE thread = CreateThread(
        nullptr,
        0,
        [](LPVOID) -> DWORD {
            constexpr unsigned short kRemoteMousePort = 7331;
            ShimLog("RemoteMouse: UDP thread starting on port %u", kRemoteMousePort);

            WSADATA wsa{};
            const int wsaResult = WSAStartup(MAKEWORD(2, 2), &wsa);
            if (wsaResult != 0) {
                ShimLog("RemoteMouse: WSAStartup failed err=%d", wsaResult);
                return 0;
            }

            SOCKET sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
            if (sock == INVALID_SOCKET) {
                ShimLog("RemoteMouse: socket failed err=%d", WSAGetLastError());
                WSACleanup();
                return 0;
            }

            sockaddr_in addr{};
            addr.sin_family = AF_INET;
            addr.sin_port = htons(kRemoteMousePort);
            addr.sin_addr.s_addr = INADDR_ANY;
            if (bind(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
                ShimLog("RemoteMouse: bind UDP %u failed err=%d", kRemoteMousePort, WSAGetLastError());
                closesocket(sock);
                WSACleanup();
                return 0;
            }

            
            
            
            int rcvBuf = 256 * 1024;
            setsockopt(sock, SOL_SOCKET, SO_RCVBUF,
                reinterpret_cast<const char*>(&rcvBuf), sizeof(rcvBuf));
            DWORD rcvTimeoutMs = 50;
            setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO,
                reinterpret_cast<const char*>(&rcvTimeoutMs), sizeof(rcvTimeoutMs));

            ShimLog("RemoteMouse: listening on UDP port %u", kRemoteMousePort);
            char buf[64];
            unsigned int packetCount = 0;
            int lastSentStatusMode = -1;
            auto getStableModeForStatus = [&]() -> int {
                const int pendingMode = g_pendingMode;
                if (pendingMode >= 0) {
                    const ULONGLONG elapsed = GetTickCount64() - g_modeChangeTime;
                    if (elapsed < 150) {
                        return g_reportedMode;
                    }
                    g_reportedMode = pendingMode;
                    g_pendingMode = -1;
                }
                return g_reportedMode;
            };
            auto sendModeStatus = [&](bool force) {
                const int statusMode = getStableModeForStatus();
                if (!force && statusMode == lastSentStatusMode) return;
                lastSentStatusMode = statusMode;
                const char* status = (statusMode == GLFW_CURSOR_DISABLED) ? "MODE:GAMEPLAY" : "MODE:MENU";
                char packet[160] = {};
                sprintf_s(packet, "%s cursorw=%.0f,%.0f size=%dx%d menu=%dx%d",
                    status,
                    g_menu_abs_x, g_menu_abs_y,
                    g_window_width, g_window_height,
                    g_menu_window_width, g_menu_window_height);
                SendMouseRelayStatusText(packet);
            };

            while (true) {
                sockaddr_in from{};
                int fromLen = sizeof(from);
                const int len = recvfrom(sock, buf, sizeof(buf) - 1, 0,
                    reinterpret_cast<sockaddr*>(&from), &fromLen);
                if (len <= 0) {
                    
                    
                    sendModeStatus(false);
                    continue;
                }
                buf[len] = 0;
                RememberMouseRelayStatusAddress(from);
                MarkMouseCompanionSeen();

                if (strcmp(buf, "hello") == 0 || strcmp(buf, "ping") == 0) {
                    char ack[160] = {};
                    const int statusMode = getStableModeForStatus();
                    
                    
                    sprintf_s(ack, "javauwp_glfw_mouse:ready mode=%d cursor=%.0f,%.0f cursorw=%.0f,%.0f size=%dx%d menu=%dx%d",
                        statusMode,
                        WindowToProtocolX(g_menu_abs_x), WindowToProtocolY(g_menu_abs_y),
                        g_menu_abs_x, g_menu_abs_y,
                        g_window_width, g_window_height,
                        g_menu_window_width, g_menu_window_height);
                    sendto(sock, ack, (int)strlen(ack), 0,
                        reinterpret_cast<sockaddr*>(&from), fromLen);
                    sendModeStatus(true);
                    ShimLog("RemoteMouse: handshake replied");
                    continue;
                }

                float dx = 0.0f, dy = 0.0f, wheelY = 0.0f;
                int lb = -1, rb = -1, mb = -1, x1 = -1, x2 = -1;
                bool absolutePacket = false;
                bool absoluteWindowPacket = false;
                int fields = 0;
                if (strncmp(buf, "ABSW:", 5) == 0) {
                    absolutePacket = true;
                    absoluteWindowPacket = true;
                    fields = sscanf_s(buf + 5, "%f,%f,%d,%d,%d,%f,%d,%d",
                        &dx, &dy, &lb, &rb, &mb, &wheelY, &x1, &x2);
                } else if (strncmp(buf, "ABS:", 4) == 0) {
                    absolutePacket = true;
                    fields = sscanf_s(buf + 4, "%f,%f,%d,%d,%d,%f,%d,%d",
                        &dx, &dy, &lb, &rb, &mb, &wheelY, &x1, &x2);
                } else {
                    fields = sscanf_s(buf, "%f,%f,%d,%d,%d,%f,%d,%d",
                        &dx, &dy, &lb, &rb, &mb, &wheelY, &x1, &x2);
                }
                if (fields != 8) {
                    const char bad[] = "javauwp_glfw_mouse:bad_packet";
                    sendto(sock, bad, (int)sizeof(bad) - 1, 0,
                        reinterpret_cast<sockaddr*>(&from), fromLen);
                    continue;
                }

                AcquireSRWLockExclusive(&g_remote_mouse_lock);
                if (absolutePacket) {
                    g_remote_mouse_abs_x = (double)dx;
                    g_remote_mouse_abs_y = (double)dy;
                    g_remote_mouse_abs_window = absoluteWindowPacket;
                    g_remote_mouse_abs_pending = true;
                } else {
                    g_remote_mouse_dx += (double)dx;
                    g_remote_mouse_dy += (double)dy;
                }
                g_remote_mouse_wheel_y += (double)wheelY;
                QueueRemoteMouseButton(1, lb, g_remote_mouse_buttons, g_remote_mouse_button_changes);
                QueueRemoteMouseButton(2, rb, g_remote_mouse_buttons, g_remote_mouse_button_changes);
                QueueRemoteMouseButton(4, mb, g_remote_mouse_buttons, g_remote_mouse_button_changes);
                QueueRemoteMouseButton(8, x1, g_remote_mouse_buttons, g_remote_mouse_button_changes);
                QueueRemoteMouseButton(16, x2, g_remote_mouse_buttons, g_remote_mouse_button_changes);
                ReleaseSRWLockExclusive(&g_remote_mouse_lock);
                InterlockedExchange(&g_remote_mouse_seen, 1);
                
                
                
                sendModeStatus(false);

                ++packetCount;
                if (packetCount == 1 || (packetCount % 120) == 0) {
                    char ack[160] = {};
                    const int statusMode = getStableModeForStatus();
                    sprintf_s(ack, "javauwp_glfw_mouse:receiving mode=%d cursor=%.0f,%.0f cursorw=%.0f,%.0f size=%dx%d menu=%dx%d",
                        statusMode,
                        WindowToProtocolX(g_menu_abs_x), WindowToProtocolY(g_menu_abs_y),
                        g_menu_abs_x, g_menu_abs_y,
                        g_window_width, g_window_height,
                        g_menu_window_width, g_menu_window_height);
                    sendto(sock, ack, (int)strlen(ack), 0,
                        reinterpret_cast<sockaddr*>(&from), fromLen);
                    ShimLog("RemoteMouse: packets=%u last dx=%.3f dy=%.3f wheel=%.3f buttons=%d,%d,%d,%d,%d",
                        packetCount, dx, dy, wheelY, lb, rb, mb, x1, x2);
                }
            }
        },
        nullptr,
        0,
        nullptr);

    if (thread) {
        CloseHandle(thread);
    } else {
        ShimLog("RemoteMouse: CreateThread failed err=%u", GetLastError());
        InterlockedExchange(&g_remote_mouse_server_started, 0);
    }
}
static int SyntheticScancodeForKey(int key) {
    switch (key) {
    case GLFW_KEY_ESCAPE: return 1;
    case GLFW_KEY_Q: return 16;
    case GLFW_KEY_W: return 17;
    case GLFW_KEY_E: return 18;
    case GLFW_KEY_A: return 30;
    case GLFW_KEY_S: return 31;
    case GLFW_KEY_D: return 32;
    case GLFW_KEY_SPACE: return 57;
    case GLFW_KEY_ENTER: return 28;
    case GLFW_KEY_LEFT: return 75;
    case GLFW_KEY_RIGHT: return 77;
    case GLFW_KEY_UP: return 72;
    case GLFW_KEY_DOWN: return 80;
    case GLFW_KEY_LEFT_SHIFT: return 42;
    case GLFW_KEY_LEFT_CONTROL: return 29;
    default: return 0;
    }
}
static void SetControllerKeyState(int key, bool down) {
    if (key < 0 || key >= (int)sizeof(g_controller_key_state)) return;

    const unsigned char state = down ? GLFW_PRESS : GLFW_RELEASE;
    if (g_controller_key_state[key] == state) return;

    g_controller_key_state[key] = state;
    UpdateKeyState(key, state);

    if (g_key_cb) {
        g_key_cb((GLFWwindow*)&g_fake_window, key, SyntheticScancodeForKey(key),
            state, CurrentGlfwMods());
    }
}
static void ReleaseControllerKeys() {
    for (int i = 0; i < (int)sizeof(g_controller_key_state); ++i) {
        if (g_controller_key_state[i]) {
            SetControllerKeyState(i, false);
        }
    }
}
static void SetControllerMouseButtonState(int button, bool down) {
    if (button < 0 || button >= (int)sizeof(g_controller_mouse_state)) return;

    const unsigned char state = down ? GLFW_PRESS : GLFW_RELEASE;
    if (g_controller_mouse_state[button] == state) return;

    g_controller_mouse_state[button] = state;
    SetMouseButtonState(button, state, true);
}
static void ReleaseControllerMouseButtons() {
    for (int i = 0; i < (int)sizeof(g_controller_mouse_state); ++i) {
        if (g_controller_mouse_state[i]) {
            SetControllerMouseButtonState(i, false);
        }
    }
}
static int ClampInt64ToInt(int64_t value) {
    if (value > INT_MAX) return INT_MAX;
    if (value < INT_MIN) return INT_MIN;
    return (int)value;
}
static bool SyncMouseButtonsFromProperties(ABI::Windows::UI::Input::IPointerPointProperties* props, bool fireCallbacks) {
    if (!props) return false;

    unsigned char next[5] = {};
    boolean down = false;
    if (SUCCEEDED(props->get_IsLeftButtonPressed(&down)) && down) next[GLFW_MOUSE_BUTTON_LEFT] = GLFW_PRESS;
    down = false;
    if (SUCCEEDED(props->get_IsRightButtonPressed(&down)) && down) next[GLFW_MOUSE_BUTTON_RIGHT] = GLFW_PRESS;
    down = false;
    if (SUCCEEDED(props->get_IsMiddleButtonPressed(&down)) && down) next[GLFW_MOUSE_BUTTON_MIDDLE] = GLFW_PRESS;
    down = false;
    if (SUCCEEDED(props->get_IsXButton1Pressed(&down)) && down) next[GLFW_MOUSE_BUTTON_4] = GLFW_PRESS;
    down = false;
    if (SUCCEEDED(props->get_IsXButton2Pressed(&down)) && down) next[GLFW_MOUSE_BUTTON_5] = GLFW_PRESS;

    bool changed = false;
    for (int i = 0; i < 5; ++i) {
        if (g_mouse_state[i] != next[i]) {
            changed = true;
            SetMouseButtonState(i, next[i] ? GLFW_PRESS : GLFW_RELEASE, fireCallbacks);
        }
    }
    return changed;
}
static bool ButtonActionFromUpdateKind(
    ABI::Windows::UI::Input::PointerUpdateKind kind,
    int* button,
    int* action) {
    using namespace ABI::Windows::UI::Input;
    switch (kind) {
    case PointerUpdateKind_LeftButtonPressed: *button = GLFW_MOUSE_BUTTON_LEFT; *action = GLFW_PRESS; return true;
    case PointerUpdateKind_LeftButtonReleased: *button = GLFW_MOUSE_BUTTON_LEFT; *action = GLFW_RELEASE; return true;
    case PointerUpdateKind_RightButtonPressed: *button = GLFW_MOUSE_BUTTON_RIGHT; *action = GLFW_PRESS; return true;
    case PointerUpdateKind_RightButtonReleased: *button = GLFW_MOUSE_BUTTON_RIGHT; *action = GLFW_RELEASE; return true;
    case PointerUpdateKind_MiddleButtonPressed: *button = GLFW_MOUSE_BUTTON_MIDDLE; *action = GLFW_PRESS; return true;
    case PointerUpdateKind_MiddleButtonReleased: *button = GLFW_MOUSE_BUTTON_MIDDLE; *action = GLFW_RELEASE; return true;
    case PointerUpdateKind_XButton1Pressed: *button = GLFW_MOUSE_BUTTON_4; *action = GLFW_PRESS; return true;
    case PointerUpdateKind_XButton1Released: *button = GLFW_MOUSE_BUTTON_4; *action = GLFW_RELEASE; return true;
    case PointerUpdateKind_XButton2Pressed: *button = GLFW_MOUSE_BUTTON_5; *action = GLFW_PRESS; return true;
    case PointerUpdateKind_XButton2Released: *button = GLFW_MOUSE_BUTTON_5; *action = GLFW_RELEASE; return true;
    default:
        return false;
    }
}
static bool ReadPointerEvent(
    IPointerEventArgs* args,
    double* x,
    double* y,
    ComPtr<ABI::Windows::UI::Input::IPointerPointProperties>* propsOut) {
    if (!args) return false;

    ComPtr<ABI::Windows::UI::Input::IPointerPoint> point;
    if (FAILED(args->get_CurrentPoint(point.GetAddressOf())) || !point) return false;

    Point position = {};
    if (FAILED(point->get_Position(&position))) return false;

    if (x) *x = (double)position.X * CurrentPointerScaleX();
    if (y) *y = (double)position.Y * CurrentPointerScaleY();

    if (propsOut) {
        ComPtr<ABI::Windows::UI::Input::IPointerPointProperties> props;
        if (SUCCEEDED(point->get_Properties(props.GetAddressOf()))) {
            *propsOut = props;
        }
    }
    return true;
}
static void HandlePointerEvent(IPointerEventArgs* args, PointerDispatchKind kind) {
    if (kind == PointerDispatchExit) {
        DispatchCursorEnter(false);
        return;
    }

    if (kind == PointerDispatchCaptureLost) {
        for (int i = 0; i < 5; ++i) {
            SetMouseButtonState(i, GLFW_RELEASE, true);
        }
        return;
    }

    double x = g_cursor_x;
    double y = g_cursor_y;
    ComPtr<ABI::Windows::UI::Input::IPointerPointProperties> props;
    const bool hasPoint = ReadPointerEvent(args, &x, &y, &props);
    if (hasPoint) {
        DispatchCursorPos(x, y);
    } else if (kind == PointerDispatchEnter) {
        DispatchCursorEnter(true);
    }

    if (props) {
        const bool changed = SyncMouseButtonsFromProperties(props.Get(), true);
        if (!changed && (kind == PointerDispatchPress || kind == PointerDispatchRelease)) {
            ABI::Windows::UI::Input::PointerUpdateKind updateKind = ABI::Windows::UI::Input::PointerUpdateKind_Other;
            if (SUCCEEDED(props->get_PointerUpdateKind(&updateKind))) {
                int button = -1;
                int action = GLFW_RELEASE;
                if (ButtonActionFromUpdateKind(updateKind, &button, &action)) {
                    SetMouseButtonState(button, action, true);
                }
            }
        }

        if (kind == PointerDispatchWheel && g_scroll_cb && MouseCompanionActive()) {
            INT32 wheelDelta = 0;
            boolean horizontal = false;
            props->get_IsHorizontalMouseWheel(&horizontal);
            if (SUCCEEDED(props->get_MouseWheelDelta(&wheelDelta)) && wheelDelta != 0) {
                const double offset = (double)wheelDelta / 120.0;
                g_scroll_cb((GLFWwindow*)&g_fake_window, horizontal ? offset : 0.0, horizontal ? 0.0 : offset);
            }
        }
    }

    if (kind == PointerDispatchPress && g_coreWindow) {
        HRESULT hr = g_coreWindow->SetPointerCapture();
        if (FAILED(hr) && g_mouse_log_count < 8) {
            ++g_mouse_log_count;
            ShimLog("SetPointerCapture failed hr=0x%08X", hr);
        }
    } else if (kind == PointerDispatchRelease && g_coreWindow &&
               g_cursorMode != GLFW_CURSOR_DISABLED && !AnyMouseButtonDown()) {
        g_coreWindow->ReleasePointerCapture();
    }

    if (g_mouse_log_count < 24 &&
        (kind == PointerDispatchMove || kind == PointerDispatchPress ||
         kind == PointerDispatchRelease || kind == PointerDispatchWheel ||
         kind == PointerDispatchEnter)) {
        ++g_mouse_log_count;
        ShimLog("Pointer event kind=%d cursor=%.1f,%.1f buttons=%d%d%d%d%d",
            (int)kind, g_cursor_x, g_cursor_y,
            g_mouse_state[0], g_mouse_state[1], g_mouse_state[2], g_mouse_state[3], g_mouse_state[4]);
    }
}
static void HandleMouseDeviceMoved(ABI::Windows::Devices::Input::IMouseEventArgs* args) {
    if (!args) return;

    ABI::Windows::Devices::Input::MouseDelta delta = {};
    if (FAILED(args->get_MouseDelta(&delta))) return;
    DispatchMouseDelta(delta.X, delta.Y);
}
static void PollCoreWindowPointerPosition() {
    if (!g_coreWindow || g_cursorDisabled ||
        InterlockedCompareExchange(&g_remote_mouse_seen, 0, 0)) {
        return;
    }

    Point position = {};
    if (FAILED(g_coreWindow->get_PointerPosition(&position))) return;

    const double x = (double)position.X * CurrentPointerScaleX();
    const double y = (double)position.Y * CurrentPointerScaleY();
    if (x == g_cursor_x && y == g_cursor_y) return;

    g_menu_abs_x = ClampDouble(x, 0.0, CursorMaxX());
    g_menu_abs_y = ClampDouble(y, 0.0, CursorMaxY());
    DispatchCursorPos(g_menu_abs_x, g_menu_abs_y);
    if (g_mouse_log_count < 24) {
        ++g_mouse_log_count;
        ShimLog("Pointer position poll cursor=%.1f,%.1f", g_cursor_x, g_cursor_y);
    }
}
static unsigned int g_lastSyncedGameInputButtons = 0;
static void SyncGameInputMouseButtons(GameInputMouseButtons buttons) {

    const unsigned int now = (unsigned int)buttons;
    const unsigned int changed = now ^ g_lastSyncedGameInputButtons;
    g_lastSyncedGameInputButtons = now;
    if (!changed) return;

    if (changed & GameInputMouseLeftButton)
        SetMouseButtonState(GLFW_MOUSE_BUTTON_LEFT,
            (now & GameInputMouseLeftButton) ? GLFW_PRESS : GLFW_RELEASE, true);
    if (changed & GameInputMouseRightButton)
        SetMouseButtonState(GLFW_MOUSE_BUTTON_RIGHT,
            (now & GameInputMouseRightButton) ? GLFW_PRESS : GLFW_RELEASE, true);
    if (changed & GameInputMouseMiddleButton)
        SetMouseButtonState(GLFW_MOUSE_BUTTON_MIDDLE,
            (now & GameInputMouseMiddleButton) ? GLFW_PRESS : GLFW_RELEASE, true);
    if (changed & GameInputMouseButton4)
        SetMouseButtonState(GLFW_MOUSE_BUTTON_4,
            (now & GameInputMouseButton4) ? GLFW_PRESS : GLFW_RELEASE, true);
    if (changed & GameInputMouseButton5)
        SetMouseButtonState(GLFW_MOUSE_BUTTON_5,
            (now & GameInputMouseButton5) ? GLFW_PRESS : GLFW_RELEASE, true);
}
static void PollGameInputMouse() {
    if (!EnsureGameInput()) return;

    ComPtr<IGameInputReading> reading;
    HRESULT hr = g_gameInput->GetCurrentReading(GameInputKindMouse, nullptr, reading.GetAddressOf());
    if (FAILED(hr) || !reading) {
        if (g_gameinput_log_count < 8) {
            ++g_gameinput_log_count;
            ShimLog("GameInput mouse reading unavailable hr=0x%08X", hr);
        }
        return;
    }

    GameInputMouseState state = {};
    if (!reading->GetMouseState(&state)) {
        if (g_gameinput_log_count < 8) {
            ++g_gameinput_log_count;
            ShimLog("GameInput GetMouseState returned false kind=0x%X", reading->GetInputKind());
        }
        return;
    }

    SyncGameInputMouseButtons(state.buttons);

    if (!g_haveGameInputMouseState) {
        g_lastGameInputMouseState = state;
        g_haveGameInputMouseState = true;
        if (g_gameinput_log_count < 16) {
            ++g_gameinput_log_count;
            ShimLog("GameInput mouse ready kind=0x%X buttons=0x%X pos=%lld,%lld wheel=%lld,%lld",
                reading->GetInputKind(), (unsigned)state.buttons,
                (long long)state.positionX, (long long)state.positionY,
                (long long)state.wheelX, (long long)state.wheelY);
        }
        return;
    }

    const int64_t dx = state.positionX - g_lastGameInputMouseState.positionX;
    const int64_t dy = state.positionY - g_lastGameInputMouseState.positionY;
    const int64_t wheelX = state.wheelX - g_lastGameInputMouseState.wheelX;
    const int64_t wheelY = state.wheelY - g_lastGameInputMouseState.wheelY;
    g_lastGameInputMouseState = state;

    if (dx || dy) {
        DispatchMouseDelta(ClampInt64ToInt(dx), ClampInt64ToInt(dy));
    }
    if ((wheelX || wheelY) && g_scroll_cb && MouseCompanionActive()) {
        g_scroll_cb((GLFWwindow*)&g_fake_window, (double)wheelX, (double)wheelY);
    }

    if (g_gameinput_log_count < 32 && (dx || dy || wheelX || wheelY || state.buttons)) {
        ++g_gameinput_log_count;
        ShimLog("GameInput mouse delta dx=%lld dy=%lld wheel=%lld,%lld buttons=0x%X cursor=%.1f,%.1f",
            (long long)dx, (long long)dy,
            (long long)wheelX, (long long)wheelY,
            (unsigned)state.buttons, g_cursor_x, g_cursor_y);
    }
}
static float ApplyDeadzone(float value, float deadzone) {
    const float absValue = value < 0.0f ? -value : value;
    return absValue < deadzone ? 0.0f : ClampGamepadAxis(value);
}
static bool IsGamepadButtonDown(const GameInputGamepadState& state, GameInputGamepadButtons button) {
    return (state.buttons & button) != 0;
}
static void UpdateControllerBridge(const GameInputGamepadState& state) {
    const float leftX = ApplyDeadzone(state.leftThumbstickX, 0.28f);
    const float leftY = ApplyDeadzone(state.leftThumbstickY, 0.28f);
    const float rightX = ApplyDeadzone(state.rightThumbstickX, 0.18f);
    const float rightY = ApplyDeadzone(state.rightThumbstickY, 0.18f);

    SetControllerKeyState(GLFW_KEY_W, leftY > 0.0f);
    SetControllerKeyState(GLFW_KEY_S, leftY < 0.0f);
    SetControllerKeyState(GLFW_KEY_A, leftX < 0.0f);
    SetControllerKeyState(GLFW_KEY_D, leftX > 0.0f);

    SetControllerKeyState(GLFW_KEY_UP, IsGamepadButtonDown(state, GameInputGamepadDPadUp));
    SetControllerKeyState(GLFW_KEY_DOWN, IsGamepadButtonDown(state, GameInputGamepadDPadDown));
    SetControllerKeyState(GLFW_KEY_LEFT, IsGamepadButtonDown(state, GameInputGamepadDPadLeft));
    SetControllerKeyState(GLFW_KEY_RIGHT, IsGamepadButtonDown(state, GameInputGamepadDPadRight));

    const bool aDown = IsGamepadButtonDown(state, GameInputGamepadA);
    SetControllerKeyState(GLFW_KEY_SPACE, aDown);
    SetControllerKeyState(GLFW_KEY_ENTER, aDown);
    SetControllerKeyState(GLFW_KEY_ESCAPE,
        IsGamepadButtonDown(state, GameInputGamepadB) ||
        IsGamepadButtonDown(state, GameInputGamepadMenu));
    SetControllerKeyState(GLFW_KEY_E, false);
    SetControllerKeyState(GLFW_KEY_Q, IsGamepadButtonDown(state, GameInputGamepadY));
    SetControllerKeyState(GLFW_KEY_LEFT_SHIFT, IsGamepadButtonDown(state, GameInputGamepadLeftThumbstick));
    SetControllerKeyState(GLFW_KEY_LEFT_CONTROL, IsGamepadButtonDown(state, GameInputGamepadRightThumbstick));

    SetControllerMouseButtonState(GLFW_MOUSE_BUTTON_LEFT, state.rightTrigger > 0.35f);
    SetControllerMouseButtonState(GLFW_MOUSE_BUTTON_RIGHT, state.leftTrigger > 0.35f);

    const bool lbDown = IsGamepadButtonDown(state, GameInputGamepadLeftShoulder);
    const bool rbDown = IsGamepadButtonDown(state, GameInputGamepadRightShoulder);
    if (g_scroll_cb && lbDown && !g_controller_lb_down) {
        g_scroll_cb((GLFWwindow*)&g_fake_window, 0.0, 1.0);
    }
    if (g_scroll_cb && rbDown && !g_controller_rb_down) {
        g_scroll_cb((GLFWwindow*)&g_fake_window, 0.0, -1.0);
    }
    g_controller_lb_down = lbDown;
    g_controller_rb_down = rbDown;

    const int dx = (int)(rightX * 32.0f);
    const int dy = (int)(-rightY * 32.0f);
    if (dx || dy) {
        DispatchMouseDelta(dx, dy);
    }
}
static void ReleaseControllerBridge() {
    ReleaseControllerKeys();
    ReleaseControllerMouseButtons();
    g_controller_lb_down = false;
    g_controller_rb_down = false;
}
static void RemoveMouseDeviceHooks() {
    if (g_mouseDevice && g_mouseMovedToken.value) {
        g_mouseDevice->remove_MouseMoved(g_mouseMovedToken);
    }
    ZeroMemory(&g_mouseMovedToken, sizeof(g_mouseMovedToken));
    g_mouseMovedHandler.Reset();
    g_mouseDevice.Reset();
    g_mouseDeviceHooksInstalled = false;
}
static bool InstallMouseDeviceHooks() {
    if (g_mouseDeviceHooksInstalled) return true;

    ComPtr<ABI::Windows::Devices::Input::IMouseDeviceStatics> mouseStatics;
    HRESULT hr = GetActivationFactory(
        HStringReference(RuntimeClass_Windows_Devices_Input_MouseDevice).Get(),
        mouseStatics.GetAddressOf());
    if (FAILED(hr) || !mouseStatics) {
        ShimLog("MouseDevice activation failed hr=0x%08X", hr);
        return false;
    }

    hr = mouseStatics->GetForCurrentView(g_mouseDevice.GetAddressOf());
    if (FAILED(hr) || !g_mouseDevice) {
        ShimLog("MouseDevice::GetForCurrentView failed hr=0x%08X", hr);
        return false;
    }

    g_mouseMovedHandler = Callback<MouseDeviceMovedHandler>(
        [](ABI::Windows::Devices::Input::IMouseDevice*,
           ABI::Windows::Devices::Input::IMouseEventArgs* args) -> HRESULT {
            HandleMouseDeviceMoved(args);
            return S_OK;
        });
    hr = g_mouseDevice->add_MouseMoved(g_mouseMovedHandler.Get(), &g_mouseMovedToken);
    if (FAILED(hr)) {
        ShimLog("MouseDevice add_MouseMoved failed hr=0x%08X", hr);
        RemoveMouseDeviceHooks();
        return false;
    }

    g_mouseDeviceHooksInstalled = true;
    ShimLog("MouseDevice hooks installed");
    return true;
}
static void RemovePointerHooks() {
    if (g_coreWindow) {
        if (g_pointerMovedToken.value) g_coreWindow->remove_PointerMoved(g_pointerMovedToken);
        if (g_pointerPressedToken.value) g_coreWindow->remove_PointerPressed(g_pointerPressedToken);
        if (g_pointerReleasedToken.value) g_coreWindow->remove_PointerReleased(g_pointerReleasedToken);
        if (g_pointerWheelToken.value) g_coreWindow->remove_PointerWheelChanged(g_pointerWheelToken);
        if (g_pointerEnteredToken.value) g_coreWindow->remove_PointerEntered(g_pointerEnteredToken);
        if (g_pointerExitedToken.value) g_coreWindow->remove_PointerExited(g_pointerExitedToken);
        if (g_pointerCaptureLostToken.value) g_coreWindow->remove_PointerCaptureLost(g_pointerCaptureLostToken);
    }

    ZeroMemory(&g_pointerMovedToken, sizeof(g_pointerMovedToken));
    ZeroMemory(&g_pointerPressedToken, sizeof(g_pointerPressedToken));
    ZeroMemory(&g_pointerReleasedToken, sizeof(g_pointerReleasedToken));
    ZeroMemory(&g_pointerWheelToken, sizeof(g_pointerWheelToken));
    ZeroMemory(&g_pointerEnteredToken, sizeof(g_pointerEnteredToken));
    ZeroMemory(&g_pointerExitedToken, sizeof(g_pointerExitedToken));
    ZeroMemory(&g_pointerCaptureLostToken, sizeof(g_pointerCaptureLostToken));
    g_pointerMovedHandler.Reset();
    g_pointerPressedHandler.Reset();
    g_pointerReleasedHandler.Reset();
    g_pointerWheelHandler.Reset();
    g_pointerEnteredHandler.Reset();
    g_pointerExitedHandler.Reset();
    g_pointerCaptureLostHandler.Reset();
    g_pointerHooksInstalled = false;
}
static bool InstallPointerHooks() {
    if (g_pointerHooksInstalled) return true;
    if (!g_coreWindow) return false;

    g_pointerMovedHandler = Callback<CoreWindowPointerHandler>(
        [](ICoreWindow*, IPointerEventArgs* args) -> HRESULT {
            HandlePointerEvent(args, PointerDispatchMove);
            return S_OK;
        });
    g_pointerPressedHandler = Callback<CoreWindowPointerHandler>(
        [](ICoreWindow*, IPointerEventArgs* args) -> HRESULT {
            HandlePointerEvent(args, PointerDispatchPress);
            return S_OK;
        });
    g_pointerReleasedHandler = Callback<CoreWindowPointerHandler>(
        [](ICoreWindow*, IPointerEventArgs* args) -> HRESULT {
            HandlePointerEvent(args, PointerDispatchRelease);
            return S_OK;
        });
    g_pointerWheelHandler = Callback<CoreWindowPointerHandler>(
        [](ICoreWindow*, IPointerEventArgs* args) -> HRESULT {
            HandlePointerEvent(args, PointerDispatchWheel);
            return S_OK;
        });
    g_pointerEnteredHandler = Callback<CoreWindowPointerHandler>(
        [](ICoreWindow*, IPointerEventArgs* args) -> HRESULT {
            HandlePointerEvent(args, PointerDispatchEnter);
            return S_OK;
        });
    g_pointerExitedHandler = Callback<CoreWindowPointerHandler>(
        [](ICoreWindow*, IPointerEventArgs* args) -> HRESULT {
            HandlePointerEvent(args, PointerDispatchExit);
            return S_OK;
        });
    g_pointerCaptureLostHandler = Callback<CoreWindowPointerHandler>(
        [](ICoreWindow*, IPointerEventArgs* args) -> HRESULT {
            HandlePointerEvent(args, PointerDispatchCaptureLost);
            return S_OK;
        });

    HRESULT hr = g_coreWindow->add_PointerMoved(g_pointerMovedHandler.Get(), &g_pointerMovedToken);
    if (FAILED(hr)) { ShimLog("add_PointerMoved failed hr=0x%08X", hr); RemovePointerHooks(); return false; }
    hr = g_coreWindow->add_PointerPressed(g_pointerPressedHandler.Get(), &g_pointerPressedToken);
    if (FAILED(hr)) { ShimLog("add_PointerPressed failed hr=0x%08X", hr); RemovePointerHooks(); return false; }
    hr = g_coreWindow->add_PointerReleased(g_pointerReleasedHandler.Get(), &g_pointerReleasedToken);
    if (FAILED(hr)) { ShimLog("add_PointerReleased failed hr=0x%08X", hr); RemovePointerHooks(); return false; }
    hr = g_coreWindow->add_PointerWheelChanged(g_pointerWheelHandler.Get(), &g_pointerWheelToken);
    if (FAILED(hr)) { ShimLog("add_PointerWheelChanged failed hr=0x%08X", hr); RemovePointerHooks(); return false; }
    hr = g_coreWindow->add_PointerEntered(g_pointerEnteredHandler.Get(), &g_pointerEnteredToken);
    if (FAILED(hr)) { ShimLog("add_PointerEntered failed hr=0x%08X", hr); RemovePointerHooks(); return false; }
    hr = g_coreWindow->add_PointerExited(g_pointerExitedHandler.Get(), &g_pointerExitedToken);
    if (FAILED(hr)) { ShimLog("add_PointerExited failed hr=0x%08X", hr); RemovePointerHooks(); return false; }
    hr = g_coreWindow->add_PointerCaptureLost(g_pointerCaptureLostHandler.Get(), &g_pointerCaptureLostToken);
    if (FAILED(hr)) { ShimLog("add_PointerCaptureLost failed hr=0x%08X", hr); RemovePointerHooks(); return false; }

    g_pointerHooksInstalled = true;
    ShimLog("CoreWindow pointer hooks installed");
    return true;
}
static IClipboardStatics* GetClipboardStatics() {
    IClipboardStatics* statics = nullptr;
    GetActivationFactory(
        HStringReference(RuntimeClass_Windows_ApplicationModel_DataTransfer_Clipboard).Get(),
        &statics);
    return statics; 
}

extern "C" __declspec(dllexport) void glfwTerminate(void) {
    ShimLog("glfwTerminate");
    if (g_coreWindow && g_keyboardHooksInstalled) {
        g_coreWindow->remove_KeyDown(g_keyDownToken);
        g_coreWindow->remove_KeyUp(g_keyUpToken);
        g_coreWindow->remove_CharacterReceived(g_charReceivedToken);
    }
    RemovePointerHooks();
    RemoveMouseDeviceHooks();
    ZeroMemory(&g_keyDownToken, sizeof(g_keyDownToken));
    ZeroMemory(&g_keyUpToken, sizeof(g_keyUpToken));
    ZeroMemory(&g_charReceivedToken, sizeof(g_charReceivedToken));
    g_keyDownHandler.Reset();
    g_keyUpHandler.Reset();
    g_charReceivedHandler.Reset();
    g_keyboardHooksInstalled = false;
    ZeroMemory(g_key_state, sizeof(g_key_state));
    g_haveGameInputPollCache = false;
    g_lastGameInputPollMs = 0;
    if (p_eglMakeCurrent && g_eglDisplay != EGL_NO_DISPLAY) {
        p_eglMakeCurrent(g_eglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    }
    g_eglContextThreadId = 0;
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
    ShimLog("glfwTerminate complete");
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

    if (w > 0) {
        g_menu_window_width = w;
        g_window_width = w;
        g_framebuffer_width = UseRawScaledFramebuffer()
            ? ScaleDimensionToPixels((FLOAT)w, g_content_scale_x, g_framebuffer_width)
            : w;
    }
    if (h > 0) {
        g_menu_window_height = h;
        g_window_height = h;
        g_framebuffer_height = UseRawScaledFramebuffer()
            ? ScaleDimensionToPixels((FLOAT)h, g_content_scale_y, g_framebuffer_height)
            : h;
    }
    RefreshWindowMetrics(false);
    if (!CreateEglContext()) {
        ShimLog("CreateEglContext FAILED");
        return NULL;
    }

    g_fake_window.width = g_window_width;
    g_fake_window.height = g_window_height;
    g_vidmode.width = g_framebuffer_width;
    g_vidmode.height = g_framebuffer_height;
    if (g_fbsize_cb) g_fbsize_cb((GLFWwindow*)&g_fake_window, g_framebuffer_width, g_framebuffer_height);
    ShimLog("glfwCreateWindow OK");
    return (GLFWwindow*)&g_fake_window;
}

extern "C" __declspec(dllexport) void glfwDestroyWindow(GLFWwindow*) {
    ShimLog("glfwDestroyWindow");
    g_should_close = TRUE;
}
extern "C" __declspec(dllexport) int  glfwWindowShouldClose(GLFWwindow*) { return g_should_close ? GLFW_TRUE : GLFW_FALSE; }
extern "C" __declspec(dllexport) void glfwSetWindowShouldClose(GLFWwindow*, int v) { g_should_close = (v != 0); }
extern "C" __declspec(dllexport) void glfwSetWindowTitle(GLFWwindow*, const char*) {}
extern "C" __declspec(dllexport) void glfwSetWindowIcon(GLFWwindow*, int, const GLFWimage*) {}
extern "C" __declspec(dllexport) void glfwGetWindowPos(GLFWwindow*, int*x, int*y) { if(x)*x=0; if(y)*y=0; }
extern "C" __declspec(dllexport) void glfwSetWindowPos(GLFWwindow*, int, int) {}
extern "C" __declspec(dllexport) void glfwGetWindowSize(GLFWwindow*, int*w, int*h) { RefreshWindowMetrics(false); if(w)*w=g_window_width; if(h)*h=g_window_height; }
extern "C" __declspec(dllexport) void glfwSetWindowSizeLimits(GLFWwindow*, int, int, int, int) {}
extern "C" __declspec(dllexport) void glfwSetWindowAspectRatio(GLFWwindow*, int, int) {}
extern "C" __declspec(dllexport) void glfwSetWindowSize(GLFWwindow*, int, int) {}
extern "C" __declspec(dllexport) void glfwGetFramebufferSize(GLFWwindow*, int*w, int*h) { RefreshWindowMetrics(false); if(w)*w=g_framebuffer_width; if(h)*h=g_framebuffer_height; }
extern "C" __declspec(dllexport) void glfwGetWindowFrameSize(GLFWwindow*, int*l, int*t, int*r, int*b) {
    if(l)*l=0; if(t)*t=0; if(r)*r=0; if(b)*b=0;
}
extern "C" __declspec(dllexport) void glfwGetWindowContentScale(GLFWwindow*, float*x, float*y) {
    RefreshWindowMetrics(false);
    if(x)*x=g_content_scale_x; if(y)*y=g_content_scale_y;
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
    if (g_window_attrib_log_count < 32) {
        ++g_window_attrib_log_count;
        ShimLog("glfwGetWindowAttrib #%d attr=0x%08X", g_window_attrib_log_count, a);
    }
    switch (a) {
    case GLFW_VISIBLE:
    case GLFW_HOVERED:
        return GLFW_TRUE;
    case GLFW_FOCUSED:
        return CoreWindowAcceptsInput() ? GLFW_TRUE : GLFW_FALSE;
    case GLFW_MAXIMIZED:
        return GLFW_FALSE;
    case GLFW_CLIENT_API:
        return GLFW_OPENGL_API;
    case GLFW_CONTEXT_VERSION_MAJOR:
        return 3;
    case GLFW_CONTEXT_VERSION_MINOR:
        return 2;
    case GLFW_OPENGL_PROFILE:
        return EnvFlagEnabled(L"MC_LEGACY_OPENGL_CONTEXT") ? GLFW_OPENGL_COMPAT_PROFILE : GLFW_OPENGL_CORE_PROFILE;
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
        boolean hasDispatcherAccess = false;
        const HRESULT accessHr = g_dispatcher->get_HasThreadAccess(&hasDispatcherAccess);
        if (SUCCEEDED(accessHr) && hasDispatcherAccess) {
            if (InterlockedCompareExchange(&g_processing_events, 1, 0) == 0) {
                const HRESULT hr = g_dispatcher->ProcessEvents(CoreProcessEventsOption_ProcessAllIfPresent);
                InterlockedExchange(&g_processing_events, 0);
                if (FAILED(hr) && g_process_events_error_log_count < 8) {
                    ++g_process_events_error_log_count;
                    ShimLog("ProcessEvents failed hr=0x%08X", hr);
                }
            } else if (g_nested_process_log_count < 8) {
                ++g_nested_process_log_count;
                ShimLog("Skipping nested ProcessEvents #%d", g_nested_process_log_count);
            }
        } else if (g_process_events_offthread_log_count < 8) {
            ++g_process_events_offthread_log_count;
            ShimLog("Skipping ProcessEvents off dispatcher thread hr=0x%08X access=%d", accessHr, hasDispatcherAccess ? 1 : 0);
        }
    }
    const bool mouseCompanionActive = MouseCompanionActive();
    if (!mouseCompanionActive && g_mouse_active_latched) {
      
        FlushMouseButtonsForDeactivate();
    }
    g_mouse_active_latched = mouseCompanionActive;

    if (mouseCompanionActive) {
        DrainRemoteMouseInput();
        PollGameInputMouse();
    }
    if (g_controller_bridge_enabled) {
        PollGameInputGamepad(true);
    }
    if (mouseCompanionActive && !g_gamepad_present) {
        PollCoreWindowPointerPosition();
    }
    RefreshWindowMetrics(true);
}
extern "C" __declspec(dllexport) void glfwWaitEvents(void) {
    if (g_wait_log_count < 8) {
        ++g_wait_log_count;
        ShimLog("glfwWaitEvents #%d", g_wait_log_count);
    }

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
extern "C" __declspec(dllexport) int  glfwGetInputMode(GLFWwindow*, int m) {
    if (m == GLFW_CURSOR) return g_cursorMode;
    if (m == GLFW_RAW_MOUSE_MOTION) return g_raw_mouse_motion ? GLFW_TRUE : GLFW_FALSE;
    return 0;
}
extern "C" __declspec(dllexport) void glfwSetInputMode(GLFWwindow*, int mode, int value) {
    if (mode == GLFW_RAW_MOUSE_MOTION) {
        g_raw_mouse_motion = (value != GLFW_FALSE);
        ShimLog("Raw mouse motion %s", g_raw_mouse_motion ? "enabled" : "disabled");
        return;
    }
    if (mode != GLFW_CURSOR) return;

    if (value != GLFW_CURSOR_NORMAL &&
        value != GLFW_CURSOR_HIDDEN &&
        value != GLFW_CURSOR_DISABLED) {
        return;
    }

    if (g_cursorMode != value) {
        g_pendingMode = value;
        g_modeChangeTime = GetTickCount64();
    }
    g_cursorMode = value;
    g_cursorDisabled = (value == GLFW_CURSOR_DISABLED);
    if (!g_cursorDisabled) {
        g_menu_abs_x = ClampDouble(g_cursor_x, 0.0, CursorMaxX());
        g_menu_abs_y = ClampDouble(g_cursor_y, 0.0, CursorMaxY());
        DispatchCursorPos(g_menu_abs_x, g_menu_abs_y);
    }
    if (value == GLFW_CURSOR_NORMAL) {
        
        
        SendMouseRelayCursorSync(WindowToProtocolX(g_menu_abs_x), WindowToProtocolY(g_menu_abs_y));
        SendMouseRelayWindowCursorSync(g_menu_abs_x, g_menu_abs_y);
    }
    SendCursorOverlayState();
    ShimLog("Cursor mode %s", g_cursorDisabled ? "GAMEPLAY" : "MENU");
    if (!AcquireCoreWindow()) return;

    if (g_cursorDisabled) {
        const HRESULT hr = g_coreWindow->SetPointerCapture();
        if (FAILED(hr) && g_mouse_log_count < 8) {
            ++g_mouse_log_count;
            ShimLog("SetPointerCapture(cursor disabled) failed hr=0x%08X", hr);
        }
    } else if (!AnyMouseButtonDown()) {
        g_coreWindow->ReleasePointerCapture();
    }
}
extern "C" __declspec(dllexport) int  glfwRawMouseMotionSupported(void) { return GLFW_FALSE; }
static const char* KeyNameFromGlfwKey(int key) {
    switch (key) {
    case GLFW_KEY_SPACE: return "Space";
    case GLFW_KEY_0: return "0";
    case GLFW_KEY_1: return "1";
    case GLFW_KEY_2: return "2";
    case GLFW_KEY_3: return "3";
    case GLFW_KEY_4: return "4";
    case GLFW_KEY_5: return "5";
    case GLFW_KEY_6: return "6";
    case GLFW_KEY_7: return "7";
    case GLFW_KEY_8: return "8";
    case GLFW_KEY_9: return "9";
    case GLFW_KEY_A: return "A";
    case GLFW_KEY_B: return "B";
    case GLFW_KEY_C: return "C";
    case GLFW_KEY_D: return "D";
    case GLFW_KEY_E: return "E";
    case GLFW_KEY_F: return "F";
    case GLFW_KEY_G: return "G";
    case GLFW_KEY_H: return "H";
    case GLFW_KEY_I: return "I";
    case GLFW_KEY_J: return "J";
    case GLFW_KEY_K: return "K";
    case GLFW_KEY_L: return "L";
    case GLFW_KEY_M: return "M";
    case GLFW_KEY_N: return "N";
    case GLFW_KEY_O: return "O";
    case GLFW_KEY_P: return "P";
    case GLFW_KEY_Q: return "Q";
    case GLFW_KEY_R: return "R";
    case GLFW_KEY_S: return "S";
    case GLFW_KEY_T: return "T";
    case GLFW_KEY_U: return "U";
    case GLFW_KEY_V: return "V";
    case GLFW_KEY_W: return "W";
    case GLFW_KEY_X: return "X";
    case GLFW_KEY_Y: return "Y";
    case GLFW_KEY_Z: return "Z";
    case GLFW_KEY_ESCAPE: return "Escape";
    case GLFW_KEY_ENTER: return "Enter";
    case GLFW_KEY_TAB: return "Tab";
    case GLFW_KEY_BACKSPACE: return "Backspace";
    case GLFW_KEY_INSERT: return "Insert";
    case GLFW_KEY_DELETE: return "Delete";
    case GLFW_KEY_RIGHT: return "Right";
    case GLFW_KEY_LEFT: return "Left";
    case GLFW_KEY_DOWN: return "Down";
    case GLFW_KEY_UP: return "Up";
    case GLFW_KEY_PAGE_UP: return "Page Up";
    case GLFW_KEY_PAGE_DOWN: return "Page Down";
    case GLFW_KEY_HOME: return "Home";
    case GLFW_KEY_END: return "End";
    case GLFW_KEY_CAPS_LOCK: return "Caps Lock";
    case GLFW_KEY_SCROLL_LOCK: return "Scroll Lock";
    case GLFW_KEY_NUM_LOCK: return "Num Lock";
    case GLFW_KEY_PRINT_SCREEN: return "Print Screen";
    case GLFW_KEY_PAUSE: return "Pause";
    case GLFW_KEY_F1: return "F1";
    case GLFW_KEY_F2: return "F2";
    case GLFW_KEY_F3: return "F3";
    case GLFW_KEY_F4: return "F4";
    case GLFW_KEY_F5: return "F5";
    case GLFW_KEY_F6: return "F6";
    case GLFW_KEY_F7: return "F7";
    case GLFW_KEY_F8: return "F8";
    case GLFW_KEY_F9: return "F9";
    case GLFW_KEY_F10: return "F10";
    case GLFW_KEY_F11: return "F11";
    case GLFW_KEY_F12: return "F12";
    case GLFW_KEY_KP_0: return "Num 0";
    case GLFW_KEY_KP_1: return "Num 1";
    case GLFW_KEY_KP_2: return "Num 2";
    case GLFW_KEY_KP_3: return "Num 3";
    case GLFW_KEY_KP_4: return "Num 4";
    case GLFW_KEY_KP_5: return "Num 5";
    case GLFW_KEY_KP_6: return "Num 6";
    case GLFW_KEY_KP_7: return "Num 7";
    case GLFW_KEY_KP_8: return "Num 8";
    case GLFW_KEY_KP_9: return "Num 9";
    case GLFW_KEY_KP_DECIMAL: return "Num .";
    case GLFW_KEY_KP_DIVIDE: return "Num /";
    case GLFW_KEY_KP_MULTIPLY: return "Num *";
    case GLFW_KEY_KP_SUBTRACT: return "Num -";
    case GLFW_KEY_KP_ADD: return "Num +";
    case GLFW_KEY_LEFT_SHIFT: return "Left Shift";
    case GLFW_KEY_LEFT_CONTROL: return "Left Control";
    case GLFW_KEY_LEFT_ALT: return "Left Alt";
    case GLFW_KEY_LEFT_SUPER: return "Left Win";
    case GLFW_KEY_RIGHT_SHIFT: return "Right Shift";
    case GLFW_KEY_RIGHT_CONTROL: return "Right Control";
    case GLFW_KEY_RIGHT_ALT: return "Right Alt";
    case GLFW_KEY_RIGHT_SUPER: return "Right Win";
    case GLFW_KEY_MENU: return "Menu";
    default: return NULL;
    }
}

extern "C" __declspec(dllexport) const char* glfwGetKeyName(int key, int) {
    return KeyNameFromGlfwKey(key);
}
extern "C" __declspec(dllexport) int  glfwGetKeyScancode(int) { return 0; }
extern "C" __declspec(dllexport) int  glfwGetKey(GLFWwindow*, int key) {
    if (key < 0 || key >= (int)sizeof(g_key_state)) return GLFW_RELEASE;
    return g_key_state[key] ? GLFW_PRESS : GLFW_RELEASE;
}
extern "C" __declspec(dllexport) int  glfwGetMouseButton(GLFWwindow*, int button) {
    if (button < 0 || button >= (int)sizeof(g_mouse_state)) return GLFW_RELEASE;
    const int state = g_mouse_state[button] ? GLFW_PRESS : GLFW_RELEASE;
    return state;
}
extern "C" __declspec(dllexport) void glfwGetCursorPos(GLFWwindow*, double*x, double*y) {
    if (x) *x = g_cursor_x;
    if (y) *y = g_cursor_y;
}
extern "C" __declspec(dllexport) void glfwSetCursorPos(GLFWwindow*, double x, double y) {
    if (g_cursorDisabled) {
        return;
    }

    
    g_menu_abs_x = ClampDouble(x, 0.0, CursorMaxX());
    g_menu_abs_y = ClampDouble(y, 0.0, CursorMaxY());
    DispatchCursorPos(g_menu_abs_x, g_menu_abs_y);
    SendCursorOverlayState();

    if (!AcquireCoreWindow()) return;
    ComPtr<ICoreWindow2> coreWindow2;
    if (SUCCEEDED(g_coreWindow.As(&coreWindow2)) && coreWindow2) {
        Point position = {};
        const double sx = CurrentPointerScaleX();
        const double sy = CurrentPointerScaleY();
        position.X = (FLOAT)(sx > 0.0 ? g_menu_abs_x / sx : g_menu_abs_x);
        position.Y = (FLOAT)(sy > 0.0 ? g_menu_abs_y / sy : g_menu_abs_y);
        coreWindow2->put_PointerPosition(position);
    }
}
extern "C" __declspec(dllexport) GLFWcursor* glfwCreateCursor(const GLFWimage*, int, int) { return (GLFWcursor*)1; }
extern "C" __declspec(dllexport) GLFWcursor* glfwCreateStandardCursor(int) { return (GLFWcursor*)1; }
extern "C" __declspec(dllexport) void glfwDestroyCursor(GLFWcursor*) {}
extern "C" __declspec(dllexport) void glfwSetCursor(GLFWwindow*, GLFWcursor*) {}
extern "C" __declspec(dllexport) const char* glfwGetClipboardString(GLFWwindow*) {
    g_clipboard_buf[0] = '\0';

    ComPtr<IClipboardStatics> clip(GetClipboardStatics());
    if (!clip) { ShimLog("Clipboard: IClipboardStatics unavailable (sandbox?)"); return g_clipboard_buf; }

    ComPtr<IDataPackageView> view;
    if (FAILED(clip->GetContent(&view)) || !view) {
        ShimLog("Clipboard: GetContent failed");
        return g_clipboard_buf;
    }

    
    boolean hasText = FALSE;
    {
        HSTRING fmtText = nullptr;
        WindowsCreateString(L"Text", 4, &fmtText);
        view->Contains(fmtText, &hasText);
        WindowsDeleteString(fmtText);
    }
    if (!hasText) { ShimLog("Clipboard: no text content"); return g_clipboard_buf; }

    ComPtr<IAsyncOperation<HSTRING>> asyncOp;
    if (FAILED(view->GetTextAsync(&asyncOp)) || !asyncOp) {
        ShimLog("Clipboard: GetTextAsync failed");
        return g_clipboard_buf;
    }

    
    
    ComPtr<IAsyncInfo> asyncInfo;
    if (FAILED(asyncOp.As(&asyncInfo)) || !asyncInfo) {
        ShimLog("Clipboard: IAsyncInfo QI failed");
        return g_clipboard_buf;
    }

    
    AsyncStatus status = AsyncStatus::Started;
    for (int i = 0; i < 200 && status == AsyncStatus::Started; ++i) {
        asyncInfo->get_Status(&status);
        if (status == AsyncStatus::Started) Sleep(1);
    }
    if (status != AsyncStatus::Completed) {
        ShimLog("Clipboard: GetTextAsync timed out or failed (status=%d)", (int)status);
        return g_clipboard_buf;
    }

    HSTRING hstr = nullptr;
    if (FAILED(asyncOp->GetResults(&hstr)) || !hstr) return g_clipboard_buf;

    UINT32 len = 0;
    const wchar_t* buf = WindowsGetStringRawBuffer(hstr, &len);
    if (buf && len > 0) {
        const int needed = WideCharToMultiByte(CP_UTF8, 0, buf, (int)len,
                                               nullptr, 0, nullptr, nullptr);
        if (needed > 0 && needed < (int)sizeof(g_clipboard_buf)) {
            WideCharToMultiByte(CP_UTF8, 0, buf, (int)len,
                                g_clipboard_buf, needed, nullptr, nullptr);
            g_clipboard_buf[needed] = '\0';
        }
    }
    WindowsDeleteString(hstr);
    ShimLog("Clipboard: paste %d chars", (int)strlen(g_clipboard_buf));
    return g_clipboard_buf;
}
extern "C" __declspec(dllexport) void glfwSetClipboardString(GLFWwindow*, const char* text) {
    if (!text) return;

    
    const int wlen = MultiByteToWideChar(CP_UTF8, 0, text, -1, nullptr, 0);
    if (wlen <= 0) return;
    std::vector<wchar_t> wtext(wlen);
    MultiByteToWideChar(CP_UTF8, 0, text, -1, wtext.data(), wlen);

    HSTRING hstr = nullptr;
    if (FAILED(WindowsCreateString(wtext.data(), (UINT32)(wlen - 1), &hstr))) return;

    
    ComPtr<IDataPackage> pkg;
    {
        ComPtr<IInspectable> insp;
        if (FAILED(RoActivateInstance(
                HStringReference(RuntimeClass_Windows_ApplicationModel_DataTransfer_DataPackage).Get(),
                &insp)) || !insp) {
            WindowsDeleteString(hstr);
            return;
        }
        insp.As(&pkg);
    }
    if (!pkg) { WindowsDeleteString(hstr); return; }
    if (FAILED(pkg->SetText(hstr))) { WindowsDeleteString(hstr); return; }
    WindowsDeleteString(hstr);

    ComPtr<IClipboardStatics> clip(GetClipboardStatics());
    if (!clip) return;
    if (FAILED(clip->SetContent(pkg.Get()))) {
        ShimLog("Clipboard: SetContent failed");
        return;
    }
    
    clip->Flush();
    ShimLog("Clipboard: copy %d chars", (int)strlen(text));
}

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
    const DWORD tid = GetCurrentThreadId();
    ShimLog("MakeContextCurrent %p tid=%lu previousTid=%lu", (void*)w, tid, g_eglContextThreadId);
    if (!w) {
        if (g_eglContextThreadId != 0 && g_eglContextThreadId != tid) {
            ShimLog("MakeContextCurrent(NULL) ignored: context owned by thread %u, caller %u",
                (unsigned)g_eglContextThreadId, (unsigned)tid);
            return;
        }
        if (p_eglMakeCurrent && g_eglDisplay != EGL_NO_DISPLAY) {
            p_eglMakeCurrent(g_eglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        }
        g_eglContextThreadId = 0;
        return;
    }
    if (!CreateEglContext()) return;
    if (g_eglContextThreadId != 0 && g_eglContextThreadId != tid) {
        ShimLog("eglMakeCurrent moving context from tid=%lu to tid=%lu", g_eglContextThreadId, tid);
    }
    if (!p_eglMakeCurrent(g_eglDisplay, g_eglSurface, g_eglSurface, g_eglContext)) {
        ReportEglError("eglMakeCurrent");
        return;
    }
    g_eglContextThreadId = tid;
    ShimLog("eglMakeCurrent OK tid=%lu", tid);
}
extern "C" __declspec(dllexport) GLFWwindow* glfwGetCurrentContext(void) {
    const DWORD tid = GetCurrentThreadId();
    GLFWwindow* current = (g_eglContext != EGL_NO_CONTEXT && g_eglContextThreadId == tid) ? (GLFWwindow*)&g_fake_window : NULL;
    if (g_current_context_log_count < 16) {
        ++g_current_context_log_count;
        ShimLog("glfwGetCurrentContext #%d tid=%lu boundTid=%lu => %p",
            g_current_context_log_count, tid, g_eglContextThreadId, (void*)current);
    }
    return current;
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
extern "C" __declspec(dllexport) int glfwExtensionSupported(const char* name) {
    if (g_extension_log_count < 32) {
        ++g_extension_log_count;
        ShimLog("glfwExtensionSupported #%d %s => false",
            g_extension_log_count, name ? name : "(null)");
    }
    return GLFW_FALSE;
}
extern "C" __declspec(dllexport) void* glfwGetProcAddress(const char* name) {
    void* p = NULL;
    if (g_graphicsRuntimeUsesGles && g_opengl32) p = (void*)GetProcAddress(g_opengl32, name);
    if (!p && g_graphicsRuntimeUsesGles && g_libGLESv2) p = (void*)GetProcAddress(g_libGLESv2, name);
    if (!p && p_eglGetProcAddress) p = p_eglGetProcAddress(name);
    if (!p && g_opengl32) p = (void*)GetProcAddress(g_opengl32, name);
    if (!p && g_libGLESv2) p = (void*)GetProcAddress(g_libGLESv2, name);
    if (!p && g_libEGL) p = (void*)GetProcAddress(g_libEGL, name);
    if (g_proc_log_count < 200) {
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
    RefreshWindowMetrics(false);
    if(x)*x=0; if(y)*y=0; if(w)*w=g_framebuffer_width; if(h)*h=g_framebuffer_height;
}
extern "C" __declspec(dllexport) void glfwGetMonitorPhysicalSize(GLFWmonitor*, int*w, int*h) { if(w)*w=527; if(h)*h=296; }
extern "C" __declspec(dllexport) void glfwGetMonitorContentScale(GLFWmonitor*, float*x, float*y) {
    RefreshWindowMetrics(false);
    if(x)*x=g_content_scale_x; if(y)*y=g_content_scale_y;
}
extern "C" __declspec(dllexport) const char* glfwGetMonitorName(GLFWmonitor*) { return "CoreWindow Display"; }
extern "C" __declspec(dllexport) void  glfwSetMonitorUserPointer(GLFWmonitor*, void*) {}
extern "C" __declspec(dllexport) void* glfwGetMonitorUserPointer(GLFWmonitor*) { return NULL; }
extern "C" __declspec(dllexport) GLFWmonitorfun glfwSetMonitorCallback(GLFWmonitorfun cb) { GLFWmonitorfun p=g_monitor_cb; g_monitor_cb=cb; return p; }
extern "C" __declspec(dllexport) const GLFWvidmode* glfwGetVideoModes(GLFWmonitor*, int*c) {
    RefreshWindowMetrics(false);
    if(c)*c=1;
    return &g_vidmode;
}
extern "C" __declspec(dllexport) const GLFWvidmode* glfwGetVideoMode(GLFWmonitor*) {
    RefreshWindowMetrics(false);
    return &g_vidmode;
}
extern "C" __declspec(dllexport) void glfwSetGamma(GLFWmonitor*, float) {}
extern "C" __declspec(dllexport) const GLFWgammaramp* glfwGetGammaRamp(GLFWmonitor*) { return NULL; }
extern "C" __declspec(dllexport) void glfwSetGammaRamp(GLFWmonitor*, const GLFWgammaramp*) {}

static bool IsSupportedJoystick(int jid) {
    return jid == GLFW_JOYSTICK_1;
}

static void LogGamepadQuery(const char* api, int jid, bool result) {
    if (g_gamepad_query_log_count >= 32) return;
    ++g_gamepad_query_log_count;
    ShimLog("%s jid=%d -> %d", api, jid, result ? 1 : 0);
}

extern "C" __declspec(dllexport) int glfwJoystickPresent(int jid) {
    const bool result = IsSupportedJoystick(jid) && PollGameInputGamepad(false);
    LogGamepadQuery("glfwJoystickPresent", jid, result);
    return result ? GLFW_TRUE : GLFW_FALSE;
}
extern "C" __declspec(dllexport) const float* glfwGetJoystickAxes(int jid, int*c) {
    const bool result = IsSupportedJoystick(jid) && PollGameInputGamepad(false);
    LogGamepadQuery("glfwGetJoystickAxes", jid, result);
    if (!result) {
        if(c)*c=0;
        return NULL;
    }
    if(c)*c=(int)(sizeof(g_joystick_axes) / sizeof(g_joystick_axes[0]));
    return g_joystick_axes;
}
extern "C" __declspec(dllexport) const unsigned char* glfwGetJoystickButtons(int jid, int*c) {
    const bool result = IsSupportedJoystick(jid) && PollGameInputGamepad(false);
    LogGamepadQuery("glfwGetJoystickButtons", jid, result);
    if (!result) {
        if(c)*c=0;
        return NULL;
    }
    if(c)*c=(int)(sizeof(g_joystick_buttons) / sizeof(g_joystick_buttons[0]));
    return g_joystick_buttons;
}
extern "C" __declspec(dllexport) const unsigned char* glfwGetJoystickHats(int, int*c) { if(c)*c=0; return NULL; }
extern "C" __declspec(dllexport) const char* glfwGetJoystickName(int jid) {
    const bool result = IsSupportedJoystick(jid) && PollGameInputGamepad(false);
    LogGamepadQuery("glfwGetJoystickName", jid, result);
    return result ? "Xbox Controller" : NULL;
}
extern "C" __declspec(dllexport) const char* glfwGetJoystickGUID(int jid) {
    const bool result = IsSupportedJoystick(jid) && PollGameInputGamepad(false);
    LogGamepadQuery("glfwGetJoystickGUID", jid, result);
    return result ? "030000005e0400008e02000000000000" : NULL;
}
extern "C" __declspec(dllexport) void  glfwSetJoystickUserPointer(int jid, void* pointer) {
    if (jid == GLFW_JOYSTICK_1) g_joystick_user_pointer = pointer;
}
extern "C" __declspec(dllexport) void* glfwGetJoystickUserPointer(int jid) {
    return jid == GLFW_JOYSTICK_1 ? g_joystick_user_pointer : NULL;
}
extern "C" __declspec(dllexport) int   glfwJoystickIsGamepad(int jid) {
    const bool result = IsSupportedJoystick(jid) && PollGameInputGamepad(false);
    LogGamepadQuery("glfwJoystickIsGamepad", jid, result);
    return result ? GLFW_TRUE : GLFW_FALSE;
}
extern "C" __declspec(dllexport) GLFWjoystickfun glfwSetJoystickCallback(GLFWjoystickfun cb) {
    GLFWjoystickfun p = g_joystick_cb;
    g_joystick_cb = cb;
    if (g_gamepad_query_log_count < 32) {
        ++g_gamepad_query_log_count;
        ShimLog("glfwSetJoystickCallback cb=%p", cb);
    }
    if (cb) {
        const bool wasPresent = g_gamepad_present;
        const bool present = PollGameInputGamepad(true);
        if (present && wasPresent) {
            ShimLog("glfwSetJoystickCallback immediate GLFW_CONNECTED");
            cb(GLFW_JOYSTICK_1, GLFW_CONNECTED);
        }
    }
    return p;
}
extern "C" __declspec(dllexport) int  glfwUpdateGamepadMappings(const char*) { return GLFW_TRUE; }
extern "C" __declspec(dllexport) const char* glfwGetGamepadName(int jid) {
    const bool result = IsSupportedJoystick(jid) && PollGameInputGamepad(false);
    LogGamepadQuery("glfwGetGamepadName", jid, result);
    return result ? "Xbox Controller" : NULL;
}
extern "C" __declspec(dllexport) int  glfwGetGamepadState(int jid, GLFWgamepadstate* state) {
    const bool result = state && IsSupportedJoystick(jid) && PollGameInputGamepad(false);
    LogGamepadQuery("glfwGetGamepadState", jid, result);
    if (!result) {
        return GLFW_FALSE;
    }
    memcpy(state, &g_gamepad_state, sizeof(g_gamepad_state));
    return GLFW_TRUE;
}

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
