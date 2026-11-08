param(
    [Parameter(Mandatory = $true)][string]$OutputPath,
    [string]$SourceDll = $env:SDL3_UWP_DLL,
    [string]$SourceCommit = "b3852a0076c358f65ee19765e85ef9eded6e50df",
    [string]$SourceUrl = ""
)

$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue"

if (-not $SourceUrl) {
    $SourceUrl = "https://github.com/XboxEmulationHub/SDL3-uwp/archive/$SourceCommit.zip"
}

. (Join-Path $PSScriptRoot "common.ps1")

$root = Resolve-RepoRoot
$cacheRoot = Join-Path (Get-ConfigPath "CacheDir") "sdl3-uwp"
$shortCommit = $SourceCommit.Substring(0, [Math]::Min(12, $SourceCommit.Length))
$archivePath = Join-Path $cacheRoot "SDL3-uwp-$shortCommit.zip"
$patchPath = Join-Path $root "patches\sdl3-uwp-external-host.patch"
$lifecyclePatchScript = Join-Path $root "scripts\apply-sdl3-v7-wgl.ps1"
$providerParityPatchScript = Join-Path $root "scripts\apply-sdl3-v8-provider-parity.ps1"
$stabilityPatchScript = Join-Path $root "scripts\apply-sdl3-v9-stability.ps1"
$renderPearlPatchScript = Join-Path $root "scripts\apply-sdl3-v12-renderpearl-contract.ps1"
$inputPatchScript = Join-Path $root "scripts\apply-sdl3-v13-input.ps1"
$inputV14PatchScript = Join-Path $root "scripts\apply-sdl3-v14-input.ps1"
$inputV15PatchScript = Join-Path $root "scripts\apply-sdl3-v15-input-contract.ps1"
$cursorV16PatchScript = Join-Path $root "scripts\apply-sdl3-v16-launcher-cursor.ps1"
$cursorV17PatchScript = Join-Path $root "scripts\apply-sdl3-v17-software-cursor.ps1"
if (-not (Test-Path -LiteralPath $patchPath)) {
    throw "SDL external-host patch is missing: $patchPath"
}
if (-not (Test-Path -LiteralPath $lifecyclePatchScript)) {
    throw "SDL V7 WinRT/WGL lifecycle patcher is missing: $lifecyclePatchScript"
}
if (-not (Test-Path -LiteralPath $providerParityPatchScript)) {
    throw "SDL V8 provider-parity patcher is missing: $providerParityPatchScript"
}
if (-not (Test-Path -LiteralPath $stabilityPatchScript)) {
    throw "SDL V9 stability patcher is missing: $stabilityPatchScript"
}
if (-not (Test-Path -LiteralPath $renderPearlPatchScript)) {
    throw "SDL V12 RenderPearl-contract patcher is missing: $renderPearlPatchScript"
}
if (-not (Test-Path -LiteralPath $inputPatchScript)) {
    throw "SDL V13 input-bridge patcher is missing: $inputPatchScript"
}
if (-not (Test-Path -LiteralPath $inputV14PatchScript)) {
    throw "SDL V14 WGI/input patcher is missing: $inputV14PatchScript"
}
if (-not (Test-Path -LiteralPath $inputV15PatchScript)) {
    throw "SDL V15 input-contract patcher is missing: $inputV15PatchScript"
}
if (-not (Test-Path -LiteralPath $cursorV16PatchScript)) {
    throw "SDL V16 launcher-cursor patcher is missing: $cursorV16PatchScript"
}
if (-not (Test-Path -LiteralPath $cursorV17PatchScript)) {
    throw "SDL V17 software-cursor patcher is missing: $cursorV17PatchScript"
}

