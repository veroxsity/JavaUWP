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

$variantRootDir = Join-Path $PSScriptRoot "src\variants"
$variantLayers = @{
    "1.20.1" = @("1.20.1")
    "1.20.4" = @("1.20.4", "1.20.1")
    "1.21" = @("1.21", "1.20.4", "1.20.1")
    "1.21.1" = @("1.21.1", "1.21", "1.20.4", "1.20.1")
    "1.21.2" = @("1.21.4", "1.21", "1.20.4", "1.20.1")
    "1.21.3" = @("1.21.4", "1.21", "1.20.4", "1.20.1")
    "1.21.4" = @("1.21.4", "1.21", "1.20.4", "1.20.1")
    "1.21.5" = @("1.21.5", "1.21.4", "1.21", "1.20.4", "1.20.1")
    "1.21.6" = @("1.21.6", "1.21.5", "1.21.4", "1.21", "1.20.4", "1.20.1")
    "1.21.7" = @("1.21.6", "1.21.5", "1.21.4", "1.21", "1.20.4", "1.20.1")
    "1.21.8" = @("1.21.6", "1.21.5", "1.21.4", "1.21", "1.20.4", "1.20.1")
    "1.21.9" = @("1.21.11", "1.21.4", "1.21", "1.20.4", "1.20.1")
    "1.21.10" = @("1.21.11", "1.21.4", "1.21", "1.20.4", "1.20.1")
    "1.21.11" = @("1.21.11", "1.21.4", "1.21", "1.20.4", "1.20.1")
    "26.1" = @("26.1", "26.2", "1.21.11", "1.21.4", "1.21", "1.20.4", "1.20.1")
    "26.1.1" = @("26.1", "26.2", "1.21.11", "1.21.4", "1.21", "1.20.4", "1.20.1")
    "26.1.2" = @("26.1", "26.2", "1.21.11", "1.21.4", "1.21", "1.20.4", "1.20.1")
    "26.2" = @("26.2", "1.21.11", "1.21.4", "1.21", "1.20.4", "1.20.1")
}
$supportedVersions = @($variantLayers.Keys)
if ($supportedVersions -notcontains $MinecraftVersion) {
    throw "Fabric controller mod sources currently support Minecraft $($supportedVersions -join ', '). Add a controller variant before bundling $MinecraftVersion."
}

$buildRoot = Join-Path (Get-ConfigPath "BuildDir") "fabric_controller_mod\$MinecraftVersion-$LoaderVersion"
$classesDir = Join-Path $buildRoot "classes"
$jarName = "banditvault-fabric-controller-1.0.0.jar"
$jarPath = Join-Path $buildRoot $jarName
$gameDir = Get-ConfigPath "GameDir"

$javaHome = Resolve-JavaHomeForMinecraft -MinecraftVersion $MinecraftVersion
$java = Join-Path $javaHome "bin\java.exe"
$javac = Join-Path $javaHome "bin\javac.exe"
$jar = Join-Path $javaHome "bin\jar.exe"
$mixinJar = Resolve-SpongeMixinJar -GameDir $gameDir -MinecraftVersion $MinecraftVersion -LoaderVersion $LoaderVersion
$clientJar = Resolve-FabricClientJar -GameDir $gameDir -MinecraftVersion $MinecraftVersion -LoaderVersion $LoaderVersion
if (-not (Test-Path $clientJar)) {
    Write-Host "Client jar missing for $MinecraftVersion-${LoaderVersion}; preparing Fabric cache."
    & (Join-Path $root "scripts\setup.ps1") `
        -MinecraftVersion $MinecraftVersion `
        -FabricLoaderVersion $LoaderVersion
    $clientJar = Resolve-FabricClientJar -GameDir $gameDir -MinecraftVersion $MinecraftVersion -LoaderVersion $LoaderVersion
}

if (-not (Test-Path $clientJar)) {
    throw "Client jar not found for $MinecraftVersion-${LoaderVersion}: $clientJar."
}

