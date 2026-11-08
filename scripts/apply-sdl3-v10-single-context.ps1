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
    if ($first -lt 0) { throw "V10 SDL patch anchor not found: $Label" }
    $second = $Text.IndexOf($Old, $first + $Old.Length, [System.StringComparison]::Ordinal)
    if ($second -ge 0) { throw "V10 SDL patch anchor is not unique: $Label" }
    return $Text.Substring(0, $first) + $New + $Text.Substring($first + $Old.Length)
}

$videoPath = Join-Path $SourceRoot "src\video\winrt\SDL_winrtvideo.cpp"
$glPath = Join-Path $SourceRoot "src\video\windows\SDL_windowsopengl.c"
$coreVideoPath = Join-Path $SourceRoot "src\video\SDL_video.c"
if (-not (Test-Path -LiteralPath $videoPath)) { throw "Missing SDL WinRT video source: $videoPath" }
if (-not (Test-Path -LiteralPath $glPath)) { throw "Missing SDL Windows OpenGL source: $glPath" }
if (-not (Test-Path -LiteralPath $coreVideoPath)) { throw "Missing SDL core video source: $coreVideoPath" }

$video = Read-NormalizedText -Path $videoPath
$gl = Read-NormalizedText -Path $glPath
$coreVideo = Read-NormalizedText -Path $coreVideoPath

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

/* V10 matches Xbox/UWP's real graphics topology: one physical CoreWindow,
   one physical Mesa WGL context, many logical SDL windows/contexts. */
extern "C" __declspec(dllexport) int BanditSDL_WinRTSinglePhysicalGLV10(void)
{
    return 10;
}
'@
$video = Replace-Once -Text $video -Old $old -New $new -Label "V10 marker"

$old = @'
static std::vector<SDL_Window *> BanditWinRTLogicalWindows;
'@
$new = @'
static std::vector<SDL_Window *> BanditWinRTLogicalWindows;
#if SDL_VIDEO_OPENGL_WGL
static HDC BanditWinRTPhysicalHdc = NULL;
#endif
'@
$video = Replace-Once -Text $video -Old $old -New $new -Label "physical HDC storage"

