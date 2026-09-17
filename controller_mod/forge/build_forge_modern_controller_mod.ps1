param(
    [Parameter(Mandatory = $true)][string]$MinecraftVersion,
    [Parameter(Mandatory = $true)][string]$ForgeVersion,
    [string]$OutputDir
)

$ErrorActionPreference = "Stop"

. (Join-Path (Split-Path (Split-Path $PSScriptRoot -Parent) -Parent) "scripts\common.ps1")

$root = Resolve-RepoRoot
$gameDir = Get-ConfigPath "GameDir"
$buildRoot = Join-Path (Get-ConfigPath "BuildDir") "controller_mod\forge\$ForgeVersion"
$classesDir = Join-Path $buildRoot "classes"
$compileOnlyDir = Join-Path $buildRoot "compile-only"
$neoSource = Join-Path $root "controller_mod\neoforge\src\main\java"
$neoVariants = Join-Path $root "controller_mod\neoforge\src\variants"
$coreSource = Join-Path $root "controller_mod\core\src\main\java"
$forgeSource = Join-Path $PSScriptRoot "src\modern\java"
$compileSource = Join-Path $PSScriptRoot "src\compile\java"
$neoCompileSource = Join-Path $root "controller_mod\neoforge\src\compile\java"
$resources = Join-Path $PSScriptRoot "src\modern\resources"
$jarName = "banditvault-forge-controller-1.0.0.jar"
$jarPath = Join-Path $buildRoot $jarName
$forgeCoordinate = if ($ForgeVersion.StartsWith("$MinecraftVersion-")) { $ForgeVersion } else { "$MinecraftVersion-$ForgeVersion" }

$variantLayers = @{
    "1.21" = @("1.21.1")
    "1.21.1" = @("1.21.1")
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
    throw "Forge controller sources do not have a version adapter for Minecraft $MinecraftVersion"
}

$javaHome = Resolve-JavaHome
$javac = Join-Path $javaHome "bin\javac.exe"
$jar = Join-Path $javaHome "bin\jar.exe"
$patchedClient = & (Join-Path $root "scripts\prepare-forge-patched-client.ps1") `
    -MinecraftVersion $MinecraftVersion `
    -ForgeVersion $ForgeVersion
if (-not (Test-Path $patchedClient)) {
    throw "Forge patched client jar missing $patchedClient"
}
$forgeCache = Join-Path (Get-ConfigPath "StagingDir") "cache\forge\$forgeCoordinate"
Ensure-Dir $forgeCache
$universalJar = Join-Path $forgeCache "forge-$forgeCoordinate-universal.jar"
if (-not (Test-Path $universalJar)) {
    Invoke-WebRequest -UseBasicParsing `
        -Uri "https://maven.minecraftforge.net/net/minecraftforge/forge/$forgeCoordinate/forge-$forgeCoordinate-universal.jar" `
        -OutFile $universalJar `
        -TimeoutSec 180
}

$libraryJars = @(Get-ChildItem -LiteralPath (Join-Path $gameDir "libraries") -Recurse -Filter "*.jar" -ErrorAction SilentlyContinue |
    Where-Object { $_.Name -notlike "*natives*" } |
    Select-Object -ExpandProperty FullName)

$mixinJar = @($libraryJars | Where-Object { (Split-Path $_ -Leaf) -like "*mixin*" } | Sort-Object -Descending) |
    Select-Object -First 1
if (-not $mixinJar) {
    throw "Mixin compile dependency is missing. prepare the Fabric cache first"
}

$compileOnlySources = @(
    Get-ChildItem $compileSource -Recurse -Filter "*.java"
    Get-ChildItem $neoCompileSource -Recurse -Filter "*.java"
)
$mainSourcesByName = @{}
Get-ChildItem $neoSource -Recurse -Filter "*.java" |
    Where-Object { $_.Name -ne "NeoForgeControllerMod.java" } |
    ForEach-Object { $mainSourcesByName[$_.Name] = $_ }
Get-ChildItem $coreSource -Recurse -Filter "*.java" | ForEach-Object { $mainSourcesByName[$_.Name] = $_ }
foreach ($layer in @($variantLayers[$MinecraftVersion])) {
    Get-ChildItem (Join-Path $neoVariants $layer) -Recurse -Filter "*.java" |
        ForEach-Object { $mainSourcesByName[$_.Name] = $_ }
}
Get-ChildItem $forgeSource -Recurse -Filter "*.java" | ForEach-Object { $mainSourcesByName[$_.Name] = $_ }
$mainSources = @($mainSourcesByName.Values)

$sourcePaths = @(@($compileOnlySources) + @($mainSources) | ForEach-Object { $_.FullName } | Sort-Object)
$resourceFiles = @(Get-ChildItem $resources -Recurse -File | Select-Object -ExpandProperty FullName)
$stampPath = Join-Path $buildRoot "build.stamp"
$stamp = New-BuildStamp `
    -Values @(
        "forge_modern_controller",
        $MinecraftVersion,
        $ForgeVersion,
        "jar=$jarName",
        "layers=$(@($variantLayers[$MinecraftVersion]) -join ',')",
        "sources=$($sourcePaths -join ';')"
    ) `
    -ContentFiles (@($PSCommandPath) + $sourcePaths + $resourceFiles) `
    -DependencyFiles @($patchedClient, $universalJar, $mixinJar)

if (Test-BuildStampCurrent -StampPath $stampPath -Stamp $stamp -RequiredOutputs @($jarPath)) {
    Write-Host "Forge controller mod up to date ($MinecraftVersion / $ForgeVersion), skipping compile."
    if ($OutputDir) {
        Ensure-Dir $OutputDir
        Copy-Item $jarPath (Join-Path $OutputDir $jarName) -Force
    }
    return
}

Remove-Item -Recurse -Force $buildRoot -ErrorAction SilentlyContinue
Ensure-Dir $classesDir, $compileOnlyDir

$classpath = @($patchedClient, $universalJar, $mixinJar) + $libraryJars

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
        throw "Forge controller $Name compilation failed"
    }
}

