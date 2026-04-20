# build.ps1 - Build and package MC Java UWP
$ErrorActionPreference = "Stop"

function Resolve-JavaHome {
    if ($env:JAVA_HOME -and (Test-Path (Join-Path $env:JAVA_HOME "bin\javac.exe"))) {
        return $env:JAVA_HOME
    }

    $candidates = @()
    if (Test-Path "C:\ms-jdk21") {
        $candidates += Get-ChildItem "C:\ms-jdk21" -Directory -ErrorAction SilentlyContinue
    }
    $candidates += Get-ChildItem "C:\" -Directory -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -like "graalvm-community-openjdk-*" -or $_.Name -like "jdk-21*" }

    $match = $candidates |
        Where-Object { Test-Path (Join-Path $_.FullName "bin\javac.exe") } |
        Select-Object -First 1
    if ($match) {
        return $match.FullName
    }

    throw "No suitable Java installation found. Set JAVA_HOME to a JDK 21 install."
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
        ClExe = $clExe
    }
}

function Resolve-RetroArchDir {
    if ($env:RETROARCH_UWP_DIR -and (Test-Path $env:RETROARCH_UWP_DIR)) {
        return $env:RETROARCH_UWP_DIR
    }

    $searchRoots = @("X:\WindowsApps", "S:\Program Files\WindowsApps")
    foreach ($root in $searchRoots) {
        if (-not (Test-Path $root)) { continue }

        $candidate = Get-ChildItem $root -Directory -ErrorAction SilentlyContinue |
            Where-Object { (Test-Path (Join-Path $_.FullName "libEGL.dll")) -and (Test-Path (Join-Path $_.FullName "opengl32.dll")) -and (Test-Path (Join-Path $_.FullName "libgallium_wgl.dll")) } |
            Select-Object -First 1
        if ($candidate) {
            return $candidate.FullName
        }
    }

    throw "RetroArch UWP directory not found. Set RETROARCH_UWP_DIR to the installed package directory containing libEGL.dll."
}

$root = $PSScriptRoot
$pkg = Join-Path $root "PackageContent"
$jreSrc = Resolve-JavaHome
$tools = Resolve-VSTools

Write-Host "=== Building MC.Xbox.exe ==="
Push-Location (Join-Path $root "MC.Xbox")

$sdkRoot = (Get-ItemProperty "HKLM:\SOFTWARE\Microsoft\Windows Kits\Installed Roots").KitsRoot10
$sdkVer = Get-ChildItem (Join-Path $sdkRoot "Include") | Sort-Object Name | Select-Object -Last 1 -ExpandProperty Name

$env:INCLUDE = "$($tools.MsvcRoot)\include;${sdkRoot}Include\$sdkVer\ucrt;${sdkRoot}Include\$sdkVer\shared;${sdkRoot}Include\$sdkVer\um;${sdkRoot}Include\$sdkVer\winrt;${sdkRoot}Include\$sdkVer\cppwinrt;$jreSrc\include;$jreSrc\include\win32"
$env:LIB = "$($tools.MsvcRoot)\lib\x64;${sdkRoot}Lib\$sdkVer\ucrt\x64;${sdkRoot}Lib\$sdkVer\um\x64"

