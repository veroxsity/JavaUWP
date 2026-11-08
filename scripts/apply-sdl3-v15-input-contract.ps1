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
    if ($first -lt 0) { throw "V15 SDL input-contract patch anchor not found: $Label" }
    $second = $Text.IndexOf($Old, $first + $Old.Length, [System.StringComparison]::Ordinal)
    if ($second -ge 0) { throw "V15 SDL input-contract patch anchor is not unique: $Label" }
    return $Text.Substring(0, $first) + $New + $Text.Substring($first + $Old.Length)
}

$videoPath = Join-Path $SourceRoot "src\video\winrt\SDL_winrtvideo.cpp"
$keyboardPath = Join-Path $SourceRoot "src\video\winrt\SDL_winrtkeyboard.cpp"
$eventsHeaderPath = Join-Path $SourceRoot "src\video\winrt\SDL_winrtevents_c.h"
$pointerPath = Join-Path $SourceRoot "src\video\winrt\SDL_winrtpointerinput.cpp"
$mousePath = Join-Path $SourceRoot "src\video\winrt\SDL_winrtmouse.cpp"
$appPath = Join-Path $SourceRoot "src\core\winrt\SDL_winrtapp_direct3d.cpp"
$appHeaderPath = Join-Path $SourceRoot "src\core\winrt\SDL_winrtapp_direct3d.h"
foreach ($path in @($videoPath, $keyboardPath, $eventsHeaderPath, $pointerPath, $mousePath, $appPath, $appHeaderPath)) {
    if (-not (Test-Path -LiteralPath $path)) { throw "Missing SDL source for V15 input-contract patch: $path" }
}

$video = Read-NormalizedText $videoPath
$keyboard = Read-NormalizedText $keyboardPath
$eventsHeader = Read-NormalizedText $eventsHeaderPath
$pointer = Read-NormalizedText $pointerPath
$mouse = Read-NormalizedText $mousePath
$app = Read-NormalizedText $appPath
$appHeader = Read-NormalizedText $appHeaderPath

# Marker makes stale V14 SDL overrides fail at packaging instead of silently
# bringing back the broken SDL2-era WinRT input calling convention.
$old = @'
extern "C" __declspec(dllexport) int BanditSDL_WinRTInputBridgeV14(void)
{
    return 14;
}
'@
$new = @'
extern "C" __declspec(dllexport) int BanditSDL_WinRTInputBridgeV14(void)
{
    return 14;
}

/* V15 corrects the WinRT input layer's SDL2 -> SDL3 API drift: keyboard
   down/up semantics, mouse button argument order, direct CoreWindow physical
   key delivery, capture-loss cleanup and true hidden-cursor support. */
extern "C" __declspec(dllexport) int BanditSDL_WinRTInputContractV15(void)
{
    return 15;
}
'@
$video = Replace-Once $video $old $new "V15 runtime marker"

# SDL3's SDL_SendKeyboardKey final parameter is bool down. The pinned WinRT
# source still used SDL2's RELEASE=0 / PRESSED=1 state convention backwards.
$old = @'
void WINRT_ProcessAcceleratorKeyActivated(Windows::UI::Core::AcceleratorKeyEventArgs ^ args)
{
    using namespace Windows::UI::Core;

    Uint8 state;
    SDL_Scancode code;
    Uint16 rawcode = 0;

    switch (args->EventType) {
    case CoreAcceleratorKeyEventType::SystemKeyDown:
    case CoreAcceleratorKeyEventType::KeyDown:
        state = 0;
        break;
    case CoreAcceleratorKeyEventType::SystemKeyUp:
    case CoreAcceleratorKeyEventType::KeyUp:
        state = 1;
        break;
    default:
        return;
    }

    code = WINRT_TranslateKeycode(args->VirtualKey, args->KeyStatus, &rawcode);
    SDL_SendKeyboardKey(0, SDL_DEFAULT_KEYBOARD_ID, rawcode, code, state);
}
'@
$new = @'
void WINRT_ProcessKeyEvent(Windows::System::VirtualKey virtualKey,
                           const Windows::UI::Core::CorePhysicalKeyStatus& keyStatus,
                           bool down)
{
    SDL_Scancode code;
    Uint16 rawcode = 0;
    code = WINRT_TranslateKeycode(virtualKey, keyStatus, &rawcode);
    SDL_SendKeyboardKey(0, SDL_DEFAULT_KEYBOARD_ID, rawcode, code, down);
}