function Resolve-VariantSources([string]$Version) {
    if (-not $variantLayers.ContainsKey($Version)) {
        throw "$Version is declared supported but has no variant layer, so it would build from src/main only and ship without the accessors and the controls and inventory screen mixins. Add a layer entry and a variant directory before claiming controller support."
    }

    $resolved = @{}
    Get-ChildItem $srcJava -Recurse -Filter "*.java" | ForEach-Object { $resolved[$_.Name] = $_.FullName }
    if (Test-Path $controllerCoreJava) {
        Get-ChildItem $controllerCoreJava -Recurse -Filter "*.java" | ForEach-Object { $resolved[$_.Name] = $_.FullName }
    }

    $layers = @($variantLayers[$Version])
    for ($i = $layers.Count - 1; $i -ge 0; $i--) {
        $layerDir = Join-Path $variantRootDir $layers[$i]
        Get-ChildItem $layerDir -Recurse -Filter "*.java" | ForEach-Object { $resolved[$_.Name] = $_.FullName }
    }
    return @($resolved.Values)
}

$sources = @(Resolve-VariantSources $MinecraftVersion)
if (-not $sources) { throw "No Fabric controller sources found" }

$parityReference = "26.2"
# the only allowed differences, each one a class the target's own Minecraft version cannot support
$referenceOnlySources = @{
    "1.20.1" = @{
        "BanditControllerOptionsSubScreenAccessor.java" = "class_4667 has no field_51824 before 1.21.1, and the 1.20.x controls screen mixin adds its button without one"
        "BanditControllerRecipeBookScreenAccessor.java" = "class_10260 does not exist before 1.21.2"
    }
    "1.20.4" = @{
        "BanditControllerOptionsSubScreenAccessor.java" = "class_4667 has no field_51824 before 1.21.1, and the 1.20.x controls screen mixin adds its button without one"
        "BanditControllerRecipeBookScreenAccessor.java" = "class_10260 does not exist before 1.21.2"
    }
    "1.21" = @{
        "BanditControllerOptionsSubScreenAccessor.java" = "class_4667 has no field_51824 before 1.21.1, and the controls screen mixin adds its button without one"
        "BanditControllerRecipeBookScreenAccessor.java" = "class_10260 does not exist before 1.21.2"
    }
    "1.21.1" = @{
        "BanditControllerRecipeBookScreenAccessor.java" = "class_10260 does not exist before 1.21.2"
    }
    "1.21.2" = @{}
    "1.21.3" = @{}
    "1.21.4" = @{}
    "1.21.5" = @{}
    "1.21.6" = @{}
    "1.21.7" = @{}
    "1.21.8" = @{}
    "1.21.9" = @{}
    "1.21.10" = @{}
    "1.21.11" = @{}
    "26.1" = @{}
    "26.1.1" = @{}
    "26.1.2" = @{}
    "26.2" = @{}
}

if ($MinecraftVersion -ne $parityReference) {
    $referenceLeaves = @(Resolve-VariantSources $parityReference | ForEach-Object { Split-Path $_ -Leaf })
    $targetLeaves = @($sources | ForEach-Object { Split-Path $_ -Leaf })
    $allowed = $referenceOnlySources[$MinecraftVersion]
    if ($null -eq $allowed) { throw "$MinecraftVersion has no `$referenceOnlySources entry, so its parity is undeclared. Add one, empty if the target is at parity." }

    $missing = @($referenceLeaves | Where-Object { $targetLeaves -notcontains $_ } | Sort-Object)
    $undeclared = @($missing | Where-Object { -not $allowed.ContainsKey($_) })
    $closed = @($allowed.Keys | Where-Object { $targetLeaves -contains $_ } | Sort-Object)
    $extra = @($targetLeaves | Where-Object { $referenceLeaves -notcontains $_ } | Sort-Object)

    if ($undeclared) {
        throw "$MinecraftVersion is missing $($undeclared.Count) source(s) that $parityReference has: $($undeclared -join ', '). Port them, or add each to `$referenceOnlySources with the reason its Minecraft version cannot support it."
    }
    if ($closed) {
        throw "$MinecraftVersion now has $($closed -join ', '), which `$referenceOnlySources still declares as unsupportable. Remove those entries."
    }
    if ($extra) {
        throw "$MinecraftVersion resolves $($extra -join ', '), which $parityReference does not have, so the reference is no longer the superset. Port them up before continuing."
    }
}