# The cache identity is content-addressed. A patch edit, script edit, source commit
# change or CMake option change must never reuse an older SDL3.dll.
$cmakeOptions = @(
    "CMAKE_SYSTEM_NAME=WindowsStore",
    "CMAKE_SYSTEM_VERSION=10.0",
    "CMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY",
    "SDL_SHARED=ON",
    "SDL_STATIC=OFF",
    "SDL_TESTS=OFF",
    "SDL_EXAMPLES=OFF",
    "SDL_OPENGL=ON",
    "SDL_OPENGLES=OFF",
    "SDL_JOYSTICK=ON",
    "HAVE_WINDOWS_GAMING_INPUT_H=1",
    "ARCH=x64",
    "CONFIG=Release"
)
$hashInputs = @(
    "source=$SourceCommit",
    "options=$($cmakeOptions -join ';')",
    "patch=$((Get-FileHash -Algorithm SHA256 -LiteralPath $patchPath).Hash)",
    "lifecyclePatch=$((Get-FileHash -Algorithm SHA256 -LiteralPath $lifecyclePatchScript).Hash)",
    "providerParityPatch=$((Get-FileHash -Algorithm SHA256 -LiteralPath $providerParityPatchScript).Hash)",
    "stabilityPatch=$((Get-FileHash -Algorithm SHA256 -LiteralPath $stabilityPatchScript).Hash)",
    "renderPearlPatch=$((Get-FileHash -Algorithm SHA256 -LiteralPath $renderPearlPatchScript).Hash)",
    "inputPatch=$((Get-FileHash -Algorithm SHA256 -LiteralPath $inputPatchScript).Hash)",
    "inputV14Patch=$((Get-FileHash -Algorithm SHA256 -LiteralPath $inputV14PatchScript).Hash)",
    "inputV15Patch=$((Get-FileHash -Algorithm SHA256 -LiteralPath $inputV15PatchScript).Hash)",
    "cursorV16Patch=$((Get-FileHash -Algorithm SHA256 -LiteralPath $cursorV16PatchScript).Hash)",
    "cursorV17Patch=$((Get-FileHash -Algorithm SHA256 -LiteralPath $cursorV17PatchScript).Hash)",
    "script=$((Get-FileHash -Algorithm SHA256 -LiteralPath $PSCommandPath).Hash)"
) -join "`n"
$sha = [System.Security.Cryptography.SHA256]::Create()
try {
    $cacheKeyBytes = [System.Text.Encoding]::UTF8.GetBytes($hashInputs)
    $cacheKey = ([BitConverter]::ToString($sha.ComputeHash($cacheKeyBytes))).Replace("-", "").ToLowerInvariant().Substring(0, 20)
} finally {
    $sha.Dispose()
}

$sourceRoot = Join-Path $cacheRoot "source-$shortCommit-$cacheKey"
$buildRoot = Join-Path $cacheRoot "build-$shortCommit-$cacheKey"
$patchStamp = Join-Path $sourceRoot ".bandit-external-host-patch"
$runtimeCacheDir = Join-Path $cacheRoot "runtime-$shortCommit-$cacheKey"
$runtimeCacheDll = Join-Path $runtimeCacheDir "SDL3.dll"

function Resolve-DumpbinExe {
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
    throw "dumpbin.exe was not found. Visual Studio C++ x64 tools are required to validate SDL3.dll."
}

function Assert-SdlRuntimeCompatibility {
    param([Parameter(Mandatory = $true)][string]$Path)

    if (-not (Test-Path -LiteralPath $Path)) {
        throw "SDL3 UWP runtime not found: $Path"
    }

    $dumpbin = Resolve-DumpbinExe
    $headers = (& $dumpbin /nologo /headers $Path 2>&1 | Out-String)
    if ($LASTEXITCODE -ne 0) {
        throw "dumpbin could not inspect SDL3 runtime headers: $Path"
    }
    if ($headers -notmatch '(?im)\b8664\s+machine\s+\(x64\)' -and $headers -notmatch '(?im)machine[^\r\n]*x64') {
        throw "SDL3 runtime is not an x64 PE image: $Path"
    }

    $exports = (& $dumpbin /nologo /exports $Path 2>&1 | Out-String)
    if ($LASTEXITCODE -ne 0) {
        throw "dumpbin could not inspect SDL3 runtime exports: $Path"
    }

    # Marker proves this is Bandit's external-IFrameworkView/CoreWindow build,
    # not a stock desktop/UWP SDL DLL supplied through SDL3_UWP_DLL.
    $requiredExports = @(
        'BanditSDL_ExternalHostPatchVersion',
        'BanditSDL_WinRTIntegrationPatchV7',
        'BanditSDL_WinRTProviderParityV2',
        'BanditSDL_WinRTStabilityV9',
        'BanditSDL_WinRTRenderPearlV12',
        'BanditSDL_WinRTInputBridgeV13',
        'BanditSDL_WinRTInputBridgeV14',
        'BanditSDL_WinRTInputContractV15',
        'BanditSDL_WinRTLauncherCursorV16',
        'BanditSDL_WinRTSoftwareCursorV17',
        'BanditSDL_WinRTAbsoluteLoadFixV1',
        'BanditSDL_WinRTWGLBackendV1',
        'BanditSDL_WinRTSafeWGLInitV1',
        'BanditSDL_WinRTMultiWindowV1',
        'SDL_Init',
        'SDL_InitSubSystem',
        'SDL_SetMainReady',
        'SDL_QuitSubSystem',
        'SDL_GetError',
        'SDL_SetHint',
        'SDL_CreateWindow',
        'SDL_DestroyWindow',
        'SDL_GetWindowSize',
        'SDL_GetWindowSizeInPixels',
        'SDL_PollEvent',
        'SDL_GL_CreateContext',
        'SDL_GL_DestroyContext',
        'SDL_GL_MakeCurrent',
        'SDL_GL_SwapWindow',
        'SDL_GL_GetProcAddress',
        'SDL_GL_SetSwapInterval',
        'SDL_GetKeyboardState',
        'SDL_GetModState',
        'SDL_StartTextInput',
        'SDL_StopTextInput',
        'SDL_SetTextInputArea',
        'SDL_SetWindowRelativeMouseMode',
        'SDL_GetWindowRelativeMouseMode',
        'SDL_GetMouseState',
        'SDL_GetGamepads',
        'SDL_OpenGamepad',
        'SDL_CloseGamepad',
        'SDL_GamepadConnected',
        'SDL_UpdateGamepads',
        'SDL_GetGamepadAxis',
        'SDL_GetGamepadButton',
        'SDL_GetTicksNS',
        'SDL_free'
    )

    $missing = @()
    foreach ($symbol in $requiredExports) {
        if ($exports -notmatch ('(?m)\s' + [regex]::Escape($symbol) + '(?:\s|$)')) {
            $missing += $symbol
        }
    }
    if ($missing.Count -gt 0) {
        throw "SDL3 runtime is missing required Bandit/LWJGL exports: $($missing -join ', '). Runtime: $Path"
    }

    Write-Host "SDL3 runtime validation passed: x64, external-host/V7 lifecycle + V8 provider parity + V9 stability + V12 RenderPearl contract + V13 mouse bridge + V14 WGI controller fix, logical-multiwindow and absolute-path markers present, required LWJGL SDL surface exported."
}

