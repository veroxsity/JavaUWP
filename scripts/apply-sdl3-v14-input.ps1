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
    if ($first -lt 0) { throw "V14 SDL input patch anchor not found: $Label" }
    $second = $Text.IndexOf($Old, $first + $Old.Length, [System.StringComparison]::Ordinal)
    if ($second -ge 0) { throw "V14 SDL input patch anchor is not unique: $Label" }
    return $Text.Substring(0, $first) + $New + $Text.Substring($first + $Old.Length)
}

$videoPath = Join-Path $SourceRoot "src\video\winrt\SDL_winrtvideo.cpp"
$mousePath = Join-Path $SourceRoot "src\video\winrt\SDL_winrtmouse.cpp"
$mouseHeaderPath = Join-Path $SourceRoot "src\video\winrt\SDL_winrtmouse_c.h"
$pointerPath = Join-Path $SourceRoot "src\video\winrt\SDL_winrtpointerinput.cpp"
$appPath = Join-Path $SourceRoot "src\core\winrt\SDL_winrtapp_direct3d.cpp"
$wgiPath = Join-Path $SourceRoot "src\joystick\windows\SDL_windows_gaming_input.c"
foreach ($path in @($videoPath, $mousePath, $mouseHeaderPath, $pointerPath, $appPath, $wgiPath)) {
    if (-not (Test-Path -LiteralPath $path)) { throw "Missing SDL source for V14 input patch: $path" }
}

$video = Read-NormalizedText $videoPath
$mouse = Read-NormalizedText $mousePath
$mouseHeader = Read-NormalizedText $mouseHeaderPath
$pointer = Read-NormalizedText $pointerPath
$app = Read-NormalizedText $appPath
$wgi = Read-NormalizedText $wgiPath

$old = @'
extern "C" __declspec(dllexport) int BanditSDL_WinRTInputBridgeV13(void)
{
    return 13;
}
'@
$new = @'
extern "C" __declspec(dllexport) int BanditSDL_WinRTInputBridgeV13(void)
{
    return 13;
}

/* V14 fixes the controller backend rather than merely checking SDL gamepad
   exports. Windows Store/UWP SDL uses Windows.Gaming.Input (WGI), whose
   backend is opt-in by default upstream. Bandit enables it by default and
   still allows an explicit SDL_JOYSTICK_WGI=0 override for diagnostics. */
extern "C" __declspec(dllexport) int BanditSDL_WinRTInputBridgeV14(void)
{
    return 14;
}
'@
$video = Replace-Once $video $old $new "V14 runtime marker"

# SDL UWP's actual Xbox controller backend is WGI. Upstream makes the backend
# opt-in, which allowed SDL_INIT_GAMEPAD to report success while enumerating no
# controllers. On Bandit's WinRT runtime WGI must be on unless explicitly
# disabled for diagnostics.
$old = @'
static bool WGI_JoystickInit(void)
{
    HRESULT hr;

    if (!SDL_GetHintBoolean(SDL_HINT_JOYSTICK_WGI, false)) {
        return true;
    }
'@
$new = @'
static bool WGI_JoystickInit(void)
{
    HRESULT hr;

#ifdef SDL_PLATFORM_WINRT
    /* Bandit/Xbox: WGI is the actual UWP controller backend. Upstream defaults
       this hint to false, which makes SDL_InitSubSystem(SDL_INIT_GAMEPAD)
       succeed while SDL_GetGamepads() stays empty forever. Default it on for
       WinRT; an explicit SDL_JOYSTICK_WGI=0 still disables it for diagnostics. */
    if (!SDL_GetHintBoolean(SDL_HINT_JOYSTICK_WGI, true)) {
        SDL_Log("Bandit SDL V14: WGI controller backend explicitly disabled");
        return true;
    }
    SDL_Log("Bandit SDL V14: WGI controller backend initializing");
#else
    if (!SDL_GetHintBoolean(SDL_HINT_JOYSTICK_WGI, false)) {
        return true;
    }
#endif
'@
$wgi = Replace-Once $wgi $old $new "enable WGI by default on WinRT"

$old = @'
    return true;
}

static int WGI_JoystickGetCount(void)
{
    return wgi.controller_count;
}
'@
$new = @'
#ifdef SDL_PLATFORM_WINRT
    SDL_Log("Bandit SDL V14: WGI initialization complete controller_count=%d", wgi.controller_count);
#endif
    return true;
}

static int WGI_JoystickGetCount(void)
{
    return wgi.controller_count;
}
'@
$wgi = Replace-Once $wgi $old $new "WGI enumeration diagnostic"

$old = @'
    if (hwdata->gamepad) {
        // FIXME: Can WGI even tell us if trigger rumble is supported?
        SDL_SetBooleanProperty(SDL_GetJoystickProperties(joystick), SDL_PROP_JOYSTICK_CAP_RUMBLE_BOOLEAN, true);
        SDL_SetBooleanProperty(SDL_GetJoystickProperties(joystick), SDL_PROP_JOYSTICK_CAP_TRIGGER_RUMBLE_BOOLEAN, true);
    }
    return true;
}
'@
$new = @'
    if (hwdata->gamepad) {
        // FIXME: Can WGI even tell us if trigger rumble is supported?
        SDL_SetBooleanProperty(SDL_GetJoystickProperties(joystick), SDL_PROP_JOYSTICK_CAP_RUMBLE_BOOLEAN, true);
        SDL_SetBooleanProperty(SDL_GetJoystickProperties(joystick), SDL_PROP_JOYSTICK_CAP_TRIGGER_RUMBLE_BOOLEAN, true);
    }
