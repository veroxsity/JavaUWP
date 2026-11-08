param(
    [Parameter(Mandatory = $true)][string]$SourceRoot
)
$ErrorActionPreference = "Stop"
$Utf8NoBom = New-Object System.Text.UTF8Encoding($false)
function Read-Sdl { param([string]$Path) return ([System.IO.File]::ReadAllText($Path) -replace "`r`n", "`n") }
function Write-Sdl { param([string]$Path,[string]$Text) [System.IO.File]::WriteAllText($Path, $Text, $Utf8NoBom) }
function Replace-SdlOnce {
    param([string]$Text,[string]$Old,[string]$New,[string]$Label)
    $i=$Text.IndexOf($Old,[System.StringComparison]::Ordinal)
    if($i -lt 0){ throw "V17 SDL patch anchor not found: $Label" }
    if($Text.IndexOf($Old,$i+$Old.Length,[System.StringComparison]::Ordinal) -ge 0){ throw "V17 SDL patch non-unique anchor: $Label" }
    return $Text.Substring(0,$i) + $New + $Text.Substring($i+$Old.Length)
}
$videoPath = Join-Path $SourceRoot 'src\video\winrt\SDL_winrtvideo.cpp'
$mousePath = Join-Path $SourceRoot 'src\video\winrt\SDL_winrtmouse.cpp'
$pointerPath = Join-Path $SourceRoot 'src\video\winrt\SDL_winrtpointerinput.cpp'
$appPath = Join-Path $SourceRoot 'src\core\winrt\SDL_winrtapp_direct3d.cpp'
foreach($p in @($videoPath,$mousePath,$pointerPath,$appPath)){
    if(-not(Test-Path -LiteralPath $p)){throw "Missing SDL WinRT source for V17: $p"}
}
$video=Read-Sdl $videoPath
$mouse=Read-Sdl $mousePath
$pointer=Read-Sdl $pointerPath
$app=Read-Sdl $appPath
$old=@'
extern "C" __declspec(dllexport) int BanditSDL_WinRTLauncherCursorV16(void)
{
    return 16;
}
'@
$new=@'
extern "C" __declspec(dllexport) int BanditSDL_WinRTLauncherCursorV16(void)
{
    return 16;
}

/* V17 draws the 26.3 menu cursor in Minecraft, leaving the Xbox shell pointer
   hidden, with MouseDevice deltas driving SDL menu position and game mouse-look. */
extern "C" __declspec(dllexport) int BanditSDL_WinRTSoftwareCursorV17(void)
{
    return 17;
}
'@
$video=Replace-SdlOnce $video $old $new 'V17 runtime marker'

# V16 relied on CoreCursor resources on Xbox, which hardware testing proved
# still drew an oversized system/Edge-style arrow. Instead match the known-good
# GLFW shim: native CoreWindow pointer hidden, cursor drawn in Minecraft.
$start=$mouse.IndexOf('static bool WINRT_ShowCursor(SDL_Cursor *cursor)', [System.StringComparison]::Ordinal)
$end=$mouse.IndexOf('static bool WINRT_SetRelativeMouseMode(bool enabled)', [System.StringComparison]::Ordinal)
if($start -lt 0 -or $end -le $start -or $mouse.IndexOf('static bool WINRT_ShowCursor(SDL_Cursor *cursor)',($start + 1),[System.StringComparison]::Ordinal) -ge 0){throw 'V17 SDL WINRT_ShowCursor range missing/ambiguous'}
$newShow=@'
static bool WINRT_ShowCursor(SDL_Cursor *cursor)
{
    // Bandit V17: Minecraft's GUI renders its own small crosshair. The Xbox
    // CoreWindow OS cursor must remain hidden both when SDL requests a visible
    // GUI cursor and when SDL requests a grabbed/gameplay cursor.
    // MouseDevice::MouseMoved continues to deliver physical movement with a
    // null PointerCursor (CoreWindow absolute PointerMoved may stop).
    CoreWindow ^ coreWindow = CoreWindow::GetForCurrentThread();
    if (coreWindow) {
        coreWindow->PointerCursor = nullptr;
    }
    return true;
}

'@
$mouse=$mouse.Substring(0,$start)+$newShow+$mouse.Substring($end)

