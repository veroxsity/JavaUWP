# build.ps1 - Build and package MC Java UWP
param(
    [string]$MesaRuntimeDir = $env:MESA_UWP_DIR,
    [string]$XboxOneGraphicsRuntimeDir = $env:XBOX_ONE_GRAPHICS_RUNTIME_DIR,
    [string]$McVersion,
    [string]$FabricLoader,
    [string]$AssetIndex,
    [switch]$KeepStaging,
    [switch]$SkipStopAppProcesses,
    [switch]$StopFileLockers
)

$ErrorActionPreference = "Stop"

# Push command-line overrides into the environment before sourcing config.
# scripts/config.ps1 honors these so every downstream script (compat mod,
# patch-fabric, etc.) sees the same chosen version.
if ($McVersion)    { $env:MC_VERSION = $McVersion }
if ($FabricLoader) { $env:FABRIC_LOADER_VERSION = $FabricLoader }
if ($AssetIndex)   { $env:MC_ASSET_INDEX = $AssetIndex }

. (Join-Path $PSScriptRoot "scripts\common.ps1")

Write-Host "=== Build preflight ==="

$root = Resolve-RepoRoot
$pkg = Get-ConfigPath "PackageContentDir"
$buildDir = Get-ConfigPath "BuildDir"
$outDir = Get-ConfigPath "OutputDir"
$gameDir = Get-ConfigPath "GameDir"
$assetsDir = Get-ConfigPath "AssetsDir"
$nativesSourceDir = Get-ConfigPath "NativesDir"
$certDir = Get-ConfigPath "CertificateDir"
$mcBuildDir = Join-Path $buildDir "MC.Xbox"
$glfwBuildDir = Join-Path $buildDir "glfw_shim"
$mcExe = Join-Path $mcBuildDir "MC.Xbox.exe"
$shimDll = Join-Path $glfwBuildDir "glfw.dll"
$jreSrc = Resolve-JavaHome
$pythonExe = Resolve-Python
$tools = Resolve-VSTools
$sdk = Resolve-WindowsSdk
$sdkRoot = $sdk.Root
$sdkVer = $sdk.Version