function Copy-SdlRuntime {
    param([Parameter(Mandatory = $true)][string]$Path)
    Assert-SdlRuntimeCompatibility -Path $Path
    $parent = Split-Path $OutputPath -Parent
    if ($parent) { Ensure-Dir $parent }
    Copy-Item -LiteralPath $Path -Destination $OutputPath -Force
    Write-Host "SDL3 UWP runtime: $Path -> $OutputPath"
}

function Resolve-CMakeExe {
    $cmd = Get-Command cmake.exe -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }

    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path -LiteralPath $vswhere) {
        $installPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
        if ($installPath) {
            $bundled = Join-Path $installPath "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
            if (Test-Path -LiteralPath $bundled) { return $bundled }
        }
    }
    throw "CMake was not found. Install the Visual Studio C++ CMake tools, or set SDL3_UWP_DLL to a prebuilt Bandit-compatible SDL3.dll."
}

function Resolve-VsGenerator {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path -LiteralPath $vswhere)) {
        throw "vswhere.exe not found. Visual Studio with C++ tools is required to build the patched UWP SDL3 runtime."
    }
    $version = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationVersion
    if (-not $version) {
        throw "Visual Studio with C++ tools was not found."
    }
    $major = [int]($version.Split('.')[0])
    switch ($major) {
        18 { return "Visual Studio 18 2026" }
        17 { return "Visual Studio 17 2022" }
        default { throw "Unsupported Visual Studio major version $major for automatic SDL3 UWP build." }
    }
}

function Expand-SdlSource {
    Ensure-Dir $cacheRoot
    if (-not (Test-Path -LiteralPath $archivePath)) {
        Write-Host "Downloading SDL3 UWP source at $SourceCommit..."
        Invoke-WebRequest -UseBasicParsing -Uri $SourceUrl -OutFile $archivePath
    }

    if (-not (Test-Path -LiteralPath $sourceRoot)) {
        $tmp = Join-Path $cacheRoot "extract-$shortCommit"
        Remove-Item -LiteralPath $tmp -Recurse -Force -ErrorAction SilentlyContinue
        Ensure-Dir $tmp
        Expand-Archive -LiteralPath $archivePath -DestinationPath $tmp -Force
        $candidate = Get-ChildItem -LiteralPath $tmp -Directory | Select-Object -First 1
        if (-not $candidate -or -not (Test-Path -LiteralPath (Join-Path $candidate.FullName "CMakeLists.txt"))) {
            throw "SDL3 UWP source archive has an unexpected layout: $archivePath"
        }
        Move-Item -LiteralPath $candidate.FullName -Destination $sourceRoot
        Remove-Item -LiteralPath $tmp -Recurse -Force -ErrorAction SilentlyContinue
    }
}

