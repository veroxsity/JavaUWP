# Developer-only 26.3 regression package. This deliberately stages a one-row
# catalog and should not be used for a normal launcher installation.
param([string]$AppxVersion = '1.0.0.15')
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$names = @('DEFAULT_MC_VERSION', 'VERSION_CATALOG')
$saved = @{}
foreach ($name in $names) { $saved[$name] = [Environment]::GetEnvironmentVariable($name, 'Process') }
try {
    $env:DEFAULT_MC_VERSION = '26.3'
    $env:VERSION_CATALOG = 'config/versions-local-263.tsv'
    & (Join-Path $root 'build.ps1') -McVersion 26.3 -FabricLoader 0.19.5 -AssetIndex 34 -AppxVersion $AppxVersion -StrictTargets -KeepStaging -LocalSigning
} finally {
    foreach ($name in $names) { [Environment]::SetEnvironmentVariable($name, $saved[$name], 'Process') }
}
