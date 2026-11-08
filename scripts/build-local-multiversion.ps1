# Full launcher package: build and validate every implemented target in config/versions.tsv.
param([string]$AppxVersion = '1.0.0.17')
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$names = @('DEFAULT_MC_VERSION', 'VERSION_CATALOG')
$saved = @{}
foreach ($name in $names) { $saved[$name] = [Environment]::GetEnvironmentVariable($name, 'Process') }
try {
    $env:DEFAULT_MC_VERSION = '26.3'
    $env:VERSION_CATALOG = 'config/versions.tsv'

    # Fail before the expensive multi-target downloads if the two fixed legacy
    # runtimes needed by the catalog are unavailable on the build machine.
    . (Join-Path $root 'scripts\common.ps1')
    $null = Resolve-JavaHomeExact -MajorVersion 21
    $null = Resolve-JavaHomeExact -MajorVersion 17
    Write-Host 'Full launcher Java preflight passed: JDK 21 + JDK 17 available.'

    & (Join-Path $root 'build.ps1') `
        -McVersion 26.3 `
        -FabricLoader 0.19.5 `
        -AssetIndex 34 `
        -AppxVersion $AppxVersion `
        -StrictTargets `
        -KeepStaging `
        -LocalSigning

    # The launcher slider reads the catalog staged into the APPX, not the source
    # file directly. Refuse to produce another accidental one-version package.
    $sourceCatalog = Join-Path $root 'config\versions.tsv'
    $stagedCatalog = Join-Path $root 'staging\package\runtime\version_catalog.tsv'
    if (-not (Test-Path -LiteralPath $stagedCatalog)) {
        throw "Full launcher build did not stage runtime\version_catalog.tsv"
    }
    $sourceRows = @(Import-Csv $sourceCatalog -Delimiter "`t")
    $stagedRows = @(Import-Csv $stagedCatalog -Delimiter "`t")
    if ($stagedRows.Count -ne $sourceRows.Count) {
        throw "Packaged version catalog is incomplete: staged=$($stagedRows.Count) source=$($sourceRows.Count)"
    }
    foreach ($requiredVersion in @('26.3', '1.21.11', '1.21.1', '1.21', '1.20.4', '1.20.1', '1.19.2', '1.18.2', '1.16.5')) {
        if (-not ($stagedRows | Where-Object { $_.minecraftVersion -eq $requiredVersion } | Select-Object -First 1)) {
            throw "Packaged version catalog is missing required launcher version $requiredVersion"
        }
    }
    Write-Host "Full launcher catalog verified: $($stagedRows.Count) targets staged into the APPX."
} finally {
    foreach ($name in $names) { [Environment]::SetEnvironmentVariable($name, $saved[$name], 'Process') }
}
