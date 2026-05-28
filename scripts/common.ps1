$ErrorActionPreference = "Stop"

. (Join-Path $PSScriptRoot "config.ps1")

function Resolve-RepoRoot {
    return (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
}

function Test-JavaHomeMinimumVersion {
    param(
        [Parameter(Mandatory = $true)]
        [string]$JavaHome,

        [Parameter(Mandatory = $true)]
        [int]$MajorVersion
    )

    $javaExe = Join-Path $JavaHome "bin\java.exe"
    $javacExe = Join-Path $JavaHome "bin\javac.exe"
    if (-not (Test-Path $javaExe) -or -not (Test-Path $javacExe)) {
        return $false
    }

    # java -version writes to stderr, and $ErrorActionPreference = 'Stop' in
    # the parent scope turns any captured ErrorRecord into a terminating throw.
    # PowerShell 7's $PSNativeCommandUseErrorActionPreference doesn't exist on
    # 5.1, so relax the error pref itself for just this native call.
    $versionOutput = $null
    try {
        $prevPref = $ErrorActionPreference
        $ErrorActionPreference = 'Continue'
        $versionOutput = (& $javaExe -version 2>&1 | Select-Object -First 1).ToString()
    } catch {
        return $false
    } finally {
        $ErrorActionPreference = $prevPref
    }
    if ($versionOutput -match '"(?<major>\d+)(\.|")') {
        return ([int]$Matches.major -ge $MajorVersion)
    }

    return $false
}

function Resolve-JavaHome {
    param(
        [int]$MajorVersion = $ProjectConfig.JavaRelease
    )

    if ($env:JAVA_HOME) {
        if (Test-JavaHomeMinimumVersion -JavaHome $env:JAVA_HOME -MajorVersion $MajorVersion) {
            return $env:JAVA_HOME
        }

        Write-Warning "JAVA_HOME is set but is older than JDK ${MajorVersion}: $env:JAVA_HOME"
    }

    $candidates = @()

    $directRoots = @(
        "C:\ms-jdk$MajorVersion",
        "C:\Program Files\Java",
        "C:\Program Files\Eclipse Adoptium",
        "C:\Program Files\Amazon Corretto",
        "C:\Program Files\Microsoft",
        "C:\"
    )

    foreach ($root in $directRoots | Select-Object -Unique) {
        if (-not (Test-Path $root)) { continue }

        if (Test-Path (Join-Path $root "bin\javac.exe")) {
            $candidates += Get-Item $root
            continue
        }

        $candidates += Get-ChildItem $root -Directory -ErrorAction SilentlyContinue |
            Where-Object {
                $_.Name -like "graalvm-community-openjdk-*" -or
                $_.Name -like "jdk-$MajorVersion*" -or
                $_.Name -like "msopenjdk-$MajorVersion*" -or
                $_.Name -like "microsoft-jdk-$MajorVersion*"
            }
    }

    $match = $candidates |
        Where-Object { Test-JavaHomeMinimumVersion -JavaHome $_.FullName -MajorVersion $MajorVersion } |
        Select-Object -First 1

    if ($match) {
        return $match.FullName
    }

    throw "No suitable Java installation found. Set JAVA_HOME to a JDK $MajorVersion or newer install."
}

function Resolve-Python {
    if ($env:PYTHON) {
        if (Test-Path $env:PYTHON) {
            return (Resolve-Path $env:PYTHON).Path
        }

        throw "PYTHON is set but does not point to a valid executable: $env:PYTHON"
    }

    $pythonCandidates = @(
        "C:\Users\Dan\AppData\Local\Programs\Python\Python314\python.exe",
        "C:\Program Files\Python314\python.exe"
    )

    foreach ($candidate in $pythonCandidates) {
        if (Test-Path $candidate) {
            return (Resolve-Path $candidate).Path
        }
    }

    $pyLauncher = Get-Command py -ErrorAction SilentlyContinue
    if ($pyLauncher) {
        return $pyLauncher.Source
    }

    $python = Get-Command python -ErrorAction SilentlyContinue
    if ($python) {
        return $python.Source
    }

    throw "No suitable Python installation found. Set PYTHON to a Python 3 install with Pillow or install Python 3."
}

function Resolve-VSTools {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path $vswhere)) {
        throw "vswhere.exe not found. Install Visual Studio Build Tools or Visual Studio with C++ tools."
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
        ClExe    = $clExe
    }
}

function Resolve-WindowsSdk {
    $sdkRoot = (Get-ItemProperty "HKLM:\SOFTWARE\Microsoft\Windows Kits\Installed Roots").KitsRoot10
    if (-not $sdkRoot) {
        throw "Windows 10/11 SDK not found."
    }

    $sdkVer = Get-ChildItem (Join-Path $sdkRoot "Include") -Directory |
        Sort-Object Name |
        Select-Object -Last 1 -ExpandProperty Name
    if (-not $sdkVer) {
        throw "Windows SDK include directory not found under $sdkRoot."
    }

    return @{
        Root    = $sdkRoot
        Version = $sdkVer
    }
}

