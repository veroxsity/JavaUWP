param(
    [string]$MinecraftVersion = "1.20.1",
    [string]$ForgeVersion = "1.20.1-47.4.20",
    [string]$OutputDir
)

$ErrorActionPreference = "Stop"

. (Join-Path (Split-Path (Split-Path $PSScriptRoot -Parent) -Parent) "scripts\common.ps1")

$root = Resolve-RepoRoot
$srcJava = Join-Path $PSScriptRoot "src\main\java"
$srcResources = Join-Path $PSScriptRoot "src\main\resources"
$controllerCoreJava = Join-Path $root "controller_mod\core\src\main\java"
$buildRoot = Join-Path (Get-ConfigPath "BuildDir") "controller_mod\forge\$ForgeVersion"
$classesDir = Join-Path $buildRoot "classes"
$compileOnlyDir = Join-Path $buildRoot "compile-only"
$modId = "banditvault_forge_controller"
$jarName = "banditvault-forge-controller-1.0.0.jar"
$jarPath = Join-Path $buildRoot $jarName
$gameDir = Get-ConfigPath "GameDir"
$profilePath = Join-Path $root "config\forge-install-profile.json"

$javaHome = Resolve-JavaHome
$javac = Join-Path $javaHome "bin\javac.exe"
$jar = Join-Path $javaHome "bin\jar.exe"

