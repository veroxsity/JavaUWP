$root = $PSScriptRoot
$gameDir = Join-Path $root "gameDir"
$version = "1.21.11"

$manifest = Invoke-WebRequest -Uri 'https://piston-meta.mojang.com/mc/game/version_manifest_v2.json' | ConvertFrom-Json
$v = $manifest.versions | Where-Object { $_.id -eq $version } | Select-Object -First 1
if (-not $v) {
    throw "Minecraft version $version not found in manifest."
}

$vj = Invoke-WebRequest -Uri $v.url | ConvertFrom-Json

$versionDir = Join-Path $gameDir "versions\$version"
New-Item -ItemType Directory -Force -Path $versionDir | Out-Null

$clientUrl = $vj.downloads.client.url
$clientJar = Join-Path $versionDir "$version.jar"
Invoke-WebRequest -Uri $clientUrl -OutFile $clientJar
Write-Host "Client jar done -> $clientJar"

$libs = $vj.libraries | Where-Object { $_.downloads.artifact -ne $null }
foreach ($lib in $libs) {
    $artifact = $lib.downloads.artifact
    $dest = Join-Path $gameDir ("libraries\" + $artifact.path.Replace('/', '\'))
    New-Item -ItemType Directory -Force -Path (Split-Path $dest) | Out-Null
    if (-not (Test-Path $dest)) {
        Invoke-WebRequest -Uri $artifact.url -OutFile $dest
        Write-Host "Downloaded: $($artifact.path)"
    }
}

Write-Host "All done"
