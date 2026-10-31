param(
    [string]$CleanroomVersion = "0.6.13-alpha",
    [Parameter(Mandatory = $true)][string]$OutputJar
)

$ErrorActionPreference = "Stop"

. (Join-Path (Split-Path (Split-Path $PSScriptRoot -Parent) -Parent) "scripts\common.ps1")

# no-op copies of cleanroom's dwm, taskbar and ntdll hooks, which load system dlls through jna and fail in the app container

$sourceRoot = $PSScriptRoot
$buildRoot = Join-Path (Get-ConfigPath "BuildDir") "cleanroom_patch\$CleanroomVersion"
$classesDir = Join-Path $buildRoot "classes"
$libraryDir = Join-Path (Get-ConfigPath "GameDir") "libraries"
$devDir = Join-Path (Get-ConfigPath "CacheDir") "cleanroom\$CleanroomVersion"

$javaHome = Resolve-JavaHome
$javac = Join-Path $javaHome "bin\javac.exe"
$jar = Join-Path $javaHome "bin\jar.exe"

Add-Type -AssemblyName System.IO.Compression.FileSystem

$installerJar = Join-Path (Get-LoaderInstallerCacheDir) "cleanroom-$CleanroomVersion-installer.jar"
Save-RemoteFile `
    -Uri "https://repo.cleanroommc.com/releases/com/cleanroommc/cleanroom/$CleanroomVersion/cleanroom-$CleanroomVersion-installer.jar" `
    -Path $installerJar

$zip = [System.IO.Compression.ZipFile]::OpenRead($installerJar)
try {
    $reader = [System.IO.StreamReader]::new($zip.GetEntry("version.json").Open())
    try { $versionJson = $reader.ReadToEnd() | ConvertFrom-Json } finally { $reader.Dispose() }
    Ensure-Dir $devDir
    $cleanroomJar = Join-Path $devDir "cleanroom-$CleanroomVersion.jar"
    $entry = $zip.GetEntry("maven/com/cleanroommc/cleanroom/$CleanroomVersion/cleanroom-$CleanroomVersion.jar")
    if (-not $entry) { throw "Cleanroom runtime jar missing from $installerJar" }
    [System.IO.Compression.ZipFileExtensions]::ExtractToFile($entry, $cleanroomJar, $true)
} finally {
    $zip.Dispose()
}

$crZip = [System.IO.Compression.ZipFile]::OpenRead($cleanroomJar)
try { $hasWindowsHooks = $null -ne $crZip.GetEntry("com/cleanroommc/client/windows/WindowsProperties.class") } finally { $crZip.Dispose() }
if (-not $hasWindowsHooks) {
    # a kept staging folder can still hold one from a newer cleanroom
    Remove-Item -LiteralPath $OutputJar -Force -ErrorAction SilentlyContinue
    Write-Host "Cleanroom $CleanroomVersion has no windows hooks, no UWP patch jar needed"
    return
}

$compileJars = @($cleanroomJar)
foreach ($lib in $versionJson.libraries) {
    if ($lib.name -notlike "net.java.dev.jna:*") { continue }
    $artifact = $lib.downloads.artifact
    $relative = if ($artifact.path) { $artifact.path } else { ([uri]$artifact.url).AbsolutePath -replace '^/(maven2/|releases/)?', '' }
    $path = Join-Path $libraryDir ($relative.Replace("/", "\"))
    Save-RemoteFile -Uri $artifact.url -Path $path -Sha1 $artifact.sha1
    $compileJars += $path
}

$release = Get-JarClassRelease $cleanroomJar
$sources = @(Get-ChildItem $sourceRoot -Recurse -Filter "*.java" | ForEach-Object { $_.FullName } | Sort-Object)
$stampPath = Join-Path $buildRoot "build.stamp"
$stamp = New-BuildStamp `
    -Values @("cleanroom_patch", $CleanroomVersion, "release=$release") `
    -ContentFiles (@($PSCommandPath) + $sources) `
    -ImmutableFiles @($installerJar)
$builtJar = Join-Path $buildRoot "cleanroom-uwp-patch.jar"

if (-not (Test-BuildStampCurrent -StampPath $stampPath -Stamp $stamp -RequiredOutputs @($builtJar))) {
    Remove-Item -Recurse -Force $buildRoot -ErrorAction SilentlyContinue
    Ensure-Dir $classesDir
    # sourcepath stays off so javac compiles these against cleanroom's other classes, not ours
    & $javac --release $release -proc:none -encoding UTF-8 -classpath ($compileJars -join ";") -d $classesDir $sources
    if ($LASTEXITCODE -ne 0) { throw "Cleanroom patch compile failed." }
    & $jar cf $builtJar -C $classesDir .
    if ($LASTEXITCODE -ne 0) { throw "Cleanroom patch jar failed." }
    Set-BuildStamp -StampPath $stampPath -Stamp $stamp
}

Ensure-Dir (Split-Path $OutputJar -Parent)
Copy-Item $builtJar $OutputJar -Force
Write-Host "Cleanroom UWP patch ($CleanroomVersion) -> $OutputJar"
