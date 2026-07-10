param(
    [string]$OutputDir
)

$ErrorActionPreference = "Stop"

. (Join-Path (Split-Path (Split-Path $PSScriptRoot -Parent) -Parent) "scripts\common.ps1")

$tools = Resolve-VSTools
$sdk = Resolve-WindowsSdk
$sdkRoot = $sdk.Root
$sdkVer = $sdk.Version
$javaHome = Resolve-JavaHomeExact -MajorVersion 21

if (-not $OutputDir) {
    $OutputDir = Join-Path (Get-ConfigPath "BuildDir") "d3d12_backend"
}
$OutputDir = (New-Item -ItemType Directory -Force -Path $OutputDir).FullName
$dllPath = Join-Path $OutputDir "bandit_d3d12_backend.dll"
$objPath = Join-Path $OutputDir "bandit_d3d12_backend.obj"
$libPath = Join-Path $OutputDir "bandit_d3d12_backend.lib"

$env:INCLUDE = "$($tools.MsvcRoot)\include;" +
               "${sdkRoot}Include\$sdkVer\ucrt;" +
               "${sdkRoot}Include\$sdkVer\shared;" +
               "${sdkRoot}Include\$sdkVer\um;" +
               "${sdkRoot}Include\$sdkVer\winrt;" +
               "${sdkRoot}Include\$sdkVer\cppwinrt;" +
               "$javaHome\include;" +
               "$javaHome\include\win32"
$env:LIB = "$($tools.MsvcRoot)\lib\x64;" +
           "${sdkRoot}Lib\$sdkVer\ucrt\x64;" +
           "${sdkRoot}Lib\$sdkVer\um\x64"

Push-Location $PSScriptRoot
Write-Host "Building bandit_d3d12_backend.dll..."
& $tools.ClExe bandit_d3d12_backend.cpp /LD /EHsc /O2 /D_UNICODE /DUNICODE /D_WIN32_WINNT=0x0A00 /Fo"$objPath" `
    /DWINAPI_FAMILY=WINAPI_FAMILY_APP `
    /link /OUT:"$dllPath" /IMPLIB:"$libPath" /MACHINE:X64 `
    kernel32.lib runtimeobject.lib windowsapp.lib ole32.lib d3d12.lib dxgi.lib d3dcompiler.lib
if ($LASTEXITCODE -ne 0) {
    Pop-Location
    throw "bandit_d3d12_backend build FAILED"
}
Pop-Location

Write-Host "bandit_d3d12_backend.dll built OK -> $dllPath"
