param(
    [Parameter(Mandatory = $true)][string]$SourceRoot
)

$ErrorActionPreference = "Stop"
$Utf8NoBom = New-Object System.Text.UTF8Encoding($false)

function Read-NormalizedText {
    param([Parameter(Mandatory = $true)][string]$Path)
    return ([System.IO.File]::ReadAllText($Path) -replace "`r`n", "`n")
}

function Write-NormalizedText {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$Text
    )
    [System.IO.File]::WriteAllText($Path, $Text, $Utf8NoBom)
}

function Replace-Once {
    param(
        [Parameter(Mandatory = $true)][string]$Text,
        [Parameter(Mandatory = $true)][string]$Old,
        [Parameter(Mandatory = $true)][string]$New,
        [Parameter(Mandatory = $true)][string]$Label
    )
    $first = $Text.IndexOf($Old, [System.StringComparison]::Ordinal)
    if ($first -lt 0) {
        throw "V7 SDL patch anchor not found: $Label"
    }
    $second = $Text.IndexOf($Old, $first + $Old.Length, [System.StringComparison]::Ordinal)
    if ($second -ge 0) {
        throw "V7 SDL patch anchor is not unique: $Label"
    }
    return $Text.Substring(0, $first) + $New + $Text.Substring($first + $Old.Length)
}

$videoPath = Join-Path $SourceRoot "src\video\winrt\SDL_winrtvideo.cpp"
$glPath = Join-Path $SourceRoot "src\video\windows\SDL_windowsopengl.c"
$glHeaderPath = Join-Path $SourceRoot "src\video\windows\SDL_windowsopengl.h"
if (-not (Test-Path -LiteralPath $videoPath)) { throw "Missing SDL WinRT video source: $videoPath" }
if (-not (Test-Path -LiteralPath $glPath)) { throw "Missing SDL Windows OpenGL source: $glPath" }
if (-not (Test-Path -LiteralPath $glHeaderPath)) { throw "Missing SDL Windows OpenGL header: $glHeaderPath" }

$video = Read-NormalizedText -Path $videoPath
$gl = Read-NormalizedText -Path $glPath
$glHeader = Read-NormalizedText -Path $glHeaderPath

$old = @'
#include <sstream>
#include <string>
'@
$new = @'
#include <sstream>
#include <string>
#include <vector>
'@
$video = Replace-Once -Text $video -Old $old -New $new -Label "WinRT C++ include block"

$old = @'
/* Exported build marker used by Bandit's packager to reject stock SDL-UWP
   binaries. Bump this whenever the external-host contract changes. */
extern "C" __declspec(dllexport) int BanditSDL_ExternalHostPatchVersion(void)
{
    return 6;
}

/* V6 keeps the external CoreWindow/event bridge and SDL_main readiness fix,
   and adds the packaged-Mesa absolute-path normalization contract. */
extern "C" __declspec(dllexport) int BanditSDL_WinRTIntegrationPatchV6(void)
{
    return 6;
}

#ifdef SDL_VIDEO_OPENGL_WGL
/* This symbol only exists when the runtime was actually compiled with WGL. */
extern "C" __declspec(dllexport) int BanditSDL_WinRTWGLBackendV1(void)
{
    return 1;
}
#endif
'@
$new = @'
/* Exported build marker used by Bandit's packager to reject stock SDL-UWP
   binaries. Bump this whenever the external-host contract changes. */
extern "C" __declspec(dllexport) int BanditSDL_ExternalHostPatchVersion(void)
{
    return 7;
}

/* V7 keeps the external CoreWindow bridge, direct SDL3/LWJGL loading and
   packaged-Mesa normalization, then fixes the WinRT/WGL lifetime itself:
   no pre-window probe, real-window pixel-format setup and logical multiwindow. */
extern "C" __declspec(dllexport) int BanditSDL_WinRTIntegrationPatchV7(void)
{
    return 7;
}

#ifdef SDL_VIDEO_OPENGL_WGL
/* This symbol only exists when the runtime was actually compiled with WGL. */
extern "C" __declspec(dllexport) int BanditSDL_WinRTWGLBackendV1(void)
{
    return 1;
}

/* Build markers for the V7 WGL lifecycle and logical-window virtualization. */
extern "C" __declspec(dllexport) int BanditSDL_WinRTSafeWGLInitV1(void)
{
    return 1;
}
extern "C" __declspec(dllexport) int BanditSDL_WinRTMultiWindowV1(void)
{
    return 1;
}
#endif

