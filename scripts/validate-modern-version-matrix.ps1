param([string]$CatalogPath, [string]$PackagePath, [string]$AppxPath)
$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "common.ps1")
$root = Resolve-RepoRoot
if (-not $CatalogPath) { $CatalogPath = Join-Path $root $ProjectConfig.VersionCatalog }
if (-not $PackagePath) { $PackagePath = Get-ConfigPath "PackageContentDir" }
$targets = @(Import-Csv $CatalogPath -Delimiter "`t" | Where-Object { $_.loaderVersion -and $_.loaderVersion -ne 'selected' -and $_.loaderVersion -ne 'none' })
Add-Type -AssemblyName System.IO.Compression.FileSystem
$zip = $null
if ($AppxPath) { $zip = [IO.Compression.ZipFile]::OpenRead($AppxPath) }
try {
    function Require-Entry([string]$Relative) {
        if ($zip) {
            $entry = $zip.GetEntry($Relative.Replace('\', '/'))
            if (-not $entry -or $entry.Length -eq 0) { throw "Package entry missing or empty $Relative" }
        } else {
            $path = Join-Path $PackagePath $Relative
            if (-not (Test-Path -LiteralPath $path -PathType Leaf) -or (Get-Item -LiteralPath $path).Length -eq 0) {
                throw "Package entry missing or empty $Relative"
            }
            if ($Relative.EndsWith('.jar') -and -not (Test-JarComplete $path)) { throw "Invalid packaged jar $Relative" }
        }
    }

    function Read-EntryText([string]$Relative) {
        if ($zip) {
            $entry = $zip.GetEntry($Relative.Replace('\', '/'))
            if (-not $entry) { throw "Package entry missing $Relative" }
            $stream = $entry.Open()
            $reader = New-Object System.IO.StreamReader($stream, [System.Text.Encoding]::UTF8, $true)
            try { return $reader.ReadToEnd() }
            finally { $reader.Dispose(); $stream.Dispose() }
        }
        return [System.IO.File]::ReadAllText((Join-Path $PackagePath $Relative), [System.Text.Encoding]::UTF8)
    }
    Require-Entry "runtime\version_catalog.tsv"
    Require-Entry "runtime\recommended-mods.json"
    Require-Entry "natives\glfw.dll"
    if ($targets | Where-Object { Test-MinecraftVersionAtLeast $_.minecraftVersion '26.3' }) {
        Require-Entry "natives\SDL3.dll"
        # SDL's UWP WGL backend loads the top-level module with a package-relative
        # path. Dependencies are also staged at the package root so their plain
        # module-name lookup remains inside the APPX package graph.
        Require-Entry "graphics\mesa\opengl32.dll"
        foreach ($mesaDll in @("libgallium_wgl.dll", "dxil.dll", "spirv_to_dxil.dll", "z-1.dll")) {
            Require-Entry $mesaDll
        }
    }
    foreach ($inputFile in @(
        @{ Source = $CatalogPath; Relative = 'runtime/version_catalog.tsv' },
        @{ Source = (Join-Path $root 'config/recommended-mods.json'); Relative = 'runtime/recommended-mods.json' }
    )) {
        if ($zip) {
            $stream = $zip.GetEntry($inputFile.Relative).Open()
            $hasher = [Security.Cryptography.SHA256]::Create()
            try { $packagedHash = [BitConverter]::ToString($hasher.ComputeHash($stream)).Replace('-', '') }
            finally { $stream.Dispose(); $hasher.Dispose() }
        } else {
            $packagedHash = (Get-FileHash -LiteralPath (Join-Path $PackagePath $inputFile.Relative)).Hash
        }
        if ($packagedHash -ne (Get-FileHash -LiteralPath $inputFile.Source).Hash) {
            throw "Packaged input does not match source $($inputFile.Relative)"
        }
    }
    foreach ($target in $targets) {
        $id = "$($target.minecraftVersion)-$($target.loader)-$($target.loaderVersion)"
        $manifestRelative = "runtime\manifests\$id.tsv"
        Require-Entry $manifestRelative
        $manifestText = Read-EntryText $manifestRelative
        $usesSdlBackend = $manifestText -match '(?i)game/libraries/org/lwjgl/lwjgl-sdl/'
        $usesGlfwBackend = $manifestText -match '(?i)game/libraries/org/lwjgl/lwjgl-glfw/'
        if ($usesSdlBackend) {
            Require-Entry "natives\SDL3.dll"
        } elseif ($usesGlfwBackend) {
            Require-Entry "natives\glfw.dll"
        } else {
            throw "Target $id manifest declares neither lwjgl-sdl nor lwjgl-glfw; no supported window backend can be selected"
        }

        if (Test-MinecraftVersionAtLeast $target.minecraftVersion '26.3') {
            $required263Libraries = @(
                'game/libraries/org/lwjgl/lwjgl/3.4.3/lwjgl-3.4.3.jar',
                'game/libraries/org/lwjgl/lwjgl/3.4.3/lwjgl-3.4.3-natives-windows.jar',
                'game/libraries/org/lwjgl/lwjgl-opengl/3.4.3/lwjgl-opengl-3.4.3.jar',
                'game/libraries/org/lwjgl/lwjgl-opengl/3.4.3/lwjgl-opengl-3.4.3-natives-windows.jar',
                'game/libraries/org/lwjgl/lwjgl-sdl/3.4.3/lwjgl-sdl-3.4.3.jar',
                'game/libraries/org/lwjgl/lwjgl-sdl/3.4.3/lwjgl-sdl-3.4.3-natives-windows.jar',
                'game/libraries/org/lwjgl/lwjgl-jemalloc/3.4.3/lwjgl-jemalloc-3.4.3-natives-windows.jar'
            )
            foreach ($relativeLibrary in $required263Libraries) {
                if ($manifestText -notmatch [regex]::Escape($relativeLibrary)) {
                    throw "26.3 target $id is missing the expected LWJGL 3.4.3 artifact/native: $relativeLibrary"
                }
            }
            if ($manifestText -notmatch ('(?m)^# minecraftVersion\t' + [regex]::Escape($target.minecraftVersion) + '\r?$')) {
                throw "Target $id manifest metadata does not identify Minecraft $($target.minecraftVersion)"
            }
        }

        if ($target.loader -eq 'fabric') {
            Require-Entry "runtime\libraries\net\fabricmc\fabric-loader\$($target.loaderVersion)\fabric-loader-$($target.loaderVersion).jar"
            Require-Entry "runtime\version-mods\$id\banditvault-xbox-compat-1.0.0.jar"
        }
        if ($target.controllerProvider -ne 'none') {
            Require-Entry "runtime\version-mods\$id\banditvault-$($target.controllerProvider)-controller-1.0.0.jar"
        }
        $runtimeDir = switch ($target.javaRuntime) { 'java17' { 'jre17' } 'java21' { 'jre21' } 'legacy' { 'jre8' } default { 'jre' } }
        Require-Entry "$runtimeDir\bin\server\jvm.dll"
        $suffix = switch ($runtimeDir) { 'jre17' { '-17' } 'jre21' { '-21' } 'jre8' { '-8' } default { '' } }
        foreach ($base in @('java-base-uwp-filesystem','java-zipfs-realpath','java-desktop-uwp-awt')) {
            Require-Entry "$base$suffix.jar"
        }
    }
    Write-Host "Package validation passed $($targets.Count) targets"
} finally { if ($zip) { $zip.Dispose() } }
