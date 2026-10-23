param(
    [string]$MinecraftVersion,
    [string]$FabricLoaderVersion,
    [string]$AssetIndex
)
$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue"

if ($MinecraftVersion) { $env:MC_VERSION = $MinecraftVersion }
if ($FabricLoaderVersion) { $env:FABRIC_LOADER_VERSION = $FabricLoaderVersion }
if ($AssetIndex) { $env:MC_ASSET_INDEX = $AssetIndex }

. (Join-Path $PSScriptRoot "common.ps1")

$root = Resolve-RepoRoot
$gameDir = Get-ConfigPath "GameDir"
$assetsDir = Get-ConfigPath "AssetsDir"
$version = if ($MinecraftVersion) { $MinecraftVersion } else { $ProjectConfig.MinecraftVersion }
$loaderVersion = if ($FabricLoaderVersion) { $FabricLoaderVersion } else { $ProjectConfig.FabricLoaderVersion }
$nativesDir = Get-ConfigPath "NativesDir"
$toolsDir = Get-ConfigPath "ToolsDir"
$notesDir = Get-ConfigPath "NotesDir"
$assetIndex = if ($AssetIndex) { $AssetIndex } else { "" }
$javaHome = Resolve-JavaHomeForMinecraft -MinecraftVersion $version
$javaExe = Join-Path $javaHome "bin\java.exe"

function Get-SafeFileName {
    param([Parameter(Mandatory = $true)][string]$Value)
    return ($Value -replace '[^A-Za-z0-9_.-]', '_')
}

function Get-MinecraftVersionJson {
    $manifest = Get-MinecraftVersionManifest
    $entry = $manifest.versions | Where-Object { $_.id -eq $version } | Select-Object -First 1
    if (-not $entry) {
        throw "Minecraft version $version not found in Mojang manifest."
    }

    return Get-CachedRemoteJson -Uri $entry.url
}

function Expand-NativeJar {
    param(
        [Parameter(Mandatory = $true)][string]$Path
    )

    $extracted = 0
    $zip = [System.IO.Compression.ZipFile]::OpenRead($Path)
    try {
        foreach ($entry in $zip.Entries) {
            if (-not $entry.Name -or -not $entry.Name.EndsWith(".dll", [System.StringComparison]::OrdinalIgnoreCase)) {
                continue
            }

            $dest = Join-Path $nativesDir $entry.Name
            [System.IO.Compression.ZipFileExtensions]::ExtractToFile($entry, $dest, $true)
            Write-Host "Native: $($entry.Name)"
            $extracted++
        }
    } finally {
        $zip.Dispose()
    }

    return $extracted
}

function ConvertTo-QuotedProcessArgument {
    param([Parameter(Mandatory = $true)][string]$Value)

    if ($Value -notmatch '[\s"]') {
        return $Value
    }

    return '"' + $Value.Replace('"', '\"') + '"'
}

