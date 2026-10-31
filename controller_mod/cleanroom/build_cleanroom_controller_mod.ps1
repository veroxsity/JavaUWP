param(
    [string]$MinecraftVersion = "1.12.2",
    [string]$CleanroomVersion = "0.6.13-alpha",
    [string]$OutputDir
)

$ErrorActionPreference = "Stop"

. (Join-Path (Split-Path (Split-Path $PSScriptRoot -Parent) -Parent) "scripts\common.ps1")

# the mod is written in srg names against a renamed client, cleanroom's notch named jar goes on as shipped

$root = Resolve-RepoRoot
$srcJava = Join-Path $PSScriptRoot "src\main\java"
$srcResources = Join-Path $PSScriptRoot "src\main\resources"
$controllerCoreJava = Join-Path $root "controller_mod\core\src\main\java"
$buildRoot = Join-Path (Get-ConfigPath "BuildDir") "controller_mod\cleanroom\$CleanroomVersion"
$classesDir = Join-Path $buildRoot "classes"
$jarName = "banditvault-cleanroom-controller-1.0.0.jar"
$jarPath = Join-Path $buildRoot $jarName
$gameDir = Get-ConfigPath "GameDir"
$libraryDir = Join-Path $gameDir "libraries"
$devDir = Join-Path (Get-ConfigPath "CacheDir") "cleanroom\$CleanroomVersion"

$javaHome = Resolve-JavaHome
$java = Join-Path $javaHome "bin\java.exe"
$javac = Join-Path $javaHome "bin\javac.exe"
$jar = Join-Path $javaHome "bin\jar.exe"

function Read-ZipText([string]$ZipPath, [string]$EntryName) {
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $zip = [System.IO.Compression.ZipFile]::OpenRead($ZipPath)
    try {
        $entry = $zip.GetEntry($EntryName)
        if (-not $entry) { throw "$EntryName not found in $ZipPath" }
        $reader = [System.IO.StreamReader]::new($entry.Open())
        try { return $reader.ReadToEnd() } finally { $reader.Dispose() }
    } finally {
        $zip.Dispose()
    }
}

function Expand-ZipEntry([string]$ZipPath, [string]$EntryName, [string]$Destination) {
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $zip = [System.IO.Compression.ZipFile]::OpenRead($ZipPath)
    try {
        $entry = $zip.GetEntry($EntryName)
        if (-not $entry) { throw "$EntryName not found in $ZipPath" }
        Ensure-Dir (Split-Path $Destination -Parent)
        [System.IO.Compression.ZipFileExtensions]::ExtractToFile($entry, $Destination, $true)
    } finally {
        $zip.Dispose()
    }
}

function Invoke-Fart([string]$InputJar, [string]$OutputJar, [string]$Mappings, [string[]]$Libraries) {
    $fartArgs = @("-jar", $fartJar, "--input", $InputJar, "--output", $OutputJar, "--map", $Mappings)
    foreach ($lib in $Libraries) { $fartArgs += @("--lib", $lib) }
    & $java @fartArgs | Out-Null
    if ($LASTEXITCODE -ne 0 -or -not (Test-JarComplete $OutputJar)) {
        throw "Rename to srg failed for $InputJar"
    }
}

$installerJar = Join-Path (Get-LoaderInstallerCacheDir) "cleanroom-$CleanroomVersion-installer.jar"
Save-RemoteFile `
    -Uri "https://repo.cleanroommc.com/releases/com/cleanroommc/cleanroom/$CleanroomVersion/cleanroom-$CleanroomVersion-installer.jar" `
    -Path $installerJar
$versionJson = Read-ZipText $installerJar "version.json" | ConvertFrom-Json
$installProfile = Read-ZipText $installerJar "install_profile.json" | ConvertFrom-Json

$clientJar = Join-Path $gameDir "versions\$MinecraftVersion\$MinecraftVersion.jar"
# older cleanroom profiles inherit from vanilla's and carry no client download of their own
$clientDownload = $versionJson.downloads.client
if (-not $clientDownload) {
    $mcVersion = (Get-CachedRemoteJson -Uri "https://piston-meta.mojang.com/mc/game/version_manifest_v2.json").versions |
        Where-Object { $_.id -eq $MinecraftVersion } | Select-Object -First 1
    $clientDownload = (Get-CachedRemoteJson -Uri $mcVersion.url).downloads.client
}
Save-RemoteFile -Uri $clientDownload.url -Path $clientJar -Sha1 $clientDownload.sha1