void WINRT_ProcessAcceleratorKeyActivated(Windows::UI::Core::AcceleratorKeyEventArgs ^ args)
{
    using namespace Windows::UI::Core;

    bool down;
    switch (args->EventType) {
    case CoreAcceleratorKeyEventType::SystemKeyDown:
    case CoreAcceleratorKeyEventType::KeyDown:
        down = true;
        break;
    case CoreAcceleratorKeyEventType::SystemKeyUp:
    case CoreAcceleratorKeyEventType::KeyUp:
        down = false;
        break;
    default:
        return;
    }

    WINRT_ProcessKeyEvent(args->VirtualKey, args->KeyStatus, down);
}
'@
$keyboard = Replace-Once $keyboard $old $new "correct SDL3 keyboard down/up contract"

$old = @'
// Keyboard
extern void WINRT_ProcessAcceleratorKeyActivated(Windows::UI::Core::AcceleratorKeyEventArgs ^ args);
extern void WINRT_ProcessCharacterReceivedEvent(SDL_Window *window, Windows::UI::Core::CharacterReceivedEventArgs ^ args);
'@
$new = @'
// Keyboard
extern void WINRT_ProcessKeyEvent(Windows::System::VirtualKey virtualKey,
                                  const Windows::UI::Core::CorePhysicalKeyStatus& keyStatus,
                                  bool down);
extern void WINRT_ProcessAcceleratorKeyActivated(Windows::UI::Core::AcceleratorKeyEventArgs ^ args);
extern void WINRT_ProcessCharacterReceivedEvent(SDL_Window *window, Windows::UI::Core::CharacterReceivedEventArgs ^ args);
'@
$eventsHeader = Replace-Once $eventsHeader $old $new "declare direct WinRT key bridge"

# SDL3 changed SDL_SendMouseButton to (..., button, bool down). The WinRT
# source still passed (..., pressed, button), including button 0 on press.
$old = @'
        WINRT_GetSDLButtonForPointerPoint(pointerPoint, &button, &pressed);
        SDL_assert(pressed == 1);
        SDL_SendMouseButton(0, window, SDL_DEFAULT_MOUSE_ID, 0, button);
'@
$new = @'
        if (!WINRT_GetSDLButtonForPointerPoint(pointerPoint, &button, &pressed) || button == 0) {
            SDL_Log("Bandit SDL V15: ignored pointer press without a valid SDL button");
            return;
        }
        SDL_assert(pressed == 1);
        SDL_SendMouseButton(0, window, SDL_DEFAULT_MOUSE_ID, button, true);
'@
$pointer = Replace-Once $pointer $old $new "mouse pressed button/down order + zero-button guard"

$old = @'
        if (WINRT_GetSDLButtonForPointerPoint(pointerPoint, &button, &pressed)) {
            SDL_SendMouseButton(0, window, SDL_DEFAULT_MOUSE_ID, pressed, button);
        }
'@
$new = @'
        if (WINRT_GetSDLButtonForPointerPoint(pointerPoint, &button, &pressed) && button != 0) {
            SDL_SendMouseButton(0, window, SDL_DEFAULT_MOUSE_ID, button, pressed != 0);
        }
'@
$pointer = Replace-Once $pointer $old $new "mouse moved button/down order + zero-button guard"

$old = @'
        WINRT_GetSDLButtonForPointerPoint(pointerPoint, &button, &pressed);
        SDL_assert(pressed == 0);
        SDL_SendMouseButton(0, window, SDL_DEFAULT_MOUSE_ID, 1, button);
'@
$new = @'
        if (!WINRT_GetSDLButtonForPointerPoint(pointerPoint, &button, &pressed) || button == 0) {
            SDL_Log("Bandit SDL V15: ignored pointer release without a valid SDL button");
            return;
        }
        SDL_assert(pressed == 0);
        SDL_SendMouseButton(0, window, SDL_DEFAULT_MOUSE_ID, button, false);
'@
$pointer = Replace-Once $pointer $old $new "mouse released button/down order + zero-button guard"