/* Xbox/UWP exposes one physical CoreWindow, but Minecraft 26.3 creates
   several logical SDL windows during RenderPearl startup (utility, test,
   then the real game window). Keep the logical objects while mapping all
   of them to that single CoreWindow. The newest logical window receives
   CoreWindow events. */
static std::vector<SDL_Window *> BanditWinRTLogicalWindows;
'@
$video = Replace-Once -Text $video -Old $old -New $new -Label "V7 build markers"

$old = @'
    // Make sure that only one window gets created, at least until multimonitor
    // support is added.
    if (WINRT_GlobalSDLWindow != NULL) {
        return SDL_SetError("WinRT only supports one window");
    }

'@
$new = @'
    /* Bandit: do not reject a second logical SDL window. WinRT still has one
       physical CoreWindow; Minecraft 26.3 intentionally creates multiple SDL
       window objects over it during OpenGL startup. */

'@
$video = Replace-Once -Text $video -Old $old -New $new -Label "remove WinRT one-window rejection"

$old = @'
#elif SDL_VIDEO_OPENGL_WGL
    data->hdc = (HDC)data->coreWindow.Get();
#endif
'@
$new = @'
#elif SDL_VIDEO_OPENGL_WGL
    if (data->coreWindow.Get() == nullptr) {
        delete data;
        window->internal = NULL;
        return SDL_SetError("Bandit WinRT WGL: CoreWindow is unavailable on the SDL thread");
    }

    data->hdc = (HDC)data->coreWindow.Get();
    if (window->flags & SDL_WINDOW_OPENGL) {
    if (!_this->gl_data) {
        delete data;
        window->internal = NULL;
        return SDL_SetError("Bandit WinRT WGL: OpenGL library was not loaded before window creation");
    }

    /* The desktop Windows backend calls WIN_GL_SetupWindow before context
       creation. The upstream WinRT/WGL branch did not, leaving Mesa without
       a pixel format/framebuffer and relying on WIN_GL_LoadLibrary's unsafe
       temporary-context probe. Set the pixel format on the real CoreWindow.

       All Minecraft logical SDL windows share the same physical CoreWindow,
       and WGL permits SetPixelFormat only once, so later logical windows reuse
       the already-established format. */
    const int banditPixelFormat = WIN_GL_GetWindowPixelFormat(_this, data->hdc);
    if (banditPixelFormat < 0) {
        delete data;
        window->internal = NULL;
        return false;
    }
    if (banditPixelFormat > 0) {
        SDL_Log("Bandit SDL: WinRT WGL reusing CoreWindow pixel format=%d", banditPixelFormat);
    } else {
        SDL_Log("Bandit SDL: WinRT WGL pixel-format setup begin hdc=%p", data->hdc);
        if (!WIN_GL_SetupWindow(_this, window)) {
            delete data;
            window->internal = NULL;
            return false;
        }
        SDL_Log("Bandit SDL: WinRT WGL pixel-format setup ready hdc=%p", data->hdc);
    }
    }
#endif
'@
$video = Replace-Once -Text $video -Old $old -New $new -Label "WinRT real-window WGL setup"

$old = @'
    WINRT_GlobalSDLWindow = window;

    /* Bandit Launcher embeds SDL inside an already-running UWP IFrameworkView.
       Attach SDL's standard WinRT event sink to the host-owned CoreWindow so
       SDL_PollEvent pumps keyboard, mouse, text, window and activation events. */
'@
$new = @'
    BanditWinRTLogicalWindows.push_back(window);
    WINRT_GlobalSDLWindow = window;
    SDL_Log("Bandit SDL: logical WinRT window created count=%llu window=%p",
            (unsigned long long)BanditWinRTLogicalWindows.size(), window);

    /* Bandit Launcher embeds SDL inside an already-running UWP IFrameworkView.
       Attach SDL's standard WinRT event sink to the host-owned CoreWindow so
       SDL_PollEvent pumps keyboard, mouse, text, window and activation events. */
'@
$video = Replace-Once -Text $video -Old $old -New $new -Label "logical WinRT window registration"