$mcpConfig = $installProfile.libraries | Where-Object { $_.name -like "de.oceanlabs.mcp:mcp_config:*" } | Select-Object -First 1
if (-not $mcpConfig) { throw "Cleanroom install profile has no mcp_config library" }
$mcpConfigZip = Join-Path $libraryDir ($mcpConfig.downloads.artifact.path.Replace("/", "\"))
Save-RemoteFile -Uri $mcpConfig.downloads.artifact.url -Path $mcpConfigZip -Sha1 $mcpConfig.downloads.artifact.sha1

$fartJar = Join-Path $libraryDir "net\minecraftforge\ForgeAutoRenamingTool\0.1.22\ForgeAutoRenamingTool-0.1.22-all.jar"
Save-RemoteFile `
    -Uri "https://maven.minecraftforge.net/net/minecraftforge/ForgeAutoRenamingTool/0.1.22/ForgeAutoRenamingTool-0.1.22-all.jar" `
    -Path $fartJar

# compile only, natives and the cleanroom jar itself have no plain artifact
$compileJars = @()
foreach ($lib in $versionJson.libraries) {
    $artifact = $lib.downloads.artifact
    if (-not $artifact -or -not $artifact.url -or $lib.name -like "*:natives-*") { continue }
    # cleanroom's own entries carry a path, the ones it copied from vanilla only have the url
    $relative = if ($artifact.path) { $artifact.path } else { ([uri]$artifact.url).AbsolutePath -replace '^/(maven2/|releases/)?', '' }
    $path = Join-Path $libraryDir ($relative.Replace("/", "\"))
    Save-RemoteFile -Uri $artifact.url -Path $path -Sha1 $artifact.sha1
    $compileJars += $path
}

$sourceFiles = @(Get-ChildItem $srcJava -Recurse -Filter "*.java") + @(Get-ChildItem $controllerCoreJava -Recurse -Filter "*.java")
$sourcePaths = @($sourceFiles | ForEach-Object { $_.FullName } | Sort-Object)
$resourceFiles = @(Get-ChildItem $srcResources -Recurse -File | Select-Object -ExpandProperty FullName)
$stampPath = Join-Path $buildRoot "build.stamp"
$stamp = New-BuildStamp `
    -Values @("cleanroom_controller", $MinecraftVersion, $CleanroomVersion, "jar=$jarName", "sources=$($sourcePaths -join ';')") `
    -ContentFiles (@($PSCommandPath) + $sourcePaths + $resourceFiles) `
    -ImmutableFiles @($installerJar, $clientJar, $mcpConfigZip)

if (Test-BuildStampCurrent -StampPath $stampPath -Stamp $stamp -RequiredOutputs @($jarPath)) {
    Write-Host "Cleanroom controller mod up to date ($CleanroomVersion), skipping compile."
    if ($OutputDir) {
        Ensure-Dir $OutputDir
        Copy-Item $jarPath (Join-Path $OutputDir $jarName) -Force
    }
    return
}

Ensure-Dir $devDir
$joinedTsrg = Join-Path $devDir "joined.tsrg"
Expand-ZipEntry $mcpConfigZip "config/joined.tsrg" $joinedTsrg
$cleanroomJar = Join-Path $devDir "cleanroom-$CleanroomVersion.jar"
Expand-ZipEntry $installerJar "maven/com/cleanroommc/cleanroom/$CleanroomVersion/cleanroom-$CleanroomVersion.jar" $cleanroomJar

$clientSrg = Join-Path $devDir "client-$MinecraftVersion-srg.jar"
if (-not (Test-JarComplete $clientSrg)) {
    Write-Host "Renaming Minecraft $MinecraftVersion client to srg..."
    Invoke-Fart $clientJar $clientSrg $joinedTsrg @($compileJars)
}

Remove-Item -Recurse -Force $buildRoot -ErrorAction SilentlyContinue
Ensure-Dir $classesDir

$classpath = (@($clientSrg, $cleanroomJar) + $compileJars + @($clientJar) | Select-Object -Unique) -join ";"
$argsFile = Join-Path $buildRoot "javac-args.txt"
$javacArgs = @(
    # javac will not read cleanroom's classes for an older release than they were built for
    "--release", (Get-JarClassRelease $cleanroomJar),
    "-proc:none",
    "-encoding", "UTF-8",
    "-classpath", $classpath,
    "-d", $classesDir
) + $sourcePaths
[System.IO.File]::WriteAllLines($argsFile, $javacArgs)
& $javac "@$argsFile"
if ($LASTEXITCODE -ne 0) { throw "Cleanroom controller mod compile failed." }

Copy-Item -Recurse "$srcResources\*" $classesDir -Force
& $jar cf $jarPath -C $classesDir .
if ($LASTEXITCODE -ne 0) { throw "Cleanroom controller mod jar failed." }

$jarListing = & $jar tf $jarPath
foreach ($required in @("banditvault/cleanroomcontroller/CleanroomControllerMod.class", "banditvault/controllercore/ControllerState.class", "mcmod.info")) {
    if ($jarListing -notcontains $required) { throw "Packaged Cleanroom controller mod is missing $required" }
}
if ($jarListing | Where-Object { $_ -like "net/minecraft*" -or $_ -like "org/lwjgl*" }) {
    throw "Cleanroom controller mod must not ship game or LWJGL classes."
}

Set-BuildStamp -StampPath $stampPath -Stamp $stamp

if ($OutputDir) {
    Ensure-Dir $OutputDir
    Copy-Item $jarPath (Join-Path $OutputDir $jarName) -Force
}
Write-Host "Cleanroom controller mod built ($CleanroomVersion) -> $jarPath"
