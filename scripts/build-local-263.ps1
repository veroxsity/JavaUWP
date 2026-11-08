# Historical entry point kept for convenience. V16 intentionally builds the
# full launcher catalog so installing this package never produces a 26.3-only
# version slider again. Use build-regression-263.ps1 only for developer-only
# graphics regression cycles.
param([string]$AppxVersion = '1.0.0.17')
$ErrorActionPreference = 'Stop'
& (Join-Path $PSScriptRoot 'build-local-multiversion.ps1') -AppxVersion $AppxVersion
