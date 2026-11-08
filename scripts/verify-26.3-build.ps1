param(
    [string]$MinecraftVersion = "26.3",
    [string]$FabricLoader = "0.19.5",
    [string]$ForgeLoader = "66.1.0",
    [string]$NeoForgeLoader = "26.3.0.97",
    [string]$MesaRuntimeDir = $env:MESA_UWP_DIR,
    [string]$SdlSourceDll = $env:SDL3_UWP_DLL,
    [switch]$SkipPackage,
    [switch]$SkipSetup,
    [switch]$FabricOnly,
    [switch]$LocalSigning,
    [switch]$StageOnly,
    [switch]$KeepStaging
)

$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue"

. (Join-Path $PSScriptRoot "common.ps1")

$root = Resolve-RepoRoot
$verifyRoot = Join-Path (Get-ConfigPath "BuildDir") "verify-26.3"
$manifestRoot = Join-Path $verifyRoot "manifests"
$modRoot = Join-Path $verifyRoot "mods"
$sdlOut = Join-Path $verifyRoot "SDL3.dll"

Ensure-Dir $verifyRoot
Ensure-Dir $manifestRoot
Ensure-Dir $modRoot

function Assert-ContainsLiteral {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$Text,
        [Parameter(Mandatory = $true)][string]$Description
    )
    if (-not (Test-Path -LiteralPath $Path)) { throw "Missing source file for ${Description}: $Path" }
    $content = [System.IO.File]::ReadAllText($Path)
    if (-not $content.Contains($Text)) { throw "Static acceptance check failed (${Description}): '$Text' not found in $Path" }
}

function Assert-NotContainsRegex {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$Pattern,
        [Parameter(Mandatory = $true)][string]$Description
    )
    $matches = @(Get-ChildItem -LiteralPath $Path -Recurse -File | Select-String -Pattern $Pattern -CaseSensitive:$false)
    if ($matches.Count -gt 0) {
        $where = ($matches | Select-Object -First 8 | ForEach-Object { "$($_.Path):$($_.LineNumber): $($_.Line.Trim())" }) -join "`n"
        throw "Static acceptance check failed (${Description}). Unexpected matches:`n$where"
    }
}

function Resolve-DumpbinForMesaContract {
    $cmd = Get-Command dumpbin.exe -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path -LiteralPath $vswhere) {
        $installPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
        if ($installPath) {
            $candidate = Get-ChildItem -Path (Join-Path $installPath "VC\Tools\MSVC\*\bin\Hostx64\x64\dumpbin.exe") -File -ErrorAction SilentlyContinue |
                Sort-Object LastWriteTimeUtc -Descending |
                Select-Object -First 1
            if ($candidate) { return $candidate.FullName }
        }
    }
    throw "dumpbin.exe was not found; it is required to verify the Mesa WGL binding contract."
}

function Assert-MesaWglContract {
    param([string]$RequestedMesaRuntimeDir)
    $mesa = Resolve-MesaRuntimeDir -MesaRuntimeDir $RequestedMesaRuntimeDir
    $dumpbin = Resolve-DumpbinForMesaContract
    $gallium = Join-Path $mesa "libgallium_wgl.dll"
    $opengl = Join-Path $mesa "opengl32.dll"
    foreach ($dll in @($gallium, $opengl)) {
        $headers = (& $dumpbin /nologo /headers $dll 2>&1 | Out-String)
        if ($LASTEXITCODE -ne 0 -or ($headers -notmatch '(?im)\b8664\s+machine\s+\(x64\)' -and $headers -notmatch '(?im)machine[^\r\n]*x64')) {
            throw "Mesa WGL contract check failed: expected x64 PE: $dll"
        }
    }
    $galliumExports = (& $dumpbin /nologo /exports $gallium 2>&1 | Out-String)
    $openglExports = (& $dumpbin /nologo /exports $opengl 2>&1 | Out-String)
    foreach ($symbol in @('GetDC', 'SetPixelFormat', 'DrvSetPixelFormat')) {
        if ($galliumExports -notmatch ('(?m)\s' + [regex]::Escape($symbol) + '(?:\s|$)')) {
            throw "Mesa WGL contract check failed: libgallium_wgl.dll is missing $symbol"
        }
    }
    foreach ($symbol in @('wglChoosePixelFormat', 'wglGetPixelFormat', 'wglSetPixelFormat', 'wglCreateContext', 'wglMakeCurrent', 'wglSwapBuffers', 'wglGetProcAddress')) {
        if ($openglExports -notmatch ('(?m)\s' + [regex]::Escape($symbol) + '(?:\s|$)')) {
            throw "Mesa WGL contract check failed: opengl32.dll is missing $symbol"
        }
    }
    Write-Host "Mesa WGL contract validation passed: x64, direct chooser, Gallium SetPixelFormat, context and swap exports present."
}