function Patch-ExternalHostSupport {
    $videoPath = Join-Path $sourceRoot "src\video\winrt\SDL_winrtvideo.cpp"
    if (-not (Test-Path -LiteralPath $videoPath)) {
        throw "SDL WinRT video source missing: $videoPath"
    }

    $expectedStamp = $hashInputs
    if ((Test-Path -LiteralPath $patchStamp) -and ([System.IO.File]::ReadAllText($patchStamp) -eq $expectedStamp)) {
        return
    }

    $git = Get-Command git.exe -ErrorAction SilentlyContinue
    if (-not $git) { $git = Get-Command git -ErrorAction SilentlyContinue }
    if (-not $git) {
        throw "Git is required to apply the pinned SDL external-host patch. Install Git for Windows."
    }

    Push-Location $sourceRoot
    try {
        & $git.Source apply --check --whitespace=nowarn $patchPath
        if ($LASTEXITCODE -ne 0) {
            throw "Pinned SDL source no longer matches $patchPath. Refusing to build an unverified runtime."
        }
        & $git.Source apply --whitespace=nowarn $patchPath
        if ($LASTEXITCODE -ne 0) {
            throw "Failed to apply SDL external-host patch: $patchPath"
        }

        & $lifecyclePatchScript -SourceRoot $sourceRoot | Out-Host
        if (-not $?) {
            throw "Failed to apply SDL V7 WinRT/WGL lifecycle patch: $lifecyclePatchScript"
        }

        & $providerParityPatchScript -SourceRoot $sourceRoot | Out-Host
        if (-not $?) {
            throw "Failed to apply SDL V8 provider-parity patch: $providerParityPatchScript"
        }

        & $stabilityPatchScript -SourceRoot $sourceRoot | Out-Host
        if (-not $?) {
            throw "Failed to apply SDL V9 stability patch: $stabilityPatchScript"
        }

        & $renderPearlPatchScript -SourceRoot $sourceRoot | Out-Host
        if (-not $?) {
            throw "Failed to apply SDL V12 RenderPearl-contract patch: $renderPearlPatchScript"
        }

        & $inputPatchScript -SourceRoot $sourceRoot | Out-Host
        if (-not $?) {
            throw "Failed to apply SDL V13 input-bridge patch: $inputPatchScript"
        }

        & $inputV14PatchScript -SourceRoot $sourceRoot | Out-Host
        if (-not $?) {
            throw "Failed to apply SDL V14 WGI/input patch: $inputV14PatchScript"
        }

        & $inputV15PatchScript -SourceRoot $sourceRoot | Out-Host
        if (-not $?) {
            throw "Failed to apply SDL V15 input-contract patch: $inputV15PatchScript"
        }
        & $cursorV16PatchScript -SourceRoot $sourceRoot | Out-Host
        if (-not $?) {
            throw "Failed to apply SDL V16 launcher-cursor patch: $cursorV16PatchScript"
        }
        & $cursorV17PatchScript -SourceRoot $sourceRoot | Out-Host
        if (-not $?) {
            throw "Failed to apply SDL V17 software-cursor patch: $cursorV17PatchScript"
        }
    } finally {
        Pop-Location
    }

    [System.IO.File]::WriteAllText($patchStamp, $expectedStamp, (New-Object System.Text.UTF8Encoding($false)))
    Write-Host "Patched SDL WinRT backend for external CoreWindow hosting + V7 lifecycle/multiwindow + V8 provider parity + V9 stability + V12 RenderPearl contract + V13 mouse bridge + V14 WGI controller fix + V15 SDL3 input contract fix + V16 launcher cursor + V17 hidden OS/software GUI cursor (cache key $cacheKey)."
}

