param(
    [string]$MinecraftVersion,
    [string]$LoaderVersion,
    [string]$OutputDir
)

$ErrorActionPreference = "Stop"

. (Join-Path (Split-Path $PSScriptRoot -Parent) "scripts\common.ps1")

$root = Resolve-RepoRoot
$srcJava = Join-Path $PSScriptRoot "src\main\java"
$srcResources = Join-Path $PSScriptRoot "src\main\resources"
if (-not $MinecraftVersion) { $MinecraftVersion = $ProjectConfig.MinecraftVersion }
if (-not $LoaderVersion) { $LoaderVersion = $ProjectConfig.FabricLoaderVersion }
$buildRoot = Join-Path (Get-ConfigPath "BuildDir") "compat_mod\$MinecraftVersion-$LoaderVersion"
$classesDir = Join-Path $buildRoot "classes"
$compatJarName = "$($ProjectConfig.CompatModId)-$($ProjectConfig.CompatModVersion).jar"
$jarPath = Join-Path $buildRoot $compatJarName
$gameDir = Get-ConfigPath "GameDir"
$modsDir = Join-Path $gameDir "mods"

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
New-Item -ItemType Directory -Force -Path $modsDir | Out-Null

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

function Test-ClientJarHasClass {
    param(
        [Parameter(Mandatory = $true)][string]$ClientJar,
        [Parameter(Mandatory = $true)][string]$ClassName
    )

    return [bool](& $jar tf $ClientJar | Select-String -SimpleMatch "net/minecraft/$ClassName.class" -Quiet)
}

$disabledMixins = @()
$disabledSources = @()
$mixinAuthorVersion = $ProjectConfig.DefaultMinecraftVersion
$controllerCompatVersions = @("1.16.5", "1.19.2", "1.20.1")
if ($controllerCompatVersions -notcontains $MinecraftVersion) {
    $disabledMixins += @("BanditControllerClientMixin", "BanditControllerGameRendererMixin", "BanditControllerHandledScreenMixin", "BanditControllerRecipeBookScreenMixin", "BanditControllerScreenMixin")
    $disabledSources += @("BanditControllerCompat", "BanditControllerSettings", "BanditControllerSettingsScreen")
}
# cursor overlay now lives in the glfw shim, so the mod never draws it on any version
$disabledMixins += @("BanditMouseCursorRecipeBookScreenMixin", "BanditMouseCursorScreenMixin")
$disabledSources += "BanditMouseCursorOverlay"

$sources = Get-ChildItem $srcJava -Recurse -Filter "*.java" | Select-Object -ExpandProperty FullName

