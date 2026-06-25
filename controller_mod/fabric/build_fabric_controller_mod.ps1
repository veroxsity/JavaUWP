param(
    [string]$MinecraftVersion,
    [string]$LoaderVersion,
    [string]$OutputDir
)

$ErrorActionPreference = "Stop"

. (Join-Path (Split-Path (Split-Path $PSScriptRoot -Parent) -Parent) "scripts\common.ps1")

$root = Resolve-RepoRoot
$srcJava = Join-Path $PSScriptRoot "src\main\java"
$srcResources = Join-Path $PSScriptRoot "src\main\resources"
$controllerCoreJava = Join-Path $root "controller_mod\core\src\main\java"
if (-not $MinecraftVersion) { $MinecraftVersion = $ProjectConfig.MinecraftVersion }
if (-not $LoaderVersion) { $LoaderVersion = $ProjectConfig.FabricLoaderVersion }

$supportedVersions = @("1.16.5", "1.19.2", "1.20.1", "1.20.4", "1.21.1", "1.21.4", "1.21.11")
if ($supportedVersions -notcontains $MinecraftVersion) {
    throw "Fabric controller mod sources currently support Minecraft $($supportedVersions -join ', '). Add a controller variant before bundling $MinecraftVersion."
}

$buildRoot = Join-Path (Get-ConfigPath "BuildDir") "fabric_controller_mod\$MinecraftVersion-$LoaderVersion"
$classesDir = Join-Path $buildRoot "classes"
$jarName = "banditvault-fabric-controller-1.0.0.jar"
$jarPath = Join-Path $buildRoot $jarName
$gameDir = Get-ConfigPath "GameDir"

$javaHome = Resolve-JavaHome
$javac = Join-Path $javaHome "bin\javac.exe"
$jar = Join-Path $javaHome "bin\jar.exe"
$mixinVersion = $ProjectConfig.MixinVersion
$mixinJar = Join-Path $gameDir "libraries\net\fabricmc\sponge-mixin\$mixinVersion\sponge-mixin-$mixinVersion.jar"
$clientJar = Join-Path $gameDir ".fabric\remappedJars\minecraft-$MinecraftVersion-$LoaderVersion\client-intermediary.jar"
if (-not (Test-Path $clientJar)) {
    Write-Host "Remapped client jar missing for $MinecraftVersion-${LoaderVersion}; preparing Fabric cache."
    & (Join-Path $root "scripts\prepare-ci-cache.ps1") `
        -MinecraftVersion $MinecraftVersion `
        -FabricLoaderVersion $LoaderVersion
}
if (-not (Test-Path $clientJar)) {
    throw "Remapped client jar not found for $MinecraftVersion-${LoaderVersion}: $clientJar."
}

Remove-Item -Recurse -Force $buildRoot -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $classesDir | Out-Null

function Test-MinecraftVersionAtLeast {
    param(
        [Parameter(Mandatory = $true)][string]$Version,
        [Parameter(Mandatory = $true)][string]$Minimum
    )

    $versionParts = $Version.Split('.') | ForEach-Object { [int]$_ }
    $minimumParts = $Minimum.Split('.') | ForEach-Object { [int]$_ }
    $count = [Math]::Max($versionParts.Length, $minimumParts.Length)
    for ($i = 0; $i -lt $count; $i++) {
        $value = if ($i -lt $versionParts.Length) { $versionParts[$i] } else { 0 }
        $minimumValue = if ($i -lt $minimumParts.Length) { $minimumParts[$i] } else { 0 }
        if ($value -gt $minimumValue) { return $true }
        if ($value -lt $minimumValue) { return $false }
    }
    return $true
}

$sources = @(Get-ChildItem $srcJava -Recurse -Filter "*.java" | Select-Object -ExpandProperty FullName)
if (Test-Path $controllerCoreJava) {
    $sources += @(Get-ChildItem $controllerCoreJava -Recurse -Filter "*.java" | Select-Object -ExpandProperty FullName)
}