$old = @'
void WINRT_VideoQuit(SDL_VideoDevice *_this)
{
    SDL_VideoData *internal = _this->internal;
    if (internal && internal->displayRequest) {
        internal->displayRequest->Release();
        internal->displayRequest = NULL;
    }
    WINRT_QuitGameBar(_this);
    WINRT_QuitMouse(_this);
}
'@
$new = @'
void WINRT_VideoQuit(SDL_VideoDevice *_this)
{
    SDL_VideoData *internal = _this->internal;
    if (internal && internal->displayRequest) {
        internal->displayRequest->Release();
        internal->displayRequest = NULL;
    }
    WINRT_QuitGameBar(_this);
    WINRT_QuitMouse(_this);

    BanditWinRTLogicalWindows.clear();
    WINRT_GlobalSDLWindow = NULL;
    SDL_Log("Bandit SDL: WinRT video quit cleared logical window targets");
}
'@
$video = Replace-Once -Text $video -Old $old -New $new -Label "clear logical windows on video quit"

$old = @'
    if (WINRT_GlobalSDLWindow == window) {
        WINRT_GlobalSDLWindow = NULL;
    }

'@
$new = @'
    for (size_t i = 0; i < BanditWinRTLogicalWindows.size(); ++i) {
        if (BanditWinRTLogicalWindows[i] == window) {
            BanditWinRTLogicalWindows.erase(BanditWinRTLogicalWindows.begin() + i);
            break;
        }
    }
    WINRT_GlobalSDLWindow = BanditWinRTLogicalWindows.empty() ? NULL : BanditWinRTLogicalWindows.back();
    SDL_Log("Bandit SDL: logical WinRT window destroyed count=%llu window=%p next=%p",
            (unsigned long long)BanditWinRTLogicalWindows.size(), window, WINRT_GlobalSDLWindow);

'@
$video = Replace-Once -Text $video -Old $old -New $new -Label "logical WinRT window destruction"

$old = @'
extern bool WIN_GL_UseEGL(SDL_VideoDevice *_this);
extern bool WIN_GL_SetupWindow(SDL_VideoDevice *_this, SDL_Window *window);
extern SDL_GLContext WIN_GL_CreateContext(SDL_VideoDevice *_this, SDL_Window *window);
'@
$new = @'
extern bool WIN_GL_UseEGL(SDL_VideoDevice *_this);
extern int WIN_GL_GetWindowPixelFormat(SDL_VideoDevice *_this, HDC hdc);
extern bool WIN_GL_SetupWindow(SDL_VideoDevice *_this, SDL_Window *window);
extern SDL_GLContext WIN_GL_CreateContext(SDL_VideoDevice *_this, SDL_Window *window);
'@
$glHeader = Replace-Once -Text $glHeader -Old $old -New $new -Label "declare safe WinRT pixel-format query"

$old = @'
/* Sets an error message based on GetLastError(). Always return -1. */
extern int WIN_SetError(const char *prefix);
'@
$new = @'
/* Keep the declaration ABI/type-correct with SDL_windows.h. The pinned WinRT
   fork carried an obsolete int/-1 declaration even though WIN_SetError now
   returns bool/false. Returning -1 from bool WGL functions masks failures. */
extern bool WIN_SetError(const char *prefix);
'@
$gl = Replace-Once -Text $gl -Old $old -New $new -Label "correct WinRT WIN_SetError declaration"

$old = @'
#define SetPixelFormat       _this->gl_data->wglSetPixelFormat
#endif

bool WIN_GL_LoadLibrary(SDL_VideoDevice *_this, const char *path)
'@
$new = @'
#define SetPixelFormat       _this->gl_data->wglSetPixelFormat
#endif

#if defined(SDL_PLATFORM_WINRT) && defined(_MSC_VER)
/* Mesa-UWP lives outside SDL. If a driver entrypoint faults, turn it into a
   useful SDL error instead of terminating the embedded JVM with an opaque
   EXCEPTION_ACCESS_VIOLATION. */
static HGLRC BANDIT_WIN_GL_CreateContextSafe(SDL_VideoDevice *_this, HDC hdc)
{
    HGLRC result = NULL;
    __try {
        result = _this->gl_data->wglCreateContext(hdc);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        SDL_SetError("Bandit WinRT WGL: wglCreateContext raised SEH 0x%08lX", (unsigned long)GetExceptionCode());
    }
    return result;
}

