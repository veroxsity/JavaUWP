# Build SDL3 UWP (WinRT) from the XboxEmulationHub fork.
# This packages SDL3.dll alongside the existing GLFW shim; it does not replace
# LWJGL's GLFW calls or make Minecraft use SDL3 automatically.
param(
    [string]$OutputDir
)

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "..\scripts\common.ps1")

$repoUrl = "https://github.com/XboxEmulationHub/SDL3-uwp.git"
$repoRef = "uwp-3.4.20"
$commit = "ec2e0e7c77ec9a04437eb3ae884d62ae0e6129b4"
$cacheRoot = Join-Path (Get-ConfigPath "CacheDir") "SDL3-uwp"
$sourceDir = Join-Path $cacheRoot "src"
$buildDir = Join-Path $cacheRoot "build"
$installDir = Join-Path $cacheRoot "install"
$stampPath = Join-Path $cacheRoot "build.stamp"
$stamp = "SDL3-UWP|$commit|x64|WindowsStore|10.0.26100.0"

if (-not $OutputDir) {
    $OutputDir = Join-Path (Get-ConfigPath "BuildDir") "sdl3-uwp"
}
Ensure-Dir $OutputDir
$dllPath = Join-Path $OutputDir "SDL3.dll"

$git = Get-Command git -ErrorAction SilentlyContinue
$cmake = Get-Command cmake -ErrorAction SilentlyContinue
if (-not $git) { throw "git is required to fetch SDL3-UWP." }
if (-not $cmake) { throw "CMake is required to build SDL3-UWP." }

if (-not (Test-Path (Join-Path $sourceDir "CMakeLists.txt"))) {
    Ensure-Dir $cacheRoot
    & $git.Source clone --branch $repoRef --depth 1 $repoUrl $sourceDir
    if ($LASTEXITCODE -ne 0) { throw "Could not clone SDL3-UWP." }
}

$head = (& $git.Source -C $sourceDir rev-parse HEAD).Trim()
if ($head -ne $commit) {
    & $git.Source -C $sourceDir fetch --depth 1 origin $commit
    if ($LASTEXITCODE -ne 0) { throw "Could not fetch pinned SDL3-UWP commit $commit." }
    & $git.Source -C $sourceDir checkout --detach $commit
    if ($LASTEXITCODE -ne 0) { throw "Could not checkout pinned SDL3-UWP commit $commit." }
}

if ((Test-Path $dllPath) -and (Test-Path $stampPath) -and ([IO.File]::ReadAllText($stampPath).Trim() -eq $stamp)) {
    Write-Host "SDL3-UWP is up to date -> $dllPath"
    return
}

Remove-Item -Recurse -Force $buildDir,$installDir -ErrorAction SilentlyContinue
Ensure-Dir $buildDir
Ensure-Dir $installDir

# Configure CMake as a Windows Store/UWP target. WINDOWS_STORE is an SDL
# option; by itself it does not select CMake's WindowsStore platform toolchain.
# The WindowsStore system name makes the VS generator emit the UWP project
# properties that provide the default platform.winmd metadata references.
& $cmake.Source -S $sourceDir -B $buildDir -G "Visual Studio 18 2026" -A "x64,version=10.0.26100.0" -DCMAKE_SYSTEM_NAME=WindowsStore -DCMAKE_SYSTEM_VERSION=10.0 -DWINDOWS_STORE=ON -DSDL_SHARED=ON -DSDL_STATIC=OFF -DSDL_TESTS=OFF -DSDL_TEST_LIBRARY=OFF -DSDL_EXAMPLES=OFF -DSDL_INSTALL=ON
if ($LASTEXITCODE -ne 0) { throw "SDL3-UWP CMake configuration failed." }

& $cmake.Source --build $buildDir --config Release
if ($LASTEXITCODE -ne 0) { throw "SDL3-UWP build failed." }

& $cmake.Source --install $buildDir --config Release --prefix $installDir
if ($LASTEXITCODE -ne 0) { throw "SDL3-UWP install failed." }

$builtDll = Get-ChildItem $installDir -Filter "SDL3.dll" -File -Recurse | Select-Object -First 1
if (-not $builtDll) {
    $builtDll = Get-ChildItem $buildDir -Filter "SDL3.dll" -File -Recurse | Select-Object -First 1
}
if (-not $builtDll) { throw "SDL3.dll was not produced by the SDL3-UWP build." }

Copy-Item $builtDll.FullName $dllPath -Force
[IO.File]::WriteAllText($stampPath, $stamp)
Write-Host "SDL3-UWP built OK -> $dllPath"
