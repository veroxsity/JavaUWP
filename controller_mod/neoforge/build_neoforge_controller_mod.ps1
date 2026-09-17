param(
    [string]$MinecraftVersion = "1.21.1",
    [string]$NeoForgeVersion = "21.1.233",
    [string]$NeoFormVersion,
    [string]$OutputDir
)

$ErrorActionPreference = "Stop"

. (Join-Path (Split-Path (Split-Path $PSScriptRoot -Parent) -Parent) "scripts\common.ps1")

$root = Resolve-RepoRoot
$gameDir = Get-ConfigPath "GameDir"
$buildRoot = Join-Path (Get-ConfigPath "BuildDir") "controller_mod\neoforge\$NeoForgeVersion"
$classesDir = Join-Path $buildRoot "classes"
$compileOnlyDir = Join-Path $buildRoot "compile-only"
$srcJava = Join-Path $PSScriptRoot "src\main\java"
$srcResources = Join-Path $PSScriptRoot "src\main\resources"
$compileJava = Join-Path $PSScriptRoot "src\compile\java"
$coreJava = Join-Path $root "controller_mod\core\src\main\java"
$jarName = "banditvault-neoforge-controller-1.0.0.jar"
$jarPath = Join-Path $buildRoot $jarName

$variantLayers = @{
    "1.21" = @("1.21.1")
    "1.21.1" = @("1.21.1")
    "1.21.2" = @("1.21.1", "1.21.4")
    "1.21.3" = @("1.21.1", "1.21.4")
    "1.21.4" = @("1.21.1", "1.21.4")
    "1.21.5" = @("1.21.1", "1.21.4", "1.21.5")
    "1.21.6" = @("1.21.1", "1.21.4", "1.21.5", "1.21.6")
    "1.21.7" = @("1.21.1", "1.21.4", "1.21.5", "1.21.6")
    "1.21.8" = @("1.21.1", "1.21.4", "1.21.5", "1.21.6")
    "1.21.9" = @("1.21.1", "1.21.9")
    "1.21.10" = @("1.21.1", "1.21.9")
    "1.21.11" = @("1.21.1", "1.21.9", "1.21.11")
    "26.1" = @("1.21.1", "1.21.9", "1.21.11", "26.1")
    "26.1.1" = @("1.21.1", "1.21.9", "1.21.11", "26.1")
    "26.1.2" = @("1.21.1", "1.21.9", "1.21.11", "26.1")
    "26.2" = @("1.21.1", "1.21.9", "1.21.11", "26.1", "26.2")
}
if (-not $variantLayers.ContainsKey($MinecraftVersion)) {
    throw "NeoForge controller sources do not have a version adapter for Minecraft $MinecraftVersion"
}