static BOOL BANDIT_WIN_GL_MakeCurrentSafe(SDL_VideoDevice *_this, HDC hdc, HGLRC context)
{
    BOOL result = FALSE;
    __try {
        result = _this->gl_data->wglMakeCurrent(hdc, context);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        SDL_SetError("Bandit WinRT WGL: wglMakeCurrent raised SEH 0x%08lX", (unsigned long)GetExceptionCode());
    }
    return result;
}

static HGLRC BANDIT_WIN_GL_CreateContextAttribsSafe(PFNWGLCREATECONTEXTATTRIBSARBPROC proc, HDC hdc, HGLRC share_context, const int *attribs)
{
    HGLRC result = NULL;
    __try {
        result = proc(hdc, share_context, attribs);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        SDL_SetError("Bandit WinRT WGL: wglCreateContextAttribsARB raised SEH 0x%08lX", (unsigned long)GetExceptionCode());
    }
    return result;
}

static BOOL BANDIT_WIN_GL_SwapBuffersSafe(SDL_VideoDevice *_this, HDC hdc)
{
    BOOL result = FALSE;
    __try {
        result = _this->gl_data->wglSwapBuffers(hdc);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        SDL_SetError("Bandit WinRT WGL: wglSwapBuffers raised SEH 0x%08lX", (unsigned long)GetExceptionCode());
    }
    return result;
}
#else
#define BANDIT_WIN_GL_CreateContextSafe(_this, hdc) ((_this)->gl_data->wglCreateContext(hdc))
#define BANDIT_WIN_GL_MakeCurrentSafe(_this, hdc, context) ((_this)->gl_data->wglMakeCurrent((hdc), (context)))
#define BANDIT_WIN_GL_CreateContextAttribsSafe(proc, hdc, share_context, attribs) ((proc)((hdc), (share_context), (attribs)))
#define BANDIT_WIN_GL_SwapBuffersSafe(_this, hdc) ((_this)->gl_data->wglSwapBuffers(hdc))
#endif

bool WIN_GL_LoadLibrary(SDL_VideoDevice *_this, const char *path)
'@
$gl = Replace-Once -Text $gl -Old $old -New $new -Label "WinRT WGL SEH guards"

$old = @'
    /* XXX Too sleazy? WIN_GL_InitExtensions looks for certain OpenGL
       extensions via SDL_GL_DeduceMaxSupportedESProfile. This uses
       SDL_GL_ExtensionSupported which in turn calls SDL_GL_GetProcAddress.
       However SDL_GL_GetProcAddress will fail if the library is not
       loaded; it checks for gl_config.driver_loaded > 0. To avoid this
       test failing, increment driver_loaded around the call to
       WIN_GLInitExtensions.

       Successful loading of the library is normally indicated by
       SDL_GL_LoadLibrary incrementing driver_loaded immediately after
       this function returns 0 to it.

       Alternatives to this are:
       - moving SDL_GL_DeduceMaxSupportedESProfile to both the WIN and
         X11 platforms while adding a function equivalent to
         SDL_GL_ExtensionSupported but which directly calls
         glGetProcAddress(). Having 3 copies of the
         SDL_GL_ExtensionSupported makes this alternative unattractive.
       - moving SDL_GL_DeduceMaxSupportedESProfile to a new file shared
         by the WIN and X11 platforms while adding a function equivalent
         to SDL_GL_ExtensionSupported. This is unattractive due to the
         number of project files that will need updating, plus there
         will be 2 copies of the SDL_GL_ExtensionSupported code.
       - Add a private equivalent of SDL_GL_ExtensionSupported to
         SDL_video.c.
       - Move the call to WIN_GL_InitExtensions back to WIN_CreateWindow
         and add a flag to gl_data to avoid multiple calls to this
         expensive function. This is probably the least objectionable
         alternative if this increment/decrement trick is unacceptable.

       Note that the driver_loaded > 0 check needs to remain in
       SDL_GL_ExtensionSupported and SDL_GL_GetProcAddress as they are
       public API functions.
    */
    ++_this->gl_config.driver_loaded;
    WIN_GL_InitExtensions(_this);
    --_this->gl_config.driver_loaded;

    return true;
'@
$new = @'
    SDL_Log("Bandit SDL: Mesa WGL exports loaded");