$variantLayers = @{
    "1.20.1" = @("1.20.1")
    "1.20.4" = @("1.20.4", "1.20.1")
    "1.21.1" = @("1.21", "1.20.4", "1.20.1")
    "1.21.4" = @("1.21.4", "1.21", "1.20.4", "1.20.1")
    "1.21.11" = @("1.21.11", "1.21.4", "1.21", "1.20.4", "1.20.1")
}
$overlayNames = @(
    "BanditControllerCompat.java",
    "BanditControllerSettingsScreen.java",
    "FabricScreenApi.java",
    "BanditControllerGameRendererMixin.java",
    "BanditControllerScreenMixin.java",
    "BanditControllerRecipeBookScreenMixin.java"
)
if ($MinecraftVersion -eq "1.21.11") {
    $overlayNames += "BanditControllerHandledScreenMixin.java"
}
if ($variantLayers.ContainsKey($MinecraftVersion)) {
    $sources = @($sources | Where-Object { $overlayNames -notcontains (Split-Path $_ -Leaf) })
    foreach ($name in $overlayNames) {
        $relative = if ($name -like "*Mixin.java") {
            Join-Path "banditvault\fabriccontroller\mixin" $name
        } else {
            Join-Path "banditvault\fabriccontroller" $name
        }
        $variantPath = $null
        foreach ($layer in $variantLayers[$MinecraftVersion]) {
            $candidate = Join-Path (Join-Path $PSScriptRoot ("src\variants\" + $layer)) $relative
            if (Test-Path $candidate) {
                $variantPath = $candidate
                break
            }
        }
        if (-not $variantPath) {
            throw "Missing Fabric controller variant source for ${MinecraftVersion}: $relative"
        }
        $sources += $variantPath
    }
}
if ($MinecraftVersion -eq "1.21.11") {
    $variantRoot = Join-Path $PSScriptRoot "src\variants\1.21.11"
    $sources += @(Get-ChildItem $variantRoot -Recurse -Filter "*.java" |
        Select-Object -ExpandProperty FullName |
        Where-Object { $sources -notcontains $_ })
}
if (-not $sources) { throw "No Fabric controller sources found" }

$compileJars = @($clientJar, $mixinJar)
$allLibraryJars = Get-ChildItem -LiteralPath (Join-Path $gameDir "libraries") -Recurse -Filter "*.jar" -ErrorAction SilentlyContinue |
    Where-Object { $_.Name -notlike "*natives*" } |
    Select-Object -ExpandProperty FullName
if ($allLibraryJars) { $compileJars += $allLibraryJars }
$cp = ($compileJars | Select-Object -Unique) -join ";"
$javaRelease = if (Test-MinecraftVersionAtLeast -Version $MinecraftVersion -Minimum "1.21") { 21 } else { 8 }

$argsFile = Join-Path $buildRoot "javac-args.txt"
$javacArgs = @(
    "--release", "$javaRelease",
    "-proc:none",
    "-classpath", $cp,
    "-d", $classesDir
) + $sources
[System.IO.File]::WriteAllLines($argsFile, $javacArgs)
& $javac "@$argsFile"
if ($LASTEXITCODE -ne 0) { throw "Fabric controller mod compile failed" }

Copy-Item -Recurse "$srcResources\*" $classesDir -Force
if ($MinecraftVersion -eq "1.21.11") {
    $variantResources = Join-Path $PSScriptRoot "src\variants\1.21.11\resources"
    if (Test-Path $variantResources) {
        Copy-Item -Recurse "$variantResources\*" $classesDir -Force
    }
}
$fmj = Join-Path $classesDir "fabric.mod.json"
(Get-Content $fmj -Raw).
    Replace("__MINECRAFT_VERSION__", $MinecraftVersion).
    Replace("__FABRIC_LOADER_VERSION__", $LoaderVersion) |
    Set-Content $fmj -NoNewline

$mixinsPath = Join-Path $classesDir "banditvault-fabric-controller.mixins.json"
if (-not (Test-MinecraftVersionAtLeast -Version $MinecraftVersion -Minimum "1.21")) {
    $mixins = Get-Content -Raw -Path $mixinsPath | ConvertFrom-Json
    $mixins.compatibilityLevel = "JAVA_8"
    $mixins | ConvertTo-Json -Depth 10 | Set-Content -Path $mixinsPath
}

Push-Location $classesDir
& $jar cf $jarPath .
if ($LASTEXITCODE -ne 0) {
    Pop-Location
    throw "Fabric controller mod jar failed"
}
Pop-Location

if ($OutputDir) {
    New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null
    Copy-Item $jarPath (Join-Path $OutputDir $jarName) -Force
}
Write-Host "Fabric controller mod built ($MinecraftVersion) -> $jarPath"
