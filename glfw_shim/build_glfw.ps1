# build_glfw.ps1 - Build glfw_uwp.cpp -> glfw.dll (the CoreWindow shim)
$ErrorActionPreference = "Stop"

function Resolve-VSTools {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path $vswhere)) {
        throw "vswhere.exe not found. Install Visual Studio Build Tools or set up the MSVC environment manually."
    }

    $installPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $installPath) {
        throw "Visual Studio with C++ tools not found."
    }

    $msvcRoot = Get-ChildItem (Join-Path $installPath "VC\Tools\MSVC") -Directory |
        Sort-Object Name -Descending |
        Select-Object -First 1 -ExpandProperty FullName
    if (-not $msvcRoot) {
        throw "MSVC tools directory not found."
    }

    $clExe = Join-Path $msvcRoot "bin\Hostx64\x64\cl.exe"
    if (-not (Test-Path $clExe)) {
        throw "cl.exe not found at $clExe"
    }

    return @{
        MsvcRoot = $msvcRoot
        ClExe = $clExe
    }
}

$tools = Resolve-VSTools
$sdkRoot = (Get-ItemProperty "HKLM:\SOFTWARE\Microsoft\Windows Kits\Installed Roots").KitsRoot10
$sdkVer = Get-ChildItem (Join-Path $sdkRoot "Include") | Sort-Object Name | Select-Object -Last 1 -ExpandProperty Name

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
Write-Host "Building glfw.dll (CoreWindow shim)..."
& $tools.ClExe glfw_uwp.cpp /LD /EHsc /O2 /D_UNICODE /DUNICODE /D_WIN32_WINNT=0x0A00 `
    /DWINAPI_FAMILY=WINAPI_FAMILY_APP `
    /link /DEF:glfw_uwp.def /OUT:glfw.dll /MACHINE:X64 `
    kernel32.lib runtimeobject.lib windowsapp.lib ole32.lib oleaut32.lib
if ($LASTEXITCODE -ne 0) { Pop-Location; throw "glfw_uwp build FAILED" }
Pop-Location
Write-Host "glfw.dll built OK -> $PSScriptRoot\glfw.dll"