function Invoke-CheckedScript {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][hashtable]$Arguments,
        [Parameter(Mandatory = $true)][string]$Stage
    )
    Write-Host "`n=== $Stage ==="
    & $Path @Arguments
    if (-not $?) { throw "$Stage failed" }
}

Write-Host "=== Minecraft $MinecraftVersion / SDL3 acceptance build ==="

# Source-level guardrails. These catch accidental regression before doing expensive builds.
$launchSource = Join-Path $root "MC.Xbox\launch\minecraft_launch.cpp"
$catalog = Join-Path $root "config\versions.tsv"
$fabric263 = Join-Path $root "controller_mod\fabric\src\variants\26.3"
$compat263 = Join-Path $root "compat_mod\src\variants\26.3"
$sdlPatch = Join-Path $root "patches\sdl3-uwp-external-host.patch"
$sdlLifecyclePatch = Join-Path $root "scripts\apply-sdl3-v7-wgl.ps1"
$sdlProviderParityPatch = Join-Path $root "scripts\apply-sdl3-v8-provider-parity.ps1"
$sdlStabilityPatch = Join-Path $root "scripts\apply-sdl3-v9-stability.ps1"
$sdlRenderPearlPatch = Join-Path $root "scripts\apply-sdl3-v12-renderpearl-contract.ps1"
$sdlInputPatch = Join-Path $root "scripts\apply-sdl3-v13-input.ps1"
$sdlInputV14Patch = Join-Path $root "scripts\apply-sdl3-v14-input.ps1"
$sdlInputV15Patch = Join-Path $root "scripts\apply-sdl3-v15-input-contract.ps1"
$sdlCursorV16Patch = Join-Path $root "scripts\apply-sdl3-v16-launcher-cursor.ps1"
$hostBuildScript = Join-Path $root "build.ps1"
$hostCursorRc = Join-Path $root "MC.Xbox\SDL3-WinRTResources.rc"
$hostCursorCur = Join-Path $root "MC.Xbox\Assets\SDL3-WinRTResource_BlankCursor.cur"
$hostLauncherCursorCur = Join-Path $root "MC.Xbox\Assets\BanditLauncherCursor.cur"
$runtimeManagerSource = Join-Path $root "MC.Xbox\launch\runtime_manager.cpp"

