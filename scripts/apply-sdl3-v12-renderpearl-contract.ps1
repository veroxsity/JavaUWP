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
    param([Parameter(Mandatory = $true)][string]$Path,[Parameter(Mandatory = $true)][string]$Text)
    [System.IO.File]::WriteAllText($Path, $Text, $Utf8NoBom)
}
function Replace-Once {
    param([Parameter(Mandatory = $true)][string]$Text,[Parameter(Mandatory = $true)][string]$Old,[Parameter(Mandatory = $true)][string]$New,[Parameter(Mandatory = $true)][string]$Label)
    $first = $Text.IndexOf($Old, [System.StringComparison]::Ordinal)
    if ($first -lt 0) { throw "V12 SDL patch anchor not found: $Label" }
    $second = $Text.IndexOf($Old, $first + $Old.Length, [System.StringComparison]::Ordinal)
    if ($second -ge 0) { throw "V12 SDL patch anchor is not unique: $Label" }
    return $Text.Substring(0, $first) + $New + $Text.Substring($first + $Old.Length)
}

$videoPath = Join-Path $SourceRoot "src\video\winrt\SDL_winrtvideo.cpp"
$glPath = Join-Path $SourceRoot "src\video\windows\SDL_windowsopengl.c"
if (-not (Test-Path -LiteralPath $videoPath)) { throw "Missing SDL WinRT video source: $videoPath" }
if (-not (Test-Path -LiteralPath $glPath)) { throw "Missing SDL Windows OpenGL source: $glPath" }

$video = Read-NormalizedText -Path $videoPath
$gl = Read-NormalizedText -Path $glPath

# Marker: V12 deliberately replaces the V10 single-HGLRC experiment. Vanilla
# 26.3 creates one GL context and moves it from the hidden utility window to the
# final game window; context virtualization was based on a false premise.
$old = @'
extern "C" __declspec(dllexport) int BanditSDL_WinRTStabilityV9(void)
{
    return 9;
}
'@
$new = @'
extern "C" __declspec(dllexport) int BanditSDL_WinRTStabilityV9(void)
{
    return 9;
}

/* V12 implements the actual Minecraft 26.3 RenderPearl contract on Xbox:
   one real GL context, logical WinRT windows, deterministic Mesa pixel-format
   setup, real-context validation, and synchronous synthetic window ops. */
extern "C" __declspec(dllexport) int BanditSDL_WinRTRenderPearlV12(void)
{
    return 12;
}
'@
$video = Replace-Once -Text $video -Old $old -New $new -Label "V12 marker"

# Track pixel-format installation ourselves. Mesa 26.1.3's wglGetPixelFormat is
# NOT a passive query: it enters stw_pixelformat_get -> stw_framebuffer_create.
# V7 queried it before SetPixelFormat, which could instantiate framebuffer 0
# with an implicit/default format and then skip the intended setup entirely.
$old = @'
static std::vector<SDL_Window *> BanditWinRTLogicalWindows;
'@
$new = @'
static std::vector<SDL_Window *> BanditWinRTLogicalWindows;
#if SDL_VIDEO_OPENGL_WGL
static bool BanditWinRTPixelFormatConfigured = false;
#endif
'@
$video = Replace-Once -Text $video -Old $old -New $new -Label "pixel-format configured state"

$old = @'
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
'@
$new = @'
    /* Do not call wglGetPixelFormat here. In this Mesa-UWP build that query
       can create stw framebuffer state as a side effect before our requested
       pixel format is installed. The project's proven MesaTestApp/GLFW path
       explicitly chooses+sets the format once, so mirror that contract. */
    if (!BanditWinRTPixelFormatConfigured) {
        SDL_Log("Bandit SDL V12: deterministic CoreWindow pixel-format setup begin hdc=%p", data->hdc);
        if (!WIN_GL_SetupWindow(_this, window)) {
            delete data;
            window->internal = NULL;
            return false;
        }
        BanditWinRTPixelFormatConfigured = true;
        SDL_Log("Bandit SDL V12: deterministic CoreWindow pixel-format setup ready hdc=%p", data->hdc);
    } else {
        SDL_Log("Bandit SDL V12: reusing already-configured CoreWindow pixel format");
    }
'@
$video = Replace-Once -Text $video -Old $old -New $new -Label "remove side-effecting pre-SetPixelFormat query"

