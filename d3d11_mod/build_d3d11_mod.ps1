param(
    [string]$OutputDir
)

$ErrorActionPreference = "Stop"

. (Join-Path (Split-Path $PSScriptRoot -Parent) "scripts\common.ps1")

$root = Resolve-RepoRoot
if (-not $OutputDir) {
    $OutputDir = Join-Path (Get-ConfigPath "BuildDir") "d3d11_mod\mods"
}
$OutputDir = (New-Item -ItemType Directory -Force -Path $OutputDir).FullName

$gradleCommand = $null
$gradleArgs = @()
$localWrapper = Join-Path $root ".local\VulkanMod\gradlew.bat"
$gradle = Get-Command gradle -ErrorAction SilentlyContinue
if ($gradle) {
    $gradleCommand = $gradle.Source
} elseif (Test-Path $localWrapper) {
    $gradleCommand = $localWrapper
    $gradleArgs += @("-p", $PSScriptRoot)
} else {
    throw "Gradle is required to build d3d11_mod. Install Gradle or keep .local\VulkanMod available for its Gradle wrapper."
}

Push-Location $PSScriptRoot
& $gradleCommand @gradleArgs --no-daemon build
if ($LASTEXITCODE -ne 0) {
    Pop-Location
    throw "d3d11_mod Gradle build failed"
}
Pop-Location

$jar = Get-ChildItem -LiteralPath (Join-Path $PSScriptRoot "build\libs") -Filter "*.jar" |
    Where-Object { $_.Name -notlike "*-sources.jar" -and $_.Name -notlike "*-dev.jar" } |
    Sort-Object LastWriteTime -Descending |
    Select-Object -First 1
if (-not $jar) {
    throw "d3d11_mod jar not found under build\libs"
}

Copy-Item $jar.FullName (Join-Path $OutputDir $jar.Name) -Force
Write-Host "D3D11 mod built -> $($jar.FullName)"