Assert-ContainsLiteral $launchSource "org.lwjgl.sdl.libname" "26.3 direct SDL library override"
Assert-ContainsLiteral $launchSource "org.lwjgl.glfw.libname" "legacy GLFW library override"
Assert-ContainsLiteral $launchSource 'windowBackend == WindowBackend::Sdl3' "capability-selected SDL/GLFW launch backend"
Assert-ContainsLiteral $runtimeManagerSource 'ResolveWindowBackend' "manifest-driven window backend resolver"
Assert-ContainsLiteral $runtimeManagerSource 'lwjgl-sdl' "LWJGL SDL capability detection"
Assert-ContainsLiteral $runtimeManagerSource 'lwjgl-glfw' "LWJGL GLFW capability detection"
Assert-ContainsLiteral $launchSource "SDL_VIDEO_DRIVER" "WinRT SDL video driver selection"
Assert-ContainsLiteral $launchSource 'SetEnvironmentVariableW(L"SDL_JOYSTICK_WGI", L"1")' "SDL3 launch path forces Xbox WGI controller backend"
Assert-ContainsLiteral $launchSource "SDL_OPENGL_LIBRARY" "Mesa OpenGL selection for SDL"
Assert-ContainsLiteral $launchSource 'SetEnvironmentVariableW(L"SDL_OPENGL_LIBRARY", L"graphics\\mesa\\opengl32.dll")' "package-relative Mesa module for WinRT LoadPackagedLibrary"
Assert-ContainsLiteral $sdlPatch "BanditSDL_ExternalHostPatchVersion" "patched-SDL runtime marker"
Assert-ContainsLiteral $sdlLifecyclePatch "BanditSDL_WinRTIntegrationPatchV7" "patched-SDL V7 WinRT/WGL integration marker"
Assert-ContainsLiteral $sdlProviderParityPatch "BanditSDL_WinRTProviderParityV2" "patched-SDL V8 provider-parity marker"
Assert-ContainsLiteral $sdlStabilityPatch "BanditSDL_WinRTStabilityV9" "patched-SDL V9 stability marker"
Assert-ContainsLiteral $sdlRenderPearlPatch "BanditSDL_WinRTRenderPearlV12" "patched-SDL V12 RenderPearl/Xbox contract marker"
Assert-ContainsLiteral $sdlInputPatch "BanditSDL_WinRTInputBridgeV13" "patched-SDL V13 mouse/input bridge marker"
Assert-ContainsLiteral $sdlInputPatch "PointerCursor=nullptr suppresses CoreWindow PointerMoved events" "V13 safe cursor fallback rationale"
Assert-ContainsLiteral $sdlInputPatch "MouseDevice relative-motion source attached" "V13 relative mouse bridge diagnostic"
Assert-ContainsLiteral $sdlInputV14Patch "BanditSDL_WinRTInputBridgeV14" "patched-SDL V14 controller/input marker"
Assert-ContainsLiteral $sdlInputV14Patch 'SDL_GetHintBoolean(SDL_HINT_JOYSTICK_WGI, true)' "WinRT WGI controller backend enabled by default"
Assert-ContainsLiteral $sdlInputV14Patch "WGI initialization complete controller_count=%d" "WGI enumeration diagnostic"
Assert-ContainsLiteral $sdlInputV14Patch "WGI joystick opened" "WGI open diagnostic"
Assert-ContainsLiteral $sdlInputV14Patch "CoreWindow relative-mouse fallback" "physical-mouse fallback when raw MouseDevice is unavailable"
Assert-ContainsLiteral $sdlInputV14Patch "WINRT_BanditHasRawMouseDevice" "raw-vs-pointer relative mouse source tracking"
Assert-ContainsLiteral $sdlInputV15Patch "BanditSDL_WinRTInputContractV15" "patched-SDL V15 input-contract marker"
Assert-ContainsLiteral $sdlInputV15Patch "SDL_SendKeyboardKey(0, SDL_DEFAULT_KEYBOARD_ID, rawcode, code, down)" "SDL3 bool-down keyboard contract"
Assert-ContainsLiteral $sdlInputV15Patch "SDL_SendMouseButton(0, window, SDL_DEFAULT_MOUSE_ID, button, true)" "SDL3 mouse press button/down order"
Assert-ContainsLiteral $sdlInputV15Patch "SDL_SendMouseButton(0, window, SDL_DEFAULT_MOUSE_ID, button, false)" "SDL3 mouse release button/down order"
Assert-ContainsLiteral $sdlInputV15Patch "button == 0" "invalid WinRT mouse-button guard"
Assert-ContainsLiteral $sdlInputV15Patch "window->KeyDown" "hardware-proven CoreWindow KeyDown source"
Assert-ContainsLiteral $sdlInputV15Patch "window->KeyUp" "hardware-proven CoreWindow KeyUp source"
Assert-ContainsLiteral $sdlInputV15Patch "PointerCaptureLost" "WinRT pointer-capture loss cleanup"
Assert-ContainsLiteral $sdlInputV15Patch "WINRT_ResetMouseButtons" "mouse button release cleanup"
Assert-ContainsLiteral $sdlInputV15Patch "SDL_SetKeyboardFocus(NULL)" "focus-loss keyboard reset path"
Assert-ContainsLiteral $sdlInputV15Patch "SetPointerCapture()" "gameplay CoreWindow pointer capture"
Assert-ContainsLiteral $sdlInputV15Patch "ReleasePointerCapture()" "menu CoreWindow pointer release"
Assert-ContainsLiteral $sdlInputV15Patch "transparent WinRT cursor resource 5000 active" "hidden-cursor runtime proof"
Assert-ContainsLiteral $sdlCursorV16Patch "BanditSDL_WinRTLauncherCursorV16" "patched-SDL V16 launcher cursor marker"
Assert-ContainsLiteral $sdlCursorV16Patch "cursorResourceID = 5001" "launcher cursor resource id forwarding"
Assert-ContainsLiteral $sdlCursorV16Patch "CoreCursorType::Custom" "launcher visible cursor uses custom WinRT resource"
Assert-ContainsLiteral $sdlCursorV16Patch "default visible cursor uses launcher resource 5001" "launcher cursor runtime proof"
if (-not (Test-Path -LiteralPath $hostCursorCur -PathType Leaf)) { throw "V15 transparent WinRT cursor asset missing: $hostCursorCur" }
if ((Get-Item -LiteralPath $hostCursorCur).Length -le 0) { throw "V15 transparent WinRT cursor asset is empty: $hostCursorCur" }
if (-not (Test-Path -LiteralPath $hostLauncherCursorCur -PathType Leaf)) { throw "V16 launcher WinRT cursor asset missing: $hostLauncherCursorCur" }
if ((Get-Item -LiteralPath $hostLauncherCursorCur).Length -le 0) { throw "V16 launcher WinRT cursor asset is empty: $hostLauncherCursorCur" }
Assert-ContainsLiteral $hostCursorRc '5000 CURSOR "Assets\\SDL3-WinRTResource_BlankCursor.cur"' "resource ID 5000 transparent cursor declaration"
Assert-ContainsLiteral $hostCursorRc '5001 CURSOR "Assets\\BanditLauncherCursor.cur"' "resource ID 5001 launcher cursor declaration"
Assert-ContainsLiteral $hostBuildScript '$rcExe' "Windows SDK resource compiler resolution"
Assert-ContainsLiteral $hostBuildScript 'SDL3-WinRTResources.rc' "WinRT transparent cursor resource build"
Assert-ContainsLiteral $hostBuildScript '"$mcResourceRes"' "WinRT cursor .res linked into MC.Xbox.exe"
Assert-ContainsLiteral $sdlLifecyclePatch "BanditSDL_WinRTWGLBackendV1" "compile-time WGL backend marker retained in V7"
Assert-ContainsLiteral $sdlLifecyclePatch "BanditSDL_WinRTSafeWGLInitV1" "safe real-window WGL lifecycle marker"
Assert-ContainsLiteral $sdlLifecyclePatch "BanditSDL_WinRTMultiWindowV1" "logical WinRT multiwindow marker"
Assert-ContainsLiteral $sdlPatch "BanditSDL_WinRTAbsoluteLoadFixV1" "absolute-to-package-relative SDL loader marker"
Assert-ContainsLiteral $sdlPatch 'SDL_wcsstr(wstr, L"\\graphics\\mesa\\")' "absolute Mesa path normalization logic"
Assert-ContainsLiteral $sdlPatch "SDL_SetMainReady();" "external-host SDL_main readiness bridge"
Assert-ContainsLiteral $sdlPatch "external host marked SDL main ready" "SDL_main readiness diagnostic"
Assert-ContainsLiteral $sdlPatch 'src/loadso/windows/*.c' "WinRT-safe SDL shared-object loader build"
Assert-ContainsLiteral $sdlPatch 'src/video/windows/SDL_windowsopengl.c' "WinRT WGL implementation included in WindowsStore build"
Assert-ContainsLiteral $sdlPatch "Bandit SDL: WinRT GL backend=WGL" "WinRT WGL backend diagnostic"
Assert-ContainsLiteral $sdlLifecyclePatch "deferred WinRT WGL extension probe until a real context is current" "no pre-window WGL context probe"
Assert-ContainsLiteral $sdlLifecyclePatch "WIN_GL_SetupWindow(_this, window)" "real WinRT window pixel-format setup"
Assert-ContainsLiteral $sdlLifecyclePatch "initialized WGL extensions from real WinRT context" "real-context WGL extension initialization"
Assert-ContainsLiteral $sdlLifecyclePatch "logical WinRT window created" "Minecraft 26.3 logical multiwindow support"
Assert-ContainsLiteral $sdlLifecyclePatch "BANDIT_WIN_GL_CreateContextSafe" "native WGL SEH diagnostics"
Assert-ContainsLiteral $sdlLifecyclePatch "extern bool WIN_SetError" "WinRT WGL bool error propagation"
Assert-ContainsLiteral $sdlProviderParityPatch "_this->gl_data->wglGetProcAddress(proc)" "LWJGL-compatible WGL-first function lookup"
Assert-ContainsLiteral $sdlProviderParityPatch "GetProcAddress((HMODULE)_this->gl_config.dll_handle, proc)" "LWJGL-compatible DLL-export fallback"
Assert-ContainsLiteral $sdlProviderParityPatch "glGetError LWJGL-order pointer" "runtime glGetError provider-parity diagnostic"
Assert-ContainsLiteral $sdlStabilityPatch "BANDIT_WIN_GL_GetProcAddressSafe" "guarded Mesa WGL proc resolver"
Assert-ContainsLiteral $sdlStabilityPatch "sdl_wgl_stage.log" "persistent native WGL stage breadcrumbs"
Assert-ContainsLiteral $sdlStabilityPatch "wglExtensions:minimal:ok" "minimal post-context WGL extension path"
Assert-ContainsLiteral $sdlRenderPearlPatch "BanditWinRTPixelFormatConfigured" "deterministic one-time CoreWindow pixel-format state"
Assert-ContainsLiteral $sdlRenderPearlPatch "GalliumSetPixelFormat:ok" "proven MesaTestApp Gallium SetPixelFormat path"
Assert-ContainsLiteral $sdlRenderPearlPatch 'BANDIT_WIN_GL_ChoosePixelFormatProven' "guarded MesaTestApp direct wglChoosePixelFormat path"
Assert-ContainsLiteral $sdlRenderPearlPatch "pfd.cColorBits = 32" "MesaTestApp-proven Xbox drawable color format"
Assert-ContainsLiteral $sdlRenderPearlPatch "pfd.cDepthBits = 24" "MesaTestApp-proven Xbox drawable depth format"
Assert-ContainsLiteral $sdlRenderPearlPatch "pfd.cStencilBits = 8" "MesaTestApp-proven Xbox drawable stencil format"
Assert-ContainsLiteral $sdlRenderPearlPatch "Do not call wglGetPixelFormat here" "guard against Mesa wglGetPixelFormat framebuffer side effect"
Assert-ContainsLiteral $sdlRenderPearlPatch "GL_ActualContext:" "actual OpenGL runtime context validation"
Assert-ContainsLiteral $sdlRenderPearlPatch "wglMakeCurrent:clear-null-null:ok" "MesaTestApp-style null/null context unbind"
Assert-ContainsLiteral $sdlRenderPearlPatch "synthetic Xbox SetWindowPosition acknowledged" "RenderPearl final-window position contract"
Assert-ContainsLiteral $sdlRenderPearlPatch "wglSwapBuffers:first-present-ok" "first successful present breadcrumb"
Assert-ContainsLiteral $launchSource "PreloadMesaGraphicsStackForSdl" "Mesa support stack preload before JVM/LWJGL"
Assert-ContainsLiteral $launchSource 'L"dxil.dll"' "DXIL signer preload"
Assert-ContainsLiteral $launchSource 'L"graphics\\mesa\\opengl32.dll"' "exact Mesa OpenGL provider preload"
Assert-ContainsLiteral (Join-Path $root "MC.Xbox\App.cpp") 'SetEnvironmentVariableW(L"mesa_glthread", L"false")' "stability-first Mesa threading default"
Assert-ContainsLiteral (Join-Path $root "MC.Xbox\App.cpp") 'SetEnvironmentVariableW(L"SDL_JOYSTICK_WGI", L"1")' "launcher forces Xbox WGI controller backend"
Assert-ContainsLiteral (Join-Path $root "MC.Xbox\App.cpp") 'SetEnvironmentVariableW(L"SDL_JOYSTICK_ALLOW_BACKGROUND_EVENTS", L"1")' "launcher allows WGI controller events"
Assert-ContainsLiteral (Join-Path $fabric263 "banditvault\fabriccontroller\BanditControllerCompat.java") 'SDLHints.SDL_SetHint("SDL_JOYSTICK_WGI", "1")' "26.3 Fabric controller enables WGI before SDL gamepad init"
Assert-ContainsLiteral $sdlPatch "attached SDL event sink to external CoreWindow host" "external CoreWindow attachment"
Assert-ContainsLiteral $sdlLifecyclePatch "do not reject a second logical SDL window" "remove upstream WinRT one-window restriction"
$sdlPrepare = Join-Path $root "scripts\prepare-sdl3-uwp.ps1"
$glfwWglReference = Join-Path $root "glfw_shim\wgl_backend.h"
Assert-ContainsLiteral $glfwWglReference 'GetProcAddress(s_gallium, "SetPixelFormat")' "MesaTestApp-proven Gallium SetPixelFormat reference"
Assert-ContainsLiteral $glfwWglReference 'p_choosepf(s_dc, &pfd)' "MesaTestApp-proven direct pixel-format chooser reference"
Assert-ContainsLiteral $sdlPrepare '$renderPearlPatchScript' "V12 active SDL patch chain"
Assert-ContainsLiteral $sdlPrepare '$inputPatchScript' "V13 active SDL input patch chain"
Assert-ContainsLiteral $sdlPrepare '$inputV14PatchScript' "V14 active WGI/input patch chain"
Assert-ContainsLiteral $sdlPrepare '$inputV15PatchScript' "V15 active input-contract patch chain"
Assert-ContainsLiteral $sdlPrepare '$cursorV16PatchScript' "V16 active launcher-cursor patch chain"
$sdlCursorV17Patch = Join-Path $root "scripts\apply-sdl3-v17-software-cursor.ps1"
Assert-ContainsLiteral $sdlCursorV17Patch "BanditSDL_WinRTSoftwareCursorV17" "V17 SDL new cursor marker"
Assert-ContainsLiteral $sdlCursorV17Patch "coreWindow->PointerCursor = nullptr" "V17 system pointer suppressed"
Assert-ContainsLiteral $sdlCursorV17Patch "SDL_SendMouseMotion(0, window, SDL_DEFAULT_MOUSE_ID, false, x, y)" "V17 raw SDL menu cursor bridge"
Assert-ContainsLiteral $sdlPrepare '$cursorV17PatchScript' "V17 active software cursor patch chain"