$old = @'
    BanditWinRTLogicalWindows.clear();
    WINRT_GlobalSDLWindow = NULL;
    SDL_Log("Bandit SDL: WinRT video quit cleared logical window targets");
'@
$new = @'
    BanditWinRTLogicalWindows.clear();
    WINRT_GlobalSDLWindow = NULL;
#if SDL_VIDEO_OPENGL_WGL
    BanditWinRTPixelFormatConfigured = false;
#endif
    SDL_Log("Bandit SDL: WinRT video quit cleared logical window targets");
'@
$video = Replace-Once -Text $video -Old $old -New $new -Label "reset deterministic pixel-format state"

# Preserve Minecraft's logical window flags. Stock WinRT flattens every window
# to borderless/resizable because it assumes one app window. RenderPearl uses
# HIDDEN/UTILITY/NOT_FOCUSABLE for its probe window and HIGH_PIXEL_DENSITY for
# the final window; those logical properties still matter even though all SDL
# objects map to one physical CoreWindow.
$old = @'
    window->flags =
        SDL_WINDOW_BORDERLESS |
        SDL_WINDOW_RESIZABLE;
'@
$new = @'
    window->flags = requestedFlags | SDL_WINDOW_BORDERLESS;
    /* The physical Xbox CoreWindow cannot be desktop-resized, but keeping the
       logical resizable bit preserves Minecraft's expected SDL window model. */
    window->flags |= SDL_WINDOW_RESIZABLE;
'@
$video = Replace-Once -Text $video -Old $old -New $new -Label "preserve RenderPearl logical window flags"

# Minecraft's final Window constructor calls SetWindowFullscreen(false),
# SetWindowSize, SetWindowPosition and SDL_SyncWindow before the first frame.
# WinRT had no SetWindowPosition/SyncWindow callback and therefore returned
# SDL_Unsupported during every launch. Xbox has one fixed CoreWindow, so these
# desktop window-manager operations are acknowledged synchronously while the
# physical surface remains fixed.
$old = @'
static bool WINRT_CreateWindow(SDL_VideoDevice *_this, SDL_Window *window, SDL_PropertiesID create_props);
static void WINRT_SetWindowSize(SDL_VideoDevice *_this, SDL_Window *window);
static SDL_FullscreenResult WINRT_SetWindowFullscreen(SDL_VideoDevice *_this, SDL_Window *window, SDL_VideoDisplay *display, SDL_FullscreenOp fullscreen);
static void WINRT_DestroyWindow(SDL_VideoDevice *_this, SDL_Window *window);
'@
$new = @'
static bool WINRT_CreateWindow(SDL_VideoDevice *_this, SDL_Window *window, SDL_PropertiesID create_props);
static bool WINRT_SetWindowPosition(SDL_VideoDevice *_this, SDL_Window *window);
static void WINRT_SetWindowSize(SDL_VideoDevice *_this, SDL_Window *window);
static bool WINRT_SyncWindow(SDL_VideoDevice *_this, SDL_Window *window);
static SDL_FullscreenResult WINRT_SetWindowFullscreen(SDL_VideoDevice *_this, SDL_Window *window, SDL_VideoDisplay *display, SDL_FullscreenOp fullscreen);
static void WINRT_DestroyWindow(SDL_VideoDevice *_this, SDL_Window *window);
'@
$video = Replace-Once -Text $video -Old $old -New $new -Label "RenderPearl window callback declarations"

$old = @'
    device->CreateSDLWindow = WINRT_CreateWindow;
    device->SetWindowSize = WINRT_SetWindowSize;
    device->SetWindowFullscreen = WINRT_SetWindowFullscreen;
'@
$new = @'
    device->CreateSDLWindow = WINRT_CreateWindow;
    device->SetWindowPosition = WINRT_SetWindowPosition;
    device->SetWindowSize = WINRT_SetWindowSize;
    device->SyncWindow = WINRT_SyncWindow;
    device->SetWindowFullscreen = WINRT_SetWindowFullscreen;
'@
$video = Replace-Once -Text $video -Old $old -New $new -Label "RenderPearl window callback registration"

