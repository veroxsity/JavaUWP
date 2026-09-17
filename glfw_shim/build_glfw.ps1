# build_glfw.ps1 - Build glfw_uwp.cpp -> glfw.dll (the CoreWindow shim)
param(
    [string]$OutputDir,
    [string]$MouseSupportLib,
    [string]$MouseSupportInclude
)

$ErrorActionPreference = "Stop"

. (Join-Path (Split-Path $PSScriptRoot -Parent) "scripts\common.ps1")

$tools = Resolve-VSTools
$sdk = Resolve-WindowsSdk
$sdkRoot = $sdk.Root
$sdkVer = $sdk.Version

if (-not $OutputDir) {
    $OutputDir = Join-Path (Get-ConfigPath "BuildDir") "glfw_shim"
}
$OutputDir = (New-Item -ItemType Directory -Force -Path $OutputDir).FullName
$dllPath = Join-Path $OutputDir "glfw.dll"
$objPath = Join-Path $OutputDir "glfw_uwp.obj"
$libPath = Join-Path $OutputDir "glfw_uwp.lib"

if (-not $MouseSupportInclude) {
    $MouseSupportInclude = Join-Path (Split-Path $PSScriptRoot -Parent) "mouse_support"
}
if (-not $MouseSupportLib) {
    $MouseSupportLib = Join-Path (Get-ConfigPath "BuildDir") "mouse_support\mouse_support.lib"
}
if (-not (Test-Path $MouseSupportLib)) {
    throw "mouse_support import library missing: $MouseSupportLib (build mouse_support before glfw)"
}

$env:INCLUDE = "$($tools.MsvcRoot)\include;" +
               "${sdkRoot}Include\$sdkVer\ucrt;" +
               "${sdkRoot}Include\$sdkVer\shared;" +
               "${sdkRoot}Include\$sdkVer\um;" +
               "${sdkRoot}Include\$sdkVer\winrt;" +
               "${sdkRoot}Include\$sdkVer\cppwinrt"
$env:LIB = "$($tools.MsvcRoot)\lib\x64;" +
           "${sdkRoot}Lib\$sdkVer\ucrt\x64;" +
           "${sdkRoot}Lib\$sdkVer\um\x64"

$sources = @(Get-ChildItem $PSScriptRoot -File -Include *.cpp, *.h, *.def -Recurse | Select-Object -ExpandProperty FullName)
$sources += @(Get-ChildItem $MouseSupportInclude -File -Include *.h -Recurse -ErrorAction SilentlyContinue | Select-Object -ExpandProperty FullName)
$stampPath = Join-Path $OutputDir "build.stamp"
$stamp = New-BuildStamp `
    -Values @("glfw_shim", $tools.ClExe, $sdkVer) `
    -ContentFiles (@($PSCommandPath) + $sources) `
    -DependencyFiles @($MouseSupportLib)

if (Test-BuildStampCurrent -StampPath $stampPath -Stamp $stamp -RequiredOutputs @($dllPath)) {
    Write-Host "glfw.dll up to date -> $dllPath"
    return
}

Push-Location $PSScriptRoot
Write-Host "Building glfw.dll (CoreWindow shim)..."
& $tools.ClExe glfw_uwp.cpp /LD /EHsc /std:c++17 $CommonClFlags /O2 /GL /Gw /arch:AVX2 /DNDEBUG /D_UNICODE /DUNICODE /D_WIN32_WINNT=0x0A00 /D_SILENCE_EXPERIMENTAL_COROUTINE_DEPRECATION_WARNINGS /I"$MouseSupportInclude" /Fo"$objPath" `
    /DWINAPI_FAMILY=WINAPI_FAMILY_APP `
    /link /LTCG /DEF:glfw_uwp.def /OUT:"$dllPath" /IMPLIB:"$libPath" /MACHINE:X64 `
    "$MouseSupportLib" `
    kernel32.lib runtimeobject.lib windowsapp.lib ole32.lib oleaut32.lib gameinput.lib ws2_32.lib
if ($LASTEXITCODE -ne 0) { Pop-Location; throw "glfw_uwp build FAILED" }
Pop-Location
Set-BuildStamp -StampPath $stampPath -Stamp $stamp
Write-Host "glfw.dll built OK -> $dllPath"