if ($MinecraftVersion -ne "1.20.1") {
    & (Join-Path $PSScriptRoot "build_forge_modern_controller_mod.ps1") `
        -MinecraftVersion $MinecraftVersion `
        -ForgeVersion $ForgeVersion `
        -OutputDir $OutputDir
    return
}

if ($ForgeVersion -ne "1.20.1-47.4.20") {
    throw "Forge controller mod sources currently support only Minecraft 1.20.1 / Forge 1.20.1-47.4.20."
}

$patchedClient = & (Join-Path $root "scripts\prepare-forge-patched-client.ps1") `
    -MinecraftVersion $MinecraftVersion `
    -ForgeVersion $ForgeVersion
if (-not (Test-Path $patchedClient)) {
    throw "Forge patched client jar missing: $patchedClient"
}

$mixinJar = Get-ChildItem -LiteralPath (Join-Path $gameDir "libraries\net\fabricmc\sponge-mixin") -Recurse -Filter "sponge-mixin-*.jar" -ErrorAction SilentlyContinue |
    Sort-Object FullName -Descending |
    Select-Object -First 1
if (-not $mixinJar) {
    throw "Sponge Mixin jar not found in cache; run Fabric cache prep first."
}

if (-not (Test-Path $profilePath)) {
    throw "Forge install profile missing at $profilePath"
}
$forgeProfile = Get-Content -Raw -Path $profilePath | ConvertFrom-Json

$compileJava = Join-Path $PSScriptRoot "src\compile\java"
$compileOnlySources = @(Get-ChildItem $compileJava -Recurse -Filter "*.java" -ErrorAction SilentlyContinue)
$mainSources = @(Get-ChildItem $srcJava -Recurse -Filter "*.java")
if (Test-Path $controllerCoreJava) {
    $mainSources += @(Get-ChildItem $controllerCoreJava -Recurse -Filter "*.java")
}
if (-not $mainSources) { throw "No Forge controller sources found." }

$sourcePaths = @(@($compileOnlySources) + @($mainSources) | ForEach-Object { $_.FullName } | Sort-Object)
$resourceFiles = @(Get-ChildItem $srcResources -Recurse -File | Select-Object -ExpandProperty FullName)
$stampPath = Join-Path $buildRoot "build.stamp"
$stamp = New-BuildStamp `
    -Values @(
        "forge_controller",
        $MinecraftVersion,
        $ForgeVersion,
        "jar=$jarName",
        "sources=$($sourcePaths -join ';')"
    ) `
    -ContentFiles (@($PSCommandPath, $profilePath) + $sourcePaths + $resourceFiles) `
    -DependencyFiles @($patchedClient, $mixinJar.FullName)

if (Test-BuildStampCurrent -StampPath $stampPath -Stamp $stamp -RequiredOutputs @($jarPath)) {
    Write-Host "Forge controller mod up to date ($ForgeVersion), skipping compile."
    if ($OutputDir) {
        Ensure-Dir $OutputDir
        Copy-Item $jarPath (Join-Path $OutputDir $jarName) -Force
    }
    return
}

Remove-Item -Recurse -Force $buildRoot -ErrorAction SilentlyContinue
Ensure-Dir $classesDir, $compileOnlyDir

$compileJars = @()
if ($patchedClient -is [string]) {
    $compileJars += $patchedClient
} else {
    $compileJars += $patchedClient.FullName
}
$compileJars += $mixinJar.FullName

foreach ($lib in $forgeProfile.libraries) {
    $artifact = $lib.downloads.artifact
    if (-not $artifact) { continue }
    $path = Join-Path $gameDir ("libraries\" + $artifact.path.Replace("/", "\"))
    if ((Test-Path $path) -and $path -like "*.jar") {
        $compileJars += $path
    }
}

$mcClientJar = Join-Path $gameDir "versions\$MinecraftVersion\$MinecraftVersion.jar"
if (Test-Path $mcClientJar) { $compileJars += $mcClientJar }
$mcSrgJar = Get-ChildItem -LiteralPath (Join-Path $gameDir "libraries\net\minecraft\client") -Recurse -Filter "client-*-srg.jar" -ErrorAction SilentlyContinue |
    Sort-Object FullName -Descending |
    Select-Object -First 1 -ExpandProperty FullName
if ($mcSrgJar) { $compileJars += $mcSrgJar }

$allLwjglJars = Get-ChildItem -LiteralPath (Join-Path $gameDir "libraries\org\lwjgl") -Recurse -Filter "*.jar" -ErrorAction SilentlyContinue |
    Where-Object { $_.Name -notlike "*natives*" } |
    Select-Object -ExpandProperty FullName
if ($allLwjglJars) { $compileJars += $allLwjglJars }

$allLibraryJars = Get-ChildItem -LiteralPath (Join-Path $gameDir "libraries") -Recurse -Filter "*.jar" -ErrorAction SilentlyContinue |
    Where-Object { $_.Name -notlike "*natives*" } |
    Select-Object -ExpandProperty FullName
if ($allLibraryJars) { $compileJars += $allLibraryJars }

$cpEntries = $compileJars | Select-Object -Unique
function Invoke-JavacArgsFile([string]$argsFile, [string[]]$argsList) {
    [System.IO.File]::WriteAllLines($argsFile, $argsList)
    & $javac "@$argsFile"
    if ($LASTEXITCODE -ne 0) { throw "Forge controller mod compile failed." }
}

if ($compileOnlySources.Count -gt 0) {
    $compileOnlyArgsFile = Join-Path $buildRoot "javac-compile-only-args.txt"
    $compileOnlyArgs = @(
        "--release", "17",
        "-proc:none",
        "-classpath", ($cpEntries -join ";"),
        "-d", $compileOnlyDir
    ) + @($compileOnlySources | ForEach-Object { $_.FullName })
    Invoke-JavacArgsFile $compileOnlyArgsFile $compileOnlyArgs
}

$mainArgsFile = Join-Path $buildRoot "javac-main-args.txt"
$mainClasspath = if ($compileOnlySources.Count -gt 0) {
    "$compileOnlyDir;" + ($cpEntries -join ";")
} else {
    ($cpEntries -join ";")
}
$mainArgs = @(
    "--release", "17",
    "-proc:none",
    "-classpath", $mainClasspath,
    "-d", $classesDir
) + @($mainSources | ForEach-Object { $_.FullName })
Invoke-JavacArgsFile $mainArgsFile $mainArgs

$shippedForgeApi = Join-Path $classesDir "net\minecraftforge"
if (Test-Path $shippedForgeApi) {
    throw "Forge controller mod must not ship Forge API classes (found $shippedForgeApi)."
}

Copy-Item -Recurse "$srcResources\*" $classesDir -Force

$manifestPath = Join-Path $classesDir "META-INF\MANIFEST.MF"
if (-not (Test-Path $manifestPath)) {
    throw "Forge controller manifest missing: $manifestPath"
}

Push-Location $classesDir
& $jar cfm $jarPath $manifestPath .
if ($LASTEXITCODE -ne 0) {
    Pop-Location
    throw "Forge controller mod jar failed."
}
Pop-Location

$jarListing = & $jar tf $jarPath
if ($jarListing | Where-Object { $_ -like "net/minecraftforge/*" }) {
    throw "Packaged Forge controller mod still contains Forge API classes."
}
if ($jarListing -notcontains "banditvault/forgecontroller/ForgeControllerMod.class") {
    throw "Packaged Forge controller mod is missing @Mod entrypoint class."
}
if ($jarListing -notcontains "META-INF/mods.toml") {
    throw "Packaged Forge controller mod is missing mods.toml."
}
if ($jarListing -notcontains "banditvault-forge-controller.mixins.json") {
    throw "Packaged Forge controller mod is missing mixin config."
}
if ($jarListing -notcontains "pack.mcmeta") {
    throw "Packaged Forge controller mod is missing pack.mcmeta."
}
$manifestText = Get-Content $manifestPath -Raw
if ($manifestText -notmatch "MixinConfigs:\s*banditvault-forge-controller\.mixins\.json") {
    throw "Forge controller manifest is missing MixinConfigs entry."
}

Set-BuildStamp -StampPath $stampPath -Stamp $stamp

if ($OutputDir) {
    Ensure-Dir $OutputDir
    Copy-Item $jarPath (Join-Path $OutputDir $jarName) -Force
}
Write-Host "Forge controller mod built ($ForgeVersion) -> $jarPath"