# Release every physical mouse button when CoreWindow loses pointer capture or
# activation. SDL deliberately doesn't reset mouse buttons on focus loss.
$old = @'
void WINRT_ProcessPointerReleasedEvent(SDL_Window *window, Windows::UI::Input::PointerPoint ^ pointerPoint)
{
'@
$new = @'
void WINRT_ResetMouseButtons(SDL_Window *window)
{
    if (!window) {
        return;
    }
    SDL_SendMouseButton(0, window, SDL_DEFAULT_MOUSE_ID, SDL_BUTTON_LEFT, false);
    SDL_SendMouseButton(0, window, SDL_DEFAULT_MOUSE_ID, SDL_BUTTON_MIDDLE, false);
    SDL_SendMouseButton(0, window, SDL_DEFAULT_MOUSE_ID, SDL_BUTTON_RIGHT, false);
    SDL_SendMouseButton(0, window, SDL_DEFAULT_MOUSE_ID, SDL_BUTTON_X1, false);
    SDL_SendMouseButton(0, window, SDL_DEFAULT_MOUSE_ID, SDL_BUTTON_X2, false);
}

void WINRT_ProcessPointerReleasedEvent(SDL_Window *window, Windows::UI::Input::PointerPoint ^ pointerPoint)
{
'@
$pointer = Replace-Once $pointer $old $new "mouse release-state reset helper"

$old = @'
extern void WINRT_ProcessPointerReleasedEvent(SDL_Window *window, Windows::UI::Input::PointerPoint ^ pointerPoint);
extern void WINRT_ProcessPointerEnteredEvent(SDL_Window *window, Windows::UI::Input::PointerPoint ^ pointerPoint);
'@
$new = @'
extern void WINRT_ProcessPointerReleasedEvent(SDL_Window *window, Windows::UI::Input::PointerPoint ^ pointerPoint);
extern void WINRT_ResetMouseButtons(SDL_Window *window);
extern void WINRT_ProcessPointerEnteredEvent(SDL_Window *window, Windows::UI::Input::PointerPoint ^ pointerPoint);
'@
$eventsHeader = Replace-Once $eventsHeader $old $new "declare WinRT mouse reset helper"

# Use the exact same physical key event source as Bandit's hardware-proven
# GLFW path. CharacterReceived remains separate for text input.
$old = @'
    window->Dispatcher->AcceleratorKeyActivated +=
        ref new TypedEventHandler<CoreDispatcher ^, AcceleratorKeyEventArgs ^>(this, &SDL_WinRTApp::OnAcceleratorKeyActivated);

    window->CharacterReceived +=
'@
$new = @'
    window->KeyDown +=
        ref new TypedEventHandler<CoreWindow ^, KeyEventArgs ^>(this, &SDL_WinRTApp::OnKeyDown);

    window->KeyUp +=
        ref new TypedEventHandler<CoreWindow ^, KeyEventArgs ^>(this, &SDL_WinRTApp::OnKeyUp);

    window->CharacterReceived +=
'@
$app = Replace-Once $app $old $new "direct CoreWindow KeyDown/KeyUp subscription"

$old = @'
    window->PointerExited +=
        ref new TypedEventHandler<CoreWindow ^, PointerEventArgs ^>(this, &SDL_WinRTApp::OnPointerExited);

    window->PointerWheelChanged +=
'@
$new = @'
    window->PointerExited +=
        ref new TypedEventHandler<CoreWindow ^, PointerEventArgs ^>(this, &SDL_WinRTApp::OnPointerExited);

    window->PointerCaptureLost +=
        ref new TypedEventHandler<CoreWindow ^, PointerEventArgs ^>(this, &SDL_WinRTApp::OnPointerCaptureLost);

    window->PointerWheelChanged +=
'@
$app = Replace-Once $app $old $new "pointer-capture-loss subscription"

$old = @'
void SDL_WinRTApp::OnAcceleratorKeyActivated(Windows::UI::Core::CoreDispatcher ^ sender, Windows::UI::Core::AcceleratorKeyEventArgs ^ args)
{
    WINRT_ProcessAcceleratorKeyActivated(args);
}

void SDL_WinRTApp::OnCharacterReceived(Windows::UI::Core::CoreWindow ^ sender, Windows::UI::Core::CharacterReceivedEventArgs ^ args)
'@
$new = @'
void SDL_WinRTApp::OnKeyDown(Windows::UI::Core::CoreWindow ^ sender, Windows::UI::Core::KeyEventArgs ^ args)
{
    WINRT_ProcessKeyEvent(args->VirtualKey, args->KeyStatus, true);
}

void SDL_WinRTApp::OnKeyUp(Windows::UI::Core::CoreWindow ^ sender, Windows::UI::Core::KeyEventArgs ^ args)
{
    WINRT_ProcessKeyEvent(args->VirtualKey, args->KeyStatus, false);
}

void SDL_WinRTApp::OnAcceleratorKeyActivated(Windows::UI::Core::CoreDispatcher ^ sender, Windows::UI::Core::AcceleratorKeyEventArgs ^ args)
{
    WINRT_ProcessAcceleratorKeyActivated(args);
}

void SDL_WinRTApp::OnCharacterReceived(Windows::UI::Core::CoreWindow ^ sender, Windows::UI::Core::CharacterReceivedEventArgs ^ args)
'@
$app = Replace-Once $app $old $new "direct CoreWindow key handlers"

$old = @'
void SDL_WinRTApp::OnPointerExited(CoreWindow ^ sender, PointerEventArgs ^ args)
{
#if LOG_POINTER_EVENTS
    WINRT_LogPointerEvent("pointer exited", args, WINRT_TransformCursorPosition(WINRT_GlobalSDLWindow, args->CurrentPoint->Position, TransformToSDLWindowSize));
#endif

    WINRT_ProcessPointerExitedEvent(WINRT_GlobalSDLWindow, args->CurrentPoint);
}

void SDL_WinRTApp::OnPointerWheelChanged(CoreWindow ^ sender, PointerEventArgs ^ args)
'@
$new = @'
void SDL_WinRTApp::OnPointerExited(CoreWindow ^ sender, PointerEventArgs ^ args)
{
#if LOG_POINTER_EVENTS
    WINRT_LogPointerEvent("pointer exited", args, WINRT_TransformCursorPosition(WINRT_GlobalSDLWindow, args->CurrentPoint->Position, TransformToSDLWindowSize));
#endif

    WINRT_ProcessPointerExitedEvent(WINRT_GlobalSDLWindow, args->CurrentPoint);
}

void SDL_WinRTApp::OnPointerCaptureLost(CoreWindow ^ sender, PointerEventArgs ^ args)
{
    WINRT_ResetMouseButtons(WINRT_GlobalSDLWindow);
}

void SDL_WinRTApp::OnPointerWheelChanged(CoreWindow ^ sender, PointerEventArgs ^ args)
'@
$app = Replace-Once $app $old $new "pointer-capture-loss cleanup handler"

$old = @'
        } else {
            if (SDL_GetKeyboardFocus() == window) {
                SDL_SetKeyboardFocus(NULL);
            }
        }
'@
$new = @'
        } else {
            WINRT_ResetMouseButtons(window);
            if (SDL_GetKeyboardFocus() != NULL) {
                /* All SDL logical WinRT windows share one physical CoreWindow.
                   Clear whichever logical window owns focus, not only the newest
                   WINRT_GlobalSDLWindow. SDL_SetKeyboardFocus(NULL) calls
                   SDL_ResetKeyboard() and emits releases for every held key. */
                SDL_SetKeyboardFocus(NULL);
            }
        }