function Build-SdlRuntime {
    $cmake = Resolve-CMakeExe
    $generator = Resolve-VsGenerator
    Remove-Item -LiteralPath $buildRoot -Recurse -Force -ErrorAction SilentlyContinue
    Ensure-Dir $buildRoot

    Write-Host "Configuring patched SDL3 UWP runtime ($generator, cache key $cacheKey)..."
    & $cmake -S $sourceRoot -B $buildRoot `
        -G $generator -A x64 `
        -DCMAKE_SYSTEM_NAME=WindowsStore `
        "-DCMAKE_SYSTEM_VERSION=10.0" `
        -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY `
        -DSDL_SHARED=ON `
        -DSDL_STATIC=OFF `
        -DSDL_TESTS=OFF `
        -DSDL_EXAMPLES=OFF `
        -DSDL_OPENGL=ON `
        -DSDL_OPENGLES=OFF `
        -DSDL_JOYSTICK=ON `
        -DHAVE_WINDOWS_GAMING_INPUT_H=1 | Out-Host
    if ($LASTEXITCODE -ne 0) { throw "SDL3 UWP CMake configure failed with exit code $LASTEXITCODE" }

    $cmakeCache = Join-Path $buildRoot "CMakeCache.txt"
    if (-not (Test-Path -LiteralPath $cmakeCache)) { throw "SDL3 UWP CMake cache was not produced: $cmakeCache" }
    $cacheText = [System.IO.File]::ReadAllText($cmakeCache)
    if ($cacheText -notmatch '(?m)^SDL_OPENGL:BOOL=ON\r?$') { throw "SDL3 UWP configure did not enable desktop OpenGL/WGL" }
    if ($cacheText -notmatch '(?m)^SDL_OPENGLES:BOOL=OFF\r?$') { throw "SDL3 UWP configure unexpectedly enabled OpenGL ES/EGL" }
    if ($cacheText -notmatch '(?m)^SDL_JOYSTICK:BOOL=ON\r?$') { throw "SDL3 UWP configure did not enable the joystick/gamepad subsystem" }
    if ($cacheText -notmatch '(?m)^HAVE_WINDOWS_GAMING_INPUT_H(?::[^=]+)?=1\r?$') { throw "SDL3 UWP configure did not retain Windows.Gaming.Input support" }
    $generatedConfigs = @(Get-ChildItem -LiteralPath $buildRoot -Recurse -Filter 'SDL_build_config.h' -File -ErrorAction SilentlyContinue)
    $wgiConfigured = $false
    foreach ($generatedConfig in $generatedConfigs) {
        if ([System.IO.File]::ReadAllText($generatedConfig.FullName).Contains('#define SDL_JOYSTICK_WGI 1')) {
            $wgiConfigured = $true
            break
        }
    }
    if (-not $wgiConfigured) { throw "SDL3 UWP generated build config does not enable SDL_JOYSTICK_WGI" }
    Write-Host "SDL3 configure validation passed: desktop OpenGL/WGL ON, EGL OFF, joystick ON, WGI ON."

    Write-Host "Building patched SDL3 UWP runtime..."
    & $cmake --build $buildRoot --config Release --parallel 4 | Out-Host
    if ($LASTEXITCODE -ne 0) { throw "SDL3 UWP build failed with exit code $LASTEXITCODE" }

    $dll = Get-ChildItem -LiteralPath $buildRoot -Recurse -Filter SDL3.dll -File -ErrorAction SilentlyContinue |
        Where-Object { $_.FullName -match '[\\/]Release[\\/]' -or $_.DirectoryName -eq $buildRoot } |
        Sort-Object LastWriteTimeUtc -Descending |
        Select-Object -First 1
    if (-not $dll) {
        $dll = Get-ChildItem -LiteralPath $buildRoot -Recurse -Filter SDL3.dll -File -ErrorAction SilentlyContinue |
            Sort-Object LastWriteTimeUtc -Descending |
            Select-Object -First 1
    }
    if (-not $dll) { throw "Patched SDL3.dll was not produced under $buildRoot" }
    return $dll.FullName
}

# Advanced override for a runtime the developer has already built with the
# external-host WinRT patch. Stock desktop or stock SDL-UWP binaries are not
# sufficient because their event sink is never attached to Bandit's view.
if ($SourceDll) {
    Write-Host "Using SDL3_UWP_DLL override; validating architecture, Bandit V7 lifecycle + V8 provider-parity + V9 stability + V12 RenderPearl/WGL + V13 mouse + V14 WGI input/multiwindow/absolute-path markers and required exports..."
    Copy-SdlRuntime -Path $SourceDll
    return
}

if (Test-Path -LiteralPath $runtimeCacheDll) {
    Write-Host "Using cached patched SDL3 UWP runtime: $runtimeCacheDll"
    Copy-SdlRuntime -Path $runtimeCacheDll
    return
}

Expand-SdlSource
Patch-ExternalHostSupport
$builtDll = Build-SdlRuntime
Ensure-Dir $runtimeCacheDir
Copy-Item -LiteralPath $builtDll -Destination $runtimeCacheDll -Force
Copy-SdlRuntime -Path $runtimeCacheDll