$old = @'
void WINRT_SetWindowSize(SDL_VideoDevice *_this, SDL_Window *window)
{
#if NTDDI_VERSION >= NTDDI_WIN10
    SDL_WindowData *data = window->internal;
    const Windows::Foundation::Size size((float)window->floating.w, (float)window->floating.h);
    if (data->appView->TryResizeView(size)) {
        SDL_SendWindowEvent(window, SDL_EVENT_WINDOW_RESIZED, window->floating.w, window->floating.h);
    }
#endif
}
'@
$new = @'
bool WINRT_SetWindowPosition(SDL_VideoDevice *_this, SDL_Window *window)
{
    (void)_this;
    if (SDL_GetWinRTDeviceFamily() == SDL_WINRT_DEVICEFAMILY_XBOX) {
        /* Xbox has no desktop window position. Acknowledge the request and
           clear SDL's pending-position state without touching CoreWindow. */
        SDL_SendWindowEvent(window, SDL_EVENT_WINDOW_MOVED, window->pending.x, window->pending.y);
        SDL_Log("Bandit SDL V12: synthetic Xbox SetWindowPosition acknowledged");
        return true;
    }
    return SDL_Unsupported();
}

void WINRT_SetWindowSize(SDL_VideoDevice *_this, SDL_Window *window)
{
    if (SDL_GetWinRTDeviceFamily() == SDL_WINRT_DEVICEFAMILY_XBOX) {
        /* The CoreWindow size is controlled by Xbox. Clear SDL's pending size
           request using SDL's requested logical size. Pixel dimensions are provided
           independently by WINRT_GetWindowSizeInPixels from the fixed TV output. */
        SDL_SendWindowEvent(window, SDL_EVENT_WINDOW_RESIZED, window->pending.w, window->pending.h);
        SDL_Log("Bandit SDL V12: synthetic Xbox SetWindowSize acknowledged logical=%dx%d",
                window->pending.w, window->pending.h);
        return;
    }
#if NTDDI_VERSION >= NTDDI_WIN10
    SDL_WindowData *data = window->internal;
    const Windows::Foundation::Size size((float)window->floating.w, (float)window->floating.h);
    if (data->appView->TryResizeView(size)) {
        SDL_SendWindowEvent(window, SDL_EVENT_WINDOW_RESIZED, window->floating.w, window->floating.h);
    }
#endif
}

bool WINRT_SyncWindow(SDL_VideoDevice *_this, SDL_Window *window)
{
    (void)_this;
    (void)window;
    /* CoreWindow operations above are synchronous no-ops on Xbox. */
    return true;
}
'@
$video = Replace-Once -Text $video -Old $old -New $new -Label "Xbox synchronous window-manager contract"

# ---- Mesa support stack and exact proven pixel-format path ------------------
# The old GLFW/CoreWindow backend in this same project is explicitly documented
# as proven on Series S. Mirror its load order and use libgallium_wgl!SetPixelFormat
# rather than opengl32!wglSetPixelFormat/DrvSetPixelFormat.
$old = @'
static PROC BANDIT_WIN_GL_GetProcAddressSafe(SDL_VideoDevice *_this, const char *proc)
'@
$new = @'
#ifdef SDL_PLATFORM_WINRT
static void *BanditWinRTMesaDxil = NULL;
static void *BanditWinRTMesaZlib = NULL;
static void *BanditWinRTMesaSpirvToDxil = NULL;
static void *BanditWinRTMesaVulkanDzn = NULL;
static void *BanditWinRTMesaGallium = NULL;
typedef BOOL (WINAPI *BANDIT_PFNUWPSETPIXELFORMATPROC)(HDC, int, const PIXELFORMATDESCRIPTOR *);
static BANDIT_PFNUWPSETPIXELFORMATPROC BanditWinRTGalliumSetPixelFormat = NULL;

static void BANDIT_WIN_GL_UnloadMesaCompanions(void)
{
    BanditWinRTGalliumSetPixelFormat = NULL;
    if (BanditWinRTMesaGallium) { SDL_UnloadObject(BanditWinRTMesaGallium); BanditWinRTMesaGallium = NULL; }
    if (BanditWinRTMesaVulkanDzn) { SDL_UnloadObject(BanditWinRTMesaVulkanDzn); BanditWinRTMesaVulkanDzn = NULL; }
    if (BanditWinRTMesaSpirvToDxil) { SDL_UnloadObject(BanditWinRTMesaSpirvToDxil); BanditWinRTMesaSpirvToDxil = NULL; }
    if (BanditWinRTMesaZlib) { SDL_UnloadObject(BanditWinRTMesaZlib); BanditWinRTMesaZlib = NULL; }
    if (BanditWinRTMesaDxil) { SDL_UnloadObject(BanditWinRTMesaDxil); BanditWinRTMesaDxil = NULL; }
}

