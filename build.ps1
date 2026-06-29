# build.ps1 - Build and package MC Java UWP
param(
    [string]$MesaRuntimeDir = $env:MESA_UWP_DIR,
    [string]$McVersion,
    [string]$FabricLoader,
    [string]$AssetIndex,
    [string]$AppxVersion = $env:APPX_VERSION,
    [switch]$KeepStaging,
    [switch]$SkipStopAppProcesses,
    [switch]$StopFileLockers,
    [switch]$SkipVersionManifests,
    [switch]$SkipVersionCompat,
    [switch]$IncludePrebuiltNeoForgeArtifacts
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
$nativesSourceDir = Get-ConfigPath "NativesDir"
$certDir = Get-ConfigPath "CertificateDir"
$mcBuildDir = Join-Path $buildDir "MC.Xbox"
$glfwBuildDir = Join-Path $buildDir "glfw_shim"
$mouseSupportBuildDir = Join-Path $buildDir "mouse_support"
$mouseSupportDll = Join-Path $mouseSupportBuildDir "mouse_support.dll"
$mouseSupportLib = Join-Path $mouseSupportBuildDir "mouse_support.lib"
$mcExe = Join-Path $mcBuildDir "MC.Xbox.exe"
$shimDll = Join-Path $glfwBuildDir "glfw.dll"
$jreSrc = Resolve-JavaHome
$jre21Src = Resolve-JavaHomeExact -MajorVersion 21
$jarExe = Join-Path $jreSrc "bin\jar.exe"
if (-not (Test-Path $jarExe)) { $jarExe = "jar" }
$pythonExe = Resolve-Python
$tools = Resolve-VSTools
$sdk = Resolve-WindowsSdk
$sdkRoot = $sdk.Root
$sdkVer = $sdk.Version

function Assert-AppxVersion {
    param([Parameter(Mandatory = $true)][string]$Version)

    $parts = $Version -split '\.'
    if ($parts.Count -ne 4) {
        throw "APPX version must have four numeric fields: $Version"
    }
    foreach ($part in $parts) {
        $value = 0
        if (-not [int]::TryParse($part, [ref]$value) -or $value -lt 0 -or $value -gt 65535) {
            throw "APPX version field is out of range 0..65535: $Version"
        }
    }
}

$manifestSourcePath = Join-Path $root "MC.Xbox\Package.appxmanifest"
[xml]$sourceManifest = Get-Content $manifestSourcePath
$baseVersionParts = ([string]$sourceManifest.Package.Identity.Version) -split '\.'
if ($baseVersionParts.Count -ne 4) {
    throw "Package.appxmanifest Identity Version must have four numeric fields."
}
$appVersionBase = "$($baseVersionParts[0]).$($baseVersionParts[1]).$($baseVersionParts[2])"

# Auto-increment local builds from the package manifest base version so installs
# update in place while CI can provide an exact APPX_VERSION for nightlies.
$verFile = Join-Path $root ".local\app_build.txt"
if ($AppxVersion) {
    Assert-AppxVersion $AppxVersion
    $appVersion = $AppxVersion
} else {
    $verRev = [int]$baseVersionParts[3]
    if (Test-Path $verFile) {
        $prevParts = ((Get-Content $verFile -Raw).Trim()) -split '\.'
        $prevBase = if ($prevParts.Count -eq 4) { "$($prevParts[0]).$($prevParts[1]).$($prevParts[2])" } else { "" }
        if ($prevBase -eq $appVersionBase) { $verRev = [int]$prevParts[3] + 1 }
    }
    if ($verRev -gt 65535) { $verRev = 65535 }
    $appVersion = "$appVersionBase.$verRev"
}
New-Item -ItemType Directory -Force -Path (Split-Path $verFile) | Out-Null
Set-Content -Path $verFile -Value $appVersion -NoNewline
$appx = Join-Path $outDir ("BanditLauncher_{0}.appx" -f $appVersion)

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
    $packageName = $sourceManifest.Package.Identity.Name
    $lockPaths = if ($StopFileLockers) { @($appx) } else { @() }
    Stop-BuildBlockingProcesses `
        -PackageName $packageName `
        -RootPath $root `
        -PackageContentPath $pkg `
        -OutputPath $appx `
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

& $tools.ClExe App.cpp launch\app_globals.cpp common\launcher_common.cpp common\crash_report.cpp mods\mod_defaults.cpp mods\modpack_io.cpp mods\world_io.cpp net\http_client.cpp profiles\profiles.cpp net\remote_file_server.cpp net\web_relay_server.cpp auth\minecraft_auth.cpp ui\launcher_ui.cpp ui\launcher_mouse.cpp ui\mods_ui_globals.cpp mods\mods_browser.cpp launch\runtime_manager.cpp launch\minecraft_launch.cpp launch\launch_internal.cpp launch\loaders\loader_common.cpp launch\loaders\loader.cpp launch\loaders\fabric.cpp launch\loaders\neoforge.cpp launch\loaders\forge.cpp third_party\miniz\miniz.c /std:c++17 /EHsc /W3 /O2 /GL /Gw /MP /arch:AVX2 /DNDEBUG /D_UNICODE /DUNICODE /D_WIN32_WINNT=0x0A00 /D_SILENCE_EXPERIMENTAL_COROUTINE_DEPRECATION_WARNINGS /DMINIZ_NO_STDIO /DMINIZ_NO_TIME /I. /Icommon /Inet /Iauth /Iui /Imods /Iprofiles /Ilaunch /Ilaunch\loaders /I..\mouse_support /Fo"$mcBuildDir\" `
    /DWINAPI_FAMILY=WINAPI_FAMILY_APP `
    /link /LTCG /SUBSYSTEM:WINDOWS /ENTRY:wWinMainCRTStartup /MACHINE:X64 `
    /OUT:"$mcExe" kernel32.lib shell32.lib runtimeobject.lib windowsapp.lib ole32.lib oleaut32.lib d2d1.lib dwrite.lib d3d11.lib dxgi.lib windowscodecs.lib winhttp.lib bcrypt.lib ws2_32.lib
if ($LASTEXITCODE -ne 0) { throw "Compile failed" }
Pop-Location
Write-Host "MC.Xbox.exe built"

Write-Host "=== Building mouse support DLL ==="
& (Join-Path $root "mouse_support\build_mouse_support.ps1") -OutputDir $mouseSupportBuildDir
if (-not (Test-Path $mouseSupportDll)) { throw "mouse_support DLL missing after build: $mouseSupportDll" }

Write-Host "=== Building GLFW CoreWindow shim ==="
& (Join-Path $root "glfw_shim\build_glfw.ps1") -OutputDir $glfwBuildDir -MouseSupportLib $mouseSupportLib -MouseSupportInclude (Join-Path $root "mouse_support")
if (-not (Test-Path $shimDll)) { throw "GLFW shim DLL missing after build: $shimDll" }

Write-Host "=== Building Xbox compatibility mod ==="
& (Join-Path $root "compat_mod\build_compat_mod.ps1")

Write-Host "=== Building default Fabric controller mod ==="
& (Join-Path $root "controller_mod\fabric\build_fabric_controller_mod.ps1") `
    -MinecraftVersion $ProjectConfig.MinecraftVersion `
    -LoaderVersion $ProjectConfig.FabricLoaderVersion `
    -OutputDir (Join-Path $gameDir "mods")

Write-Host "=== Patching Fabric Loader for Xbox filesystem ==="
& (Join-Path $root "scripts\patch-fabric.ps1")

Write-Host "=== Assembling PackageContent ==="
Remove-Item -Recurse -Force $pkg -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path (Join-Path $pkg "Assets") | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $pkg "natives") | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $pkg "graphics\mesa") | Out-Null
# runtime/ holds only launcher-owned or intentionally patched runtime pieces.
# Mojang/Fabric game files are downloaded into LocalState after auth.
New-Item -ItemType Directory -Force -Path (Join-Path $pkg "runtime") | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $pkg "runtime\log_configs") | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $pkg "runtime\bundled-mods") | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $pkg "runtime\libraries") | Out-Null

Copy-Item $mcExe (Join-Path $pkg "MC.Xbox.exe")

$manifestOut = Join-Path $pkg "AppxManifest.xml"
$manifestText = [System.IO.File]::ReadAllText($manifestSourcePath)
$manifestText = [regex]::Replace($manifestText, '(<Identity\b[^>]*\bVersion=")\d+\.\d+\.\d+\.\d+(")', ('${1}' + $appVersion + '${2}'))
[System.IO.File]::WriteAllText($manifestOut, $manifestText)
Write-Host "App package version: $appVersion"

Write-Host "Copying launcher-owned runtime files..."
$versionCatalogSource = Join-Path $root $ProjectConfig.VersionCatalog
if (-not (Test-Path $versionCatalogSource)) {
    throw "Version catalog not found at $versionCatalogSource"
}
Copy-Item $versionCatalogSource (Join-Path $pkg "runtime\version_catalog.tsv") -Force
Write-Host "Version catalog: $versionCatalogSource"

# NeoForge client jars are derived from the Minecraft client. Keep them out of normal and
# nightly builds; opt in only for private diagnostics while on-device generation is being fixed.
$prebuiltLibs = Join-Path $root "prebuilt\neoforge\libraries"
if (($IncludePrebuiltNeoForgeArtifacts -or $env:BANDIT_INCLUDE_PREBUILT_NEOFORGE_ARTIFACTS -eq "1") -and (Test-Path $prebuiltLibs)) {
    $prebuiltDst = Join-Path $pkg "runtime\libraries"
    Get-ChildItem $prebuiltLibs -Recurse -File | ForEach-Object {
        $rel = $_.FullName.Substring($prebuiltLibs.Length).TrimStart('\')
        $dst = Join-Path $prebuiltDst $rel
        New-Item -ItemType Directory -Force -Path (Split-Path $dst -Parent) | Out-Null
        Copy-Item $_.FullName $dst -Force
    }
    Write-Host "Packaged prebuilt NeoForge client jars from $prebuiltLibs"
} elseif (Test-Path $prebuiltLibs) {
    Write-Host "Skipping prebuilt NeoForge client jars. Use -IncludePrebuiltNeoForgeArtifacts for private diagnostics."
}

function Ensure-FabricLoaderJar {
    param([Parameter(Mandatory = $true)][string]$LoaderVersion)

    $loaderRelative = "net\fabricmc\fabric-loader\$LoaderVersion\fabric-loader-$LoaderVersion.jar"
    $loaderSrc = Join-Path $gameDir "libraries\$loaderRelative"
    if (-not (Test-Path $loaderSrc)) {
        $loaderUrl = "https://maven.fabricmc.net/net/fabricmc/fabric-loader/$LoaderVersion/fabric-loader-$LoaderVersion.jar"
        Write-Host "Downloading Fabric loader $LoaderVersion"
        New-Item -ItemType Directory -Force -Path (Split-Path $loaderSrc -Parent) | Out-Null
        Invoke-WebRequest -UseBasicParsing -Uri $loaderUrl -OutFile $loaderSrc -TimeoutSec 60
    }

    & (Join-Path $root "scripts\patch-fabric.ps1") -LoaderVersion $LoaderVersion
    if ($LASTEXITCODE -ne 0) { throw "Fabric loader patch failed for $LoaderVersion" }

    $loaderDst = Join-Path $pkg "runtime\libraries\$loaderRelative"
    New-Item -ItemType Directory -Force -Path (Split-Path $loaderDst -Parent) | Out-Null
    Copy-Item $loaderSrc $loaderDst -Force
    Write-Host "Packaged patched Fabric loader $LoaderVersion"
}

function Ensure-TinyRemapperJar {
    param([Parameter(Mandatory = $true)][string]$TinyRemapperVersion)

    $tinyRelative = "net\fabricmc\tiny-remapper\$TinyRemapperVersion\tiny-remapper-$TinyRemapperVersion.jar"
    $tinySrc = Join-Path $gameDir "libraries\$tinyRelative"
    if (-not (Test-Path $tinySrc)) {
        $tinyUrl = "https://maven.fabricmc.net/net/fabricmc/tiny-remapper/$TinyRemapperVersion/tiny-remapper-$TinyRemapperVersion.jar"
        Write-Host "Downloading TinyRemapper $TinyRemapperVersion"
        New-Item -ItemType Directory -Force -Path (Split-Path $tinySrc -Parent) | Out-Null
        Invoke-WebRequest -UseBasicParsing -Uri $tinyUrl -OutFile $tinySrc -TimeoutSec 60
    }

    Write-Host "Patching TinyRemapper $TinyRemapperVersion for Xbox filesystem..."
    $java = Resolve-JavaHome
    $jarExe = Join-Path $java "bin\jar.exe"
    $tmp = Join-Path $buildDir "patch-tinyremapper\$TinyRemapperVersion"
    $srcTmp = Join-Path $tmp "src\net\fabricmc\tinyremapper"
    $classesTmp = Join-Path $tmp "classes"
    $jarTmp = Join-Path $tmp "jar"
    $patchedTiny = Join-Path $tmp "tiny-remapper-$TinyRemapperVersion-patched.jar"
    Remove-Item -Recurse -Force $tmp -ErrorAction SilentlyContinue
    New-Item -ItemType Directory -Force -Path $srcTmp, $classesTmp, $jarTmp | Out-Null

    foreach ($name in @("FileSystemReference.java", "FileSystemHandler.java", "OutputConsumerPath.java")) {
        $sourcePath = Join-Path $root "patch\$name"
        $sourceText = [System.IO.File]::ReadAllText($sourcePath)
        $sourceText = $sourceText.Replace(
            "package net.fabricmc.loader.impl.lib.tinyremapper;",
            "package net.fabricmc.tinyremapper;")
        [System.IO.File]::WriteAllText((Join-Path $srcTmp $name), $sourceText)
    }

    & (Join-Path $java "bin\javac.exe") --release 21 -cp $tinySrc -d $classesTmp `
        (Join-Path $srcTmp "FileSystemReference.java") `
        (Join-Path $srcTmp "FileSystemHandler.java") `
        (Join-Path $srcTmp "OutputConsumerPath.java")
    if ($LASTEXITCODE -ne 0) { throw "TinyRemapper patch compile failed for $TinyRemapperVersion" }

    Push-Location $jarTmp
    & $jarExe xf $tinySrc
    Pop-Location
    if ($LASTEXITCODE -ne 0) { throw "TinyRemapper JAR extract failed for $TinyRemapperVersion" }

    $classFiles = Get-ChildItem -LiteralPath $classesTmp -Recurse -Filter "*.class"
    foreach ($classFile in $classFiles) {
        $relativePath = $classFile.FullName.Substring($classesTmp.Length).TrimStart('\', '/')
        $dst = Join-Path $jarTmp $relativePath
        New-Item -ItemType Directory -Force -Path (Split-Path $dst -Parent) | Out-Null
        Copy-Item -LiteralPath $classFile.FullName -Destination $dst -Force
        Write-Host "  injected $($relativePath.Replace('\', '/'))"
    }

    $metaInf = Join-Path $jarTmp "META-INF"
    if (Test-Path $metaInf) {
        Get-ChildItem -LiteralPath $metaInf -File |
            Where-Object { $_.Name -match '\.(SF|RSA|DSA|EC)$' } |
            ForEach-Object { Remove-Item -LiteralPath $_.FullName -Force }
    }
    $manifest = Join-Path $jarTmp "META-INF\MANIFEST.MF"
    $manifestCopy = Join-Path $tmp "MANIFEST.MF"
    if (Test-Path $manifest) {
        Copy-Item -LiteralPath $manifest -Destination $manifestCopy -Force
        Remove-Item -LiteralPath $manifest -Force
    }
    if (Test-Path $manifestCopy) {
        & $jarExe cfm $patchedTiny $manifestCopy -C $jarTmp .
    } else {
        & $jarExe cf $patchedTiny -C $jarTmp .
    }
    if ($LASTEXITCODE -ne 0) { throw "TinyRemapper JAR repack failed for $TinyRemapperVersion" }

    $tinyDst = Join-Path $pkg "runtime\libraries\$tinyRelative"
    New-Item -ItemType Directory -Force -Path (Split-Path $tinyDst -Parent) | Out-Null
    Copy-Item $patchedTiny $tinyDst -Force
    Write-Host "Packaged patched TinyRemapper $TinyRemapperVersion"
}

$fabricTargets = @(
    Import-Csv -Path $versionCatalogSource -Delimiter "`t" |
        Where-Object { $_.loader -eq "fabric" -and $_.loaderVersion -and $_.loaderVersion -ne "selected" -and $_.loaderVersion -ne "none" }
)
$forgeTargets = @(
    Import-Csv -Path $versionCatalogSource -Delimiter "`t" |
        Where-Object { $_.loader -eq "forge" -and $_.loaderVersion -and $_.loaderVersion -ne "selected" -and $_.loaderVersion -ne "none" }
)
$neoForgeTargets = @(
    Import-Csv -Path $versionCatalogSource -Delimiter "`t" |
        Where-Object { $_.loader -eq "neoforge" -and $_.loaderVersion -and $_.loaderVersion -ne "selected" -and $_.loaderVersion -ne "none" }
)
$manifestTargets = @(
    Import-Csv -Path $versionCatalogSource -Delimiter "`t" |
        Where-Object { $_.loader -and $_.loaderVersion -and $_.loaderVersion -ne "selected" -and $_.loaderVersion -ne "none" }
)
function Test-ForgeControllerTarget {
    param([Parameter(Mandatory = $true)]$Target)

    return $Target.loader -eq "forge" -and $Target.controllerProvider -eq "forge"
}

function Test-FabricControllerTarget {
    param([Parameter(Mandatory = $true)]$Target)

    return $Target.loader -eq "fabric" -and $Target.controllerProvider -eq "fabric"
}

function Test-NeoForgeControllerTarget {
    param([Parameter(Mandatory = $true)]$Target)

    return $Target.loader -eq "neoforge" -and $Target.controllerProvider -eq "neoforge"
}

$fabricLoaderVersions = @($ProjectConfig.FabricLoaderVersion) + @($fabricTargets | ForEach-Object { $_.loaderVersion }) |
    Where-Object { $_ } |
    Select-Object -Unique
foreach ($loaderVersion in $fabricLoaderVersions) {
    try {
        Ensure-FabricLoaderJar -LoaderVersion $loaderVersion
    } catch {
        if ($loaderVersion -eq $ProjectConfig.FabricLoaderVersion) {
            throw
        }
        Write-Warning "Skipping patched Fabric loader ${loaderVersion}: $($_.Exception.Message)"
    }
}
if ($fabricLoaderVersions -contains "0.14.25") {
    Ensure-TinyRemapperJar -TinyRemapperVersion "0.8.2"
}
# Bundled mods (compat mod, optionally diagnostics) live under runtime\bundled-mods.
# App.cpp copies them into LocalState\game\mods on launch.
Copy-Item -Recurse (Join-Path $gameDir "mods\*") (Join-Path $pkg "runtime\bundled-mods\") -Force

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

Write-Host "Copying GLFW shim..."
Copy-Item $shimDll (Join-Path $pkg "natives\glfw.dll") -Force
Copy-Item $mouseSupportDll (Join-Path $pkg "mouse_support.dll") -Force
Copy-Item $mouseSupportDll (Join-Path $pkg "natives\mouse_support.dll") -Force

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

Write-Host "Generating official download manifest..."
& (Join-Path $root "scripts\new-download-manifest.ps1") `
    -MinecraftVersion $ProjectConfig.MinecraftVersion `
    -Loader "fabric" `
    -LoaderVersion $ProjectConfig.FabricLoaderVersion `
    -FabricLoaderVersion $ProjectConfig.FabricLoaderVersion `
    -OutputPath (Join-Path $pkg "download_manifest.tsv")

$manifestsDir = Join-Path $pkg "runtime\manifests"
New-Item -ItemType Directory -Force -Path $manifestsDir | Out-Null
$defaultTargetId = "$($ProjectConfig.MinecraftVersion)-fabric-$($ProjectConfig.FabricLoaderVersion)"
Copy-Item -Force (Join-Path $pkg "download_manifest.tsv") (Join-Path $manifestsDir "$defaultTargetId.tsv")
Write-Host "Default per-version manifest: $defaultTargetId.tsv"

if (-not $SkipVersionManifests) {
    foreach ($row in $manifestTargets) {
        $lv = $row.loaderVersion
        $loader = $row.loader.ToLowerInvariant()
        $targetId = "$($row.minecraftVersion)-$loader-$lv"
        if ($targetId -eq $defaultTargetId) { continue }
        $out = Join-Path $manifestsDir "$targetId.tsv"
        Write-Host "Generating per-version manifest: $targetId"
        try {
            & (Join-Path $root "scripts\new-download-manifest.ps1") `
                -MinecraftVersion $row.minecraftVersion `
                -Loader $loader `
                -LoaderVersion $lv `
                -OutputPath $out
        } catch {
            Write-Warning "Skipping ${targetId}: $($_.Exception.Message)"
            if (Test-Path $out) { Remove-Item -Force $out }
        }
    }
} else {
    Write-Host "Skipping extra per-version manifests (-SkipVersionManifests)"
}

if (-not $SkipVersionCompat) {
    $versionModsRoot = Join-Path $pkg "runtime\version-mods"
    New-Item -ItemType Directory -Force -Path $versionModsRoot | Out-Null
    foreach ($row in $fabricTargets) {
        $lv = $row.loaderVersion
        $targetId = "$($row.minecraftVersion)-fabric-$lv"
        if ($targetId -eq $defaultTargetId) { continue }
        $outDir = Join-Path $versionModsRoot $targetId
        Write-Host "Building per-version compat mod: $targetId"
        try {
            & (Join-Path $root "compat_mod\build_compat_mod.ps1") `
                -MinecraftVersion $row.minecraftVersion `
                -LoaderVersion $lv `
                -OutputDir $outDir
        } catch {
            Write-Warning "Per-version compat mod skipped for ${targetId}: $($_.Exception.Message)"
            if (Test-Path $outDir) { Remove-Item -Recurse -Force $outDir }
        }
        if (Test-FabricControllerTarget -Target $row) {
            Write-Host "Building per-version Fabric controller mod: $targetId"
            try {
                & (Join-Path $root "controller_mod\fabric\build_fabric_controller_mod.ps1") `
                    -MinecraftVersion $row.minecraftVersion `
                    -LoaderVersion $lv `
                    -OutputDir $outDir
            } catch {
                Write-Warning "Per-version Fabric controller mod skipped for ${targetId}: $($_.Exception.Message)"
            }
        } else {
            Write-Host "Skipping per-version Fabric controller mod for ${targetId}: no bundled controller provider for this target"
        }
    }
    foreach ($row in $forgeTargets) {
        $lv = $row.loaderVersion
        $targetId = "$($row.minecraftVersion)-forge-$lv"
        if (-not (Test-ForgeControllerTarget -Target $row)) {
            Write-Host "Skipping per-version forge controller mod for ${targetId}: no bundled controller provider for this target"
            continue
        }
        $outDir = Join-Path $versionModsRoot $targetId
        Write-Host "Building per-version forge controller mod: $targetId"
        try {
            & (Join-Path $root "controller_mod\forge\build_forge_controller_mod.ps1") `
                -MinecraftVersion $row.minecraftVersion `
                -ForgeVersion "$($row.minecraftVersion)-$lv" `
                -OutputDir $outDir
        } catch {
            Write-Warning "Per-version Forge controller mod skipped for ${targetId}: $($_.Exception.Message)"
            if (Test-Path $outDir) { Remove-Item -Recurse -Force $outDir }
        }
    }
    foreach ($row in $neoForgeTargets) {
        $lv = $row.loaderVersion
        $targetId = "$($row.minecraftVersion)-neoforge-$lv"
        if (-not (Test-NeoForgeControllerTarget -Target $row)) {
            Write-Host "Skipping per-version NeoForge controller mod for ${targetId}: no bundled controller provider for this target"
            continue
        }
        $outDir = Join-Path $versionModsRoot $targetId
        Write-Host "Building per-version NeoForge controller mod: $targetId"
        try {
            & (Join-Path $root "controller_mod\neoforge\build_neoforge_controller_mod.ps1") `
                -MinecraftVersion $row.minecraftVersion `
                -NeoForgeVersion $lv `
                -OutputDir $outDir
        } catch {
            Write-Warning "Per-version NeoForge controller mod skipped for ${targetId}: $($_.Exception.Message)"
            if (Test-Path $outDir) { Remove-Item -Recurse -Force $outDir }
        }
    }
} else {
    Write-Host "Skipping per-version compat mods (-SkipVersionCompat)"
}

Copy-Item -Force (Join-Path $root "log_configs\client-uwp.xml") (Join-Path $pkg "runtime\log_configs\client-uwp.xml")

$screenshotSource = Join-Path $root "MC.Xbox\Assets\screenshots"
if (Test-Path $screenshotSource) {
    $screenshotTarget = Join-Path $pkg "Assets\screenshots"
    New-Item -ItemType Directory -Force -Path $screenshotTarget | Out-Null
    Copy-Item -Force (Join-Path $screenshotSource "*.png") $screenshotTarget
    Write-Host "Copied menu screenshot assets from $screenshotSource"
}

function Copy-PackagedJre {
    param(
        [Parameter(Mandatory = $true)][string]$JavaHome,
        [Parameter(Mandatory = $true)][string]$PackageRelativeDir,
        [Parameter(Mandatory = $true)][string]$SecurityPropertiesPath
    )

    $dest = Join-Path $pkg $PackageRelativeDir
    Write-Host "Copying JRE ($PackageRelativeDir)..."
    Write-Host "JRE source: $JavaHome"
    Copy-Item -Recurse $JavaHome $dest
    Copy-Item $SecurityPropertiesPath (Join-Path $dest "conf\security\xbox.properties") -Force
    Copy-Item $SecurityPropertiesPath (Join-Path $dest "conf\security\java.security") -Force
}

function Build-JavaBaseUwpFilesystemPatch {
    param(
        [Parameter(Mandatory = $true)][string]$JavaHome,
        [Parameter(Mandatory = $true)][string]$OutputJar,
        [Parameter(Mandatory = $true)][string]$WorkName
    )

    Write-Host "Building Java base UWP filesystem patch: $OutputJar"
    $javacExe = Join-Path $JavaHome "bin\javac.exe"
    if (-not (Test-Path $javacExe)) { throw "javac.exe not found at $javacExe; Java base UWP filesystem patch requires a JDK, not a JRE." }
    $runtimeJarExe = Join-Path $JavaHome "bin\jar.exe"
    if (-not (Test-Path $runtimeJarExe)) { $runtimeJarExe = $jarExe }
    $srcZip = Join-Path $JavaHome "lib\src.zip"
    if (-not (Test-Path $srcZip)) { throw "JDK source archive not found at $srcZip; Java base UWP filesystem patch cannot be generated." }
    $javaBasePatchDir = Join-Path $buildDir $WorkName
    $javaBasePatchSrcDir = Join-Path $javaBasePatchDir "src"
    $javaBasePatchClassesDir = Join-Path $javaBasePatchDir "classes"
    Remove-Item -Recurse -Force $javaBasePatchDir -ErrorAction SilentlyContinue
    New-Item -ItemType Directory -Force -Path `
        (Join-Path $javaBasePatchSrcDir "java\io"), `
        (Join-Path $javaBasePatchSrcDir "java\security"), `
        (Join-Path $javaBasePatchSrcDir "sun\nio\fs"), `
        $javaBasePatchClassesDir | Out-Null
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $srcArchive = [System.IO.Compression.ZipFile]::OpenRead($srcZip)
    try {
        $securityEntry = $srcArchive.Entries | Where-Object { $_.FullName -eq "java.base/java/security/Security.java" } | Select-Object -First 1
        if (-not $securityEntry) { throw "Security.java not found inside $srcZip" }
        $reader = [System.IO.StreamReader]::new($securityEntry.Open())
        try {
            $securitySource = $reader.ReadToEnd()
        } finally {
            $reader.Dispose()
        }

        $fileEntry = $srcArchive.Entries | Where-Object { $_.FullName -eq "java.base/java/io/File.java" } | Select-Object -First 1
        if (-not $fileEntry) { throw "File.java not found inside $srcZip" }
        $reader = [System.IO.StreamReader]::new($fileEntry.Open())
        try {
            $fileSource = $reader.ReadToEnd()
        } finally {
            $reader.Dispose()
        }

        $windowsPathEntry = $srcArchive.Entries | Where-Object { $_.FullName -eq "java.base/sun/nio/fs/WindowsPath.java" } | Select-Object -First 1
        if (-not $windowsPathEntry) { throw "WindowsPath.java not found inside $srcZip" }
        $reader = [System.IO.StreamReader]::new($windowsPathEntry.Open())
        try {
            $windowsPathSource = $reader.ReadToEnd()
        } finally {
            $reader.Dispose()
        }
    } finally {
        $srcArchive.Dispose()
    }
    $oldSecurityLine = "path = path.toRealPath();"
    $newSecurityLine = "try { path = path.toRealPath(); } catch (IOException realPathFailure) { path = path.toAbsolutePath(); }"
    $javaBasePatchSources = @()
    if (-not $securitySource.Contains($oldSecurityLine)) {
        Write-Warning "Security.java realpath patch target not found for $JavaHome; continuing with other java.base UWP filesystem patches."
    } else {
        $securitySource = $securitySource.Replace($oldSecurityLine, $newSecurityLine)
        $securitySourcePath = Join-Path $javaBasePatchSrcDir "java\security\Security.java"
        [System.IO.File]::WriteAllText($securitySourcePath, $securitySource)
        $javaBasePatchSources += $securitySourcePath
    }

    $oldFileLine = "            int nameMax = FS.getNameMax(dir.getPath());"
    $newFileBlock = @'
            int nameMax;
            try {
                nameMax = FS.getNameMax(dir.getPath());
            } catch (Throwable nameMaxFailure) {
                nameMax = 255;
            }
'@
    if (-not $fileSource.Contains($oldFileLine)) {
        throw "File.java temp-file nameMax patch target not found for $JavaHome."
    }
    $fileSource = $fileSource.Replace($oldFileLine, $newFileBlock)
    $fileSourcePath = Join-Path $javaBasePatchSrcDir "java\io\File.java"
    [System.IO.File]::WriteAllText($fileSourcePath, $fileSource)
    $javaBasePatchSources += $fileSourcePath

    $oldWindowsPathBlock = @'
        String rp = WindowsLinkSupport.getRealPath(this, Util.followLinks(options));
        return createFromNormalizedPath(getFileSystem(), rp);
'@
    $newWindowsPathBlock = @'
        try {
            String rp = WindowsLinkSupport.getRealPath(this, Util.followLinks(options));
            return createFromNormalizedPath(getFileSystem(), rp);
        } catch (IOException realPathFailure) {
            return (WindowsPath)toAbsolutePath().normalize();
        }
'@
    if (-not $windowsPathSource.Contains($oldWindowsPathBlock)) {
        throw "WindowsPath.java realpath patch target not found for $JavaHome."
    }
    $windowsPathSource = $windowsPathSource.Replace($oldWindowsPathBlock, $newWindowsPathBlock)
    $windowsPathSourcePath = Join-Path $javaBasePatchSrcDir "sun\nio\fs\WindowsPath.java"
    [System.IO.File]::WriteAllText($windowsPathSourcePath, $windowsPathSource)
    $javaBasePatchSources += $windowsPathSourcePath

    & $javacExe --patch-module "java.base=$javaBasePatchSrcDir" -d $javaBasePatchClassesDir $javaBasePatchSources
    if ($LASTEXITCODE -ne 0) { throw "Java base UWP filesystem patch compile failed" }
    Push-Location $javaBasePatchClassesDir
    & $runtimeJarExe cf $OutputJar .
    Pop-Location
    if ($LASTEXITCODE -ne 0) { throw "Java base UWP filesystem patch jar creation failed" }
    Write-Host "Java base UWP filesystem patch: $OutputJar"
}

function Build-JavaZipfsRealpathPatch {
    param(
        [Parameter(Mandatory = $true)][string]$JavaHome,
        [Parameter(Mandatory = $true)][string]$OutputJar,
        [Parameter(Mandatory = $true)][string]$WorkName
    )

    Write-Host "Building Java ZipFS realpath patch: $OutputJar"
    $javacExe = Join-Path $JavaHome "bin\javac.exe"
    if (-not (Test-Path $javacExe)) { throw "javac.exe not found at $javacExe; Java ZipFS patch requires a JDK, not a JRE." }
    $runtimeJarExe = Join-Path $JavaHome "bin\jar.exe"
    if (-not (Test-Path $runtimeJarExe)) { $runtimeJarExe = $jarExe }
    $srcZip = Join-Path $JavaHome "lib\src.zip"
    if (-not (Test-Path $srcZip)) { throw "JDK source archive not found at $srcZip; Java ZipFS patch cannot be generated." }

    $zipfsPatchDir = Join-Path $buildDir $WorkName
    $zipfsPatchSrcDir = Join-Path $zipfsPatchDir "src"
    $zipfsPatchClassesDir = Join-Path $zipfsPatchDir "classes"
    Remove-Item -Recurse -Force $zipfsPatchDir -ErrorAction SilentlyContinue
    New-Item -ItemType Directory -Force -Path (Join-Path $zipfsPatchSrcDir "jdk\nio\zipfs"), $zipfsPatchClassesDir | Out-Null

    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $srcArchive = [System.IO.Compression.ZipFile]::OpenRead($srcZip)
    try {
        $providerEntry = $srcArchive.Entries | Where-Object { $_.FullName -eq "jdk.zipfs/jdk/nio/zipfs/ZipFileSystemProvider.java" } | Select-Object -First 1
        if (-not $providerEntry) { throw "ZipFileSystemProvider.java not found inside $srcZip" }
        $reader = [System.IO.StreamReader]::new($providerEntry.Open())
        try {
            $providerSource = $reader.ReadToEnd()
        } finally {
            $reader.Dispose()
        }
    } finally {
        $srcArchive.Dispose()
    }

    $providerSource = $providerSource.Replace("uriToPath(uri).toRealPath()", "safeRealPath(uriToPath(uri))")
    $providerSource = $providerSource.Replace("zfpath.toRealPath()", "safeRealPath(zfpath)")
    $providerSource = $providerSource.Replace("PrivilegedExceptionAction<Path> action = tempPath::toRealPath;", "PrivilegedExceptionAction<Path> action = () -> safeRealPath(tempPath);")
    $providerSource = $providerSource.Replace("path.toRealPath()", "safeRealPath(path)")

    $helper = @'
    private static Path safeRealPath(Path path) throws IOException {
        try {
            return path.toRealPath();
        } catch (IOException realPathFailure) {
            return path.toAbsolutePath().normalize();
        }
    }

'@
    $insertAfter = "    }`r`n`r`n    @Override`r`n    public FileSystem newFileSystem"
    if (-not $providerSource.Contains($insertAfter)) {
        $insertAfter = "    }`n`n    @Override`n    public FileSystem newFileSystem"
    }
    if (-not $providerSource.Contains($insertAfter)) {
        throw "ZipFileSystemProvider.java patch insert point not found."
    }
    $insertIndex = $providerSource.IndexOf($insertAfter)
    if ($insertIndex -lt 0) {
        throw "ZipFileSystemProvider.java patch insert point not found."
    }
    $replacement = "    }`r`n`r`n$helper    @Override`r`n    public FileSystem newFileSystem"
    $providerSource = $providerSource.Substring(0, $insertIndex) + $replacement + $providerSource.Substring($insertIndex + $insertAfter.Length)

    if ($providerSource -notmatch "safeRealPath\(path\)" -or
        $providerSource -match "uriToPath\(uri\)\.toRealPath\(\)|zfpath\.toRealPath\(\)|tempPath::toRealPath") {
        throw "ZipFileSystemProvider.java realpath patch did not apply cleanly."
    }

    $providerSourcePath = Join-Path $zipfsPatchSrcDir "jdk\nio\zipfs\ZipFileSystemProvider.java"
    [System.IO.File]::WriteAllText($providerSourcePath, $providerSource)
    & $javacExe --patch-module "jdk.zipfs=$zipfsPatchSrcDir" -d $zipfsPatchClassesDir $providerSourcePath
    if ($LASTEXITCODE -ne 0) { throw "Java ZipFS patch compile failed" }
    Push-Location $zipfsPatchClassesDir
    & $runtimeJarExe cf $OutputJar .
    Pop-Location
    if ($LASTEXITCODE -ne 0) { throw "Java ZipFS patch jar creation failed" }
    Write-Host "Java ZipFS patch: $OutputJar"
}

function Build-JavaDesktopUwpAwtPatch {
    param(
        [Parameter(Mandatory = $true)][string]$JavaHome,
        [Parameter(Mandatory = $true)][string]$OutputJar,
        [Parameter(Mandatory = $true)][string]$WorkName
    )

    Write-Host "Building Java desktop UWP AWT patch: $OutputJar"
    $javacExe = Join-Path $JavaHome "bin\javac.exe"
    if (-not (Test-Path $javacExe)) { throw "javac.exe not found at $javacExe; Java desktop UWP AWT patch requires a JDK, not a JRE." }
    $runtimeJarExe = Join-Path $JavaHome "bin\jar.exe"
    if (-not (Test-Path $runtimeJarExe)) { $runtimeJarExe = $jarExe }
    $srcZip = Join-Path $JavaHome "lib\src.zip"
    if (-not (Test-Path $srcZip)) { throw "JDK source archive not found at $srcZip; Java desktop UWP AWT patch cannot be generated." }

    $desktopPatchDir = Join-Path $buildDir $WorkName
    $desktopPatchSrcDir = Join-Path $desktopPatchDir "src"
    $desktopPatchClassesDir = Join-Path $desktopPatchDir "classes"
    Remove-Item -Recurse -Force $desktopPatchDir -ErrorAction SilentlyContinue
    New-Item -ItemType Directory -Force -Path (Join-Path $desktopPatchSrcDir "sun\awt\windows"), $desktopPatchClassesDir | Out-Null

    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $srcArchive = [System.IO.Compression.ZipFile]::OpenRead($srcZip)
    try {
        $desktopEntry = $srcArchive.Entries | Where-Object { $_.FullName -eq "java.desktop/sun/awt/windows/WDesktopProperties.java" } | Select-Object -First 1
        if (-not $desktopEntry) { throw "WDesktopProperties.java not found inside $srcZip" }
        $reader = [System.IO.StreamReader]::new($desktopEntry.Open())
        try {
            $desktopSource = $reader.ReadToEnd()
        } finally {
            $reader.Dispose()
        }
    } finally {
        $srcArchive.Dispose()
    }

    $oldDesktopCall = "        getWindowsParameters();"
    $newDesktopCall = @'
        if (!Boolean.getBoolean("banditvault.awt.skipDesktopProperties")) {
            getWindowsParameters();
        }
'@
    if (-not $desktopSource.Contains($oldDesktopCall)) {
        throw "WDesktopProperties.java native desktop-properties call target not found for $JavaHome."
    }
    $desktopSource = $desktopSource.Replace($oldDesktopCall, $newDesktopCall)

    $desktopSourcePath = Join-Path $desktopPatchSrcDir "sun\awt\windows\WDesktopProperties.java"
    [System.IO.File]::WriteAllText($desktopSourcePath, $desktopSource)
    & $javacExe --patch-module "java.desktop=$desktopPatchSrcDir" -d $desktopPatchClassesDir $desktopSourcePath
    if ($LASTEXITCODE -ne 0) { throw "Java desktop UWP AWT patch compile failed" }
    Push-Location $desktopPatchClassesDir
    & $runtimeJarExe cf $OutputJar .
    Pop-Location
    if ($LASTEXITCODE -ne 0) { throw "Java desktop UWP AWT patch jar creation failed" }
    Write-Host "Java desktop UWP AWT patch: $OutputJar"
}

function Resolve-SecureJarHandlerJar {
    param([Parameter(Mandatory = $true)][string]$Version)

    $relative = "cpw\mods\securejarhandler\$Version\securejarhandler-$Version.jar"
    $candidates = @(
        (Join-Path $gameDir "libraries\$relative"),
        (Join-Path $root ".local\pcgen\mc\libraries\$relative")
    )

    foreach ($candidate in $candidates) {
        if (Test-Path $candidate) {
            return (Resolve-Path $candidate).Path
        }
    }

    $downloadPath = Join-Path $gameDir "libraries\$relative"
    $url = "https://maven.neoforged.net/releases/cpw/mods/securejarhandler/$Version/securejarhandler-$Version.jar"
    Write-Host "Downloading securejarhandler $Version"
    New-Item -ItemType Directory -Force -Path (Split-Path $downloadPath -Parent) | Out-Null
    Invoke-WebRequest -UseBasicParsing -Uri $url -OutFile $downloadPath -TimeoutSec 60
    return (Resolve-Path $downloadPath).Path
}

function Build-SecureJarHandlerUwpPatch {
    param(
        [Parameter(Mandatory = $true)][string]$JavaHome,
        [Parameter(Mandatory = $true)][string]$Version,
        [Parameter(Mandatory = $true)][string]$OutputJar
    )

    Write-Host "Building securejarhandler UWP patch: $OutputJar"
    $javacExe = Join-Path $JavaHome "bin\javac.exe"
    if (-not (Test-Path $javacExe)) { throw "javac.exe not found at $javacExe; securejarhandler patch requires a JDK." }
    $runtimeJarExe = Join-Path $JavaHome "bin\jar.exe"
    if (-not (Test-Path $runtimeJarExe)) { $runtimeJarExe = $jarExe }

    $secureJar = Resolve-SecureJarHandlerJar -Version $Version
    $patchSourceRoot = Join-Path $root "patch\securejarhandler"
    $patchSources = @(Get-ChildItem -LiteralPath $patchSourceRoot -Recurse -Filter "*.java" | ForEach-Object { $_.FullName })
    if ($patchSources.Count -eq 0) {
        throw "securejarhandler patch sources missing: $patchSourceRoot"
    }

    $patchDir = Join-Path $buildDir "securejarhandler_uwp_patch\$Version"
    $classesDir = Join-Path $patchDir "classes"
    Remove-Item -Recurse -Force $patchDir -ErrorAction SilentlyContinue
    New-Item -ItemType Directory -Force -Path $classesDir | Out-Null

    & $javacExe --release 21 -cp $secureJar -d $classesDir $patchSources
    if ($LASTEXITCODE -ne 0) { throw "securejarhandler UWP patch compile failed" }

    & $runtimeJarExe cf $OutputJar -C $classesDir .
    if ($LASTEXITCODE -ne 0) { throw "securejarhandler UWP patch jar creation failed" }
    Write-Host "securejarhandler UWP patch: $OutputJar"
}

Write-Host "Copying JRE..."
$xboxSecurityProperties = Join-Path $root "xbox_security.properties"
Copy-Item $xboxSecurityProperties (Join-Path $pkg "xbox_security.properties") -Force
Copy-PackagedJre -JavaHome $jreSrc -PackageRelativeDir "jre" -SecurityPropertiesPath $xboxSecurityProperties
Copy-PackagedJre -JavaHome $jre21Src -PackageRelativeDir "jre21" -SecurityPropertiesPath $xboxSecurityProperties
try {
    $jre17Src = Resolve-JavaHomeExact -MajorVersion 17
    Copy-PackagedJre -JavaHome $jre17Src -PackageRelativeDir "jre17" -SecurityPropertiesPath $xboxSecurityProperties
    Build-JavaBaseUwpFilesystemPatch -JavaHome $jre17Src -OutputJar (Join-Path $pkg "java-base-uwp-filesystem-17.jar") -WorkName "java_base_uwp_filesystem_patch_17"
    Build-JavaZipfsRealpathPatch -JavaHome $jre17Src -OutputJar (Join-Path $pkg "java-zipfs-realpath-17.jar") -WorkName "java_zipfs_realpath_patch_17"
    Build-JavaDesktopUwpAwtPatch -JavaHome $jre17Src -OutputJar (Join-Path $pkg "java-desktop-uwp-awt-17.jar") -WorkName "java_desktop_uwp_awt_patch_17"
    Write-Host "Packaged Java 17 runtime for 1.18.x / 1.20.2-1.20.4 targets"
} catch {
    Write-Warning "Java 17 JRE not packaged: $($_.Exception.Message). Targets with javaRuntime=java17 need JDK 17 on the build machine."
}
Build-JavaBaseUwpFilesystemPatch -JavaHome $jreSrc -OutputJar (Join-Path $pkg "java-base-uwp-filesystem.jar") -WorkName "java_base_uwp_filesystem_patch_current"
Build-JavaBaseUwpFilesystemPatch -JavaHome $jre21Src -OutputJar (Join-Path $pkg "java-base-uwp-filesystem-21.jar") -WorkName "java_base_uwp_filesystem_patch_21"
Build-JavaZipfsRealpathPatch -JavaHome $jreSrc -OutputJar (Join-Path $pkg "java-zipfs-realpath.jar") -WorkName "java_zipfs_realpath_patch_current"
Build-JavaZipfsRealpathPatch -JavaHome $jre21Src -OutputJar (Join-Path $pkg "java-zipfs-realpath-21.jar") -WorkName "java_zipfs_realpath_patch_21"
Build-JavaDesktopUwpAwtPatch -JavaHome $jreSrc -OutputJar (Join-Path $pkg "java-desktop-uwp-awt.jar") -WorkName "java_desktop_uwp_awt_patch_current"
Build-JavaDesktopUwpAwtPatch -JavaHome $jre21Src -OutputJar (Join-Path $pkg "java-desktop-uwp-awt-21.jar") -WorkName "java_desktop_uwp_awt_patch_21"
Build-SecureJarHandlerUwpPatch -JavaHome $jre21Src -Version "3.0.8" -OutputJar (Join-Path $pkg "securejarhandler-uwp-patch.jar")

Write-Host "Generating UWP tile assets..."
& $pythonExe (Join-Path $root "scripts\generate-assets.py") $pkg
if ($LASTEXITCODE -ne 0) { throw "Asset generation failed" }

Write-Host "=== Packaging ==="
$cert = Join-Path $certDir $ProjectConfig.CertificateFileName
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
$exactSigningCertCandidates = $allSigningCertCandidates | Where-Object { $_.Subject -eq $certName } | Sort-Object NotBefore -Descending
$banditVaultSigningCertCandidates = $allSigningCertCandidates | Where-Object { $_.Subject -like '*BanditVault*' -and $_.Subject -ne $certName } | Sort-Object NotBefore -Descending
$otherSigningCertCandidates = $allSigningCertCandidates | Where-Object { $_.Subject -notlike '*BanditVault*' } | Sort-Object NotBefore -Descending
$signingCertCandidates = @($exactSigningCertCandidates) + @($banditVaultSigningCertCandidates) + @($otherSigningCertCandidates)
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