# The SDL host initially assigns Arrow in SetWindow, before SDL's ShowCursor
# callback. Remove that one-frame restoration as well.
$old=@'
#if !SDL_WINAPI_FAMILY_PHONE
    window->PointerCursor = ref new CoreCursor(CoreCursorType::Arrow, 0);
#endif
'@
$new=@'
#if !SDL_WINAPI_FAMILY_PHONE
    // V17: SDL does not own a visible Xbox system cursor. Java's GUI draws it.
    window->PointerCursor = nullptr;
    SDL_Log("Bandit SDL V17: CoreWindow OS pointer hidden on input attach");
#endif
'@
$app=Replace-SdlOnce $app $old $new 'SetWindow Arrow removal'

# On Xbox, PointerCursor=nullptr suppresses CoreWindow absolute motion but not
# MouseDevice delta. Drive a virtual SDL absolute cursor in menus. This keeps
# normal Minecraft screen hit-testing, click/wheel and inventory operations.
$old=@'
void WINRT_ProcessMouseMovedEvent(SDL_Window *window, Windows::Devices::Input::MouseEventArgs ^ args)
{
    if (!window || !WINRT_UsingRelativeMouseMode) {
        return;
    }
'@
$new=@'
void WINRT_ProcessMouseMovedEvent(SDL_Window *window, Windows::Devices::Input::MouseEventArgs ^ args)
{
    if (!window || !args) {
        return;
    }
'@
$pointer=Replace-SdlOnce $pointer $old $new 'raw MouseDevice guard supports menu mode'
$old=@'
    const Windows::Foundation::Point mouseDeltaInDIPs((float)args->MouseDelta.X, (float)args->MouseDelta.Y);
    const Windows::Foundation::Point mouseDeltaInSDLWindowCoords = WINRT_TransformCursorPosition(window, mouseDeltaInDIPs, TransformToSDLWindowSize);
    SDL_SendMouseMotion(0, window, SDL_DEFAULT_MOUSE_ID, true, mouseDeltaInSDLWindowCoords.X, mouseDeltaInSDLWindowCoords.Y);
}
'@
$new=@'
    const Windows::Foundation::Point mouseDeltaInDIPs((float)args->MouseDelta.X, (float)args->MouseDelta.Y);
    const Windows::Foundation::Point mouseDeltaInSDLWindowCoords = WINRT_TransformCursorPosition(window, mouseDeltaInDIPs, TransformToSDLWindowSize);
    const float dx = mouseDeltaInSDLWindowCoords.X;
    const float dy = mouseDeltaInSDLWindowCoords.Y;
    if (dx == 0.0f && dy == 0.0f) return;
    if (WINRT_UsingRelativeMouseMode) {
        SDL_SendMouseMotion(0, window, SDL_DEFAULT_MOUSE_ID, true, dx, dy);
    } else {
        // The physical OS cursor is intentionally hidden in menus too. Keep
        // SDL's virtual absolute position updated using unbounded mouse deltas.
        float x = 0.0f, y = 0.0f;
        SDL_GetMouseState(&x, &y);
        static bool banditMenuMouseSeeded = false;
        if (!banditMenuMouseSeeded) {
            banditMenuMouseSeeded = true;
            if (x == 0.0f && y == 0.0f) {
                x = window->w * 0.5f;
                y = window->h * 0.5f;
            }
            SDL_Log("Bandit SDL V17: menu cursor driven by raw MouseDevice deltas");
        }
        x = SDL_clamp(x + dx, 0.0f, SDL_max(0.0f, (float)window->w - 1.0f));
        y = SDL_clamp(y + dy, 0.0f, SDL_max(0.0f, (float)window->h - 1.0f));
        SDL_SendMouseMotion(0, window, SDL_DEFAULT_MOUSE_ID, false, x, y);
    }
}
'@
$pointer=Replace-SdlOnce $pointer $old $new 'physical mouse virtual-menu cursor and unchanged gameplay relative deltas'
Write-Sdl $videoPath $video
Write-Sdl $mousePath $mouse
Write-Sdl $pointerPath $pointer
Write-Sdl $appPath $app
Write-Host 'Applied V17 software-menu-cursor, hidden system pointer and SDL raw menu delta patch.'
