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
    if ($first -lt 0) { throw "V13 SDL input patch anchor not found: $Label" }
    $second = $Text.IndexOf($Old, $first + $Old.Length, [System.StringComparison]::Ordinal)
    if ($second -ge 0) { throw "V13 SDL input patch anchor is not unique: $Label" }
    return $Text.Substring(0, $first) + $New + $Text.Substring($first + $Old.Length)
}

$videoPath = Join-Path $SourceRoot "src\video\winrt\SDL_winrtvideo.cpp"
$mousePath = Join-Path $SourceRoot "src\video\winrt\SDL_winrtmouse.cpp"
$pointerPath = Join-Path $SourceRoot "src\video\winrt\SDL_winrtpointerinput.cpp"
$appPath = Join-Path $SourceRoot "src\core\winrt\SDL_winrtapp_direct3d.cpp"
foreach ($path in @($videoPath, $mousePath, $pointerPath, $appPath)) {
    if (-not (Test-Path -LiteralPath $path)) { throw "Missing SDL source for V13 input patch: $path" }
}

$video = Read-NormalizedText $videoPath
$mouse = Read-NormalizedText $mousePath
$pointer = Read-NormalizedText $pointerPath
$app = Read-NormalizedText $appPath

$old = @'
extern "C" __declspec(dllexport) int BanditSDL_WinRTRenderPearlV12(void)
{
    return 12;
}
'@
$new = @'
extern "C" __declspec(dllexport) int BanditSDL_WinRTRenderPearlV12(void)
{
    return 12;
}

/* V13 keeps the proven V12 Mesa/RenderPearl path untouched and hardens
   external-host WinRT mouse input for both menu and relative gameplay use. */
extern "C" __declspec(dllexport) int BanditSDL_WinRTInputBridgeV13(void)
{
    return 13;
}
'@
$video = Replace-Once $video $old $new "V13 runtime marker"

# Never use PointerCursor=nullptr as the fallback. SDL's own WinRT comments
# document that doing so suppresses CoreWindow pointer-motion events. The
# transparent cursor resource is optional in Bandit's host EXE, so preserve
# motion by keeping a stock cursor if that resource is unavailable.
$old = @'
        if (FAILED(hr)) {
            // The app doesn't contain the cursor resource, or some other error
            // occurred.  Just use the other, but mouse-motion-preventing, means of
            // hiding the cursor.
            coreWindow->PointerCursor = nullptr;
        }
'@
$new = @'
        if (FAILED(hr)) {
            /* Bandit's UWP host may not link SDL's optional cursor resource.
               PointerCursor=nullptr suppresses CoreWindow PointerMoved events,
               which makes Minecraft menus appear to lose the mouse. Keep a
               normal cursor instead; raw MouseDevice deltas still drive SDL
               relative mode for first-person camera input. */
            static bool loggedMissingBlankCursor = false;
            coreWindow->PointerCursor = ref new CoreCursor(CoreCursorType::Arrow, 0);
            if (!loggedMissingBlankCursor) {
                loggedMissingBlankCursor = true;
                SDL_Log("Bandit SDL V13: transparent cursor resource unavailable; preserving pointer motion with Arrow fallback");
            }
        }
'@
$mouse = Replace-Once $mouse $old $new "safe hidden-cursor fallback"

$old = @'
static bool WINRT_SetRelativeMouseMode(bool enabled)
{
    WINRT_UsingRelativeMouseMode = enabled;
    return true;
}
'@
$new = @'
static bool WINRT_SetRelativeMouseMode(bool enabled)
{
    if (WINRT_UsingRelativeMouseMode != enabled) {
        WINRT_UsingRelativeMouseMode = enabled;
        SDL_Log("Bandit SDL V13: relative mouse mode=%d", enabled ? 1 : 0);
    }
    return true;
}
'@
$mouse = Replace-Once $mouse $old $new "relative mouse mode diagnostics"

# Keep one-time proof that both halves of the bridge are alive without flooding
# the log at mouse polling frequency.
$old = @'
    if (!WINRT_IsTouchEvent(pointerPoint)) {
        // For some odd reason Moved events are used for multiple mouse buttons
'@
$new = @'
    if (!WINRT_IsTouchEvent(pointerPoint)) {
        static bool loggedAbsoluteMouseMotion = false;
        if (!loggedAbsoluteMouseMotion) {
            loggedAbsoluteMouseMotion = true;
            SDL_Log("Bandit SDL V13: first absolute CoreWindow mouse motion delivered");
        }
        // For some odd reason Moved events are used for multiple mouse buttons
'@
$pointer = Replace-Once $pointer $old $new "absolute mouse-motion proof"

$old = @'
    const Windows::Foundation::Point mouseDeltaInDIPs((float)args->MouseDelta.X, (float)args->MouseDelta.Y);
    const Windows::Foundation::Point mouseDeltaInSDLWindowCoords = WINRT_TransformCursorPosition(window, mouseDeltaInDIPs, TransformToSDLWindowSize);
    SDL_SendMouseMotion(0, window, SDL_DEFAULT_MOUSE_ID, true, mouseDeltaInSDLWindowCoords.X, mouseDeltaInSDLWindowCoords.Y);
'@
$new = @'
    static bool loggedRelativeMouseMotion = false;
    if (!loggedRelativeMouseMotion) {
        loggedRelativeMouseMotion = true;
        SDL_Log("Bandit SDL V13: first raw MouseDevice relative delta delivered");
    }
    const Windows::Foundation::Point mouseDeltaInDIPs((float)args->MouseDelta.X, (float)args->MouseDelta.Y);
    const Windows::Foundation::Point mouseDeltaInSDLWindowCoords = WINRT_TransformCursorPosition(window, mouseDeltaInDIPs, TransformToSDLWindowSize);
    SDL_SendMouseMotion(0, window, SDL_DEFAULT_MOUSE_ID, true, mouseDeltaInSDLWindowCoords.X, mouseDeltaInSDLWindowCoords.Y);
'@
$pointer = Replace-Once $pointer $old $new "relative mouse-motion proof"

# GetForCurrentView can legitimately return null when no physical mouse exists.
# Do not dereference it while attaching SDL to Bandit's externally-owned view.
$old = @'
    // Retrieves relative-only mouse movements:
    Windows::Devices::Input::MouseDevice::GetForCurrentView()->MouseMoved +=
        ref new TypedEventHandler<MouseDevice ^, MouseEventArgs ^>(this, &SDL_WinRTApp::OnMouseMoved);
'@
$new = @'
    // Retrieves relative-only mouse movements:
    auto banditMouseDevice = Windows::Devices::Input::MouseDevice::GetForCurrentView();
    if (banditMouseDevice != nullptr) {
        banditMouseDevice->MouseMoved +=
            ref new TypedEventHandler<MouseDevice ^, MouseEventArgs ^>(this, &SDL_WinRTApp::OnMouseMoved);
        SDL_Log("Bandit SDL V13: WinRT MouseDevice relative-motion source attached");
    } else {
        SDL_Log("Bandit SDL V13: no physical WinRT MouseDevice present while attaching input bridge");
    }
'@
$app = Replace-Once $app $old $new "null-safe MouseDevice subscription"

Write-NormalizedText $videoPath $video
Write-NormalizedText $mousePath $mouse
Write-NormalizedText $pointerPath $pointer
Write-NormalizedText $appPath $app

Write-Host "Applied SDL V13 external-host mouse/input bridge hardening."