static bool BANDIT_WIN_GL_PreloadMesaCompanions(void)
{
    if (BanditWinRTMesaDxil && BanditWinRTMesaZlib &&
        BanditWinRTMesaSpirvToDxil && BanditWinRTMesaGallium &&
        BanditWinRTGalliumSetPixelFormat) {
        return true;
    }

    BanditWinRTMesaDxil = SDL_LoadObject("dxil.dll");
    if (!BanditWinRTMesaDxil) goto failed;
    BanditWinRTMesaZlib = SDL_LoadObject("z-1.dll");
    if (!BanditWinRTMesaZlib) goto failed;
    BanditWinRTMesaSpirvToDxil = SDL_LoadObject("spirv_to_dxil.dll");
    if (!BanditWinRTMesaSpirvToDxil) goto failed;
    BanditWinRTMesaVulkanDzn = SDL_LoadObject("vulkan_dzn.dll"); /* optional */
    if (!BanditWinRTMesaVulkanDzn) SDL_ClearError();
    BanditWinRTMesaGallium = SDL_LoadObject("libgallium_wgl.dll");
    if (!BanditWinRTMesaGallium) goto failed;

    BanditWinRTGalliumSetPixelFormat =
        (BANDIT_PFNUWPSETPIXELFORMATPROC)SDL_LoadFunction(BanditWinRTMesaGallium, "SetPixelFormat");
    if (!BanditWinRTGalliumSetPixelFormat) goto failed;

    SDL_Log("Bandit SDL V12: Mesa support stack resident; Gallium SetPixelFormat=%p",
            (void *)BanditWinRTGalliumSetPixelFormat);
    return true;

failed:
    SDL_LogError(SDL_LOG_CATEGORY_VIDEO, "Bandit SDL V12: Mesa companion preload/SetPixelFormat resolve failed: %s", SDL_GetError());
    BANDIT_WIN_GL_UnloadMesaCompanions();
    return false;
}

static int BANDIT_WIN_GL_ChoosePixelFormatProven(SDL_VideoDevice *_this, HDC hdc, const PIXELFORMATDESCRIPTOR *pfd)
{
#if defined(_MSC_VER)
    int result = 0;
    __try {
        result = _this->gl_data->wglChoosePixelFormat(hdc, pfd);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        BANDIT_WIN_GL_Breadcrumb("wglChoosePixelFormat:SEH");
        SDL_SetError("Bandit WinRT WGL: wglChoosePixelFormat raised SEH 0x%08lX", (unsigned long)GetExceptionCode());
    }
    return result;
#else
    return _this->gl_data->wglChoosePixelFormat(hdc, pfd);
#endif
}

static BOOL BANDIT_WIN_GL_SetPixelFormatProven(HDC hdc, int pixel_format, const PIXELFORMATDESCRIPTOR *pfd)
{
    if (!BanditWinRTGalliumSetPixelFormat) {
        SDL_SetError("Bandit WinRT WGL: Gallium SetPixelFormat is unavailable");
        return FALSE;
    }
#if defined(_MSC_VER)
    BOOL result = FALSE;
    __try {
        result = BanditWinRTGalliumSetPixelFormat(hdc, pixel_format, pfd);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        BANDIT_WIN_GL_Breadcrumb("GalliumSetPixelFormat:SEH");
        SDL_SetError("Bandit WinRT WGL: Gallium SetPixelFormat raised SEH 0x%08lX", (unsigned long)GetExceptionCode());
    }
    return result;
#else
    return BanditWinRTGalliumSetPixelFormat(hdc, pixel_format, pfd);
#endif
}
#endif

static PROC BANDIT_WIN_GL_GetProcAddressSafe(SDL_VideoDevice *_this, const char *proc)
'@
$gl = Replace-Once -Text $gl -Old $old -New $new -Label "proven Mesa support/pixel-format helpers"