Invoke-Javac "compile-only" @($compileOnlySources.FullName) $compileOnlyDir $classpath
Invoke-Javac "main" @($mainSources.FullName) $classesDir (@($compileOnlyDir) + $classpath)

& (Join-Path $javaHome "bin\java.exe") -ea -cp $classesDir banditvault.controllercore.GridNavigation
if ($LASTEXITCODE -ne 0) {
    throw "Forge controller navigation self-check failed"
}
& (Join-Path $javaHome "bin\java.exe") -ea -cp $classesDir banditvault.controllercore.ControllerBindings
if ($LASTEXITCODE -ne 0) {
    throw "Forge controller binding self-check failed"
}
if (Test-Path (Join-Path $classesDir "net\minecraftforge")) {
    throw "Forge controller jar must not ship compile-only Forge API classes"
}

Copy-Item -Recurse "$resources\*" $classesDir -Force
$modsToml = Join-Path $classesDir "META-INF\mods.toml"
$forgeLoaderVersion = $ForgeVersion
if ($ForgeVersion.StartsWith("$MinecraftVersion-")) {
    $forgeLoaderVersion = $ForgeVersion.Substring($MinecraftVersion.Length + 1)
}
(Get-Content $modsToml -Raw).
    Replace("__FORGE_VERSION__", $forgeLoaderVersion).
    Replace("__MINECRAFT_VERSION__", $MinecraftVersion) |
    Set-Content $modsToml -NoNewline

$manifest = Join-Path $classesDir "META-INF\MANIFEST.MF"
Push-Location $classesDir
try {
    & $jar cfm $jarPath $manifest .
    if ($LASTEXITCODE -ne 0) { throw "Forge controller jar creation failed" }
} finally {
    Pop-Location
}

$listing = & $jar tf $jarPath
foreach ($required in @(
    "banditvault/forgecontroller/ForgeControllerMod.class",
    "banditvault/neoforgecontroller/NeoForgeControllerCompat.class",
    "META-INF/mods.toml",
    "banditvault-forge-controller.mixins.json",
    "pack.mcmeta"
)) {
    if ($listing -notcontains $required) { throw "Forge controller jar is missing $required" }
}
if ($listing | Where-Object { $_ -like "net/minecraftforge/*" }) {
    throw "Forge controller jar contains compile-only Forge classes"
}

Set-BuildStamp -StampPath $stampPath -Stamp $stamp

if ($OutputDir) {
    Ensure-Dir $OutputDir
    Copy-Item $jarPath (Join-Path $OutputDir $jarName) -Force
}
Write-Host "Forge controller mod built ($MinecraftVersion / $ForgeVersion) -> $jarPath"
