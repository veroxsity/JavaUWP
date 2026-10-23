param(
    [string]$MinecraftVersion
)

$ErrorActionPreference = "Stop"

if ($MinecraftVersion) { $env:MC_VERSION = $MinecraftVersion }
. (Join-Path $PSScriptRoot "common.ps1")

$root = Resolve-RepoRoot
$gameDir = Get-ConfigPath "GameDir"
$version = if ($MinecraftVersion) { $MinecraftVersion } else { $ProjectConfig.MinecraftVersion }

$manifest = Get-MinecraftVersionManifest
$v = $manifest.versions | Where-Object { $_.id -eq $version } | Select-Object -First 1
if (-not $v) {
    throw "Minecraft version $version not found in manifest."
}

$vj = Get-CachedRemoteJson -Uri $v.url

$versionDir = Join-Path $gameDir "versions\$version"
Ensure-Dir $versionDir

$clientUrl = $vj.downloads.client.url
$clientJar = Join-Path $versionDir "$version.jar"
Save-RemoteFile -Uri $clientUrl -Path $clientJar -Sha1 $vj.downloads.client.sha1 -Size $vj.downloads.client.size
Write-Host "Client jar done -> $clientJar"

$libs = $vj.libraries | Where-Object { $_.downloads.artifact -ne $null }
foreach ($lib in $libs) {
    $artifact = $lib.downloads.artifact
    $dest = Join-Path $gameDir ("libraries\" + $artifact.path.Replace('/', '\'))
    Ensure-Dir (Split-Path $dest)
    Save-RemoteFile -Uri $artifact.url -Path $dest -Sha1 $artifact.sha1 -Size $artifact.size
    Write-Host "Verified $($artifact.path)"
}

Write-Host "All done"
