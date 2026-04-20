# Download Minecraft 1.21.11 assets
$root = $PSScriptRoot
$assetsDir = Join-Path $root "assets"

New-Item -ItemType Directory -Force -Path "$assetsDir\indexes" | Out-Null
New-Item -ItemType Directory -Force -Path "$assetsDir\objects" | Out-Null

# Get version json to find asset index
$manifest = Invoke-WebRequest -Uri 'https://piston-meta.mojang.com/mc/game/version_manifest_v2.json' | ConvertFrom-Json
$v = $manifest.versions | Where-Object { $_.id -eq '1.21.11' }
$vj = Invoke-WebRequest -Uri $v.url | ConvertFrom-Json

# Download asset index
$assetIndexUrl = $vj.assetIndex.url
$assetIndexId = $vj.assetIndex.id
Write-Host "Downloading asset index: $assetIndexId"
Invoke-WebRequest -Uri $assetIndexUrl -OutFile "$assetsDir\indexes\$assetIndexId.json"

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
    New-Item -ItemType Directory -Force -Path $destDir | Out-Null
    if (-not (Test-Path $dest)) {
        $url = "https://resources.download.minecraft.net/$subdir/$hash"
        Invoke-WebRequest -Uri $url -OutFile $dest
    }
    $i++
    if ($i % 100 -eq 0) { Write-Host "$i / $total assets downloaded" }
}
Write-Host "All $total assets downloaded"