function Get-MesaRuntimeDllNames {
    return @(
        "libEGL.dll",
        "libGLESv2.dll",
        "libGLESv1_CM.dll",
        "opengl32.dll",
        "libgallium_wgl.dll",
        "libglapi.dll",
        "spirv_to_dxil.dll",
        "vulkan_dzn.dll",
        "glu32.dll",
        "dxil.dll",
        "z-1.dll"
    )
}

function Test-MesaRuntimeDir {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path
    )

    if (-not (Test-Path $Path)) {
        return $false
    }

    $required = @("libEGL.dll", "opengl32.dll", "libgallium_wgl.dll")
    foreach ($dll in $required) {
        if (-not (Test-Path (Join-Path $Path $dll))) {
            return $false
        }
    }

    return $true
}

function Get-XboxOneGraphicsRuntimeDllNames {
    return @(
        "opengl32.dll",
        "libEGL.dll",
        "libGLESv2.dll",
        "libGLESv1_CM.dll",
        "glu32.dll"
    )
}

function Test-XboxOneGraphicsRuntimeDir {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path
    )

    if (-not (Test-Path $Path)) {
        return $false
    }

    $required = @("opengl32.dll", "libEGL.dll", "libGLESv2.dll")
    foreach ($dll in $required) {
        if (-not (Test-Path (Join-Path $Path $dll))) {
            return $false
        }
    }

    return $true
}

function Resolve-XboxOneGraphicsRuntimeDir {
    param(
        [string]$XboxOneGraphicsRuntimeDir
    )

    $candidates = @()
    if ($XboxOneGraphicsRuntimeDir) {
        $candidates += $XboxOneGraphicsRuntimeDir
    }
    if ($env:XBOX_ONE_GRAPHICS_RUNTIME_DIR) {
        $candidates += $env:XBOX_ONE_GRAPHICS_RUNTIME_DIR
    }

    $repoRuntimeDir = Get-ConfigPath "XboxOneGraphicsRuntimeDir"
    if (Test-Path $repoRuntimeDir) {
        $candidates += $repoRuntimeDir
    }

    foreach ($candidate in $candidates | Where-Object { $_ } | Select-Object -Unique) {
        if (Test-XboxOneGraphicsRuntimeDir -Path $candidate) {
            return (Resolve-Path $candidate).Path
        }
    }

    return $null
}

function Resolve-MesaRuntimeDir {
    param(
        [string]$MesaRuntimeDir
    )

    $candidates = @()

    if ($MesaRuntimeDir) {
        $candidates += $MesaRuntimeDir
    }
    if ($env:MESA_UWP_DIR) {
        $candidates += $env:MESA_UWP_DIR
    }

    $localMesaDir = Get-ConfigPath "MesaRuntimeDir"
    if (Test-Path $localMesaDir) {
        $candidates += $localMesaDir
    }

    $cachedMesaDir = Join-Path (Resolve-RepoRoot) "staging\cache\mesa-runtime"
    if (Test-Path $cachedMesaDir) {
        $candidates += $cachedMesaDir
    }

    # Backward-compatible convenience for users who source Mesa DLLs from
    # RetroArch UWP. RetroArch is not required by the project.
    if ($env:RETROARCH_UWP_DIR) {
        $candidates += $env:RETROARCH_UWP_DIR
    }

    $searchRoots = @("X:\WindowsApps", "S:\Program Files\WindowsApps")
    foreach ($root in $searchRoots) {
        if (-not (Test-Path $root)) { continue }

        $candidates += Get-ChildItem $root -Directory -ErrorAction SilentlyContinue |
            Select-Object -ExpandProperty FullName
    }

    foreach ($candidate in $candidates | Where-Object { $_ } | Select-Object -Unique) {
        if (Test-MesaRuntimeDir -Path $candidate) {
            return (Resolve-Path $candidate).Path
        }
    }

    throw "Mesa UWP runtime DLLs not found. Set MESA_UWP_DIR, pass -MesaRuntimeDir, or place the DLLs in the tracked mesa-runtime folder."
}

function Get-ProjectPath {
    param(
        [Parameter(Mandatory = $true)]
        [string]$RelativePath
    )

    return (Join-Path (Resolve-RepoRoot) $RelativePath)
}

function Get-ConfigPath {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Name
    )

    if (-not $ProjectConfig.Contains($Name)) {
        throw "Unknown project path config key: $Name"
    }

    return (Get-ProjectPath $ProjectConfig[$Name])
}