#ifdef SDL_PLATFORM_WINRT
    /* Do not create SDL's temporary WGL probe context here. Minecraft 26.3
       calls SDL_GL_LoadLibrary before it has created any SDL_Window. Mesa-UWP
       needs the real CoreWindow/pixel-format lifecycle first; probing here can
       fault natively. WGL capabilities are populated after the first real
       OpenGL context becomes current. */
    SDL_Log("Bandit SDL: deferred WinRT WGL extension probe until a real context is current");
#else
    /* XXX Too sleazy? WIN_GL_InitExtensions looks for certain OpenGL
       extensions via SDL_GL_DeduceMaxSupportedESProfile. This uses
       SDL_GL_ExtensionSupported which in turn calls SDL_GL_GetProcAddress.
       However SDL_GL_GetProcAddress will fail if the library is not
       loaded; it checks for gl_config.driver_loaded > 0. To avoid this
       test failing, increment driver_loaded around the call to
       WIN_GLInitExtensions. */
    ++_this->gl_config.driver_loaded;
    WIN_GL_InitExtensions(_this);
    --_this->gl_config.driver_loaded;
#endif

    return true;
'@
$gl = Replace-Once -Text $gl -Old $old -New $new -Label "defer pre-window WGL extension probe"

$old = @'
SDL_FunctionPointer WIN_GL_GetProcAddress(SDL_VideoDevice *_this, const char *proc)
{
    SDL_FunctionPointer func;

    // This is to pick up extensions
    func = (SDL_FunctionPointer)_this->gl_data->wglGetProcAddress(proc);
    if (!func) {
        // This is probably a normal GL function
        func = (SDL_FunctionPointer)GetProcAddress((HMODULE)_this->gl_config.dll_handle, proc);
    }
    return func;
}
'@
$new = @'
SDL_FunctionPointer WIN_GL_GetProcAddress(SDL_VideoDevice *_this, const char *proc)
{
    SDL_FunctionPointer func;

#ifdef SDL_PLATFORM_WINRT
    /* Minecraft 26.3 requires SDL's core-GL function pointer to be exactly
       the same address as LWJGL's SharedLibrary provider. Prefer the DLL
       export for core OpenGL symbols; use wglGetProcAddress for extensions. */
    func = (SDL_FunctionPointer)GetProcAddress((HMODULE)_this->gl_config.dll_handle, proc);
    if (func) {
        return func;
    }
#endif

    // This is to pick up extensions
    func = (SDL_FunctionPointer)_this->gl_data->wglGetProcAddress(proc);
    if (!func) {
        // This is probably a normal GL function
        func = (SDL_FunctionPointer)GetProcAddress((HMODULE)_this->gl_config.dll_handle, proc);
    }
    return func;
}
'@
$gl = Replace-Once -Text $gl -Old $old -New $new -Label "core GL provider pointer parity"

$old = @'
    return func;
}

void WIN_GL_UnloadLibrary(SDL_VideoDevice *_this)
'@
$new = @'
    return func;
}

int WIN_GL_GetWindowPixelFormat(SDL_VideoDevice *_this, HDC hdc)
{
    if (!_this->gl_data || !_this->gl_data->wglGetPixelFormat) {
        SDL_SetError("Bandit WinRT WGL: wglGetPixelFormat export is unavailable");
        return -1;
    }
#if defined(SDL_PLATFORM_WINRT) && defined(_MSC_VER)
    int result = -1;
    __try {
        result = _this->gl_data->wglGetPixelFormat(hdc);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        SDL_SetError("Bandit WinRT WGL: wglGetPixelFormat raised SEH 0x%08lX", (unsigned long)GetExceptionCode());
        return -1;
    }
    return result;
#else
    return _this->gl_data->wglGetPixelFormat(hdc);
#endif
}

void WIN_GL_UnloadLibrary(SDL_VideoDevice *_this)
'@
$gl = Replace-Once -Text $gl -Old $old -New $new -Label "safe WinRT pixel-format query"

$old = @'
    return false;
}

#ifdef SDL_PLATFORM_WINRT
HWND uwp_window_handle();
#endif
'@
$new = @'
    return false;
}

#ifdef SDL_PLATFORM_WINRT
/* Populate the WGL capabilities that SDL needs only after Mesa has a real
   CoreWindow-backed context. Intentionally leave HAS_WGL_ARB_pixel_format
   false so later logical SDL windows reuse the one physical CoreWindow pixel
   format instead of creating more temporary probe contexts. */
