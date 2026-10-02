param(
    [string]$MinecraftVersion = "1.20.1",
    [string]$ForgeVersion = "1.20.1-47.4.20",
    [string]$McpVersion = "20230612.114412"
)

$ErrorActionPreference = "Stop"

. (Join-Path $PSScriptRoot "common.ps1")

$root = Resolve-RepoRoot
$gameDir = Get-ConfigPath "GameDir"
$libraryDir = Join-Path $gameDir "libraries"
$javaHome = Resolve-JavaHome
$javaExe = Join-Path $javaHome "bin\java.exe"

if ($MinecraftVersion -ne "1.20.1") {
    $forgeCoordinate = if ($ForgeVersion.StartsWith("$MinecraftVersion-")) { $ForgeVersion } else { "$MinecraftVersion-$ForgeVersion" }
    $cacheRoot = Join-Path (Get-ConfigPath "StagingDir") "cache\forge\$forgeCoordinate"
    $installRoot = Get-LoaderInstallRoot "forge"
    $installerJar = Join-Path (Get-LoaderInstallerCacheDir) "forge-$forgeCoordinate-installer.jar"
    $installedClient = Join-Path $installRoot "libraries\net\minecraftforge\forge\$forgeCoordinate\forge-$forgeCoordinate-client.jar"
    $stagedClient = Join-Path $root "prebuilt\forge\libraries\net\minecraftforge\forge\$forgeCoordinate\forge-$forgeCoordinate-client.jar"
    if ((Test-Path $stagedClient) -and (Get-Item $stagedClient).Length -gt 1MB) {
        Write-Host "Using cached Forge patched client -> $stagedClient"
        return $stagedClient
    }

    Ensure-Dir $cacheRoot, $installRoot
    '{"profiles":{},"settings":{},"version":3}' | Set-Content (Join-Path $installRoot "launcher_profiles.json") -Encoding ascii
    if (-not (Test-Path $installerJar)) {
        $installerUrl = "https://maven.minecraftforge.net/net/minecraftforge/forge/$forgeCoordinate/forge-$forgeCoordinate-installer.jar"
        Write-Host "Downloading $installerUrl"
        Invoke-WebRequest -UseBasicParsing -Uri $installerUrl -OutFile $installerJar -TimeoutSec 180
    }

    $installerLog = Join-Path $cacheRoot "installer.log"
    & $javaExe -jar $installerJar --installClient $installRoot *> $installerLog
    if ($LASTEXITCODE -ne 0) {
        Get-Content $installerLog -Tail 30
        throw "Forge installer failed ($LASTEXITCODE)"
    }
    if (-not (Test-Path $installedClient)) { throw "Forge installer did not produce $installedClient" }
    Ensure-Dir (Split-Path $stagedClient -Parent)
    Copy-Item $installedClient $stagedClient -Force
    Write-Host "Forge patched client ready -> $stagedClient"
    return $stagedClient
}

$profilePath = Join-Path $root "config\forge-install-profile.json"
if (-not (Test-Path $profilePath)) {
    throw "Forge install profile missing at $profilePath"
}

$profile = Get-Content -Raw -Path $profilePath | ConvertFrom-Json
$mcAndMcp = "$MinecraftVersion-$McpVersion"

