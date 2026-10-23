param(
    [string]$NeoForgeVersion = "21.1.233",
    [string]$McVersion = "1.21.1",
    [string]$NeoFormVersion = "20240808.144430",
    [string]$JavaExe
)

$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
. (Join-Path $PSScriptRoot "common.ps1")

if (-not $JavaExe) {
    $JavaExe = Join-Path (Resolve-JavaHomeForMinecraft -MinecraftVersion $McVersion) "bin\java.exe"
}
if (-not (Test-Path $JavaExe)) {
    throw "java.exe not found at $JavaExe"
}

$mcAndNeoForm = "$McVersion-$NeoFormVersion"
$target = Get-LoaderInstallRoot "neoforge"
$installer = Join-Path (Get-LoaderInstallerCacheDir) "neoforge-$NeoForgeVersion-installer.jar"
$url = "https://maven.neoforged.net/releases/net/neoforged/neoforge/$NeoForgeVersion/neoforge-$NeoForgeVersion-installer.jar"

Ensure-Dir $target
'{"profiles":{},"settings":{},"version":3}' | Set-Content (Join-Path $target "launcher_profiles.json") -Encoding ascii

Save-RemoteFile -Uri $url -Path $installer

Write-Host "Running NeoForge installer (downloads vanilla + runs processors)"
& $JavaExe -jar $installer --install-client $target
if ($LASTEXITCODE -ne 0) { throw "installer failed ($LASTEXITCODE)" }

$lib = Join-Path $target "libraries"
$srg = "net\minecraft\client\$mcAndNeoForm\client-$mcAndNeoForm-srg.jar"
$extra = "net\minecraft\client\$mcAndNeoForm\client-$mcAndNeoForm-extra.jar"
$patched = "net\neoforged\neoforge\$NeoForgeVersion\neoforge-$NeoForgeVersion-client.jar"
$newPatched = "net\neoforged\minecraft-client-patched\$NeoForgeVersion\minecraft-client-patched-$NeoForgeVersion.jar"
$dstRoot = Join-Path $root "prebuilt\neoforge\libraries"
$modernArtifacts = [version]$McVersion -ge [version]"1.21.10"
$requiredClientEntry = if ($modernArtifacts) { "net/minecraft/client/Minecraft.class" } else { "" }
$artifacts = if ($modernArtifacts) { @($newPatched) } else { @($srg, $extra, $patched) }
foreach ($rel in $artifacts) {
    $src = Join-Path $lib $rel
    if (-not (Test-JarComplete -Path $src -RequiredEntry $requiredClientEntry)) { throw "installer did not produce a valid $rel" }
    $dst = Join-Path $dstRoot $rel
    Ensure-Dir (Split-Path $dst -Parent)
    Copy-Item $src $dst -Force
    Write-Host ("staged {0}  ({1:N0} bytes)" -f $rel, (Get-Item $dst).Length)
}

Write-Host "done. prebuilt jars updated under prebuilt\neoforge\libraries"
