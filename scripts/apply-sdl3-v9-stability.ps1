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
    if ($first -lt 0) { throw "V9 SDL patch anchor not found: $Label" }
    $second = $Text.IndexOf($Old, $first + $Old.Length, [System.StringComparison]::Ordinal)
    if ($second -ge 0) { throw "V9 SDL patch anchor is not unique: $Label" }
    return $Text.Substring(0, $first) + $New + $Text.Substring($first + $Old.Length)
}

$videoPath = Join-Path $SourceRoot "src\video\winrt\SDL_winrtvideo.cpp"
$glPath = Join-Path $SourceRoot "src\video\windows\SDL_windowsopengl.c"
if (-not (Test-Path -LiteralPath $videoPath)) { throw "Missing SDL WinRT video source: $videoPath" }
if (-not (Test-Path -LiteralPath $glPath)) { throw "Missing SDL Windows OpenGL source: $glPath" }

$video = Read-NormalizedText -Path $videoPath
$gl = Read-NormalizedText -Path $glPath

$old = @'
extern "C" __declspec(dllexport) int BanditSDL_WinRTProviderParityV2(void)
{
    return 2;
}
'@
$new = @'
extern "C" __declspec(dllexport) int BanditSDL_WinRTProviderParityV2(void)
{
    return 2;
}

/* V9 hardens the first real Mesa/WGL handoff. It adds crash-resistant proc
   resolution, persistent stage breadcrumbs and a deliberately minimal WGL
   extension setup until Minecraft has produced its first frame. */
extern "C" __declspec(dllexport) int BanditSDL_WinRTStabilityV9(void)
{
    return 9;
}
'@
$video = Replace-Once -Text $video -Old $old -New $new -Label "V9 stability marker"

$old = @'
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
'@
$new = @'
static void BANDIT_WIN_GL_Breadcrumb(const char *stage)
{
    WCHAR logDir[768];
    DWORD chars = GetEnvironmentVariableW(L"MC_LOG_DIR", logDir, SDL_arraysize(logDir));
    if (chars == 0 || chars >= SDL_arraysize(logDir)) {
        return;
    }
    if (SDL_wcslcat(logDir, L"\\sdl_wgl_stage.log", SDL_arraysize(logDir)) >= SDL_arraysize(logDir)) {
        return;
    }

    HANDLE f = CreateFile2(logDir, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           OPEN_ALWAYS, NULL);
    if (f == INVALID_HANDLE_VALUE) {
        return;
    }
    DWORD written = 0;
    WriteFile(f, stage, (DWORD)SDL_strlen(stage), &written, NULL);
    WriteFile(f, "\r\n", 2, &written, NULL);
    FlushFileBuffers(f);
    CloseHandle(f);
}

static PROC BANDIT_WIN_GL_GetProcAddressSafe(SDL_VideoDevice *_this, const char *proc)
{
    PROC result = NULL;
    __try {
        result = _this->gl_data->wglGetProcAddress(proc);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        char msg[192];
        SDL_snprintf(msg, sizeof(msg), "wglGetProcAddress SEH 0x%08lX for %s",
                     (unsigned long)GetExceptionCode(), proc ? proc : "<null>");
        BANDIT_WIN_GL_Breadcrumb(msg);
        SDL_LogError(SDL_LOG_CATEGORY_VIDEO,
                     "Bandit WinRT WGL: wglGetProcAddress(%s) raised SEH 0x%08lX; falling back when possible",
                     proc ? proc : "<null>", (unsigned long)GetExceptionCode());
    }
    return result;
}

static HGLRC BANDIT_WIN_GL_CreateContextSafe(SDL_VideoDevice *_this, HDC hdc)
{
    HGLRC result = NULL;
    BANDIT_WIN_GL_Breadcrumb("wglCreateContext:begin");
    __try {
        result = _this->gl_data->wglCreateContext(hdc);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        BANDIT_WIN_GL_Breadcrumb("wglCreateContext:SEH");
        SDL_SetError("Bandit WinRT WGL: wglCreateContext raised SEH 0x%08lX", (unsigned long)GetExceptionCode());
    }
    BANDIT_WIN_GL_Breadcrumb(result ? "wglCreateContext:ok" : "wglCreateContext:fail");
    return result;
}

