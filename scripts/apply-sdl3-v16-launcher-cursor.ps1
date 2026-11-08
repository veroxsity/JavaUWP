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
    if ($first -lt 0) { throw "V16 SDL launcher-cursor patch anchor not found: $Label" }
    $second = $Text.IndexOf($Old, $first + $Old.Length, [System.StringComparison]::Ordinal)
    if ($second -ge 0) { throw "V16 SDL launcher-cursor patch anchor is not unique: $Label" }
    return $Text.Substring(0, $first) + $New + $Text.Substring($first + $Old.Length)
}

$videoPath = Join-Path $SourceRoot "src\video\winrt\SDL_winrtvideo.cpp"
$mousePath = Join-Path $SourceRoot "src\video\winrt\SDL_winrtmouse.cpp"
foreach ($path in @($videoPath, $mousePath)) {
    if (-not (Test-Path -LiteralPath $path)) { throw "Missing SDL source for V16 launcher cursor patch: $path" }
}

$video = Read-NormalizedText $videoPath
$mouse = Read-NormalizedText $mousePath

$old = @'
extern "C" __declspec(dllexport) int BanditSDL_WinRTInputContractV15(void)
{
    return 15;
}
'@
$new = @'
extern "C" __declspec(dllexport) int BanditSDL_WinRTInputContractV15(void)
{
    return 15;
}

/* V16 makes the visible SDL/WinRT mouse cursor use Bandit's launcher cursor
   resource while keeping the V15 input semantics and V12 graphics contract. */
extern "C" __declspec(dllexport) int BanditSDL_WinRTLauncherCursorV16(void)
{
    return 16;
}
'@
$video = Replace-Once $video $old $new "V16 runtime marker"

$old = @'
static SDL_Cursor *WINRT_CreateSystemCursor(SDL_SystemCursor id)
{
    SDL_Cursor *cursor;
    CoreCursorType cursorType = CoreCursorType::Arrow;
'@
$new = @'
static SDL_Cursor *WINRT_CreateSystemCursor(SDL_SystemCursor id)
{
    SDL_Cursor *cursor;
    CoreCursorType cursorType = CoreCursorType::Arrow;
    unsigned int cursorResourceID = 0;
'@
$mouse = Replace-Once $mouse $old $new "cursor resource-id state"

$old = @'
    case SDL_SYSTEM_CURSOR_DEFAULT:
        cursorType = CoreCursorType::Arrow;
        break;
'@
$new = @'
    case SDL_SYSTEM_CURSOR_DEFAULT:
        /* Match the visible native Bandit launcher cursor instead of exposing
           WinRT's browser/desktop Arrow inside Minecraft 26.3 menus. Resource
           5001 is linked into MC.Xbox.exe beside SDL's transparent 5000 cursor. */
        cursorType = CoreCursorType::Custom;
        cursorResourceID = 5001;
        break;
'@
$mouse = Replace-Once $mouse $old $new "default launcher-style cursor"

$old = @'
        CoreCursor ^ *theCursor = new CoreCursor ^ (nullptr);
        *theCursor = ref new CoreCursor(cursorType, 0);
        cursor->internal = (SDL_CursorData *)theCursor;
'@
$new = @'
        CoreCursor ^ *theCursor = new CoreCursor ^ (nullptr);
        *theCursor = ref new CoreCursor(cursorType, cursorResourceID);
        cursor->internal = (SDL_CursorData *)theCursor;
'@
$mouse = Replace-Once $mouse $old $new "custom cursor resource id forwarding"

$old = @'
static SDL_Cursor *WINRT_CreateDefaultCursor()
{
    return WINRT_CreateSystemCursor(SDL_SYSTEM_CURSOR_DEFAULT);
}
'@
$new = @'
static SDL_Cursor *WINRT_CreateDefaultCursor()
{
    SDL_Log("Bandit SDL V16: default visible cursor uses launcher resource 5001");
    return WINRT_CreateSystemCursor(SDL_SYSTEM_CURSOR_DEFAULT);
}
'@
$mouse = Replace-Once $mouse $old $new "visible cursor runtime proof"

Write-NormalizedText $videoPath $video
Write-NormalizedText $mousePath $mouse
Write-Host "Applied SDL V16 launcher-style visible cursor resource mapping."
