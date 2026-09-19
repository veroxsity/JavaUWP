# avoids a full appx build for source checks
param(
    [string[]]$Files = @()
)

$ErrorActionPreference = "Stop"

# avoid inherited build target overrides
Remove-Item Env:MC_VERSION, Env:MC_ASSET_INDEX, Env:FABRIC_LOADER_VERSION -ErrorAction SilentlyContinue
if (-not ${env:ProgramFiles(x86)}) { ${env:ProgramFiles(x86)} = "C:\Program Files (x86)" }

$root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
. (Join-Path $root "scripts\common.ps1")

if ($Files.Count -eq 0) {
    throw "nothing to check, pass -Files with paths relative to MC.Xbox"
}

$tools = Resolve-VSTools
$sdk = Resolve-WindowsSdk
$sdkRoot = $sdk.Root
$sdkVer = $sdk.Version
$jreSrc = Resolve-JavaHome

$mcBuildDir = Join-Path $root "staging\build\MC.Xbox"

$env:INCLUDE = "$mcBuildDir;$($tools.MsvcRoot)\include;${sdkRoot}Include\$sdkVer\ucrt;${sdkRoot}Include\$sdkVer\shared;${sdkRoot}Include\$sdkVer\um;${sdkRoot}Include\$sdkVer\winrt;${sdkRoot}Include\$sdkVer\cppwinrt;$jreSrc\include;$jreSrc\include\win32"

Push-Location (Join-Path $root "MC.Xbox")
try {
    & $tools.ClExe /Zs @Files `
        /std:c++17 /EHsc $CommonClFlags /D_UNICODE /DUNICODE /D_WIN32_WINNT=0x0A00 `
        /D_SILENCE_EXPERIMENTAL_COROUTINE_DEPRECATION_WARNINGS /DMINIZ_NO_STDIO /DMINIZ_NO_TIME `
        /DWINAPI_FAMILY=WINAPI_FAMILY_APP `
        /I. /Icommon /Inet /Iauth /Iui /Imods /Iprofiles /Ilaunch /Ilaunch\loaders /Itelemetry
    if ($LASTEXITCODE -ne 0) { throw "syntax check failed" }
}
finally {
    Pop-Location
}

Write-Host "SYNTAX_OK"