static BOOL BANDIT_WIN_GL_MakeCurrentSafe(SDL_VideoDevice *_this, HDC hdc, HGLRC context)
{
    BOOL result = FALSE;
    static volatile LONG loggedFirst = 0;
    const BOOL first = InterlockedCompareExchange(&loggedFirst, 1, 0) == 0;
    if (first) BANDIT_WIN_GL_Breadcrumb(context ? "wglMakeCurrent:set:begin" : "wglMakeCurrent:clear:begin");
    __try {
        result = _this->gl_data->wglMakeCurrent(hdc, context);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        BANDIT_WIN_GL_Breadcrumb("wglMakeCurrent:SEH");
        SDL_SetError("Bandit WinRT WGL: wglMakeCurrent raised SEH 0x%08lX", (unsigned long)GetExceptionCode());
    }
    if (first || !result) BANDIT_WIN_GL_Breadcrumb(result ? "wglMakeCurrent:ok" : "wglMakeCurrent:fail");
    return result;
}

static HGLRC BANDIT_WIN_GL_CreateContextAttribsSafe(PFNWGLCREATECONTEXTATTRIBSARBPROC proc, HDC hdc, HGLRC share_context, const int *attribs)
{
    HGLRC result = NULL;
    BANDIT_WIN_GL_Breadcrumb("wglCreateContextAttribsARB:begin");
    __try {
        result = proc(hdc, share_context, attribs);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        BANDIT_WIN_GL_Breadcrumb("wglCreateContextAttribsARB:SEH");
        SDL_SetError("Bandit WinRT WGL: wglCreateContextAttribsARB raised SEH 0x%08lX", (unsigned long)GetExceptionCode());
    }
    BANDIT_WIN_GL_Breadcrumb(result ? "wglCreateContextAttribsARB:ok" : "wglCreateContextAttribsARB:fail");
    return result;
}

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
#else
#define BANDIT_WIN_GL_Breadcrumb(stage) ((void)0)
#define BANDIT_WIN_GL_GetProcAddressSafe(_this, proc) ((_this)->gl_data->wglGetProcAddress(proc))
#define BANDIT_WIN_GL_CreateContextSafe(_this, hdc) ((_this)->gl_data->wglCreateContext(hdc))
#define BANDIT_WIN_GL_MakeCurrentSafe(_this, hdc, context) ((_this)->gl_data->wglMakeCurrent((hdc), (context)))
#define BANDIT_WIN_GL_CreateContextAttribsSafe(proc, hdc, share_context, attribs) ((proc)((hdc), (share_context), (attribs)))
#define BANDIT_WIN_GL_SwapBuffersSafe(_this, hdc) ((_this)->gl_data->wglSwapBuffers(hdc))
#endif
'@
$gl = Replace-Once -Text $gl -Old $old -New $new -Label "persistent WGL breadcrumbs and guarded proc resolver"

$old = @'
    func = (SDL_FunctionPointer)_this->gl_data->wglGetProcAddress(proc);
    if (!func) {
        func = (SDL_FunctionPointer)GetProcAddress((HMODULE)_this->gl_config.dll_handle, proc);
    }
'@
$new = @'
    func = (SDL_FunctionPointer)BANDIT_WIN_GL_GetProcAddressSafe(_this, proc);
    if (!func) {
        func = (SDL_FunctionPointer)GetProcAddress((HMODULE)_this->gl_config.dll_handle, proc);
    }
'@
$gl = Replace-Once -Text $gl -Old $old -New $new -Label "guard WIN_GL_GetProcAddress"

$old = @'
        wglCreateContextAttribsARB =
            (PFNWGLCREATECONTEXTATTRIBSARBPROC)_this->gl_data->wglGetProcAddress("wglCreateContextAttribsARB");
'@
$new = @'
        wglCreateContextAttribsARB =
            (PFNWGLCREATECONTEXTATTRIBSARBPROC)BANDIT_WIN_GL_GetProcAddressSafe(_this, "wglCreateContextAttribsARB");
'@
$gl = Replace-Once -Text $gl -Old $old -New $new -Label "guard attributed-context proc lookup"