Assert-ContainsLiteral $sdlPrepare '-DSDL_JOYSTICK=ON' "SDL joystick subsystem explicitly enabled"
Assert-ContainsLiteral $sdlPrepare '-DHAVE_WINDOWS_GAMING_INPUT_H=1' "Windows.Gaming.Input compile capability forced for UWP"
if ([System.IO.File]::ReadAllText($sdlPrepare).Contains('$singleContextPatchScript')) { throw "Static acceptance check failed: V10 single-context patch is still active in SDL preparer" }
Assert-ContainsLiteral $sdlPrepare "-DSDL_OPENGL=ON" "SDL desktop OpenGL enabled for Mesa/WGL"
Assert-ContainsLiteral $sdlPrepare "-DSDL_OPENGLES=OFF" "SDL EGL/OpenGL ES disabled for 26.3 WinRT path"
$normalBuild = Join-Path $root "scripts\build-local-263.ps1"
$multiBuild = Join-Path $root "scripts\build-local-multiversion.ps1"
Assert-ContainsLiteral $normalBuild "build-local-multiversion.ps1" "normal local build delegates to full launcher catalog"
Assert-ContainsLiteral $multiBuild "config/versions.tsv" "full launcher packages complete version catalog"
Assert-NotContainsRegex $normalBuild 'versions-local-263\.tsv' "normal local build must not package one-version catalog"
Assert-NotContainsRegex $fabric263 '\bGLFW\b|org\.lwjgl\.glfw' "26.3 Fabric controller must not depend on GLFW"
if (-not (Test-Path -LiteralPath $compat263 -PathType Container)) { throw "Dedicated 26.3 compatibility overlay is missing: $compat263" }

