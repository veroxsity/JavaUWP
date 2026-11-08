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
    if ($first -lt 0) { throw "V8 SDL patch anchor not found: $Label" }
    $second = $Text.IndexOf($Old, $first + $Old.Length, [System.StringComparison]::Ordinal)
    if ($second -ge 0) { throw "V8 SDL patch anchor is not unique: $Label" }
    return $Text.Substring(0, $first) + $New + $Text.Substring($first + $Old.Length)
}

$videoPath = Join-Path $SourceRoot "src\video\winrt\SDL_winrtvideo.cpp"
$glPath = Join-Path $SourceRoot "src\video\windows\SDL_windowsopengl.c"
if (-not (Test-Path -LiteralPath $videoPath)) { throw "Missing SDL WinRT video source: $videoPath" }
if (-not (Test-Path -LiteralPath $glPath)) { throw "Missing SDL Windows OpenGL source: $glPath" }

$video = Read-NormalizedText -Path $videoPath
$gl = Read-NormalizedText -Path $glPath

$old = @'
extern "C" __declspec(dllexport) int BanditSDL_WinRTIntegrationPatchV7(void)
{
    return 7;
}
'@
$new = @'
extern "C" __declspec(dllexport) int BanditSDL_WinRTIntegrationPatchV7(void)
{
    return 7;
}

/* V8 corrects the OpenGL provider contract used by Minecraft 26.3. LWJGL's
   Windows GL FunctionProvider calls wglGetProcAddress first and only then
   falls back to the SharedLibrary export. SDL must use the same order or
   RenderPearl's exact glGetError pointer check rejects an otherwise-valid GL. */
extern "C" __declspec(dllexport) int BanditSDL_WinRTProviderParityV2(void)
{
    return 2;
}
'@
$video = Replace-Once -Text $video -Old $old -New $new -Label "V8 provider-parity marker"

$old = @'
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
$new = @'
SDL_FunctionPointer WIN_GL_GetProcAddress(SDL_VideoDevice *_this, const char *proc)
{
    SDL_FunctionPointer func;

    /* Keep this lookup order identical to LWJGL's Windows OpenGL provider:
       wglGetProcAddress first, SharedLibrary/GetProcAddress second. Minecraft
       26.3 compares SDL_GL_GetProcAddress("glGetError") with LWJGL's pointer
       for exact equality before it accepts the OpenGL backend. */
    func = (SDL_FunctionPointer)_this->gl_data->wglGetProcAddress(proc);
    if (!func) {
        func = (SDL_FunctionPointer)GetProcAddress((HMODULE)_this->gl_config.dll_handle, proc);
    }

#ifdef SDL_PLATFORM_WINRT
    if (SDL_strcmp(proc, "glGetError") == 0) {
        SDL_Log("Bandit SDL V8: glGetError LWJGL-order pointer=%p module=%p (wgl-first)",
                (void *)func, (void *)_this->gl_config.dll_handle);
    }
#endif
    return func;
}
'@
$gl = Replace-Once -Text $gl -Old $old -New $new -Label "LWJGL provider lookup order"

if ($gl.IndexOf('GetProcAddress((HMODULE)_this->gl_config.dll_handle, proc);', [System.StringComparison]::Ordinal) -lt 0) {
    throw "V8 provider parity validation failed: DLL fallback missing"
}
$wglIndex = $gl.IndexOf('func = (SDL_FunctionPointer)_this->gl_data->wglGetProcAddress(proc);', [System.StringComparison]::Ordinal)
$dllIndex = $gl.IndexOf('func = (SDL_FunctionPointer)GetProcAddress((HMODULE)_this->gl_config.dll_handle, proc);', $wglIndex, [System.StringComparison]::Ordinal)
if ($wglIndex -lt 0 -or $dllIndex -lt 0 -or $wglIndex -ge $dllIndex) {
    throw "V8 provider parity validation failed: SDL does not mirror LWJGL wgl-first lookup order"
}

Write-NormalizedText -Path $videoPath -Text $video
Write-NormalizedText -Path $glPath -Text $gl

Write-Host "Applied SDL V8 provider-parity fix: LWJGL/SDL both use wglGetProcAddress first, DLL export second."