#ifdef SDL_PLATFORM_WINRT
    SDL_Log("Bandit SDL V14: WGI joystick opened index=%d axes=%d buttons=%d hats=%d gamepad=%d",
            device_index, joystick->naxes, joystick->nbuttons, joystick->nhats, hwdata->gamepad ? 1 : 0);
#endif
    return true;
}
'@
$wgi = Replace-Once $wgi $old $new "WGI open diagnostic"

# Track whether WinRT gave SDL the raw relative MouseDevice source. If it did
# not, CoreWindow PointerMoved remains our only mouse source and V14 uses it as
# a bounded relative-motion fallback instead of discarding the events.
$old = @'
extern "C" bool WINRT_UsingRelativeMouseMode = false;
'@
$new = @'
extern "C" bool WINRT_UsingRelativeMouseMode = false;
extern "C" bool WINRT_BanditHasRawMouseDevice = false;
'@
$mouse = Replace-Once $mouse $old $new "raw MouseDevice availability state"

$old = @'
extern bool WINRT_UsingRelativeMouseMode;
'@
$new = @'
extern bool WINRT_UsingRelativeMouseMode;
extern bool WINRT_BanditHasRawMouseDevice;
'@
$mouseHeader = Replace-Once $mouseHeader $old $new "raw MouseDevice availability declaration"

$old = @'
    if (!window || WINRT_UsingRelativeMouseMode) {
        return;
    }

    Windows::Foundation::Point normalizedPoint = WINRT_TransformCursorPosition(window, pointerPoint->Position, NormalizeZeroToOne);
    Windows::Foundation::Point windowPoint = WINRT_TransformCursorPosition(window, pointerPoint->Position, TransformToSDLWindowSize);
'@
$new = @'
    if (!window) {
        return;
    }

    Windows::Foundation::Point normalizedPoint = WINRT_TransformCursorPosition(window, pointerPoint->Position, NormalizeZeroToOne);
    Windows::Foundation::Point windowPoint = WINRT_TransformCursorPosition(window, pointerPoint->Position, TransformToSDLWindowSize);
    static bool banditRelativeSeedValid = false;
    static Windows::Foundation::Point banditRelativeSeed;

    if (WINRT_UsingRelativeMouseMode) {
        if (WINRT_BanditHasRawMouseDevice) {
            banditRelativeSeedValid = false;
            return; // raw MouseDevice::MouseMoved is the preferred unbounded source
        }

        if (!banditRelativeSeedValid) {
            banditRelativeSeed = windowPoint;
            banditRelativeSeedValid = true;
            SDL_Log("Bandit SDL V14: using CoreWindow relative-mouse fallback (raw MouseDevice unavailable)");
            return;
        }

        const float dx = windowPoint.X - banditRelativeSeed.X;
        const float dy = windowPoint.Y - banditRelativeSeed.Y;
        banditRelativeSeed = windowPoint;
        if (dx != 0.0f || dy != 0.0f) {
            SDL_SendMouseMotion(0, window, SDL_DEFAULT_MOUSE_ID, true, dx, dy);
        }
        return;
    }

    banditRelativeSeedValid = false;
'@
$pointer = Replace-Once $pointer $old $new "relative pointer fallback when raw mouse is unavailable"

$old = @'
    auto banditMouseDevice = Windows::Devices::Input::MouseDevice::GetForCurrentView();
    if (banditMouseDevice != nullptr) {
        banditMouseDevice->MouseMoved +=
            ref new TypedEventHandler<MouseDevice ^, MouseEventArgs ^>(this, &SDL_WinRTApp::OnMouseMoved);
        SDL_Log("Bandit SDL V13: WinRT MouseDevice relative-motion source attached");
    } else {
        SDL_Log("Bandit SDL V13: no physical WinRT MouseDevice present while attaching input bridge");
    }
'@
$new = @'
    auto banditMouseDevice = Windows::Devices::Input::MouseDevice::GetForCurrentView();
    if (banditMouseDevice != nullptr) {
        WINRT_BanditHasRawMouseDevice = true;
        banditMouseDevice->MouseMoved +=
            ref new TypedEventHandler<MouseDevice ^, MouseEventArgs ^>(this, &SDL_WinRTApp::OnMouseMoved);
        SDL_Log("Bandit SDL V14: WinRT MouseDevice raw relative-motion source attached");
    } else {
        WINRT_BanditHasRawMouseDevice = false;
        SDL_Log("Bandit SDL V14: no WinRT MouseDevice; CoreWindow relative-motion fallback enabled");
    }
'@
$app = Replace-Once $app $old $new "raw MouseDevice availability handoff"

Write-NormalizedText $videoPath $video
Write-NormalizedText $mousePath $mouse
Write-NormalizedText $mouseHeaderPath $mouseHeader
Write-NormalizedText $pointerPath $pointer
Write-NormalizedText $appPath $app
Write-NormalizedText $wgiPath $wgi
Write-Host "Applied SDL V14 WinRT WGI controller + mouse fallback fix."