function Resolve-NeoFormVersion([string]$Version) {
    $installerDir = Join-Path (Get-ConfigPath "StagingDir") "cache\neoforge\$Version"
    Ensure-Dir $installerDir
    $neoFormCache = Join-Path $installerDir "neoform.txt"
    if (Test-Path $neoFormCache) {
        $cached = ([System.IO.File]::ReadAllText($neoFormCache)).Trim()
        if ($cached) { return $cached }
    }
    $installerJar = Join-Path $installerDir "neoforge-$Version-installer.jar"
    if (-not (Test-Path $installerJar)) {
        Invoke-WebRequest -UseBasicParsing `
            -Uri "https://maven.neoforged.net/releases/net/neoforged/neoforge/$Version/neoforge-$Version-installer.jar" `
            -OutFile $installerJar `
            -TimeoutSec 180
    }

    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $zip = [System.IO.Compression.ZipFile]::OpenRead($installerJar)
    try {
        $entry = $zip.GetEntry("version.json")
        if (-not $entry) { throw "NeoForge installer has no version.json" }
        $reader = [System.IO.StreamReader]::new($entry.Open())
        try {
            $profile = $reader.ReadToEnd() | ConvertFrom-Json
        } finally {
            $reader.Dispose()
        }
    } finally {
        $zip.Dispose()
    }

    $arguments = @($profile.arguments.game)
    for ($i = 0; $i -lt $arguments.Count - 1; $i++) {
        if ([string]$arguments[$i] -eq "--fml.neoFormVersion") {
            $resolved = [string]$arguments[$i + 1]
            [System.IO.File]::WriteAllText($neoFormCache, $resolved, (New-Object System.Text.UTF8Encoding($false)))
            return $resolved
        }
    }
    throw "NeoForm version missing from NeoForge $Version profile"
}

$javaHome = Resolve-JavaHome
$javac = Join-Path $javaHome "bin\javac.exe"
$jar = Join-Path $javaHome "bin\jar.exe"
if (-not $NeoFormVersion) { $NeoFormVersion = Resolve-NeoFormVersion $NeoForgeVersion }
$neoFormCoordinate = "$MinecraftVersion-$NeoFormVersion"
$generatedRoot = Join-Path $root "prebuilt\neoforge\libraries"
$srgClient = Join-Path $generatedRoot "net\minecraft\client\$neoFormCoordinate\client-$neoFormCoordinate-srg.jar"
$patchedClient = Join-Path $generatedRoot "net\neoforged\neoforge\$NeoForgeVersion\neoforge-$NeoForgeVersion-client.jar"

if (-not (Test-Path $srgClient) -or -not (Test-Path $patchedClient)) {
    & (Join-Path $root "scripts\gen-neoforge-artifacts.ps1") `
        -NeoForgeVersion $NeoForgeVersion `
        -McVersion $MinecraftVersion `
        -NeoFormVersion $NeoFormVersion
}
if (-not (Test-Path $srgClient) -or -not (Test-Path $patchedClient)) {
    throw "NeoForge compile-only client artifacts are missing after generation."
}

$dependencyDir = Join-Path (Get-ConfigPath "StagingDir") "cache\neoforge\$NeoForgeVersion"
Ensure-Dir $dependencyDir
$universalJar = Join-Path $dependencyDir "neoforge-$NeoForgeVersion-universal.jar"
if (-not (Test-Path $universalJar)) {
    $universalUrl = "https://maven.neoforged.net/releases/net/neoforged/neoforge/$NeoForgeVersion/neoforge-$NeoForgeVersion-universal.jar"
    Invoke-WebRequest -UseBasicParsing -Uri $universalUrl -OutFile $universalJar -TimeoutSec 180
}

$mixinJar = Get-ChildItem -LiteralPath (Join-Path $gameDir "libraries\net\fabricmc\sponge-mixin") -Recurse -Filter "sponge-mixin-*.jar" -ErrorAction SilentlyContinue |
    Sort-Object FullName -Descending |
    Select-Object -First 1 -ExpandProperty FullName
if (-not $mixinJar) {
    $mixinJar = Join-Path $dependencyDir "sponge-mixin-0.15.2+mixin.0.8.7.jar"
    if (-not (Test-Path $mixinJar)) {
        Invoke-WebRequest -UseBasicParsing `
            -Uri "https://maven.neoforged.net/releases/net/fabricmc/sponge-mixin/0.15.2+mixin.0.8.7/sponge-mixin-0.15.2+mixin.0.8.7.jar" `
            -OutFile $mixinJar `
            -TimeoutSec 180
    }
}

$compileOnlySources = @(Get-ChildItem $compileJava -Recurse -Filter "*.java")
$mainSourcesByName = @{}
Get-ChildItem $srcJava -Recurse -Filter "*.java" | ForEach-Object { $mainSourcesByName[$_.Name] = $_ }
Get-ChildItem $coreJava -Recurse -Filter "*.java" | ForEach-Object { $mainSourcesByName[$_.Name] = $_ }
foreach ($layer in @($variantLayers[$MinecraftVersion])) {
    $layerDir = Join-Path $PSScriptRoot "src\variants\$layer"
    Get-ChildItem $layerDir -Recurse -Filter "*.java" | ForEach-Object { $mainSourcesByName[$_.Name] = $_ }
}
$mainSources = @($mainSourcesByName.Values)

$sourcePaths = @(@($compileOnlySources) + @($mainSources) | ForEach-Object { $_.FullName } | Sort-Object)
$resourceFiles = @(Get-ChildItem $srcResources -Recurse -File | Select-Object -ExpandProperty FullName)
$stampPath = Join-Path $buildRoot "build.stamp"
$stamp = New-BuildStamp `
    -Values @(
        "neoforge_controller",
        $MinecraftVersion,
        $NeoForgeVersion,
        $NeoFormVersion,
        "jar=$jarName",
        "layers=$(@($variantLayers[$MinecraftVersion]) -join ',')",
        "sources=$($sourcePaths -join ';')"
    ) `
    -ContentFiles (@($PSCommandPath) + $sourcePaths + $resourceFiles) `
    -DependencyFiles @($patchedClient, $srgClient, $universalJar, $mixinJar)

if (Test-BuildStampCurrent -StampPath $stampPath -Stamp $stamp -RequiredOutputs @($jarPath)) {
    Write-Host "NeoForge controller mod up to date ($MinecraftVersion / $NeoForgeVersion), skipping compile."
    if ($OutputDir) {
        Ensure-Dir $OutputDir
        Copy-Item $jarPath (Join-Path $OutputDir $jarName) -Force
    }
    return
}

$lwjglJars = @(Get-ChildItem -LiteralPath (Join-Path $gameDir "libraries\org\lwjgl") -Recurse -Filter "*.jar" -ErrorAction SilentlyContinue |
    Where-Object { $_.Name -notlike "*natives*" } |
    Select-Object -ExpandProperty FullName)
if (-not $lwjglJars) {
    throw "LWJGL compile dependencies are missing; prepare the launcher cache first."
}
$libraryJars = @(Get-ChildItem -LiteralPath (Join-Path $gameDir "libraries") -Recurse -Filter "*.jar" -ErrorAction SilentlyContinue |
    Where-Object { $_.Name -notlike "*natives*" } |
    Select-Object -ExpandProperty FullName)

Remove-Item -Recurse -Force $buildRoot -ErrorAction SilentlyContinue
Ensure-Dir $classesDir, $compileOnlyDir

$classpath = @($patchedClient, $srgClient, $universalJar, $mixinJar) + $lwjglJars + $libraryJars

function Invoke-Javac([string]$Name, [string[]]$Sources, [string]$Destination, [string[]]$Classpath) {
    $argsFile = Join-Path $buildRoot "$Name-args.txt"
    $args = @(
        "--release", "21",
        "-proc:none",
        "-classpath", ($Classpath -join [IO.Path]::PathSeparator),
        "-d", $Destination
    ) + $Sources
    [IO.File]::WriteAllLines($argsFile, $args)
    & $javac "@$argsFile"
    if ($LASTEXITCODE -ne 0) {
        throw "NeoForge controller $Name compilation failed."
    }
}

Invoke-Javac "compile-only" @($compileOnlySources.FullName) $compileOnlyDir $classpath
Invoke-Javac "main" @($mainSources.FullName) $classesDir (@($compileOnlyDir) + $classpath)

& (Join-Path $javaHome "bin\java.exe") -ea -cp $classesDir banditvault.controllercore.GridNavigation
if ($LASTEXITCODE -ne 0) {
    throw "NeoForge controller navigation self-check failed."
}
& (Join-Path $javaHome "bin\java.exe") -ea -cp $classesDir banditvault.controllercore.ControllerBindings
if ($LASTEXITCODE -ne 0) {
    throw "NeoForge controller binding self-check failed."
}

if (Test-Path (Join-Path $classesDir "net\neoforged")) {
    throw "NeoForge controller jar must not ship compile-only NeoForge API classes."
}
Copy-Item -Recurse "$srcResources\*" $classesDir -Force

$modsToml = Join-Path $classesDir "META-INF\neoforge.mods.toml"
(Get-Content $modsToml -Raw).
    Replace("__NEOFORGE_VERSION__", $NeoForgeVersion).
    Replace("__MINECRAFT_VERSION__", $MinecraftVersion) |
    Set-Content $modsToml -NoNewline

$manifest = Join-Path $classesDir "META-INF\MANIFEST.MF"
Push-Location $classesDir
try {
    & $jar cfm $jarPath $manifest .
    if ($LASTEXITCODE -ne 0) {
        throw "NeoForge controller jar creation failed."
    }
} finally {
    Pop-Location
}

$listing = & $jar tf $jarPath
foreach ($required in @(
    "banditvault/neoforgecontroller/NeoForgeControllerMod.class",
    "META-INF/neoforge.mods.toml",
    "banditvault-neoforge-controller.mixins.json",
    "pack.mcmeta"
)) {
    if ($listing -notcontains $required) {
        throw "NeoForge controller jar is missing $required"
    }
}
if ($listing | Where-Object { $_ -like "net/neoforged/*" }) {
    throw "NeoForge controller jar contains compile-only NeoForge classes."
}

Set-BuildStamp -StampPath $stampPath -Stamp $stamp

if ($OutputDir) {
    Ensure-Dir $OutputDir
    Copy-Item $jarPath (Join-Path $OutputDir $jarName) -Force
}
Write-Host "NeoForge controller mod built ($MinecraftVersion / $NeoForgeVersion) -> $jarPath"