$controllerVariantRoots = @{
    "1.20.1" = "1.20.1"
}
$controllerOverlayNames = @(
    "BanditControllerCompat.java",
    "BanditControllerSettingsScreen.java",
    "BanditControllerGameRendererMixin.java",
    "BanditControllerScreenMixin.java",
    "BanditControllerRecipeBookScreenMixin.java"
)
if ($controllerCompatVersions -contains $MinecraftVersion -and $controllerVariantRoots.ContainsKey($MinecraftVersion)) {
    $variantRoot = Join-Path $PSScriptRoot ("src\variants\" + $controllerVariantRoots[$MinecraftVersion])
    $sources = @($sources | Where-Object { $controllerOverlayNames -notcontains (Split-Path $_ -Leaf) })
    foreach ($name in $controllerOverlayNames) {
        $relative = if ($name -like "*Mixin.java") {
            Join-Path "banditvault\xboxcompat\mixin" $name
        } else {
            Join-Path "banditvault\xboxcompat" $name
        }
        $variantPath = Join-Path $variantRoot $relative
        if (-not (Test-Path $variantPath)) {
            throw "Missing controller variant source for ${MinecraftVersion}: $variantPath"
        }
        $sources += $variantPath
    }
}

if ($MinecraftVersion -eq $mixinAuthorVersion) {
    $disabledMixins += "ZipFsBypass121Mixin"
} else {
    $disabledMixins += @(
        "WorldLoadProgressTrackerMixin"
    )
    if ($MinecraftVersion -eq "1.21.1") {
        $disabledMixins += "ZipFsBypassMixin"
    } elseif ($MinecraftVersion -eq "1.20.4") {
        $disabledMixins += @("MinecraftClientProbeMixin", "ZipFsBypassMixin")
    } else {
        $disabledMixins += @("MinecraftClientProbeMixin", "PathUtilBypassMixin", "ZipFsBypassMixin", "ZipFsBypass121Mixin")
    }
}

if (-not (Test-ClientJarHasClass -ClientJar $clientJar -ClassName "class_11653")) {
    $disabledMixins += "WorldLoadProgressTrackerMixin"
}
if (-not (Test-ClientJarHasClass -ClientJar $clientJar -ClassName "class_10619")) {
    $disabledMixins += "ZipFsBypassMixin"
}
if (-not (Test-ClientJarHasClass -ClientJar $clientJar -ClassName "class_7665")) {
    $disabledMixins += "ZipFsBypass121Mixin"
}

$hasSystemDetailsClass = Test-ClientJarHasClass -ClientJar $clientJar -ClassName "class_6396"
if (-not $hasSystemDetailsClass) {
    $disabledMixins += "SystemDetailsOshiBypassMixin"
}

$disabledMixins = @($disabledMixins | Select-Object -Unique)
$sources = @($sources | Where-Object {
    $name = [System.IO.Path]::GetFileNameWithoutExtension($_)
    ($disabledMixins -notcontains $name) -and ($disabledSources -notcontains $name)
})
if (-not $sources) { throw "No compatibility mod sources found" }

$compileJars = @($clientJar, $mixinJar)
$lwjglJar = Get-ChildItem -LiteralPath (Join-Path $gameDir "libraries\org\lwjgl\lwjgl") -Recurse -Filter "lwjgl-*.jar" -ErrorAction SilentlyContinue |
    Where-Object { $_.Name -notlike "*natives*" } |
    Sort-Object FullName -Descending |
    Select-Object -First 1
$lwjglGlfwJar = Get-ChildItem -LiteralPath (Join-Path $gameDir "libraries\org\lwjgl\lwjgl-glfw") -Recurse -Filter "lwjgl-glfw-*.jar" -ErrorAction SilentlyContinue |
    Where-Object { $_.Name -notlike "*natives*" } |
    Sort-Object FullName -Descending |
    Select-Object -First 1
if ($lwjglJar) {
    $compileJars += $lwjglJar.FullName
}
if ($lwjglGlfwJar) {
    $compileJars += $lwjglGlfwJar.FullName
}
$oshiJar = Get-ChildItem -LiteralPath (Join-Path $gameDir "libraries\com\github\oshi\oshi-core") -Recurse -Filter "oshi-core-*.jar" -ErrorAction SilentlyContinue |
    Sort-Object FullName -Descending |
    Select-Object -First 1
if ($oshiJar) {
    $compileJars += $oshiJar.FullName
} else {
    Write-Warning "OSHI jar not found in cache; SystemDetailsOshiBypassMixin may fail to compile."
}
$brigadierJar = Get-ChildItem -LiteralPath (Join-Path $gameDir "libraries\com\mojang\brigadier") -Recurse -Filter "brigadier-*.jar" -ErrorAction SilentlyContinue |
    Sort-Object FullName -Descending |
    Select-Object -First 1
if ($brigadierJar) {
    $compileJars += $brigadierJar.FullName
} else {
    Write-Warning "Brigadier jar not found in cache; controller settings screen may fail to compile."
}
$cp = $compileJars -join ";"
$javaRelease = if (
    $MinecraftVersion -eq $mixinAuthorVersion -or
    (Test-MinecraftVersionAtLeast -Version $MinecraftVersion -Minimum "1.21")
) {
    $ProjectConfig.JavaRelease
} else {
    8
}
& $javac --release $javaRelease -proc:none -cp $cp -d $classesDir $sources
if ($LASTEXITCODE -ne 0) { throw "compatibility mod compile failed" }

Copy-Item -Recurse "$srcResources\*" $classesDir -Force
$fmj = Join-Path $classesDir "fabric.mod.json"
(Get-Content $fmj -Raw).
    Replace("__MINECRAFT_VERSION__", $MinecraftVersion).
    Replace("__FABRIC_LOADER_VERSION__", $LoaderVersion) |
    Set-Content $fmj -NoNewline

$usesLegacyMixinCompat = (
    $MinecraftVersion -ne $mixinAuthorVersion -and
    -not (Test-MinecraftVersionAtLeast -Version $MinecraftVersion -Minimum "1.21")
)
if ($disabledMixins.Count -gt 0 -or $usesLegacyMixinCompat) {
    $mixinsPath = Join-Path $classesDir "banditvault-xbox-compat.mixins.json"
    $mixins = Get-Content -Raw -Path $mixinsPath | ConvertFrom-Json
    if ($usesLegacyMixinCompat) {
        $mixins.compatibilityLevel = "JAVA_8"
    }
    if ($disabledMixins.Count -gt 0) {
        $mixins.client = @($mixins.client | Where-Object { $disabledMixins -notcontains $_ })
    }
    $mixins | ConvertTo-Json -Depth 10 | Set-Content -Path $mixinsPath
}

Push-Location $classesDir
& $jar cf $jarPath .
if ($LASTEXITCODE -ne 0) {
    Pop-Location
    throw "compatibility mod jar failed"
}
Pop-Location

if ($OutputDir) {
    New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null
    Copy-Item $jarPath (Join-Path $OutputDir $compatJarName) -Force
} else {
    Copy-Item $jarPath (Join-Path $modsDir $compatJarName) -Force
}
Write-Host "Compatibility mod built ($MinecraftVersion) -> $jarPath"