$old = @'
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
'@
$new = @'
static void BANDIT_WIN_GL_InitExtensionsFromCurrentContext(SDL_VideoDevice *_this, HDC hdc)
{
    (void)hdc;
    BANDIT_WIN_GL_Breadcrumb("wglExtensions:minimal:begin");

    /* Stability-first on WinRT: do not call wglGetExtensionsStringARB here.
       Mesa-UWP has already proven it can create a 3.x context; querying and
       then parsing the full WGL extension string before the first frame adds
       another driver call that can fault without being required for Minecraft.
       Keep all feature flags conservative and resolve only swap-control entry
       points directly. */
    _this->gl_data->HAS_WGL_ARB_pixel_format = false;
    _this->gl_data->wglChoosePixelFormatARB = NULL;
    _this->gl_data->wglGetPixelFormatAttribivARB = NULL;
    _this->gl_data->HAS_WGL_EXT_swap_control_tear = false;
    _this->gl_data->HAS_WGL_ARB_context_flush_control = false;
    _this->gl_data->HAS_WGL_ARB_create_context_robustness = false;
    _this->gl_data->HAS_WGL_ARB_create_context_no_error = false;
    _this->gl_data->HAS_WGL_ARB_framebuffer_sRGB = false;
    _this->gl_data->HAS_WGL_ARB_pixel_format_float = false;
    _this->gl_data->HAS_WGL_EXT_create_context_es2_profile = false;

    _this->gl_data->wglSwapIntervalEXT =
        (BOOL (WINAPI *)(int))BANDIT_WIN_GL_GetProcAddressSafe(_this, "wglSwapIntervalEXT");
    _this->gl_data->wglGetSwapIntervalEXT =
        (int (WINAPI *)(void))BANDIT_WIN_GL_GetProcAddressSafe(_this, "wglGetSwapIntervalEXT");

    BANDIT_WIN_GL_Breadcrumb("wglExtensions:minimal:ok");
    SDL_Log("Bandit SDL V9: minimal WGL extension init complete (swap_control=%d)",
            _this->gl_data->wglSwapIntervalEXT != NULL ? 1 : 0);
}
'@
$gl = Replace-Once -Text $gl -Old $old -New $new -Label "remove risky WGL extension-string probe"

$old = @'
        SDL_Log("Bandit SDL: WinRT WGL context create begin hdc=%p", hdc);
        HGLRC temp_context = BANDIT_WIN_GL_CreateContextSafe(_this, hdc);
'@
$new = @'
        SDL_Log("Bandit SDL: WinRT WGL context create begin hdc=%p", hdc);
        BANDIT_WIN_GL_Breadcrumb("GL_CreateContext:begin");
        HGLRC temp_context = BANDIT_WIN_GL_CreateContextSafe(_this, hdc);
'@
$gl = Replace-Once -Text $gl -Old $old -New $new -Label "context-stage breadcrumb"

$old = @'
    BANDIT_WIN_GL_InitExtensionsFromCurrentContext(_this, hdc);
    SDL_Log("Bandit SDL: WinRT WGL OpenGL %d.%d context ready context=%p",
'@
$new = @'
    BANDIT_WIN_GL_InitExtensionsFromCurrentContext(_this, hdc);
    BANDIT_WIN_GL_Breadcrumb("GL_CreateContext:ready");
    SDL_Log("Bandit SDL: WinRT WGL OpenGL %d.%d context ready context=%p",
'@
$gl = Replace-Once -Text $gl -Old $old -New $new -Label "context-ready breadcrumb"

foreach ($required in @(
    "BanditSDL_WinRTStabilityV9",
    "BANDIT_WIN_GL_GetProcAddressSafe",
    "sdl_wgl_stage.log",
    "wglExtensions:minimal:ok",
    "GL_CreateContext:ready"
)) {
    if (-not ($video.Contains($required) -or $gl.Contains($required))) {
        throw "V9 SDL validation failed: missing $required"
    }
}
$extensionInit = [regex]::Matches($gl, '(?ms)^static void BANDIT_WIN_GL_InitExtensionsFromCurrentContext\([^\n]*\)\n\{.*?^\}')
if ($extensionInit.Count -ne 1) { throw 'V9 SDL validation failed: expected one real-context extension initializer' }
if ($extensionInit[0].Value.Contains('wglGetExtensionsStringARB(hdc)')) {
    throw "V9 SDL validation failed: risky wglGetExtensionsStringARB call remains"
}

Write-NormalizedText -Path $videoPath -Text $video
Write-NormalizedText -Path $glPath -Text $gl
Write-Host "Applied SDL V9 stability pass: guarded proc resolution, persistent WGL breadcrumbs, minimal extension probing."