static void BANDIT_WIN_GL_InitExtensionsFromCurrentContext(SDL_VideoDevice *_this, HDC hdc)
{
    const char *(WINAPI *wglGetExtensionsStringARB)(HDC) = NULL;
    const char *extensions = NULL;

    _this->gl_data->HAS_WGL_ARB_pixel_format = false;
    _this->gl_data->wglChoosePixelFormatARB = NULL;
    _this->gl_data->wglGetPixelFormatAttribivARB = NULL;
    _this->gl_data->wglSwapIntervalEXT = NULL;
    _this->gl_data->wglGetSwapIntervalEXT = NULL;

    wglGetExtensionsStringARB = (const char *(WINAPI *)(HDC))
        _this->gl_data->wglGetProcAddress("wglGetExtensionsStringARB");
    if (wglGetExtensionsStringARB) {
        extensions = wglGetExtensionsStringARB(hdc);
    }

    _this->gl_data->HAS_WGL_EXT_swap_control_tear = false;
    if (HasExtension("WGL_EXT_swap_control", extensions)) {
        _this->gl_data->wglSwapIntervalEXT =
            (BOOL (WINAPI *)(int))_this->gl_data->wglGetProcAddress("wglSwapIntervalEXT");
        _this->gl_data->wglGetSwapIntervalEXT =
            (int (WINAPI *)(void))_this->gl_data->wglGetProcAddress("wglGetSwapIntervalEXT");
        _this->gl_data->HAS_WGL_EXT_swap_control_tear = HasExtension("WGL_EXT_swap_control_tear", extensions);
    }

    _this->gl_data->HAS_WGL_ARB_context_flush_control = HasExtension("WGL_ARB_context_flush_control", extensions);
    _this->gl_data->HAS_WGL_ARB_create_context_robustness = HasExtension("WGL_ARB_create_context_robustness", extensions);
    _this->gl_data->HAS_WGL_ARB_create_context_no_error = HasExtension("WGL_ARB_create_context_no_error", extensions);
    _this->gl_data->HAS_WGL_ARB_framebuffer_sRGB =
        HasExtension("WGL_ARB_framebuffer_sRGB", extensions) || HasExtension("WGL_EXT_framebuffer_sRGB", extensions);
    _this->gl_data->HAS_WGL_ARB_pixel_format_float = HasExtension("WGL_ARB_pixel_format_float", extensions);
    _this->gl_data->HAS_WGL_EXT_create_context_es2_profile = false;

    SDL_Log("Bandit SDL: initialized WGL extensions from real WinRT context (swap_control=%d)",
            _this->gl_data->wglSwapIntervalEXT != NULL ? 1 : 0);
}

HWND uwp_window_handle();
#endif
'@
$gl = Replace-Once -Text $gl -Old $old -New $new -Label "real-context WGL extension initialization"

$old = @'
    WIN_GL_SetupPixelFormat(_this, &pfd);

    // setup WGL_ARB_pixel_format attribs
'@
$new = @'
    WIN_GL_SetupPixelFormat(_this, &pfd);

#ifdef SDL_PLATFORM_WINRT
    /* One physical CoreWindow/HDC is shared by Minecraft's logical windows.
       Use the ordinary Mesa-UWP pixel-format API and never create another
       temporary context just to choose an ARB pixel format. */
    pixel_format = WIN_GL_ChoosePixelFormat(_this, hdc, &pfd);
    if (!pixel_format) {
        return SDL_SetError("No matching WinRT GL pixel format available");
    }
    if (!SetPixelFormat(hdc, pixel_format, &pfd)) {
        return WIN_SetError("wglSetPixelFormat()");
    }
    return true;
#endif

    // setup WGL_ARB_pixel_format attribs
'@
$gl = Replace-Once -Text $gl -Old $old -New $new -Label "WinRT standard pixel-format path"

$old = @'
        context = _this->gl_data->wglCreateContext(hdc);
'@
$new = @'
        context = BANDIT_WIN_GL_CreateContextSafe(_this, hdc);
'@
$gl = Replace-Once -Text $gl -Old $old -New $new -Label "safe legacy context creation"

$old = @'
        HGLRC temp_context = _this->gl_data->wglCreateContext(hdc);
