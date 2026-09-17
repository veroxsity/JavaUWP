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

$javaHome = Resolve-JavaHomeForMinecraft -MinecraftVersion $MinecraftVersion
$javac = Join-Path $javaHome "bin\javac.exe"
$jar = Join-Path $javaHome "bin\jar.exe"
$javap = Join-Path $javaHome "bin\javap.exe"
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

Ensure-Dir $modsDir

$script:ClientJarEntries = @{}
function Test-ClientJarHasClass {
    param(
        [Parameter(Mandatory = $true)][string]$ClientJar,
        [Parameter(Mandatory = $true)][string]$ClassName
    )

    # jar tf starts a jvm and pipes 25k lines through Select-String per probe, four probes per
    # target across 20 targets. reading the central directory once is about 20x cheaper
    if (-not $script:ClientJarEntries.ContainsKey($ClientJar)) {
        Add-Type -AssemblyName System.IO.Compression.FileSystem
        $archive = [System.IO.Compression.ZipFile]::OpenRead($ClientJar)
        try {
            $names = New-Object "System.Collections.Generic.HashSet[string]"
            foreach ($entry in $archive.Entries) { [void]$names.Add($entry.FullName) }
            $script:ClientJarEntries[$ClientJar] = $names
        } finally {
            $archive.Dispose()
        }
    }

    return $script:ClientJarEntries[$ClientJar].Contains("net/minecraft/$ClassName.class")
}

$disabledMixins = @()
$disabledSources = @()
$mixinAuthorVersion = $ProjectConfig.DefaultMinecraftVersion
# cursor overlay now lives in the glfw shim, so the mod never draws it on any version
$disabledMixins += @("BanditMouseCursorRecipeBookScreenMixin", "BanditMouseCursorScreenMixin")
$disabledSources += "BanditMouseCursorOverlay"

$sources = Get-ChildItem $srcJava -Recurse -Filter "*.java" | Select-Object -ExpandProperty FullName

# a version folder overlays same-named main sources and owns its own mixin list, which is how
# unobfuscated targets opt out of the intermediary-keyed gating below
$variantVersion = if ($MinecraftVersion -like "26.*") { "26.2" } else { $MinecraftVersion }
$variantDir = Join-Path $PSScriptRoot "src\variants\$variantVersion"
$variantMixinsJson = Join-Path $variantDir "resources\banditvault-xbox-compat.mixins.json"
$hasVariant = Test-Path $variantDir

if ($hasVariant) {
    $variantSources = @(Get-ChildItem $variantDir -Recurse -Filter "*.java" | Select-Object -ExpandProperty FullName)
    $variantLeafNames = @($variantSources | ForEach-Object { Split-Path $_ -Leaf })
    $sources = @($sources | Where-Object { $variantLeafNames -notcontains (Split-Path $_ -Leaf) })
    $sources += $variantSources

    if (-not (Test-Path $variantMixinsJson)) {
        throw "Variant compat mod for $MinecraftVersion needs $variantMixinsJson"
    }
    $keep = @((Get-Content -Raw -Path $variantMixinsJson | ConvertFrom-Json).client)
    $sources = @($sources | Where-Object {
        $name = [System.IO.Path]::GetFileNameWithoutExtension($_)
        ($name -notlike "*Mixin") -or ($keep -contains $name)
    })
} elseif ($MinecraftVersion -eq $mixinAuthorVersion) {
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

if (-not $hasVariant) {
    if (-not (Test-ClientJarHasClass -ClientJar $clientJar -ClassName "class_11653")) {
        $disabledMixins += "WorldLoadProgressTrackerMixin"
    }
    if (-not (Test-ClientJarHasClass -ClientJar $clientJar -ClassName "class_10619")) {
        $disabledMixins += "ZipFsBypassMixin"
    }
    if (-not (Test-ClientJarHasClass -ClientJar $clientJar -ClassName "class_7665")) {
        $disabledMixins += "ZipFsBypass121Mixin"
    }
    if (-not (Test-ClientJarHasClass -ClientJar $clientJar -ClassName "class_6396")) {
        $disabledMixins += "SystemDetailsOshiBypassMixin"
    }
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
$usesLegacyMixinCompat = (
    $MinecraftVersion -ne $mixinAuthorVersion -and
    -not (Test-MinecraftVersionAtLeast -Version $MinecraftVersion -Minimum "1.21")
)
$variantResources = Join-Path $variantDir "resources"

$stampPath = Join-Path $buildRoot "build.stamp"
$resourceFiles = @(Get-ChildItem $srcResources -Recurse -File -ErrorAction SilentlyContinue | Select-Object -ExpandProperty FullName)
if ($hasVariant -and (Test-Path $variantResources)) {
    $resourceFiles += @(Get-ChildItem $variantResources -Recurse -File | Select-Object -ExpandProperty FullName)
}
$stamp = New-BuildStamp `
    -Values @(
        "compat_mod",
        $MinecraftVersion,
        $LoaderVersion,
        "release=$javaRelease",
        "legacyMixin=$usesLegacyMixinCompat",
        "jar=$compatJarName",
        "disabled=$(($disabledMixins | Sort-Object) -join ',')",
        "sources=$(($sources | Sort-Object) -join ';')"
    ) `
    -ContentFiles (@($PSCommandPath) + $sources + $resourceFiles) `
    -DependencyFiles $compileJars

if (Test-BuildStampCurrent -StampPath $stampPath -Stamp $stamp -RequiredOutputs @($jarPath)) {
    Write-Host "Compatibility mod up to date ($MinecraftVersion), skipping compile."
} else {
    Remove-Item -Recurse -Force $buildRoot -ErrorAction SilentlyContinue
    Ensure-Dir $classesDir

    & $javac --release $javaRelease -proc:none -cp $cp -d $classesDir $sources
    if ($LASTEXITCODE -ne 0) { throw "compatibility mod compile failed" }

    Copy-Item -Recurse "$srcResources\*" $classesDir -Force
    if ($hasVariant -and (Test-Path $variantResources)) {
        Copy-Item -Recurse "$variantResources\*" $classesDir -Force
    }
    $fmj = Join-Path $classesDir "fabric.mod.json"
    (Get-Content $fmj -Raw).
        Replace("__MINECRAFT_VERSION__", $MinecraftVersion).
        Replace("__FABRIC_LOADER_VERSION__", $LoaderVersion) |
        Set-Content $fmj -NoNewline

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

    Set-BuildStamp -StampPath $stampPath -Stamp $stamp
}

if ($OutputDir) {
    Ensure-Dir $OutputDir
    Copy-Item $jarPath (Join-Path $OutputDir $compatJarName) -Force
} else {
    Copy-Item $jarPath (Join-Path $modsDir $compatJarName) -Force
}
Write-Host "Compatibility mod built ($MinecraftVersion) -> $jarPath"