$old = @'
    data->hdc = (HDC)data->coreWindow.Get();
    if (window->flags & SDL_WINDOW_OPENGL) {
'@
$new = @'
    const HDC discoveredHdc = (HDC)data->coreWindow.Get();
    if (!BanditWinRTPhysicalHdc) {
        BanditWinRTPhysicalHdc = discoveredHdc;
        SDL_Log("Bandit SDL V10: captured physical WinRT HDC token=%p", BanditWinRTPhysicalHdc);
    } else if (BanditWinRTPhysicalHdc != discoveredHdc) {
        SDL_LogWarn(SDL_LOG_CATEGORY_VIDEO,
                    "Bandit SDL V10: logical window exposed a different CoreWindow token=%p; reusing physical token=%p",
                    discoveredHdc, BanditWinRTPhysicalHdc);
    }
    data->hdc = BanditWinRTPhysicalHdc;
    if (window->flags & SDL_WINDOW_OPENGL) {
'@
$video = Replace-Once -Text $video -Old $old -New $new -Label "pin logical windows to one HDC"

$old = @'
    BanditWinRTLogicalWindows.clear();
    WINRT_GlobalSDLWindow = NULL;
    SDL_Log("Bandit SDL: WinRT video quit cleared logical window targets");
'@
$new = @'
    BanditWinRTLogicalWindows.clear();
    WINRT_GlobalSDLWindow = NULL;
#if SDL_VIDEO_OPENGL_WGL
    BanditWinRTPhysicalHdc = NULL;
#endif
    SDL_Log("Bandit SDL: WinRT video quit cleared logical window targets");
'@
$video = Replace-Once -Text $video -Old $old -New $new -Label "clear physical HDC on video quit"

$old = @'
static PROC BANDIT_WIN_GL_GetProcAddressSafe(SDL_VideoDevice *_this, const char *proc)
'@
$new = @'
#ifdef SDL_PLATFORM_WINRT
typedef struct BanditWinRTLogicalGLContext
{
    HGLRC physical;
    LONG id;
    struct BanditWinRTLogicalGLContext *next;
} BanditWinRTLogicalGLContext;

static HGLRC BanditWinRTPhysicalGLContext = NULL;
static int BanditWinRTContextMajor, BanditWinRTContextMinor;
static int BanditWinRTContextProfile, BanditWinRTContextFlags;
static HDC BanditWinRTPhysicalGLHdc = NULL;
static BanditWinRTLogicalGLContext *BanditWinRTLogicalGLContexts = NULL;
static volatile LONG BanditWinRTNextLogicalContextId = 0;
static volatile LONG BanditWinRTPresentedFrames = 0;

static SDL_GLContext BANDIT_WIN_GL_WrapPhysicalContext(HGLRC physical)
{
    BanditWinRTLogicalGLContext *logical =
        (BanditWinRTLogicalGLContext *)SDL_calloc(1, sizeof(*logical));
    if (!logical) {
        SDL_OutOfMemory();
        return NULL;
    }
    logical->physical = physical;
    logical->id = InterlockedIncrement(&BanditWinRTNextLogicalContextId);
    logical->next = BanditWinRTLogicalGLContexts;
    BanditWinRTLogicalGLContexts = logical;
    return (SDL_GLContext)logical;
}

static HGLRC BANDIT_WIN_GL_UnwrapContext(SDL_GLContext context)
{
    BanditWinRTLogicalGLContext *it = BanditWinRTLogicalGLContexts;
    while (it) {
        if ((SDL_GLContext)it == context) return it->physical;
        it = it->next;
    }
    return (HGLRC)context; /* internal temporary/native context */
}

static bool BANDIT_WIN_GL_FreeLogicalContext(SDL_GLContext context)
{
    BanditWinRTLogicalGLContext **link = &BanditWinRTLogicalGLContexts;
    while (*link) {
        if ((SDL_GLContext)(*link) == context) {
            BanditWinRTLogicalGLContext *dead = *link;
            *link = dead->next;
            SDL_Log("Bandit SDL V10: destroyed logical GL context id=%ld physical=%p",
                    dead->id, dead->physical);
            SDL_free(dead);
            return true;
        }
        link = &(*link)->next;
    }
    return false;
}

static void BANDIT_WIN_GL_FreeAllLogicalContexts(void)
{
    while (BanditWinRTLogicalGLContexts) {
        BanditWinRTLogicalGLContext *dead = BanditWinRTLogicalGLContexts;
        BanditWinRTLogicalGLContexts = dead->next;
        SDL_free(dead);
    }
}
#endif

static PROC BANDIT_WIN_GL_GetProcAddressSafe(SDL_VideoDevice *_this, const char *proc)
'@
$gl = Replace-Once -Text $gl -Old $old -New $new -Label "physical GL singleton state"

$old = @'
static BOOL BANDIT_WIN_GL_SwapBuffersSafe(SDL_VideoDevice *_this, HDC hdc)
{
    BOOL result = FALSE;
    static volatile LONG loggedFirst = 0;
    const BOOL first = InterlockedCompareExchange(&loggedFirst, 1, 0) == 0;
    if (first) BANDIT_WIN_GL_Breadcrumb("wglSwapBuffers:begin");
    __try {
        result = _this->gl_data->wglSwapBuffers(hdc);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        BANDIT_WIN_GL_Breadcrumb("wglSwapBuffers:SEH");
        SDL_SetError("Bandit WinRT WGL: wglSwapBuffers raised SEH 0x%08lX", (unsigned long)GetExceptionCode());
    }
    if (first || !result) BANDIT_WIN_GL_Breadcrumb(result ? "wglSwapBuffers:ok" : "wglSwapBuffers:fail");
    return result;
}
'@
$new = @'
static BOOL BANDIT_WIN_GL_SwapBuffersSafe(SDL_VideoDevice *_this, HDC hdc)
{
    BOOL result = FALSE;
    static volatile LONG loggedFirst = 0;
    const BOOL first = InterlockedCompareExchange(&loggedFirst, 1, 0) == 0;
    if (first) BANDIT_WIN_GL_Breadcrumb("wglSwapBuffers:begin");
    __try {
        result = _this->gl_data->wglSwapBuffers(hdc);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        BANDIT_WIN_GL_Breadcrumb("wglSwapBuffers:SEH");
        SDL_SetError("Bandit WinRT WGL: wglSwapBuffers raised SEH 0x%08lX", (unsigned long)GetExceptionCode());
    }
    if (result) {
#ifdef SDL_PLATFORM_WINRT
        const LONG frame = InterlockedIncrement(&BanditWinRTPresentedFrames);
        if (frame == 1) BANDIT_WIN_GL_Breadcrumb("wglSwapBuffers:first-present-ok");
        if (frame == 60) BANDIT_WIN_GL_Breadcrumb("wglSwapBuffers:60-present-ok");
#endif
    }
    if (first || !result) BANDIT_WIN_GL_Breadcrumb(result ? "wglSwapBuffers:ok" : "wglSwapBuffers:fail");
    return result;
}

static BOOL BANDIT_WIN_GL_DeleteContextSafe(SDL_VideoDevice *_this, HGLRC context)
{
    BOOL result = TRUE;
    if (!context) return TRUE;
    __try {
        result = _this->gl_data->wglDeleteContext(context);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        BANDIT_WIN_GL_Breadcrumb("wglDeleteContext:SEH");
        SDL_SetError("Bandit WinRT WGL: wglDeleteContext raised SEH 0x%08lX", (unsigned long)GetExceptionCode());
        result = FALSE;
    }
    return result;
}
'@
$gl = Replace-Once -Text $gl -Old $old -New $new -Label "present counter and safe delete"

$old = @'
#define BANDIT_WIN_GL_SwapBuffersSafe(_this, hdc) ((_this)->gl_data->wglSwapBuffers(hdc))
#endif
'@
$new = @'
#define BANDIT_WIN_GL_SwapBuffersSafe(_this, hdc) ((_this)->gl_data->wglSwapBuffers(hdc))
#define BANDIT_WIN_GL_DeleteContextSafe(_this, context) ((_this)->gl_data->wglDeleteContext(context))
#endif
'@
$gl = Replace-Once -Text $gl -Old $old -New $new -Label "non-MSVC delete helper"

$old = @'
void WIN_GL_UnloadLibrary(SDL_VideoDevice *_this)
{
    SDL_UnloadObject(_this->gl_config.dll_handle);
'@
$new = @'
void WIN_GL_UnloadLibrary(SDL_VideoDevice *_this)
{
#ifdef SDL_PLATFORM_WINRT
    if (BanditWinRTPhysicalGLContext && _this->gl_data) {
        BANDIT_WIN_GL_Breadcrumb("GL_PhysicalContext:destroy:begin");
        (void)BANDIT_WIN_GL_MakeCurrentSafe(_this, BanditWinRTPhysicalGLHdc, NULL);
        (void)BANDIT_WIN_GL_DeleteContextSafe(_this, BanditWinRTPhysicalGLContext);
        BanditWinRTPhysicalGLContext = NULL;
        BanditWinRTPhysicalGLHdc = NULL;
        BANDIT_WIN_GL_FreeAllLogicalContexts();
        BanditWinRTNextLogicalContextId = 0;
        BanditWinRTPresentedFrames = 0;
        BANDIT_WIN_GL_Breadcrumb("GL_PhysicalContext:destroy:ok");
    }
#endif
    SDL_UnloadObject(_this->gl_config.dll_handle);
'@
$gl = Replace-Once -Text $gl -Old $old -New $new -Label "physical context unload"

$old = @'
SDL_GLContext WIN_GL_CreateContext(SDL_VideoDevice *_this, SDL_Window *window)
{
    HDC hdc = window->internal->hdc;
    HGLRC context, share_context;
'@
$new = @'
SDL_GLContext WIN_GL_CreateContext(SDL_VideoDevice *_this, SDL_Window *window)
{
    HDC hdc = window->internal->hdc;
    HGLRC context, share_context;

#ifdef SDL_PLATFORM_WINRT
    /* Xbox UWP has one CoreWindow/swapchain. Keep one real Mesa HGLRC alive
       and virtualize RenderPearl's utility/test/game SDL contexts over it. */
    if (BanditWinRTPhysicalGLContext) {
        /* Fail clearly rather than silently satisfy a different context contract.
           These are the attributes used to create the retained context, not a
           claim about all features supported by the driver. */
        if (_this->gl_config.major_version != BanditWinRTContextMajor ||
            _this->gl_config.minor_version != BanditWinRTContextMinor ||
            _this->gl_config.profile_mask != BanditWinRTContextProfile ||
            _this->gl_config.flags != BanditWinRTContextFlags) {
            SDL_SetError("Bandit WinRT: retained GL context was created for %d.%d profile=%d flags=%d; requested %d.%d profile=%d flags=%d",
                BanditWinRTContextMajor, BanditWinRTContextMinor, BanditWinRTContextProfile, BanditWinRTContextFlags,
                _this->gl_config.major_version, _this->gl_config.minor_version, _this->gl_config.profile_mask, _this->gl_config.flags);
            BANDIT_WIN_GL_Breadcrumb("GL_CreateContext:incompatible-reuse-rejected");
            return NULL;
        }
        if (BanditWinRTPhysicalGLHdc && hdc != BanditWinRTPhysicalGLHdc) {
            SDL_LogWarn(SDL_LOG_CATEGORY_VIDEO,
                        "Bandit SDL V10: logical GL context requested HDC=%p; pinning physical HDC=%p",
                        hdc, BanditWinRTPhysicalGLHdc);
            hdc = BanditWinRTPhysicalGLHdc;
            window->internal->hdc = hdc;
        }
        BANDIT_WIN_GL_Breadcrumb("GL_CreateContext:reuse-physical");
        if (!BANDIT_WIN_GL_MakeCurrentSafe(_this, hdc, BanditWinRTPhysicalGLContext)) {
            if (!*SDL_GetError()) WIN_SetError("wglMakeCurrent(reused physical context)");
            return NULL;
        }
        SDL_GLContext logical = BANDIT_WIN_GL_WrapPhysicalContext(BanditWinRTPhysicalGLContext);
        if (!logical) return NULL;
        SDL_Log("Bandit SDL V10: reused physical Mesa context=%p logical=%p",
                BanditWinRTPhysicalGLContext, logical);
        return logical;
    }
#endif
'@
$gl = Replace-Once -Text $gl -Old $old -New $new -Label "reuse one physical context"

$old = @'
    BANDIT_WIN_GL_InitExtensionsFromCurrentContext(_this, hdc);
    BANDIT_WIN_GL_Breadcrumb("GL_CreateContext:ready");
    SDL_Log("Bandit SDL: WinRT WGL OpenGL %d.%d context ready context=%p",
'@
$new = @'
    BANDIT_WIN_GL_InitExtensionsFromCurrentContext(_this, hdc);
#ifdef SDL_PLATFORM_WINRT
    if (!BanditWinRTPhysicalGLContext) {
        BanditWinRTPhysicalGLContext = context;
        BanditWinRTContextMajor = _this->gl_config.major_version;
        BanditWinRTContextMinor = _this->gl_config.minor_version;
        BanditWinRTContextProfile = _this->gl_config.profile_mask;
        BanditWinRTContextFlags = _this->gl_config.flags;
        SDL_LogWarn(SDL_LOG_CATEGORY_VIDEO, "Bandit V10 experimental: logical contexts share GL state; independent state isolation is not implemented");
        BanditWinRTPhysicalGLHdc = hdc;
        InterlockedExchange(&BanditWinRTPresentedFrames, 0);
        BANDIT_WIN_GL_Breadcrumb("GL_PhysicalContext:captured");
        SDL_Log("Bandit SDL V10: captured physical Mesa context=%p hdc=%p",
                BanditWinRTPhysicalGLContext, BanditWinRTPhysicalGLHdc);
    }
#endif
    BANDIT_WIN_GL_Breadcrumb("GL_CreateContext:ready");
    SDL_Log("Bandit SDL: WinRT WGL OpenGL %d.%d context ready context=%p",
'@
$gl = Replace-Once -Text $gl -Old $old -New $new -Label "capture physical context"

$old = @'
    _this->gl_config.HAS_GL_ARB_color_buffer_float =
        SDL_GL_ExtensionSupported("GL_ARB_color_buffer_float");

    return (SDL_GLContext)context;
}
'@
$new = @'
    _this->gl_config.HAS_GL_ARB_color_buffer_float =
        SDL_GL_ExtensionSupported("GL_ARB_color_buffer_float");

#ifdef SDL_PLATFORM_WINRT
    SDL_GLContext logical = BANDIT_WIN_GL_WrapPhysicalContext(context);
    if (!logical) {
        return NULL;
    }
    SDL_Log("Bandit SDL V10: created first logical GL context=%p physical=%p",
            logical, context);
    return logical;
#else
    return (SDL_GLContext)context;
#endif
}
'@
$gl = Replace-Once -Text $gl -Old $old -New $new -Label "return unique logical context wrapper"

$old = @'
    hdc = window->internal->hdc;
    if (!BANDIT_WIN_GL_MakeCurrentSafe(_this, hdc, (HGLRC)context)) {
        if (!*SDL_GetError()) {
            return WIN_SetError("wglMakeCurrent()");
        }
        return false;
    }
    return true;
}
'@
$new = @'
    hdc = window->internal->hdc;
#ifdef SDL_PLATFORM_WINRT
    HGLRC native_context = BANDIT_WIN_GL_UnwrapContext(context);
#else
    HGLRC native_context = (HGLRC)context;
#endif
    if (!BANDIT_WIN_GL_MakeCurrentSafe(_this, hdc, native_context)) {
        if (!*SDL_GetError()) {
            return WIN_SetError("wglMakeCurrent()");
        }
        return false;
    }
    return true;
}
'@
$gl = Replace-Once -Text $gl -Old $old -New $new -Label "unwrap logical context for make-current"

$old = @'
bool WIN_GL_DestroyContext(SDL_VideoDevice *_this, SDL_GLContext context)
{
    if (!_this->gl_data) {
        return true;
    }
    _this->gl_data->wglDeleteContext((HGLRC)context);
    return true;
}
'@
$new = @'
bool WIN_GL_DestroyContext(SDL_VideoDevice *_this, SDL_GLContext context)
{
    if (!_this->gl_data) {
        return true;
    }
#ifdef SDL_PLATFORM_WINRT
    if (BANDIT_WIN_GL_FreeLogicalContext(context)) {
        BANDIT_WIN_GL_Breadcrumb("GL_DestroyContext:logical-only");
        return true;
    }
#endif
    return BANDIT_WIN_GL_DeleteContextSafe(_this, (HGLRC)context) ? true : false;
}
'@
$gl = Replace-Once -Text $gl -Old $old -New $new -Label "logical context destroy"

$old = @'
    if (_this->gl_config.driver_loaded > 0) {
        if (--_this->gl_config.driver_loaded > 0) {
            return;
        }
        if (_this->GL_UnloadLibrary) {
            _this->GL_UnloadLibrary(_this);
        }
    }
'@
$new = @'
    if (_this->gl_config.driver_loaded > 0) {
#ifdef SDL_PLATFORM_WINRT
        /* Hold one GL-library reference across ordinary logical-window
           destruction. RenderPearl creates and destroys utility/test windows
           before the final game window; physically unloading Mesa here would
           force another HGLRC/swapchain creation. SDL_VideoQuit performs the
           real platform unload after all windows are gone. */
        if (_this->gl_config.driver_loaded == 1 && !_this->is_quitting) {
            return;
        }
#endif
        if (--_this->gl_config.driver_loaded > 0) {
            return;
        }
        if (_this->GL_UnloadLibrary) {
            _this->GL_UnloadLibrary(_this);
        }
    }
'@
$coreVideo = Replace-Once -Text $coreVideo -Old $old -New $new -Label "retain Mesa across logical window churn"

foreach ($required in @(
    "BanditSDL_WinRTSinglePhysicalGLV10",
    "BanditWinRTPhysicalHdc",
    "BanditWinRTPhysicalGLContext",
    "BanditWinRTLogicalGLContext",
    "GL_CreateContext:reuse-physical",
    "GL_PhysicalContext:captured",
    "wglSwapBuffers:first-present-ok",
    "GL_DestroyContext:logical-only"
)) {
    if (-not ($video.Contains($required) -or $gl.Contains($required))) {
        throw "V10 SDL validation failed: missing $required"
    }
}

if (-not $coreVideo.Contains("Hold one GL-library reference across ordinary logical-window")) {
    throw "V10 SDL validation failed: Mesa residency hold is missing"
}

Write-NormalizedText -Path $videoPath -Text $video
Write-NormalizedText -Path $glPath -Text $gl
Write-NormalizedText -Path $coreVideoPath -Text $coreVideo
Write-Host "Applied SDL V10 Xbox single-physical-GL pass: stable HDC token, one Mesa HGLRC, logical context virtualization, present counters."
