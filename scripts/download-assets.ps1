param(
    [string]$MinecraftVersion
)

# Download Minecraft assets for the configured game version.
$ErrorActionPreference = "Stop"

if ($MinecraftVersion) { $env:MC_VERSION = $MinecraftVersion }
. (Join-Path $PSScriptRoot "common.ps1")

$root = Resolve-RepoRoot
$version = if ($MinecraftVersion) { $MinecraftVersion } else { $ProjectConfig.MinecraftVersion }
$assetsDir = Get-ConfigPath "AssetsDir"

Ensure-Dir "$assetsDir\indexes"
Ensure-Dir "$assetsDir\objects"

# Get version json to find asset index
$manifest = Get-MinecraftVersionManifest
$v = $manifest.versions | Where-Object { $_.id -eq $version } | Select-Object -First 1
if (-not $v) {
    throw "Minecraft version $version not found in manifest."
}
$vj = Get-CachedRemoteJson -Uri $v.url

# Download asset index
$assetIndexUrl = $vj.assetIndex.url
$assetIndexId = $vj.assetIndex.id
Write-Host "Downloading asset index: $assetIndexId"
Save-RemoteFile -Uri $assetIndexUrl -Path "$assetsDir\indexes\$assetIndexId.json" -Sha1 $vj.assetIndex.sha1 -Size $vj.assetIndex.size

# Download all assets
$index = Get-Content "$assetsDir\indexes\$assetIndexId.json" | ConvertFrom-Json
$objects = $index.objects.PSObject.Properties
$total = ($objects | Measure-Object).Count
$i = 0

foreach ($obj in $objects) {
    $hash = $obj.Value.hash
    $subdir = $hash.Substring(0, 2)
    $destDir = "$assetsDir\objects\$subdir"
    $dest = "$destDir\$hash"
    Ensure-Dir $destDir
    $url = "https://resources.download.minecraft.net/$subdir/$hash"
    Save-RemoteFile -Uri $url -Path $dest -Sha1 $hash -Size $obj.Value.size
    $i++
    if ($i % 100 -eq 0) { Write-Host "$i / $total assets downloaded" }
}
Write-Host "All $total assets downloaded"