function Stop-BuildBlockingProcesses {
    param(
        [Parameter(Mandatory = $true)]
        [string]$PackageName,

        [Parameter(Mandatory = $true)]
        [string]$RootPath,

        [Parameter(Mandatory = $true)]
        [string]$PackageContentPath,

        [Parameter(Mandatory = $true)]
        [string]$OutputPath,

        [string[]]$LockPaths = @()
    )

    $rootMatch = $RootPath.Replace('\', '\\')
    $pkgMatch = $PackageContentPath.Replace('\', '\\')
    $outMatch = $OutputPath.Replace('\', '\\')

    function Get-RestartManagerLockingProcessIds {
        param(
            [Parameter(Mandatory = $true)]
            [string[]]$Paths
        )

        $existingPaths = @($Paths | Where-Object { $_ -and (Test-Path -LiteralPath $_) } | ForEach-Object { (Resolve-Path -LiteralPath $_).Path })
        if (-not $existingPaths) { return @() }

        if (-not ("BuildRestartManager" -as [type])) {
            Add-Type -TypeDefinition @"
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;

public static class BuildRestartManager
{
    const int CCH_RM_SESSION_KEY = 32;
    const int ERROR_MORE_DATA = 234;

    [StructLayout(LayoutKind.Sequential)]
    struct FILETIME
    {
        public uint dwLowDateTime;
        public uint dwHighDateTime;
    }

    [StructLayout(LayoutKind.Sequential)]
    struct RM_UNIQUE_PROCESS
    {
        public int dwProcessId;
        public FILETIME ProcessStartTime;
    }

    enum RM_APP_TYPE
    {
        RmUnknownApp = 0,
        RmMainWindow = 1,
        RmOtherWindow = 2,
        RmService = 3,
        RmExplorer = 4,
        RmConsole = 5,
        RmCritical = 1000
    }

    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    struct RM_PROCESS_INFO
    {
        public RM_UNIQUE_PROCESS Process;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 256)]
        public string strAppName;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 64)]
        public string strServiceShortName;
        public RM_APP_TYPE ApplicationType;
        public uint AppStatus;
        public uint TSSessionId;
        [MarshalAs(UnmanagedType.Bool)]
        public bool bRestartable;
    }

    [DllImport("rstrtmgr.dll", CharSet = CharSet.Unicode)]
    static extern int RmStartSession(out uint pSessionHandle, int dwSessionFlags, StringBuilder strSessionKey);

    [DllImport("rstrtmgr.dll", CharSet = CharSet.Unicode)]
    static extern int RmRegisterResources(uint pSessionHandle, uint nFiles, string[] rgsFilenames, uint nApplications, IntPtr rgApplications, uint nServices, string[] rgsServiceNames);

    [DllImport("rstrtmgr.dll")]
    static extern int RmGetList(uint dwSessionHandle, out uint pnProcInfoNeeded, ref uint pnProcInfo, [In, Out] RM_PROCESS_INFO[] rgAffectedApps, ref uint lpdwRebootReasons);

    [DllImport("rstrtmgr.dll")]
    static extern int RmEndSession(uint pSessionHandle);

    public static int[] GetLockingProcessIds(string[] paths)
    {
        uint handle;
        var key = new StringBuilder(CCH_RM_SESSION_KEY + 1);
        int result = RmStartSession(out handle, 0, key);
        if (result != 0) return new int[0];

        try
        {
            result = RmRegisterResources(handle, (uint)paths.Length, paths, 0, IntPtr.Zero, 0, null);
            if (result != 0) return new int[0];

            uint needed;
            uint count = 0;
            uint reasons = 0;
            result = RmGetList(handle, out needed, ref count, null, ref reasons);
            if (result != ERROR_MORE_DATA || needed == 0) return new int[0];

            count = needed;
            var processes = new RM_PROCESS_INFO[count];
            result = RmGetList(handle, out needed, ref count, processes, ref reasons);
            if (result != 0) return new int[0];

            var ids = new List<int>();
            for (int i = 0; i < count; i++)
            {
                int id = processes[i].Process.dwProcessId;
                if (!ids.Contains(id)) ids.Add(id);
            }
            return ids.ToArray();
        }
        finally
        {
            RmEndSession(handle);
        }
    }
}
"@
        }

        return [BuildRestartManager]::GetLockingProcessIds($existingPaths)
    }

    $allProcesses = @(Get-CimInstance Win32_Process)
    $lockProcessIds = @()
    if ($LockPaths -and $LockPaths.Count -gt 0) {
        Write-Host "Checking file lockers..."
        $lockProcessIds = @(Get-RestartManagerLockingProcessIds -Paths $LockPaths | Where-Object { $_ -and $_ -ne $PID })
    }

    $matches = $allProcesses |
        Where-Object {
            $name = $_.Name
            $path = [string]$_.ExecutablePath
            $cmd = [string]$_.CommandLine
            $matched = $false

            if ($_.ProcessId -ne $PID) {
                if ($lockProcessIds -contains $_.ProcessId) { $matched = $true }
                if ($name -ieq "MC.Xbox.exe") { $matched = $true }
                if ($PackageName -and ($path -like "*$PackageName*" -or $cmd -like "*$PackageName*")) { $matched = $true }

                $isPackagingTool = $name -ieq "makeappx.exe" -or $name -ieq "signtool.exe"
                if ($isPackagingTool -and (
                    $cmd -like "*$RootPath*" -or
                    $cmd -like "*$rootMatch*" -or
                    $cmd -like "*$PackageContentPath*" -or
                    $cmd -like "*$pkgMatch*" -or
                    $cmd -like "*$OutputPath*" -or
                    $cmd -like "*$outMatch*")) {
                    $matched = $true
                }
            }

            $matched
        }

    if (-not $matches) {
        Write-Host "No running app/package processes found."
        return
    }

    Write-Host "Stopping running app/package processes..."
    $restartExplorer = $false
    foreach ($proc in $matches) {
        Write-Host "  Stop PID $($proc.ProcessId) $($proc.Name)"
        if ($proc.Name -ieq "explorer.exe") {
            $restartExplorer = $true
        }
        Stop-Process -Id $proc.ProcessId -Force -ErrorAction SilentlyContinue
    }

    Start-Sleep -Milliseconds 500
    if ($restartExplorer) {
        Start-Process explorer.exe
    }
}