$catalogRows = @(Import-Csv $catalog -Delimiter "`t" | Where-Object { $_.minecraftVersion -eq $MinecraftVersion })
foreach ($required in @(
    @{ Loader = 'fabric'; Version = $FabricLoader; Controller = 'fabric' },
    @{ Loader = 'forge'; Version = $ForgeLoader; Controller = 'none' },
    @{ Loader = 'neoforge'; Version = $NeoForgeLoader; Controller = 'none' }
)) {
    $row = $catalogRows | Where-Object { $_.loader -eq $required.Loader -and $_.loaderVersion -eq $required.Version } | Select-Object -First 1
    if (-not $row) { throw "Catalog is missing $MinecraftVersion $($required.Loader) $($required.Version)" }
    if ($row.controllerProvider -ne $required.Controller) {
        throw "Catalog controller availability mismatch for $MinecraftVersion $($required.Loader): expected $($required.Controller), got $($row.controllerProvider)"
    }
}

Assert-MesaWglContract -RequestedMesaRuntimeDir $MesaRuntimeDir

# Build/validate the exact patched UWP SDL runtime. The preparer itself validates x64,
# required exports and the Bandit external-host marker with dumpbin.
$sdlArgs = @{ OutputPath = $sdlOut }
if ($SdlSourceDll) { $sdlArgs.SourceDll = $SdlSourceDll }
Invoke-CheckedScript (Join-Path $root "scripts\prepare-sdl3-uwp.ps1") $sdlArgs "Build and validate patched SDL3-UWP"