function Convert-MavenNameToPath {
    param([Parameter(Mandatory = $true)][string]$Name)

    $parts = $Name.Split(":")
    if ($parts.Length -lt 3) {
        throw "Unsupported Maven coordinate: $Name"
    }

    $group = $parts[0].Replace(".", "\")
    $artifact = $parts[1]
    $versionPart = $parts[2]
    $classifier = if ($parts.Length -ge 4) { "-$($parts[3])" } else { "" }
    return "$group\$artifact\$versionPart\$artifact-$versionPart$classifier.jar"
}

function Get-MavenArtifactKey {
    param([Parameter(Mandatory = $true)][string]$RelativePath)

    $normalized = $RelativePath.Replace('\', '/')
    $lastSlash = $normalized.LastIndexOf('/')
    if ($lastSlash -le 0) {
        return $null
    }

    $dir = $normalized.Substring(0, $lastSlash)
    $verSlash = $dir.LastIndexOf('/')
    if ($verSlash -le 0) {
        return $null
    }

    return @{
        Key = $dir.Substring(0, $verSlash)
        Version = $dir.Substring($verSlash + 1)
    }
}

function Compare-MavenVersion {
    param(
        [Parameter(Mandatory = $true)][string]$Left,
        [Parameter(Mandatory = $true)][string]$Right
    )

    $leftParts = $Left -split '[.\-_]'
    $rightParts = $Right -split '[.\-_]'
    $count = [Math]::Max($leftParts.Length, $rightParts.Length)
    for ($i = 0; $i -lt $count; $i++) {
        $l = if ($i -lt $leftParts.Length) { $leftParts[$i] } else { "0" }
        $r = if ($i -lt $rightParts.Length) { $rightParts[$i] } else { "0" }

        $lIsNum = $l -match '^\d+$'
        $rIsNum = $r -match '^\d+$'
        if ($lIsNum -and $rIsNum) {
            $lNum = [int]$l
            $rNum = [int]$r
            if ($lNum -ne $rNum) {
                return [Math]::Sign($lNum - $rNum)
            }
            continue
        }

        # a qualifier ranks below a number at the same index, so 1.0-alpha loses to 1.0
        if ($lIsNum -ne $rIsNum) {
            return $(if ($lIsNum) { 1 } else { -1 })
        }

        $cmp = [string]::Compare($l, $r, [StringComparison]::OrdinalIgnoreCase)
        if ($cmp -ne 0) {
            return $cmp
        }
    }

    return 0
}

function Add-LibraryJar {
    param(
        [hashtable]$JarEntries,
        [System.Collections.Generic.List[string]]$JarKeyOrder,
        [Parameter(Mandatory = $true)][string]$RelativePath
    )

    # natives-linux shares lwjgl's maven key and sorts first, so it would win the dedupe
    if ($RelativePath -match '-natives-[A-Za-z0-9_.-]+\.jar$') {
        return
    }

    $jarPath = Join-Path $gameDir ("libraries\" + $RelativePath.Replace('/', '\'))
    if (-not (Test-Path $jarPath)) {
        Write-Warning "Classpath library missing: $jarPath"
        return
    }

    $maven = Get-MavenArtifactKey -RelativePath $RelativePath
    if (-not $maven) {
        $fallbackKey = "__path__$jarPath"
        if (-not $JarEntries.ContainsKey($fallbackKey)) {
            $JarKeyOrder.Add($fallbackKey) | Out-Null
            $JarEntries[$fallbackKey] = @{ Path = $jarPath; Version = "" }
        }
        return
    }

    $existing = $JarEntries[$maven.Key]
    if (-not $existing) {
        $JarKeyOrder.Add($maven.Key) | Out-Null
        $JarEntries[$maven.Key] = @{ Path = $jarPath; Version = $maven.Version }
        return
    }

    if ((Compare-MavenVersion -Left $maven.Version -Right $existing.Version) -gt 0) {
        Write-Host "Classpath: using $($maven.Key) $($maven.Version) instead of $($existing.Version)"
        $JarEntries[$maven.Key] = @{ Path = $jarPath; Version = $maven.Version }
    }
}

Ensure-Dir $gameDir, $assetsDir, $nativesDir, $toolsDir, $notesDir

Write-Host "=== Downloading Minecraft libraries ==="
& (Join-Path $root "scripts\download-libs.ps1") -MinecraftVersion $version

$versionJson = Get-MinecraftVersionJson
if (-not $assetIndex) {
    $assetIndex = $versionJson.assetIndex.id
}

Write-Host "=== Downloading Minecraft native DLLs ==="
Add-Type -AssemblyName System.IO.Compression.FileSystem
foreach ($library in $versionJson.libraries) {
    if (-not $library.natives -or -not $library.natives.windows -or -not $library.downloads.classifiers) {
        continue
    }

    $classifierName = $library.natives.windows.Replace('${arch}', '64')
    $classifierProperty = $library.downloads.classifiers.PSObject.Properties[$classifierName]
    if (-not $classifierProperty -or -not $classifierProperty.Value.url) {
        continue
    }

    $classifier = $classifierProperty.Value
    $zipName = Get-SafeFileName -Value $classifier.path
    $zipPath = Join-Path $toolsDir "natives-$zipName"
    Save-RemoteFile -Uri $classifier.url -Path $zipPath -Sha1 $classifier.sha1 -Size $classifier.size

    Expand-NativeJar -Path $zipPath | Out-Null
}

foreach ($library in $versionJson.libraries) {
    if (-not $library.downloads -or -not $library.downloads.artifact) {
        continue
    }

    $artifact = $library.downloads.artifact
    $libraryName = [string]$library.name
    $artifactPath = [string]$artifact.path
    $isWindowsX64Native =
        $libraryName.EndsWith(":natives-windows", [System.StringComparison]::OrdinalIgnoreCase) -or
        $artifactPath.EndsWith("-natives-windows.jar", [System.StringComparison]::OrdinalIgnoreCase)

    if (-not $isWindowsX64Native) {
        continue
    }

    $jarPath = Join-Path $gameDir ("libraries\" + $artifactPath.Replace('/', '\'))
    Save-RemoteFile -Uri $artifact.url -Path $jarPath -Sha1 $artifact.sha1 -Size $artifact.size
    Expand-NativeJar -Path $jarPath | Out-Null
}

$nativeDlls = @(Get-ChildItem -LiteralPath $nativesDir -Filter "*.dll" -ErrorAction SilentlyContinue)
if (-not $nativeDlls) {
    throw "No native DLLs were prepared under $nativesDir."
}

Write-Host "=== Downloading Fabric installer ==="
$fabricMetadata = [xml](Invoke-WebRequest -UseBasicParsing -Uri "https://maven.fabricmc.net/net/fabricmc/fabric-installer/maven-metadata.xml").Content
$installerVersion = $fabricMetadata.metadata.versioning.release
if (-not $installerVersion) {
    $installerVersion = $fabricMetadata.metadata.versioning.latest
}
if (-not $installerVersion) {
    throw "Could not determine Fabric installer version from Maven metadata."
}

$installerJar = Join-Path $toolsDir "fabric-installer-$installerVersion.jar"
$installerUrl = "https://maven.fabricmc.net/net/fabricmc/fabric-installer/$installerVersion/fabric-installer-$installerVersion.jar"
Save-RemoteFile -Uri $installerUrl -Path $installerJar

Write-Host "=== Installing Fabric loader $loaderVersion for Minecraft $version ==="
& $javaExe -jar $installerJar client -dir $gameDir -mcversion $version -loader $loaderVersion -launcher win32 -noprofile
if ($LASTEXITCODE -ne 0) {
    throw "Fabric installer failed."
}

Write-Host "=== Patching Fabric loader $loaderVersion for cache preparation ==="
& (Join-Path $root "scripts\patch-fabric.ps1") -LoaderVersion $loaderVersion
if ($LASTEXITCODE -ne 0) {
    throw "Fabric loader patch failed."
}

Write-Host "=== Downloading asset index ==="
$indexDir = Join-Path $assetsDir "indexes"
Ensure-Dir $indexDir
$assetIndexId = $versionJson.assetIndex.id
$assetIndexPath = Join-Path $indexDir "$assetIndexId.json"
Save-RemoteFile -Uri $versionJson.assetIndex.url -Path $assetIndexPath -Sha1 $versionJson.assetIndex.sha1 -Size $versionJson.assetIndex.size
if ($assetIndexId -ne $assetIndex) {
    Write-Warning "Configured asset index is $assetIndex, but Mojang metadata for $version reports $assetIndexId."
}

Write-Host "=== Generating Fabric remapped client jar ==="
if (-not (Test-FabricTargetHasIntermediary -GameDir $gameDir -MinecraftVersion $version -LoaderVersion $loaderVersion)) {
    Write-Host "Fabric has no intermediary for $version, so there is nothing to remap. Mods build against the client jar."
    Write-Host "CI cache is ready."
    return
}

$remappedJar = Join-Path $gameDir ".fabric\remappedJars\minecraft-$version-$loaderVersion\client-intermediary.jar"
if (-not (Test-JarComplete $remappedJar "net/minecraft/class_310.class")) {
    $clientJar = Join-Path $gameDir "versions\$version\$version.jar"
    $profile = Get-Content -Raw (Join-Path $gameDir "versions\fabric-loader-$loaderVersion-$version\fabric-loader-$loaderVersion-$version.json") | ConvertFrom-Json
    $jars = @($profile.libraries | ForEach-Object { Join-Path $gameDir ("libraries\" + (Convert-MavenNameToPath $_.name)) })
    $classpath = $jars -join ";"
    $intermediary = $jars | Where-Object { $_ -match 'intermediary-[^\\]+\.jar$' } | Select-Object -First 1
    if (-not $intermediary) { throw "Intermediary mappings missing" }
    $work = Join-Path $toolsDir "fabric-remapper"
    Ensure-Dir $work, (Split-Path $remappedJar -Parent)
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $zip = [IO.Compression.ZipFile]::OpenRead($intermediary)
    $mappingPath = Join-Path $work "mappings.tiny"
    try {
        $entry = $zip.GetEntry("mappings/mappings.tiny")
        if (-not $entry) { throw "Intermediary mapping entry missing" }
        [IO.Compression.ZipFileExtensions]::ExtractToFile($entry, $mappingPath, $true)
    } finally { $zip.Dispose() }
    $loaderJar = $jars | Where-Object { $_ -match 'fabric-loader-[^\\]+\.jar$' } | Select-Object -First 1
    $remapperSource = Get-FabricRemapperSource -LoaderJar $loaderJar
    $remapperSourcePath = Join-Path $work "RemapFabricClient.java"
    [IO.File]::WriteAllText($remapperSourcePath, $remapperSource)
    & (Join-Path $javaHome "bin\javac.exe") -cp $classpath -d $work $remapperSourcePath
    if ($LASTEXITCODE -ne 0) { throw "Fabric remapper compile failed" }
    $temp = "$remappedJar.tmp"
    Remove-Item -LiteralPath $temp -Force -ErrorAction SilentlyContinue
    try {
        & $javaExe -cp "$work;$classpath" RemapFabricClient $clientJar $temp $mappingPath @jars
        if ($LASTEXITCODE -ne 0 -or -not (Test-JarComplete $temp "net/minecraft/class_310.class")) { throw "Fabric remap failed" }
        if (Test-Path -LiteralPath $remappedJar) {
            [IO.File]::Replace($temp, $remappedJar, "$remappedJar.previous")
            Remove-Item -LiteralPath "$remappedJar.previous" -Force
        }
        else { [IO.File]::Move($temp, $remappedJar) }
    } finally { Remove-Item -LiteralPath $temp -Force -ErrorAction SilentlyContinue }
}
Write-Host "Fabric remapped client ready $remappedJar"