if (-not $SkipStopAppProcesses) {
    $manifestPath = Join-Path $root "MC.Xbox\Package.appxmanifest"
    [xml]$manifest = Get-Content $manifestPath
    $packageName = $manifest.Package.Identity.Name
    $lockPaths = if ($StopFileLockers) { @((Join-Path $outDir $ProjectConfig.AppxFileName)) } else { @() }
    Stop-BuildBlockingProcesses `
        -PackageName $packageName `
        -RootPath $root `
        -PackageContentPath $pkg `
        -OutputPath (Join-Path $outDir $ProjectConfig.AppxFileName) `
        -LockPaths $lockPaths
}

New-Item -ItemType Directory -Force -Path $buildDir, $outDir, $certDir, $mcBuildDir, $glfwBuildDir | Out-Null

Write-Host "=== Generating runtime_config.h ==="
# Token-substitute MC.Xbox/runtime_config.h.in into the build dir. App.cpp
# picks it up via the INCLUDE path below. Regenerated every build so the
# header always matches the currently selected MC version.
$runtimeConfigTemplate = Join-Path $root "MC.Xbox\runtime_config.h.in"
$runtimeConfigOutput   = Join-Path $mcBuildDir "runtime_config.h"
if (-not (Test-Path $runtimeConfigTemplate)) { throw "runtime_config.h.in not found at $runtimeConfigTemplate" }
$runtimeConfigContent = [System.IO.File]::ReadAllText($runtimeConfigTemplate)
$runtimeConfigContent = $runtimeConfigContent.Replace('@@MC_VERSION@@',           $ProjectConfig.MinecraftVersion)
$runtimeConfigContent = $runtimeConfigContent.Replace('@@FABRIC_LOADER_VERSION@@', $ProjectConfig.FabricLoaderVersion)
$runtimeConfigContent = $runtimeConfigContent.Replace('@@MC_ASSET_INDEX@@',       $ProjectConfig.MinecraftAssetIndex)
if ($runtimeConfigContent -match '@@[A-Z_]+@@') { throw "runtime_config.h still contains unsubstituted tokens after generation: $($Matches[0])" }
[System.IO.File]::WriteAllText($runtimeConfigOutput, $runtimeConfigContent)
Write-Host "runtime_config.h written for MC $($ProjectConfig.MinecraftVersion) / fabric-loader $($ProjectConfig.FabricLoaderVersion) / asset index $($ProjectConfig.MinecraftAssetIndex)"

Write-Host "=== Building MC.Xbox.exe ==="
Push-Location (Join-Path $root "MC.Xbox")

$env:INCLUDE = "$mcBuildDir;$($tools.MsvcRoot)\include;${sdkRoot}Include\$sdkVer\ucrt;${sdkRoot}Include\$sdkVer\shared;${sdkRoot}Include\$sdkVer\um;${sdkRoot}Include\$sdkVer\winrt;${sdkRoot}Include\$sdkVer\cppwinrt;$jreSrc\include;$jreSrc\include\win32"
$env:LIB = "$($tools.MsvcRoot)\lib\x64;${sdkRoot}Lib\$sdkVer\ucrt\x64;${sdkRoot}Lib\$sdkVer\um\x64"

& $tools.ClExe App.cpp /std:c++17 /EHsc /W3 /O2 /D_UNICODE /DUNICODE /D_WIN32_WINNT=0x0A00 /D_SILENCE_EXPERIMENTAL_COROUTINE_DEPRECATION_WARNINGS /Fo"$mcBuildDir\" `
    /DWINAPI_FAMILY=WINAPI_FAMILY_APP `
    /link /SUBSYSTEM:WINDOWS /ENTRY:wWinMainCRTStartup /MACHINE:X64 `
    /OUT:"$mcExe" kernel32.lib shell32.lib runtimeobject.lib windowsapp.lib ole32.lib oleaut32.lib d2d1.lib dwrite.lib d3d11.lib dxgi.lib windowscodecs.lib
if ($LASTEXITCODE -ne 0) { throw "Compile failed" }
Pop-Location
Write-Host "MC.Xbox.exe built"

Write-Host "=== Building GLFW CoreWindow shim ==="
& (Join-Path $root "glfw_shim\build_glfw.ps1") -OutputDir $glfwBuildDir

Write-Host "=== Building Xbox compatibility mod ==="
& (Join-Path $root "compat_mod\build_compat_mod.ps1")

Write-Host "=== Patching Fabric Loader for Xbox filesystem ==="
& (Join-Path $root "scripts\patch-fabric.ps1")

Write-Host "=== Assembling PackageContent ==="
Remove-Item -Recurse -Force $pkg -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path (Join-Path $pkg "Assets") | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $pkg "natives") | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $pkg "graphics\mesa") | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $pkg "graphics\xboxone") | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $pkg "assets") | Out-Null
# runtime/ holds the immutable game stack (libraries, versions, fabric remapped
# jars, bundled mods, log configs). Writable state (saves, mods folder,
# config, logs) lives in LocalState and is set up by App.cpp at startup.
New-Item -ItemType Directory -Force -Path (Join-Path $pkg "runtime") | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $pkg "runtime\log_configs") | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $pkg "runtime\bundled-mods") | Out-Null