function Join-MavenPath {
    param(
        [string]$Group,
        [string]$Artifact,
        [string]$Version,
        [string]$Classifier = "",
        [string]$Extension = "jar"
    )
    $groupPath = $Group.Replace(".", "\")
    $suffix = if ($Classifier) { "-$Classifier" } else { "" }
    return "$groupPath\$Artifact\$Version\$Artifact-$Version$suffix.$Extension"
}

function Ensure-LibraryArtifact {
    param([Parameter(Mandatory = $true)]$Artifact)
    $dest = Join-Path $libraryDir ($Artifact.path.Replace("/", "\"))
    if (Test-Path $dest) { return $dest }
    Ensure-Dir (Split-Path $dest -Parent)
    Write-Host "Downloading $($Artifact.path)"
    Invoke-WebRequest -UseBasicParsing -Uri $Artifact.url -OutFile $dest
    return $dest
}

function Ensure-LibraryByName {
    param([Parameter(Mandatory = $true)][string]$Name)
    $entry = $profile.libraries | Where-Object { $_.name -eq $Name } | Select-Object -First 1
    if (-not $entry) { throw "Forge install profile library not found: $Name" }
    return Ensure-LibraryArtifact -Artifact $entry.downloads.artifact
}

function Invoke-ForgeTool {
    param(
        [Parameter(Mandatory = $true)][string]$MainClass,
        [Parameter(Mandatory = $true)][string[]]$Arguments,
        [Parameter(Mandatory = $true)][string[]]$ToolJars
    )
    $cp = ($ToolJars | Select-Object -Unique) -join ";"
    & $javaExe -cp $cp $MainClass @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "Forge tool failed: $MainClass ($LASTEXITCODE)"
    }
}

$clientJar = Join-Path $gameDir "versions\$MinecraftVersion\$MinecraftVersion.jar"
if (-not (Test-Path $clientJar)) {
    & (Join-Path $root "scripts\download-libs.ps1") -MinecraftVersion $MinecraftVersion
    if (-not (Test-Path $clientJar)) {
        throw "Minecraft client jar missing at $clientJar"
    }
}

$mcSlim = Join-Path $libraryDir (Join-MavenPath "net.minecraft" "client" $mcAndMcp "slim")
$mcExtra = Join-Path $libraryDir (Join-MavenPath "net.minecraft" "client" $mcAndMcp "extra")
$mcSrg = Join-Path $libraryDir (Join-MavenPath "net.minecraft" "client" $mcAndMcp "srg")
$patchedClient = Join-Path $libraryDir (Join-MavenPath "net.minecraftforge" "forge" $ForgeVersion "client")
$mcpConfigZip = Join-Path $libraryDir (Join-MavenPath "de.oceanlabs.mcp" "mcp_config" $mcAndMcp "" "zip")
$mappings = Join-Path $libraryDir (Join-MavenPath "de.oceanlabs.mcp" "mcp_config" $mcAndMcp "mappings" "txt")
$mojmaps = Join-Path $libraryDir (Join-MavenPath "net.minecraft" "client" $mcAndMcp "mappings" "txt")
$mergedMappings = Join-Path $libraryDir (Join-MavenPath "de.oceanlabs.mcp" "mcp_config" $mcAndMcp "mappings-merged" "txt")
$installerJar = Join-Path $libraryDir (Join-MavenPath "net.minecraftforge" "forge" $ForgeVersion "installer")
$binPatch = Join-Path $gameDir "forge\$ForgeVersion\client.lzma"

if ((Test-Path $patchedClient) -and (Get-Item $patchedClient).Length -gt 1MB) {
    Write-Host "Using cached Forge patched client -> $patchedClient"
    return $patchedClient
}

Ensure-LibraryByName "de.oceanlabs.mcp:mcp_config:$mcAndMcp@zip" | Out-Null
Ensure-LibraryByName "net.minecraftforge:installertools:1.4.1" | Out-Null
Ensure-LibraryByName "net.minecraftforge:jarsplitter:1.1.4" | Out-Null
Ensure-LibraryByName "net.minecraftforge:ForgeAutoRenamingTool:0.1.22:all" | Out-Null
Ensure-LibraryByName "net.minecraftforge:binarypatcher:1.1.1" | Out-Null
Ensure-LibraryByName "net.minecraftforge:forge:${ForgeVersion}:universal" | Out-Null
# 1.20.1 never runs the installer, it only needs client.lzma out of it
if (-not (Test-Path $installerJar)) {
    $installerUrl = "https://maven.minecraftforge.net/net/minecraftforge/forge/$ForgeVersion/forge-$ForgeVersion-installer.jar"
    Ensure-Dir (Split-Path $installerJar)
    Write-Host "Downloading $installerUrl"
    Invoke-WebRequest -UseBasicParsing -Uri $installerUrl -OutFile $installerJar -TimeoutSec 180
}

if (-not (Test-Path $binPatch)) {
    Ensure-Dir (Split-Path $binPatch)
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $zip = [System.IO.Compression.ZipFile]::OpenRead($installerJar)
    try {
        $entry = $zip.GetEntry("data/client.lzma")
        if (-not $entry) { throw "Forge installer is missing data/client.lzma" }
        [System.IO.Compression.ZipFileExtensions]::ExtractToFile($entry, $binPatch, $true)
    } finally {
        $zip.Dispose()
    }
}

$installToolsCp = @(
    Ensure-LibraryByName "net.minecraftforge:installertools:1.4.1"
    Ensure-LibraryByName "net.sf.jopt-simple:jopt-simple:6.0-alpha-3"
    Ensure-LibraryByName "com.google.code.gson:gson:2.10.1"
    Ensure-LibraryByName "de.siegmar:fastcsv:2.2.2"
    Ensure-LibraryByName "net.minecraftforge:srgutils:0.5.6"
    Ensure-LibraryByName "org.ow2.asm:asm-commons:9.6"
    Ensure-LibraryByName "org.ow2.asm:asm-tree:9.6"
    Ensure-LibraryByName "org.ow2.asm:asm:9.6"
)
$splitterCp = @(
    Ensure-LibraryByName "net.minecraftforge:jarsplitter:1.1.4"
    Ensure-LibraryByName "net.minecraftforge:srgutils:0.4.3"
    Ensure-LibraryByName "net.sf.jopt-simple:jopt-simple:5.0.4"
)
$artCp = @(
    Ensure-LibraryByName "net.minecraftforge:ForgeAutoRenamingTool:0.1.22:all"
    Ensure-LibraryByName "net.minecraftforge:srgutils:0.4.9"
)
$patchCp = @(
    Ensure-LibraryByName "net.minecraftforge:binarypatcher:1.1.1"
    Ensure-LibraryByName "com.github.jponge:lzma-java:1.3"
    Ensure-LibraryByName "com.nothome:javaxdelta:2.0.1"
    Ensure-LibraryByName "trove:trove:1.0.2"
    Ensure-LibraryByName "net.sf.jopt-simple:jopt-simple:5.0.4"
    Ensure-LibraryByName "commons-io:commons-io:2.4"
)

if (-not (Test-Path $mappings)) {
    Invoke-ForgeTool "net.minecraftforge.installertools.ConsoleTool" @(
        "--task", "MCP_DATA",
        "--input", $mcpConfigZip,
        "--output", $mappings,
        "--key", "mappings"
    ) $installToolsCp
}
if (-not (Test-Path $mojmaps)) {
    Invoke-ForgeTool "net.minecraftforge.installertools.ConsoleTool" @(
        "--task", "DOWNLOAD_MOJMAPS",
        "--version", $MinecraftVersion,
        "--side", "client",
        "--output", $mojmaps
    ) $installToolsCp
}
if (-not (Test-Path $mergedMappings)) {
    Invoke-ForgeTool "net.minecraftforge.installertools.ConsoleTool" @(
        "--task", "MERGE_MAPPING",
        "--left", $mappings,
        "--right", $mojmaps,
        "--output", $mergedMappings,
        "--classes", "--reverse-right"
    ) $installToolsCp
}

Invoke-ForgeTool "net.minecraftforge.jarsplitter.ConsoleTool" @(
    "--input", $clientJar,
    "--slim", $mcSlim,
    "--extra", $mcExtra,
    "--srg", $mergedMappings
) $splitterCp

$mcSrgTmp = "$mcSrg.tmp"
if (-not (Test-Path $mcSrg)) {
    Invoke-ForgeTool "net.minecraftforge.fart.Main" @(
        "--input", $mcSlim,
        "--output", $mcSrgTmp,
        "--names", $mergedMappings,
        "--ann-fix", "--ids-fix", "--src-fix", "--record-fix"
    ) $artCp
    Move-Item -Force $mcSrgTmp $mcSrg
}

$patchedTmp = "$patchedClient.tmp"
Invoke-ForgeTool "net.minecraftforge.binarypatcher.ConsoleTool" @(
    "--clean", $mcSrg,
    "--output", $patchedTmp,
    "--apply", $binPatch
) $patchCp
Move-Item -Force $patchedTmp $patchedClient

Write-Host "Forge patched client ready -> $patchedClient"
return $patchedClient