$old = @'
bool WIN_GL_LoadLibrary(SDL_VideoDevice *_this, const char *path)
{
    void *handle;

    if (path == NULL) {
'@
$new = @'
bool WIN_GL_LoadLibrary(SDL_VideoDevice *_this, const char *path)
{
    void *handle;

#ifdef SDL_PLATFORM_WINRT
    if (!BANDIT_WIN_GL_PreloadMesaCompanions()) {
        return false;
    }
#endif

    if (path == NULL) {
'@
$gl = Replace-Once -Text $gl -Old $old -New $new -Label "Mesa companions before opengl32"

$old = @'
void WIN_GL_UnloadLibrary(SDL_VideoDevice *_this)
{
    SDL_UnloadObject(_this->gl_config.dll_handle);
    _this->gl_config.dll_handle = NULL;

    // Free OpenGL memory
    SDL_free(_this->gl_data);
    _this->gl_data = NULL;
}
'@
$new = @'
void WIN_GL_UnloadLibrary(SDL_VideoDevice *_this)
{
    SDL_UnloadObject(_this->gl_config.dll_handle);
    _this->gl_config.dll_handle = NULL;

    // Free OpenGL memory
    SDL_free(_this->gl_data);
    _this->gl_data = NULL;
#ifdef SDL_PLATFORM_WINRT
    BANDIT_WIN_GL_UnloadMesaCompanions();
#endif
}
'@
$gl = Replace-Once -Text $gl -Old $old -New $new -Label "Mesa companion lifetime"

# V7's WinRT SetupWindow path still used SDL's desktop format enumerator and
# opengl32!wglSetPixelFormat. The known-good MesaTestApp/GLFW binding does less:
# opengl32!wglChoosePixelFormat once, then libgallium_wgl!SetPixelFormat once.
# Mirror that exact contract so no framebuffer-affecting desktop WGL query/setter
# runs before the requested format has been installed.
$old = @'
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
'@
$new = @'
#ifdef SDL_PLATFORM_WINRT
    /* MesaTestApp-proven Series S path: use the exact known-good drawable PFD,
       choose through opengl32, then install the chosen index through Gallium's
       simple SetPixelFormat export exactly once. RenderPearl renders depth and
       stencil into its own FBOs, so giving the window drawable the proven
       32-bit color / 24-bit depth / 8-bit stencil format is safe and avoids
       another untested difference from the working Xbox path. */
    SDL_zero(pfd);
    pfd.nSize = sizeof(pfd);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.cDepthBits = 24;
    pfd.cStencilBits = 8;
    pfd.iLayerType = PFD_MAIN_PLANE;
    BANDIT_WIN_GL_Breadcrumb("wglChoosePixelFormat:begin");
    pixel_format = BANDIT_WIN_GL_ChoosePixelFormatProven(_this, hdc, &pfd);
    if (!pixel_format) {
        BANDIT_WIN_GL_Breadcrumb("wglChoosePixelFormat:fail");
        return SDL_SetError("Bandit WinRT WGL: wglChoosePixelFormat returned no format");
    }
    BANDIT_WIN_GL_Breadcrumb("wglChoosePixelFormat:ok");
    BANDIT_WIN_GL_Breadcrumb("GalliumSetPixelFormat:begin");
    if (!BANDIT_WIN_GL_SetPixelFormatProven(hdc, pixel_format, &pfd)) {
        if (!*SDL_GetError()) SDL_SetError("Bandit WinRT WGL: Gallium SetPixelFormat failed");
        BANDIT_WIN_GL_Breadcrumb("GalliumSetPixelFormat:fail");
        return false;
    }
    BANDIT_WIN_GL_Breadcrumb("GalliumSetPixelFormat:ok");
    SDL_Log("Bandit SDL V12: MesaTestApp pixel format installed index=%d hdc=%p", pixel_format, hdc);
    return true;
#endif
'@
$gl = Replace-Once -Text $gl -Old $old -New $new -Label "MesaTestApp exact WinRT pixel-format path"

# Strictly require the attributed 3.3 context on WinRT. SDL's normal Windows
# fallback keeps the temporary legacy context when wglCreateContextAttribsARB is
# absent; SDL_GL_GetAttribute(MAJOR/MINOR) merely echoes the requested values, so
# vanilla 26.3 would falsely accept that legacy context and later black-screen.
$old = @'
        if (!wglCreateContextAttribsARB) {
            SDL_SetError("GL 3.x is not supported");
            context = temp_context;
        } else {
'@
$new = @'
        if (!wglCreateContextAttribsARB) {
#ifdef SDL_PLATFORM_WINRT
            (void)BANDIT_WIN_GL_MakeCurrentSafe(_this, NULL, NULL);
            _this->gl_data->wglDeleteContext(temp_context);
            SDL_SetError("Bandit WinRT WGL: wglCreateContextAttribsARB unavailable; refusing legacy fallback for Minecraft 26.3");
            BANDIT_WIN_GL_Breadcrumb("GL_CreateContext:no-attribs");
            return NULL;
#else
            SDL_SetError("GL 3.x is not supported");
            context = temp_context;
#endif
        } else {
'@
$gl = Replace-Once -Text $gl -Old $old -New $new -Label "reject fake legacy 3.x fallback"

$old = @'
            // Create the GL 3.x context
            context = BANDIT_WIN_GL_CreateContextAttribsSafe(wglCreateContextAttribsARB, hdc, share_context, attribs);
            // Delete the GL 2.x context
            _this->gl_data->wglDeleteContext(temp_context);
'@
$new = @'
            // Create the GL 3.x context
            context = BANDIT_WIN_GL_CreateContextAttribsSafe(wglCreateContextAttribsARB, hdc, share_context, attribs);
#ifdef SDL_PLATFORM_WINRT
            /* MesaTestApp's proven UWP sequence unbinds the base context before
               deleting it. Do not rely on desktop-WGL delete-current behavior. */
            if (!BANDIT_WIN_GL_MakeCurrentSafe(_this, NULL, NULL)) {
                if (context) _this->gl_data->wglDeleteContext(context);
                _this->gl_data->wglDeleteContext(temp_context);
                if (!*SDL_GetError()) SDL_SetError("Bandit WinRT WGL: failed to unbind temporary context");
                BANDIT_WIN_GL_Breadcrumb("GL_CreateContext:temp-unbind-fail");
                return NULL;
            }
#endif
            // Delete the GL 2.x context
            _this->gl_data->wglDeleteContext(temp_context);
'@
$gl = Replace-Once -Text $gl -Old $old -New $new -Label "proven temporary-context teardown order"

# Query the REAL active context, not SDL's requested gl_config fields.
$old = @'
#ifdef SDL_PLATFORM_WINRT
    BANDIT_WIN_GL_InitExtensionsFromCurrentContext(_this, hdc);
    BANDIT_WIN_GL_Breadcrumb("GL_CreateContext:ready");
    SDL_Log("Bandit SDL: WinRT WGL OpenGL %d.%d context ready context=%p",
            _this->gl_config.major_version, _this->gl_config.minor_version, context);
#endif
'@
$new = @'
#ifdef SDL_PLATFORM_WINRT
    {
        typedef const GLubyte *(APIENTRYP BANDIT_PFNGLGETSTRINGPROC)(GLenum);
        typedef void (APIENTRYP BANDIT_PFNGLGETINTEGERVPROC)(GLenum, GLint *);
        BANDIT_PFNGLGETSTRINGPROC banditGetString =
            (BANDIT_PFNGLGETSTRINGPROC)WIN_GL_GetProcAddress(_this, "glGetString");
        BANDIT_PFNGLGETINTEGERVPROC banditGetIntegerv =
            (BANDIT_PFNGLGETINTEGERVPROC)WIN_GL_GetProcAddress(_this, "glGetIntegerv");
        if (!banditGetString || !banditGetIntegerv) {
            (void)BANDIT_WIN_GL_MakeCurrentSafe(_this, NULL, NULL);
            _this->gl_data->wglDeleteContext(context);
            SDL_SetError("Bandit WinRT WGL: cannot query actual OpenGL context version");
            BANDIT_WIN_GL_Breadcrumb("GL_ActualContext:query-missing");
            return NULL;
        }

        const char *actualVersion = (const char *)banditGetString(0x1F02 /* GL_VERSION */);
        const char *actualVendor = (const char *)banditGetString(0x1F00 /* GL_VENDOR */);
        const char *actualRenderer = (const char *)banditGetString(0x1F01 /* GL_RENDERER */);
        const char *actualGlsl = (const char *)banditGetString(0x8B8C /* GL_SHADING_LANGUAGE_VERSION */);
        GLint actualMajor = 0, actualMinor = 0, actualProfile = 0, actualFlags = 0;
        banditGetIntegerv(0x821B /* GL_MAJOR_VERSION */, &actualMajor);
        banditGetIntegerv(0x821C /* GL_MINOR_VERSION */, &actualMinor);
        if (actualMajor >= 3) {
            banditGetIntegerv(0x9126 /* GL_CONTEXT_PROFILE_MASK */, &actualProfile);
            banditGetIntegerv(0x821E /* GL_CONTEXT_FLAGS */, &actualFlags);
        }
        if (actualMajor <= 0 && actualVersion) {
            (void)SDL_sscanf(actualVersion, "%d.%d", &actualMajor, &actualMinor);
        }

        char actualMsg[768];
        SDL_snprintf(actualMsg, sizeof(actualMsg),
                     "GL_ActualContext:%d.%d profile=0x%X flags=0x%X version=%s renderer=%s",
                     (int)actualMajor, (int)actualMinor, (unsigned)actualProfile, (unsigned)actualFlags,
                     actualVersion ? actualVersion : "<null>", actualRenderer ? actualRenderer : "<null>");
        BANDIT_WIN_GL_Breadcrumb(actualMsg);
        SDL_Log("Bandit SDL V12: actual OpenGL=%d.%d profile=0x%X flags=0x%X version='%s' vendor='%s' renderer='%s' GLSL='%s'",
                (int)actualMajor, (int)actualMinor, (unsigned)actualProfile, (unsigned)actualFlags,
                actualVersion ? actualVersion : "<null>", actualVendor ? actualVendor : "<null>",
                actualRenderer ? actualRenderer : "<null>", actualGlsl ? actualGlsl : "<null>");

        if (actualMajor < _this->gl_config.major_version ||
            (actualMajor == _this->gl_config.major_version && actualMinor < _this->gl_config.minor_version)) {
            (void)BANDIT_WIN_GL_MakeCurrentSafe(_this, NULL, NULL);
            _this->gl_data->wglDeleteContext(context);
            SDL_SetError("Bandit WinRT WGL: actual OpenGL %d.%d is below requested %d.%d",
                         (int)actualMajor, (int)actualMinor,
                         _this->gl_config.major_version, _this->gl_config.minor_version);
            BANDIT_WIN_GL_Breadcrumb("GL_ActualContext:version-rejected");
            return NULL;
        }
        if ((_this->gl_config.profile_mask & SDL_GL_CONTEXT_PROFILE_CORE) &&
            !(actualProfile & 0x00000001 /* GL_CONTEXT_CORE_PROFILE_BIT */)) {
            (void)BANDIT_WIN_GL_MakeCurrentSafe(_this, NULL, NULL);
            _this->gl_data->wglDeleteContext(context);
            SDL_SetError("Bandit WinRT WGL: requested core profile but actual profile mask is 0x%X", (unsigned)actualProfile);
            BANDIT_WIN_GL_Breadcrumb("GL_ActualContext:profile-rejected");
            return NULL;
        }
    }

    BANDIT_WIN_GL_InitExtensionsFromCurrentContext(_this, hdc);
    BANDIT_WIN_GL_Breadcrumb("GL_CreateContext:ready-v12");
#endif
'@
$gl = Replace-Once -Text $gl -Old $old -New $new -Label "actual GL context validation"

# MesaTestApp unbinds with wglMakeCurrent(NULL, NULL). The desktop SDL helper
# normally keeps the old HDC when context==NULL; mirror the proven UWP contract
# so context teardown and rebinds never depend on desktop-WGL tolerance.
$old = @'
    /* Some Windows drivers freak out if hdc is NULL, even when context is
       NULL, against spec. Since hdc is _supposed_ to be ignored if context
       is NULL, we either use the current GL window, or do nothing if we
       already have no current context. */
    if (!window) {
        window = SDL_GL_GetCurrentWindow();
        if (!window) {
            SDL_assert(SDL_GL_GetCurrentContext() == NULL);
            return true; // already done.
        }
    }

    hdc = window->internal->hdc;
'@
$new = @'
#ifdef SDL_PLATFORM_WINRT
    if (!context) {
        /* WIN_GL_SetupWindow restores a previously-current context after setting
           the pixel format. Before the very first context exists there is
           nothing to unbind, so avoid calling into Mesa with NULL/NULL just for
           that bookkeeping restore. Once SDL has a real current context,
           teardown follows MesaTestApp's proven NULL/NULL unbind contract. */
        if (!SDL_GL_GetCurrentContext()) {
            return true;
        }
        if (!BANDIT_WIN_GL_MakeCurrentSafe(_this, NULL, NULL)) {
            if (!*SDL_GetError()) return WIN_SetError("wglMakeCurrent(NULL,NULL)");
            return false;
        }
        BANDIT_WIN_GL_Breadcrumb("wglMakeCurrent:clear-null-null:ok");
        return true;
    }
#endif

    /* Some Windows drivers freak out if hdc is NULL, even when context is
       NULL, against spec. Since hdc is _supposed to be ignored if context is
       NULL, desktop builds either use the current GL window or do nothing. */
    if (!window) {
        window = SDL_GL_GetCurrentWindow();
        if (!window) {
            SDL_assert(SDL_GL_GetCurrentContext() == NULL);
            return true; // already done.
        }
    }

    hdc = window->internal->hdc;
'@
$gl = Replace-Once -Text $gl -Old $old -New $new -Label "proven WinRT null/null context unbind"

# First-swap diagnostics survive even if Java never writes a crash report.
$old = @'
static BOOL BANDIT_WIN_GL_SwapBuffersSafe(SDL_VideoDevice *_this, HDC hdc)
{
    BOOL result = FALSE;
    static volatile LONG loggedFirst = 0;
    const BOOL first = InterlockedCompareExchange(&loggedFirst, 1, 0) == 0;
    if (first) BANDIT_WIN_GL_Breadcrumb("wglSwapBuffers:begin");
'@
$new = @'
static BOOL BANDIT_WIN_GL_SwapBuffersSafe(SDL_VideoDevice *_this, HDC hdc)
{
    BOOL result = FALSE;
    static volatile LONG loggedFirst = 0;
    static volatile LONG presented = 0;
    const BOOL first = InterlockedCompareExchange(&loggedFirst, 1, 0) == 0;
    if (first) BANDIT_WIN_GL_Breadcrumb("wglSwapBuffers:begin");
'@
$gl = Replace-Once -Text $gl -Old $old -New $new -Label "present counter storage"

$old = @'
    if (first || !result) BANDIT_WIN_GL_Breadcrumb(result ? "wglSwapBuffers:ok" : "wglSwapBuffers:fail");
    return result;
}
'@
$new = @'
    if (result) {
        const LONG frame = InterlockedIncrement(&presented);
        if (frame == 1) BANDIT_WIN_GL_Breadcrumb("wglSwapBuffers:first-present-ok");
        if (frame == 60) BANDIT_WIN_GL_Breadcrumb("wglSwapBuffers:60-present-ok");
    }
    if (first || !result) BANDIT_WIN_GL_Breadcrumb(result ? "wglSwapBuffers:ok" : "wglSwapBuffers:fail");
    return result;
}
'@
$gl = Replace-Once -Text $gl -Old $old -New $new -Label "present success breadcrumbs"

# Validation: this patch is intentionally incompatible with the V10 context
# virtualization. The preparer must apply V7+V8+V9+V12, never V10+V12.
foreach ($required in @(
    "BanditSDL_WinRTRenderPearlV12",
    "BanditWinRTPixelFormatConfigured",
    "synthetic Xbox SetWindowPosition acknowledged",
    "BANDIT_WIN_GL_PreloadMesaCompanions",
    "BanditWinRTGalliumSetPixelFormat",
    "BANDIT_WIN_GL_ChoosePixelFormatProven",
    "GalliumSetPixelFormat:ok",
    "GL_ActualContext:",
    "GL_CreateContext:ready-v12",
    "wglMakeCurrent:clear-null-null:ok",
    "wglSwapBuffers:first-present-ok"
)) {
    if (-not ($video.Contains($required) -or $gl.Contains($required))) {
        throw "V12 SDL validation failed: missing $required"
    }
}
if ($video.Contains("const int banditPixelFormat = WIN_GL_GetWindowPixelFormat")) {
    throw "V12 SDL validation failed: side-effecting pre-SetPixelFormat query remains"
}
if ($gl.Contains("BanditWinRTPhysicalGLContext") -or $gl.Contains("GL_CreateContext:reuse-physical")) {
    throw "V12 SDL validation failed: V10 context virtualization leaked into the active source"
}

Write-NormalizedText -Path $videoPath -Text $video
Write-NormalizedText -Path $glPath -Text $gl
Write-Host "Applied SDL V12 RenderPearl/Xbox contract: deterministic Gallium pixel format, actual GL 3.3 validation, synthetic window ops, one real context."