'@
$app = Replace-Once $app $old $new "activation-loss input reset"

# BackRequested synthesized key events were also using the inverted old state
# convention. Keep the event as a clean press followed by release.
$old = @'
    SDL_SendKeyboardKey(0, SDL_GLOBAL_KEYBOARD_ID, 0, SDL_SCANCODE_AC_BACK, 0);
    SDL_SendKeyboardKey(0, SDL_GLOBAL_KEYBOARD_ID, 0, SDL_SCANCODE_AC_BACK, 1);
'@
$new = @'
    SDL_SendKeyboardKey(0, SDL_GLOBAL_KEYBOARD_ID, 0, SDL_SCANCODE_AC_BACK, true);
    SDL_SendKeyboardKey(0, SDL_GLOBAL_KEYBOARD_ID, 0, SDL_SCANCODE_AC_BACK, false);
'@
$app = Replace-Once $app $old $new "back key down/up order"

$old = @'
    void OnPointerExited(Windows::UI::Core::CoreWindow ^ sender, Windows::UI::Core::PointerEventArgs ^ args);
    void OnMouseMoved(Windows::Devices::Input::MouseDevice ^ mouseDevice, Windows::Devices::Input::MouseEventArgs ^ args);
    void OnAcceleratorKeyActivated(Windows::UI::Core::CoreDispatcher ^ sender, Windows::UI::Core::AcceleratorKeyEventArgs ^ args);