& $tools.ClExe App.cpp /std:c++17 /EHsc /W3 /O2 /D_UNICODE /DUNICODE /D_WIN32_WINNT=0x0A00 `
    /DWINAPI_FAMILY=WINAPI_FAMILY_APP `
    /link /SUBSYSTEM:WINDOWS /ENTRY:wWinMainCRTStartup /MACHINE:X64 `
    /OUT:MC.Xbox.exe kernel32.lib shell32.lib runtimeobject.lib windowsapp.lib ole32.lib oleaut32.lib
if ($LASTEXITCODE -ne 0) { throw "Compile failed" }
Pop-Location
Write-Host "MC.Xbox.exe built"

Write-Host "=== Building GLFW CoreWindow shim ==="
& (Join-Path $root "glfw_shim\build_glfw.ps1")

Write-Host "=== Building Xbox compatibility mod ==="
& (Join-Path $root "compat_mod\build_compat_mod.ps1")

Write-Host "=== Assembling PackageContent ==="
Remove-Item -Recurse -Force $pkg -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path (Join-Path $pkg "Assets") | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $pkg "natives") | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $pkg "assets") | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $pkg "game") | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $pkg "game\log_configs") | Out-Null

Copy-Item (Join-Path $root "MC.Xbox\MC.Xbox.exe") (Join-Path $pkg "MC.Xbox.exe")
Copy-Item (Join-Path $root "MC.Xbox\Package.appxmanifest") (Join-Path $pkg "AppxManifest.xml")

Write-Host "Copying game files..."
Copy-Item -Recurse (Join-Path $root "gameDir\libraries") (Join-Path $pkg "game\libraries")
Copy-Item -Recurse (Join-Path $root "gameDir\versions") (Join-Path $pkg "game\versions")
Copy-Item -Recurse (Join-Path $root "gameDir\mods") (Join-Path $pkg "game\mods")
if (Test-Path (Join-Path $root "gameDir\.fabric")) {
    Copy-Item -Recurse (Join-Path $root "gameDir\.fabric") (Join-Path $pkg "game\.fabric")
}

$remapped = Join-Path $root "gameDir\.fabric\remappedJars"
if (Test-Path $remapped) {
    Write-Host "Copying .fabric remapped jars..."
    New-Item -ItemType Directory -Force (Join-Path $pkg "game\.fabric\remappedJars") | Out-Null
    Copy-Item -Recurse (Join-Path $remapped "*") (Join-Path $pkg "game\.fabric\remappedJars\") -Force
}

Write-Host "Copying natives..."
Copy-Item (Join-Path $root "natives-1.21\*.dll") (Join-Path $pkg "natives\") 

Write-Host "Extracting JNA native..."
Add-Type -AssemblyName System.IO.Compression.FileSystem
$jnaJar = Join-Path $root "gameDir\libraries\net\java\dev\jna\jna\5.17.0\jna-5.17.0.jar"
if (Test-Path $jnaJar) {
    $zip = [System.IO.Compression.ZipFile]::OpenRead($jnaJar)
    try {
        $entry = $zip.Entries | Where-Object { $_.FullName -eq "com/sun/jna/win32-x86-64/jnidispatch.dll" } | Select-Object -First 1
        if ($entry) {
            [System.IO.Compression.ZipFileExtensions]::ExtractToFile($entry, (Join-Path $pkg "natives\jnidispatch.dll"), $true)
            Write-Host "JNA: jnidispatch.dll"
        } else {
            Write-Warning "win32-x86-64/jnidispatch.dll not found in $jnaJar"
        }
    } finally {
        $zip.Dispose()
    }
}

Write-Host "Injecting GLFW shim into LWJGL JAR..."
$glfwJar  = Join-Path $pkg "game\libraries\org\lwjgl\lwjgl-glfw\3.3.3\lwjgl-glfw-3.3.3-natives-windows.jar"
$shimDll  = Join-Path $root "glfw_shim\glfw.dll"
$jarExe   = Join-Path $pkg "jre\bin\jar.exe"
if (-not (Test-Path $jarExe)) { $jarExe = "jar" }

if (Test-Path $glfwJar) {
    # Extract JAR into a fresh temp dir, replace glfw.dll, repack
    $jarTmpDir = Join-Path $root "patch\glfw_jar_tmp"
    Remove-Item -Recurse -Force $jarTmpDir -ErrorAction SilentlyContinue
    New-Item -ItemType Directory -Force -Path $jarTmpDir | Out-Null
    Push-Location $jarTmpDir
    & $jarExe xf $glfwJar
    Pop-Location

    $glfwInJar = Get-ChildItem -Recurse $jarTmpDir -Filter "glfw.dll" | Select-Object -First 1
    if ($glfwInJar) {
        Copy-Item $shimDll $glfwInJar.FullName -Force
        Write-Host "  Replaced $($glfwInJar.FullName)"
        Push-Location $jarTmpDir
        & $jarExe cf $glfwJar .
        Pop-Location
        Write-Host "  JAR repacked: $glfwJar"
    } else {
        Write-Warning "  glfw.dll entry not found inside JAR after extraction"
    }
} else {
    Write-Warning "  LWJGL GLFW JAR not found: $glfwJar"
}
Copy-Item $shimDll (Join-Path $pkg "natives\glfw.dll") -Force

Write-Host "Copying Mesa runtime..."
$retroArch = Resolve-RetroArchDir
foreach ($dll in @("libEGL.dll","libGLESv2.dll","opengl32.dll","libgallium_wgl.dll","libglapi.dll","glu32.dll","dxil.dll","z-1.dll")) {
    $source = Join-Path $retroArch $dll
    if (Test-Path $source) {
        Copy-Item $source (Join-Path $pkg $dll) -Force
        Copy-Item $source (Join-Path $pkg "natives\$dll") -Force
        Write-Host "Mesa: $dll"
    }
}

Write-Host "Copying assets..."
Copy-Item -Recurse -Force (Join-Path $root "assets\*") (Join-Path $pkg "assets\")
Copy-Item -Force (Join-Path $root "log_configs\client-uwp.xml") (Join-Path $pkg "game\log_configs\client-uwp.xml")

Write-Host "Copying JRE..."
Write-Host "JRE source: $jreSrc"
Copy-Item -Recurse $jreSrc (Join-Path $pkg "jre")
Copy-Item (Join-Path $root "xbox_security.properties") (Join-Path $pkg "jre\conf\security\xbox.properties")

Write-Host "Generating UWP tile assets..."
& python (Join-Path $root "generate_assets.py") $pkg

Write-Host "=== Packaging ==="
$cert = Join-Path $root "MC_DevMode.pfx"
$appx = Join-Path $root "MC_Java_1.0.0.0.appx"

if (-not (Test-Path $cert)) {
    $certName = if ($env:APPX_CERT_SUBJECT) { $env:APPX_CERT_SUBJECT } else { "CN=MinecraftJavaUWP Dev" }
    $c = New-SelfSignedCertificate -Type CodeSigningCert -Subject $certName `
        -KeyUsage DigitalSignature -CertStoreLocation "Cert:\CurrentUser\My" `
        -TextExtension @("2.5.29.37={text}1.3.6.1.5.5.7.3.3","2.5.29.19={text}")
    Export-PfxCertificate -Cert $c -FilePath $cert `
        -Password (ConvertTo-SecureString "devmode" -AsPlainText -Force) | Out-Null
    Write-Host "Generated cert"
}

$makeappx = Get-ChildItem "${sdkRoot}bin\$sdkVer\x64\makeappx.exe","${sdkRoot}bin\10.0.26100.0\x64\makeappx.exe" -ErrorAction SilentlyContinue | Select-Object -First 1 -ExpandProperty FullName
if (-not $makeappx) {
    $cmd = Get-Command makeappx -ErrorAction SilentlyContinue
    if ($cmd) { $makeappx = $cmd.Source }
}
if (-not $makeappx) { throw "makeappx.exe not found. Add Windows SDK bin to PATH." }
$signtool = Get-ChildItem "${sdkRoot}bin\$sdkVer\x64\signtool.exe","${sdkRoot}bin\10.0.26100.0\x64\signtool.exe" -ErrorAction SilentlyContinue | Select-Object -First 1 -ExpandProperty FullName
if (-not $signtool) { $signtool = "signtool" }

& $makeappx pack /d $pkg /p $appx /overwrite
& $signtool sign /fd SHA256 /a /f $cert /p devmode $appx

Write-Host ""
Write-Host "=== Done ==="
Write-Host "Package: $appx"