$compileJars = @($clientJar, $mixinJar)
$libraryJarItems = @(Get-ChildItem -LiteralPath (Join-Path $gameDir "libraries") -Recurse -Filter "*.jar" -ErrorAction SilentlyContinue |
    Where-Object { $_.Name -notlike "*natives*" })
$allLibraryJars = @($libraryJarItems | Select-Object -ExpandProperty FullName)
if ($allLibraryJars) { $compileJars += $allLibraryJars }
# count and total size stands in for stamping 600 jars individually, which costs a Get-Item each
$libraryDigest = "{0}:{1}" -f $allLibraryJars.Count, (($libraryJarItems | Measure-Object Length -Sum).Sum)
$cp = ($compileJars | Select-Object -Unique) -join ";"
$javaRelease = if (Test-MinecraftVersionAtLeast -Version $MinecraftVersion -Minimum "1.21") { 21 } elseif (Test-MinecraftVersionAtLeast -Version $MinecraftVersion -Minimum "1.17") { 17 } else { 8 }

$stampPath = Join-Path $buildRoot "build.stamp"
$variantResources = $null
foreach ($layer in $variantLayers[$MinecraftVersion]) {
    $candidate = Join-Path $PSScriptRoot "src\variants\$layer\resources"
    if (Test-Path $candidate) {
        $variantResources = $candidate
        break
    }
}
if (-not $variantResources) {
    throw "Missing Fabric controller resources for $MinecraftVersion"
}
$resourceFiles = @(Get-ChildItem $srcResources -Recurse -File -ErrorAction SilentlyContinue | Select-Object -ExpandProperty FullName)
$resourceFiles += @(Get-ChildItem $variantResources -Recurse -File | Select-Object -ExpandProperty FullName)
$compatLevel = if (Test-MinecraftVersionAtLeast -Version $MinecraftVersion -Minimum "1.21") { "JAVA_21" } elseif (Test-MinecraftVersionAtLeast -Version $MinecraftVersion -Minimum "1.17") { "JAVA_17" } else { "JAVA_8" }
$stamp = New-BuildStamp `
    -Values @(
        "fabric_controller_mod",
        $MinecraftVersion,
        $LoaderVersion,
        "release=$javaRelease",
        "compatLevel=$compatLevel",
        "jar=$jarName",
        "libs=$libraryDigest",
        "sources=$(($sources | Sort-Object) -join ';')"
    ) `
    -ContentFiles (@($PSCommandPath) + $sources + $resourceFiles) `
    -DependencyFiles @($clientJar, $mixinJar)

if (Test-BuildStampCurrent -StampPath $stampPath -Stamp $stamp -RequiredOutputs @($jarPath)) {
    Write-Host "Fabric controller mod up to date ($MinecraftVersion), skipping compile."
} else {
    Remove-Item -Recurse -Force $buildRoot -ErrorAction SilentlyContinue
    Ensure-Dir $classesDir

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
    Copy-Item -Recurse "$variantResources\*" $classesDir -Force
    $fmj = Join-Path $classesDir "fabric.mod.json"
    (Get-Content $fmj -Raw).
        Replace("__MINECRAFT_VERSION__", $MinecraftVersion).
        Replace("__FABRIC_LOADER_VERSION__", $LoaderVersion) |
        Set-Content $fmj -NoNewline

    $mixinsPath = Join-Path $classesDir "banditvault-fabric-controller.mixins.json"
    (Get-Content $mixinsPath -Raw).Replace("__COMPAT_LEVEL__", $compatLevel) | Set-Content $mixinsPath -NoNewline
    Push-Location $classesDir
    & $jar cf $jarPath .
    if ($LASTEXITCODE -ne 0) {
        Pop-Location
        throw "Fabric controller mod jar failed"
    }
    Pop-Location

    Set-BuildStamp -StampPath $stampPath -Stamp $stamp
}

& $java -ea -cp $jarPath banditvault.controllercore.ControllerBindings
if ($LASTEXITCODE -ne 0) { throw "Fabric controller binding self-check failed" }

if ($OutputDir) {
    Ensure-Dir $OutputDir
    Copy-Item $jarPath (Join-Path $OutputDir $jarName) -Force
}
Write-Host "Fabric controller mod built ($MinecraftVersion) -> $jarPath"