'@
$new = @'
    void OnPointerExited(Windows::UI::Core::CoreWindow ^ sender, Windows::UI::Core::PointerEventArgs ^ args);
    void OnPointerCaptureLost(Windows::UI::Core::CoreWindow ^ sender, Windows::UI::Core::PointerEventArgs ^ args);
    void OnMouseMoved(Windows::Devices::Input::MouseDevice ^ mouseDevice, Windows::Devices::Input::MouseEventArgs ^ args);
    void OnKeyDown(Windows::UI::Core::CoreWindow ^ sender, Windows::UI::Core::KeyEventArgs ^ args);
    void OnKeyUp(Windows::UI::Core::CoreWindow ^ sender, Windows::UI::Core::KeyEventArgs ^ args);
    void OnAcceleratorKeyActivated(Windows::UI::Core::CoreDispatcher ^ sender, Windows::UI::Core::AcceleratorKeyEventArgs ^ args);
'@
$appHeader = Replace-Once $appHeader $old $new "declare key and pointer-capture handlers"

# Prove that the official transparent cursor resource linked into MC.Xbox.exe
# is actually resolving. V13's Arrow fallback remains only as an emergency path.
$old = @'
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
$new = @'
        if (FAILED(hr)) {
            /* Emergency-only fallback. V15 packages resource 5000 in MC.Xbox.exe,
               so hardware should normally take the success branch below. */
            static bool loggedMissingBlankCursor = false;
            coreWindow->PointerCursor = ref new CoreCursor(CoreCursorType::Arrow, 0);
            if (!loggedMissingBlankCursor) {
                loggedMissingBlankCursor = true;
                SDL_Log("Bandit SDL V15: ERROR transparent cursor resource 5000 unavailable; using Arrow fallback");
            }
        } else {
            static bool loggedBlankCursor = false;
            if (!loggedBlankCursor) {
                loggedBlankCursor = true;
                SDL_Log("Bandit SDL V15: transparent WinRT cursor resource 5000 active");
            }
        }
'@
$mouse = Replace-Once $mouse $old $new "transparent WinRT cursor resource proof"

# Mirror the known-good GLFW path's capture semantics. The official blank
# cursor resource linked into MC.Xbox.exe keeps CoreWindow motion alive while
# the cursor is hidden in gameplay.
$old = @'
static bool WINRT_SetRelativeMouseMode(bool enabled)
{
    if (WINRT_UsingRelativeMouseMode != enabled) {
        WINRT_UsingRelativeMouseMode = enabled;
        SDL_Log("Bandit SDL V13: relative mouse mode=%d", enabled ? 1 : 0);
    }
    return true;
}
'@
$new = @'
static bool WINRT_SetRelativeMouseMode(bool enabled)
{
    if (WINRT_UsingRelativeMouseMode != enabled) {
        WINRT_UsingRelativeMouseMode = enabled;
        SDL_Log("Bandit SDL V15: relative mouse mode=%d", enabled ? 1 : 0);
    }

    CoreWindow ^ coreWindow = CoreWindow::GetForCurrentThread();
    if (coreWindow) {
        try {
            if (enabled) {
                coreWindow->SetPointerCapture();
            } else {
                coreWindow->ReleasePointerCapture();
            }
        } catch (...) {
            SDL_Log("Bandit SDL V15: CoreWindow pointer capture transition failed enabled=%d", enabled ? 1 : 0);
        }
    }
    return true;
}
'@
$mouse = Replace-Once $mouse $old $new "relative-mode CoreWindow capture"

Write-NormalizedText $videoPath $video
Write-NormalizedText $keyboardPath $keyboard
Write-NormalizedText $eventsHeaderPath $eventsHeader
Write-NormalizedText $pointerPath $pointer
Write-NormalizedText $mousePath $mouse
Write-NormalizedText $appPath $app
Write-NormalizedText $appHeaderPath $appHeader
Write-Host "Applied SDL V15 WinRT input contract + direct keyboard + capture cleanup fix."