# Prepare the real 26.3 Fabric client/mappings used by both Java mod compilers.
if (-not $SkipSetup) { Invoke-CheckedScript (Join-Path $root "scripts\setup.ps1") @{
    MinecraftVersion = $MinecraftVersion
    FabricLoaderVersion = $FabricLoader
} "Prepare $MinecraftVersion Fabric cache" }

# Compare the patched runtime against Mojang/LWJGL's matching desktop SDL native.
# LWJGL resolves SDL symbols lazily per generated binding class, so a small curated
# list is not enough to establish binary compatibility. Requiring the patched UWP
# DLL to export every SDL_* symbol exported by the exact 26.3 native gives us a
# conservative ABI parity check before packaging.
function Get-PeExportSet {
    param(
        [Parameter(Mandatory = $true)][string]$Dumpbin,
        [Parameter(Mandatory = $true)][string]$Path
    )
    $text = (& $Dumpbin /nologo /exports $Path 2>&1 | Out-String)
    if ($LASTEXITCODE -ne 0) { throw "dumpbin /exports failed for $Path" }
    $exports = @()
    foreach ($line in ($text -split "`r?`n")) {
        if ($line -match '^\s+\d+\s+[0-9A-Fa-f]+\s+[0-9A-Fa-f]+\s+(\S+)') {
            $exports += $Matches[1]
        }
    }
    return @($exports | Sort-Object -Unique)
}