Copy-Item $mcExe (Join-Path $pkg "MC.Xbox.exe")
Copy-Item (Join-Path $root "MC.Xbox\Package.appxmanifest") (Join-Path $pkg "AppxManifest.xml")

Write-Host "Copying runtime files..."
Copy-Item -Recurse (Join-Path $gameDir "libraries") (Join-Path $pkg "runtime\libraries")
Copy-Item -Recurse (Join-Path $gameDir "versions")  (Join-Path $pkg "runtime\versions")
# Bundled mods (compat mod, optionally diagnostics) live under runtime\bundled-mods.
# App.cpp copies them into LocalState\game\mods on launch.
Copy-Item -Recurse (Join-Path $gameDir "mods\*") (Join-Path $pkg "runtime\bundled-mods\") -Force
if (Test-Path (Join-Path $gameDir ".fabric")) {
    Copy-Item -Recurse (Join-Path $gameDir ".fabric") (Join-Path $pkg "runtime\.fabric")
}

$remapped = Join-Path $gameDir ".fabric\remappedJars"
if (Test-Path $remapped) {
    Write-Host "Copying .fabric remapped jars..."
    New-Item -ItemType Directory -Force (Join-Path $pkg "runtime\.fabric\remappedJars") | Out-Null
    Copy-Item -Recurse (Join-Path $remapped "*") (Join-Path $pkg "runtime\.fabric\remappedJars\") -Force
}

Write-Host "Copying natives..."
Copy-Item (Join-Path $nativesSourceDir "*.dll") (Join-Path $pkg "natives\")

Write-Host "Extracting JNA native..."
Add-Type -AssemblyName System.IO.Compression.FileSystem
$jnaVersion = $ProjectConfig.JnaVersion
$jnaJar = Join-Path $gameDir "libraries\net\java\dev\jna\jna\$jnaVersion\jna-$jnaVersion.jar"
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
$lwjglGlfwVersion = $ProjectConfig.LwjglGlfwVersion
$glfwJar  = Join-Path $pkg "runtime\libraries\org\lwjgl\lwjgl-glfw\$lwjglGlfwVersion\lwjgl-glfw-$lwjglGlfwVersion-natives-windows.jar"
$jarExe   = Join-Path $jreSrc "bin\jar.exe"
if (-not (Test-Path $jarExe)) { $jarExe = "jar" }

if (Test-Path $glfwJar) {
    # Extract JAR into a fresh temp dir, replace glfw.dll, repack
    $jarTmpDir = Join-Path $buildDir "glfw_jar_tmp"
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
$mesaRuntime = Resolve-MesaRuntimeDir -MesaRuntimeDir $MesaRuntimeDir
Write-Host "Mesa runtime source: $mesaRuntime"
foreach ($dll in Get-MesaRuntimeDllNames) {
    $source = Join-Path $mesaRuntime $dll
    if (Test-Path $source) {
        Copy-Item $source (Join-Path $pkg $dll) -Force
        Copy-Item $source (Join-Path $pkg "natives\$dll") -Force
        Copy-Item $source (Join-Path $pkg "graphics\mesa\$dll") -Force
        Write-Host "Mesa: $dll"
    }
}

Write-Host "Copying Xbox One graphics runtime..."
$xboxOneRuntime = Resolve-XboxOneGraphicsRuntimeDir -XboxOneGraphicsRuntimeDir $XboxOneGraphicsRuntimeDir
if ($xboxOneRuntime) {
    Write-Host "Xbox One graphics runtime source: $xboxOneRuntime"
    foreach ($dll in Get-XboxOneGraphicsRuntimeDllNames) {
        $source = Join-Path $xboxOneRuntime $dll
        if (Test-Path $source) {
            Copy-Item $source (Join-Path $pkg "graphics\xboxone\$dll") -Force
            Write-Host "Xbox One graphics: $dll"
        }
    }
} else {
    $partialXboxOneRuntime = @($XboxOneGraphicsRuntimeDir, $env:XBOX_ONE_GRAPHICS_RUNTIME_DIR, (Get-ConfigPath "XboxOneGraphicsRuntimeDir")) |
        Where-Object { $_ -and (Test-Path $_) } |
        Select-Object -First 1
    if ($partialXboxOneRuntime) {
        Write-Warning "Xbox One graphics runtime found at '$partialXboxOneRuntime', but it is missing opengl32.dll, libEGL.dll, or libGLESv2.dll. Package will keep the Series/Mesa runtime only until a MobileGlues opengl32.dll is added."
    } else {
        Write-Warning "Xbox One graphics runtime not found. Set -XboxOneGraphicsRuntimeDir or XBOX_ONE_GRAPHICS_RUNTIME_DIR after building/adding MobileGlues opengl32.dll."
    }
}

Write-Host "Copying assets..."
Copy-Item -Recurse -Force (Join-Path $assetsDir "*") (Join-Path $pkg "assets\")
Copy-Item -Force (Join-Path $root "log_configs\client-uwp.xml") (Join-Path $pkg "runtime\log_configs\client-uwp.xml")

$panoramaSource = Join-Path $root "MC.Xbox\Assets\panorama"
if (Test-Path $panoramaSource) {
    $panoramaTarget = Join-Path $pkg "Assets\panorama"
    New-Item -ItemType Directory -Force -Path $panoramaTarget | Out-Null
    Copy-Item -Force (Join-Path $panoramaSource "panorama_*.png") $panoramaTarget
    if (Test-Path (Join-Path $panoramaSource "panorama_overlay.png")) {
        Copy-Item -Force (Join-Path $panoramaSource "panorama_overlay.png") $panoramaTarget
    }
    Write-Host "Copied menu panorama assets from $panoramaSource"
}

Write-Host "Copying JRE..."
Write-Host "JRE source: $jreSrc"
Copy-Item -Recurse $jreSrc (Join-Path $pkg "jre")
Copy-Item (Join-Path $root "xbox_security.properties") (Join-Path $pkg "jre\conf\security\xbox.properties")

Write-Host "Generating UWP tile assets..."
& $pythonExe (Join-Path $root "scripts\generate-assets.py") $pkg
if ($LASTEXITCODE -ne 0) { throw "Asset generation failed" }

Write-Host "=== Packaging ==="
$cert = Join-Path $certDir $ProjectConfig.CertificateFileName
$appx = Join-Path $outDir $ProjectConfig.AppxFileName
$certName = if ($env:APPX_CERT_SUBJECT) { $env:APPX_CERT_SUBJECT } else { $ProjectConfig.DefaultCertificateSubject }

if (-not (Test-Path $cert)) {
    $c = New-SelfSignedCertificate -Type CodeSigningCert -Subject $certName `
        -KeyUsage DigitalSignature -CertStoreLocation "Cert:\CurrentUser\My" `
        -TextExtension @("2.5.29.37={text}1.3.6.1.5.5.7.3.3","2.5.29.19={text}")
    Export-PfxCertificate -Cert $c -FilePath $cert `
        -Password (ConvertTo-SecureString $ProjectConfig.CertificatePassword -AsPlainText -Force) | Out-Null
    Write-Host "Generated cert"
}

$allSigningCertCandidates = Get-ChildItem Cert:\CurrentUser\My |
    Where-Object {
        $_.HasPrivateKey -and
        ($_.EnhancedKeyUsageList | Where-Object { $_.FriendlyName -eq 'Code Signing' })
    }
$banditVaultSigningCertCandidates = $allSigningCertCandidates | Where-Object { $_.Subject -like '*BanditVault*' } | Sort-Object NotBefore -Descending
$otherSigningCertCandidates = $allSigningCertCandidates | Where-Object { $_.Subject -notlike '*BanditVault*' } | Sort-Object NotBefore -Descending
$signingCertCandidates = @($banditVaultSigningCertCandidates) + @($otherSigningCertCandidates)
if (-not $signingCertCandidates) {
    throw "Signing certificate not found in the current user certificate store."
}

$makeappx = Get-ChildItem "${sdkRoot}bin\$sdkVer\x64\makeappx.exe","${sdkRoot}bin\10.0.26100.0\x64\makeappx.exe" -ErrorAction SilentlyContinue | Select-Object -First 1 -ExpandProperty FullName
if (-not $makeappx) {
    $cmd = Get-Command makeappx -ErrorAction SilentlyContinue
    if ($cmd) { $makeappx = $cmd.Source }
}
if (-not $makeappx) { throw "makeappx.exe not found. Add Windows SDK bin to PATH." }
$signtool = Get-ChildItem "${sdkRoot}bin\$sdkVer\x64\signtool.exe","${sdkRoot}bin\10.0.26100.0\x64\signtool.exe" -ErrorAction SilentlyContinue | Select-Object -First 1 -ExpandProperty FullName
if (-not $signtool) { $signtool = "signtool" }

if (-not $SkipStopAppProcesses) {
    $lockPaths = if ($StopFileLockers) { @($appx) } else { @() }
    Stop-BuildBlockingProcesses `
        -PackageName $packageName `
        -RootPath $root `
        -PackageContentPath $pkg `
        -OutputPath $appx `
        -LockPaths $lockPaths
}

& $makeappx pack /d $pkg /p $appx /overwrite
if ($LASTEXITCODE -ne 0) { throw "MakeAppx failed" }

function Invoke-AppxSign {
    param(
        [Parameter(Mandatory = $true)]
        [string]$AppxPath,

        [Parameter(Mandatory = $true)]
        [string]$CertificateThumbprint,

        [Parameter(Mandatory = $true)]
        [string]$SigntoolPath
    )

    & $SigntoolPath sign /fd SHA256 /sha1 $CertificateThumbprint $AppxPath
    return ($LASTEXITCODE -eq 0)
}

$signingSucceeded = $false
foreach ($signingCert in $signingCertCandidates) {
    if (Invoke-AppxSign -AppxPath $appx -CertificateThumbprint $signingCert.Thumbprint -SigntoolPath $signtool) {
        $signingSucceeded = $true
        Write-Host "Signed appx with $($signingCert.Subject)"
        break
    }
}

if (-not $signingSucceeded) {
    Write-Warning "Appx signing failed with the existing store certificates; generating a fresh dev certificate and retrying once."
    Remove-Item $cert -Force -ErrorAction SilentlyContinue

    $c = New-SelfSignedCertificate -Type CodeSigningCert -Subject $certName `
        -KeyUsage DigitalSignature -CertStoreLocation "Cert:\CurrentUser\My" `
        -TextExtension @("2.5.29.37={text}1.3.6.1.5.5.7.3.3","2.5.29.19={text}")
    Export-PfxCertificate -Cert $c -FilePath $cert `
        -Password (ConvertTo-SecureString $ProjectConfig.CertificatePassword -AsPlainText -Force) | Out-Null

    if (-not (Invoke-AppxSign -AppxPath $appx -CertificateThumbprint $c.Thumbprint -SigntoolPath $signtool)) {
        throw "Appx signing failed"
    }
}
if (-not (Test-Path $appx)) { throw "Appx package was not created" }

if (-not $KeepStaging) {
    Remove-Item -Recurse -Force $pkg -ErrorAction SilentlyContinue
    Write-Host "Removed staging package directory"
}

Write-Host ""
Write-Host "=== Done ==="
Write-Host "Package: $appx"
