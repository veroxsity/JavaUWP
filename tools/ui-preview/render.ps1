# renders launcher screens to png on a pc, no appx build, deploy or console install
param(
    [string]$OutDir
)

$ErrorActionPreference = "Stop"

Remove-Item Env:MC_VERSION, Env:MC_ASSET_INDEX, Env:FABRIC_LOADER_VERSION -ErrorAction SilentlyContinue
if (-not ${env:ProgramFiles(x86)}) { ${env:ProgramFiles(x86)} = "C:\Program Files (x86)" }

$root = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
. (Join-Path $root "scripts\common.ps1")

$tools = Resolve-VSTools
$sdk = Resolve-WindowsSdk
$sdkRoot = $sdk.Root
$sdkVer = $sdk.Version
$jreSrc = Resolve-JavaHome

$mcBuildDir = Join-Path $root "staging\build\MC.Xbox"
if (-not (Test-Path (Join-Path $mcBuildDir "runtime_config.h"))) {
    throw "staging\build\MC.Xbox\runtime_config.h is missing, run build.ps1 once to generate it"
}
$work = Join-Path $root "staging\build\ui-preview"
Ensure-Dir $work
if (-not $OutDir) { $OutDir = Join-Path $root ".local\ui-preview" }
Ensure-Dir $OutDir
# a renamed scene would otherwise leave its old png looking current
Remove-Item (Join-Path $OutDir "*.png") -ErrorAction SilentlyContinue

$env:INCLUDE = "$mcBuildDir;$($tools.MsvcRoot)\include;${sdkRoot}Include\$sdkVer\ucrt;${sdkRoot}Include\$sdkVer\shared;${sdkRoot}Include\$sdkVer\um;${sdkRoot}Include\$sdkVer\winrt;${sdkRoot}Include\$sdkVer\cppwinrt;$jreSrc\include;$jreSrc\include\win32"
$env:LIB = "$($tools.MsvcRoot)\lib\x64;${sdkRoot}Lib\$sdkVer\ucrt\x64;${sdkRoot}Lib\$sdkVer\um\x64"

# a desktop exe, so no WINAPI_FAMILY_APP. the renderer only needs d2d, dwrite and wic, which both families have
Push-Location (Join-Path $root "MC.Xbox")
try {
    & $tools.ClExe /nologo `
        ..\tools\ui-preview\ui_preview.cpp common\launcher_common.cpp profiles\profiles.cpp ui\mods_ui_globals.cpp third_party\miniz\miniz.c `
        /std:c++17 /EHsc /W3 /D_UNICODE /DUNICODE /D_WIN32_WINNT=0x0A00 /D_SILENCE_EXPERIMENTAL_COROUTINE_DEPRECATION_WARNINGS /DMINIZ_NO_STDIO /DMINIZ_NO_TIME `
        /I. /Icommon /Inet /Iauth /Iui /Imods /Iprofiles /Ilaunch /Ilaunch\loaders /Itelemetry `
        /Fo"$work\\" /Fe"$work\ui_preview.exe" `
        /link d2d1.lib dwrite.lib d3d11.lib dxgi.lib windowscodecs.lib ole32.lib shell32.lib runtimeobject.lib windowsapp.lib
    if ($LASTEXITCODE -ne 0) { throw "ui preview build failed" }
}
finally {
    Pop-Location
}

# the main menu draws these from Assets\screenshots next to the exe, the same as the package
$screenshotTarget = Join-Path $work "Assets\screenshots"
Ensure-Dir $screenshotTarget
Copy-Item -Force (Join-Path $root "MC.Xbox\Assets\screenshots\*.png") $screenshotTarget

& (Join-Path $work "ui_preview.exe") $OutDir
if ($LASTEXITCODE -ne 0) { throw "ui preview render failed" }
Write-Host "UI_PREVIEW_OK $OutDir"