$gameDir = Get-ConfigPath "GameDir"
$lwjglSdlNativeJar = Join-Path $gameDir "libraries\org\lwjgl\lwjgl-sdl\3.4.3\lwjgl-sdl-3.4.3-natives-windows.jar"
if (-not (Test-Path -LiteralPath $lwjglSdlNativeJar)) {
    throw "Matching 26.3 LWJGL SDL native jar is missing after setup: $lwjglSdlNativeJar"
}
$desktopSdl = Join-Path $verifyRoot "reference-desktop-SDL3.dll"
Add-Type -AssemblyName System.IO.Compression.FileSystem
$zip = [System.IO.Compression.ZipFile]::OpenRead($lwjglSdlNativeJar)
try {
    $entry = $zip.Entries | Where-Object { $_.Name -ieq 'SDL3.dll' } | Select-Object -First 1
    if (-not $entry) { throw "SDL3.dll was not found in $lwjglSdlNativeJar" }
    [System.IO.Compression.ZipFileExtensions]::ExtractToFile($entry, $desktopSdl, $true)
} finally {
    $zip.Dispose()
}
$vsTools = Resolve-VSTools
$dumpbin = Join-Path $vsTools.MsvcRoot "bin\Hostx64\x64\dumpbin.exe"
if (-not (Test-Path -LiteralPath $dumpbin)) { throw "dumpbin.exe not found at $dumpbin" }
$referenceExports = Get-PeExportSet -Dumpbin $dumpbin -Path $desktopSdl
$uwpExports = Get-PeExportSet -Dumpbin $dumpbin -Path $sdlOut
# SDL disables its DLL-replacement trampoline on WinRT (SDL_dynapi.h).
# It is not a public LWJGL SDL binding; all public SDL exports remain required.
$missingSdlExports = @($referenceExports | Where-Object { $_ -like 'SDL_*' -and $_ -ne 'SDL_DYNAPI_entry' -and $uwpExports -notcontains $_ } | Sort-Object)
if ($missingSdlExports.Count -gt 0) {
    throw "Patched UWP SDL3 is not ABI-compatible with 26.3 lwjgl-sdl 3.4.3. Missing $($missingSdlExports.Count) SDL export(s): $($missingSdlExports -join ', ')"
}
Write-Host "SDL3 ABI parity passed: patched UWP runtime exports every SDL_* symbol in the 26.3 LWJGL desktop reference runtime."