'@
$new = @'
        SDL_Log("Bandit SDL: WinRT WGL context create begin hdc=%p", hdc);
        HGLRC temp_context = BANDIT_WIN_GL_CreateContextSafe(_this, hdc);
'@
$gl = Replace-Once -Text $gl -Old $old -New $new -Label "safe temporary context creation"

$old = @'
            context = wglCreateContextAttribsARB(hdc, share_context, attribs);
'@
$new = @'
            context = BANDIT_WIN_GL_CreateContextAttribsSafe(wglCreateContextAttribsARB, hdc, share_context, attribs);
'@
$gl = Replace-Once -Text $gl -Old $old -New $new -Label "safe attributed context creation"

$old = @'
    if (!WIN_GL_MakeCurrent(_this, window, (SDL_GLContext)context)) {
        WIN_GL_DestroyContext(_this, (SDL_GLContext)context);
        return NULL;
    }

    _this->gl_config.HAS_GL_ARB_color_buffer_float =
'@
$new = @'
    if (!WIN_GL_MakeCurrent(_this, window, (SDL_GLContext)context)) {
        WIN_GL_DestroyContext(_this, (SDL_GLContext)context);
        return NULL;
    }

#ifdef SDL_PLATFORM_WINRT
    BANDIT_WIN_GL_InitExtensionsFromCurrentContext(_this, hdc);
    SDL_Log("Bandit SDL: WinRT WGL OpenGL %d.%d context ready context=%p",
            _this->gl_config.major_version, _this->gl_config.minor_version, context);
#endif

    _this->gl_config.HAS_GL_ARB_color_buffer_float =
'@
$gl = Replace-Once -Text $gl -Old $old -New $new -Label "initialize extensions after real context"

$old = @'
    if (!_this->gl_data->wglMakeCurrent(hdc, (HGLRC)context)) {
        return WIN_SetError("wglMakeCurrent()");
    }
'@
$new = @'
    if (!BANDIT_WIN_GL_MakeCurrentSafe(_this, hdc, (HGLRC)context)) {
        if (!*SDL_GetError()) {
            return WIN_SetError("wglMakeCurrent()");
        }
        return false;
    }
'@
$gl = Replace-Once -Text $gl -Old $old -New $new -Label "safe wglMakeCurrent"

$old = @'
    if (!SwapBuffers(hdc)) {
        return WIN_SetError("SwapBuffers()");
    }
'@
$new = @'
#ifdef SDL_PLATFORM_WINRT
    if (!BANDIT_WIN_GL_SwapBuffersSafe(_this, hdc)) {
        if (!*SDL_GetError()) {
            return WIN_SetError("wglSwapBuffers()");
        }
        return false;
    }
#else
    if (!SwapBuffers(hdc)) {
        return WIN_SetError("SwapBuffers()");
    }
#endif
'@
$gl = Replace-Once -Text $gl -Old $old -New $new -Label "safe WinRT swap"

if ($video.Contains("WinRT only supports one window")) {
    throw "V7 SDL patch validation failed: upstream one-window rejection is still present"
}
foreach ($required in @(
    "BanditSDL_WinRTIntegrationPatchV7",
    "BanditSDL_WinRTSafeWGLInitV1",
    "BanditSDL_WinRTMultiWindowV1",
    "logical WinRT window created",
    "WIN_GL_SetupWindow(_this, window)"
)) {
    if (-not $video.Contains($required)) { throw "V7 SDL video validation failed: missing $required" }
}
if (-not $glHeader.Contains("WIN_GL_GetWindowPixelFormat")) { throw "V7 SDL WGL header validation failed: safe pixel-format query is missing" }
foreach ($required in @(
    "deferred WinRT WGL extension probe until a real context is current",
    "BANDIT_WIN_GL_CreateContextSafe",
    "initialized WGL extensions from real WinRT context",
    "extern bool WIN_SetError",
    "GetProcAddress((HMODULE)_this->gl_config.dll_handle, proc)"
)) {
    if (-not $gl.Contains($required)) { throw "V7 SDL WGL validation failed: missing $required" }
}

Write-NormalizedText -Path $videoPath -Text $video
Write-NormalizedText -Path $glPath -Text $gl
Write-NormalizedText -Path $glHeaderPath -Text $glHeader

Write-Host "Applied Bandit SDL V7 WinRT/WGL lifecycle patch: deferred probe, real-window pixel format, pointer parity, logical multiwindow and SEH diagnostics."
