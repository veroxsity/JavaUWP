param(
    [string]$OutputDir
)

$ErrorActionPreference = "Stop"

. (Join-Path (Split-Path $PSScriptRoot -Parent) "scripts\common.ps1")

$tools = Resolve-VSTools
$sdk = Resolve-WindowsSdk
$sdkRoot = $sdk.Root
$sdkVer = $sdk.Version

if (-not $OutputDir) {
    $OutputDir = Join-Path (Get-ConfigPath "BuildDir") "mouse_support"
}
$OutputDir = (New-Item -ItemType Directory -Force -Path $OutputDir).FullName
$dllPath = Join-Path $OutputDir "mouse_support.dll"
$objPath = Join-Path $OutputDir "mouse_support.obj"
$libPath = Join-Path $OutputDir "mouse_support.lib"

$env:INCLUDE = "$($tools.MsvcRoot)\include;" +
               "${sdkRoot}Include\$sdkVer\ucrt;" +
               "${sdkRoot}Include\$sdkVer\shared;" +
               "${sdkRoot}Include\$sdkVer\um;" +
               "${sdkRoot}Include\$sdkVer\winrt;" +
               "${sdkRoot}Include\$sdkVer\cppwinrt"
$env:LIB = "$($tools.MsvcRoot)\lib\x64;" +
           "${sdkRoot}Lib\$sdkVer\ucrt\x64;" +
           "${sdkRoot}Lib\$sdkVer\um\x64"

Push-Location $PSScriptRoot
Write-Host "Building mouse_support.dll (relay receiver + smoothing)..."
& $tools.ClExe mouse_support.cpp /LD /EHsc /O2 /D_UNICODE /DUNICODE /D_WIN32_WINNT=0x0A00 /Fo"$objPath" `
    /DWINAPI_FAMILY=WINAPI_FAMILY_APP `
    /link /OUT:"$dllPath" /IMPLIB:"$libPath" /MACHINE:X64 `
    kernel32.lib ws2_32.lib
if ($LASTEXITCODE -ne 0) { Pop-Location; throw "mouse_support build FAILED" }
Pop-Location
Write-Host "mouse_support.dll built OK -> $dllPath"