# Compile dedicated 26.3 overlays; these are not aliases to 26.2.
Invoke-CheckedScript (Join-Path $root "compat_mod\build_compat_mod.ps1") @{
    MinecraftVersion = $MinecraftVersion
    LoaderVersion = $FabricLoader
    OutputDir = (Join-Path $modRoot "compat")
} "Compile $MinecraftVersion compatibility mod"

Invoke-CheckedScript (Join-Path $root "controller_mod\fabric\build_fabric_controller_mod.ps1") @{
    MinecraftVersion = $MinecraftVersion
    LoaderVersion = $FabricLoader
    OutputDir = (Join-Path $modRoot "fabric-controller")
} "Compile $MinecraftVersion SDL3 Fabric controller"

# Generate the exact downloader manifests for all three advertised 26.3 loader targets.
$manifestScript = Join-Path $root "scripts\new-download-manifest.ps1"
foreach ($target in @(
    @{ Loader = 'fabric'; Version = $FabricLoader },
    @{ Loader = 'forge'; Version = $ForgeLoader },
    @{ Loader = 'neoforge'; Version = $NeoForgeLoader }
)) {
    if ($FabricOnly -and $target.Loader -ne 'fabric') { continue }
    $id = "$MinecraftVersion-$($target.Loader)-$($target.Version)"
    Invoke-CheckedScript $manifestScript @{
        MinecraftVersion = $MinecraftVersion
        Loader = $target.Loader
        LoaderVersion = $target.Version
        OutputPath = (Join-Path $manifestRoot "$id.tsv")
    } "Generate $id download manifest"
}

if ($SkipPackage) {
    Write-Host "`nTargeted 26.3 compile/manifests passed. Full signed APPX build skipped by request."
    exit 0
}

# Full package acceptance. build.ps1 compiles the UWP launcher/shim, stages the
# complete catalog, builds/version-bundles compatibility/controller mods, packs,
# signs and verifies the APPX. StrictTargets rejects silent target omissions.
$oldSdlOverride = $env:SDL3_UWP_DLL
$oldCatalog = $env:VERSION_CATALOG
$oldDefault = $env:DEFAULT_MC_VERSION
try {
    $env:SDL3_UWP_DLL = $sdlOut
    $env:DEFAULT_MC_VERSION = $MinecraftVersion
    if ($FabricOnly) {
        $focusedCatalog = Join-Path $verifyRoot 'versions-fabric.tsv'
        Import-Csv $catalog -Delimiter "`t" | Where-Object {
            $_.minecraftVersion -eq $MinecraftVersion -and $_.loader -eq 'fabric' -and $_.loaderVersion -eq $FabricLoader
        } | ConvertTo-Csv -Delimiter "`t" -NoTypeInformation -UseQuotes Never | Set-Content $focusedCatalog
        $env:VERSION_CATALOG = [IO.Path]::GetRelativePath($root, $focusedCatalog)
    }
    $assetLine = @(Get-Content (Join-Path $manifestRoot "$MinecraftVersion-fabric-$FabricLoader.tsv") | Where-Object { $_ -match '^# assetIndex\t' })
    if ($assetLine.Count -ne 1) { throw 'Expected one asset index in the Fabric manifest' }
    $buildArgs = @{
        McVersion = $MinecraftVersion
        FabricLoader = $FabricLoader
        AssetIndex = ($assetLine[0] -split "`t")[1].Trim()
        LocalSigning = $LocalSigning
        StageOnly = $StageOnly
        StrictTargets = $true
    }
    if ($MesaRuntimeDir) { $buildArgs.MesaRuntimeDir = $MesaRuntimeDir }
    if ($KeepStaging) { $buildArgs.KeepStaging = $true }

    Invoke-CheckedScript (Join-Path $root "build.ps1") $buildArgs "Build, sign and validate APPX"
} finally {
    $env:SDL3_UWP_DLL = $oldSdlOverride
    $env:VERSION_CATALOG = $oldCatalog
    $env:DEFAULT_MC_VERSION = $oldDefault
}

if ($StageOnly) { Write-Host 'PASS: compilation and staged package checks. Packing and signing still required.'; return }
Write-Host "`nPASS: $MinecraftVersion direct-SDL3 source, mods, manifests, launcher/package signing and package contents completed successfully."
Write-Host "Runtime-only acceptance still requires an Xbox: presentation, relative mouse/cursor behavior, text/OSK, suspend/resume, relaunch, and separate Fabric/Forge/NeoForge launches."
