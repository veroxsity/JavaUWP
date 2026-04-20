$ErrorActionPreference = "Stop"

function Resolve-JavaHome {
    if ($env:JAVA_HOME -and (Test-Path (Join-Path $env:JAVA_HOME "bin\javac.exe"))) {
        return $env:JAVA_HOME
    }

    $candidates = @()
    if (Test-Path "C:\ms-jdk21") {
        $candidates += Get-ChildItem "C:\ms-jdk21" -Directory -ErrorAction SilentlyContinue
    }
    $candidates += Get-ChildItem "C:\" -Directory -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -like "graalvm-community-openjdk-*" -or $_.Name -like "jdk-21*" }

    $match = $candidates |
        Where-Object { Test-Path (Join-Path $_.FullName "bin\javac.exe") } |
        Select-Object -First 1
    if ($match) {
        return $match.FullName
    }

    throw "No suitable Java installation found. Set JAVA_HOME to a JDK 21 install."
}

$root = Split-Path $PSScriptRoot -Parent
$srcJava = Join-Path $PSScriptRoot "src\main\java"
$srcResources = Join-Path $PSScriptRoot "src\main\resources"
$buildRoot = Join-Path $PSScriptRoot "build"
$classesDir = Join-Path $buildRoot "classes"
$jarPath = Join-Path $buildRoot "banditvault-xbox-compat-1.0.0.jar"
$modsDir = Join-Path $root "gameDir\mods"

$javaHome = Resolve-JavaHome
$javac = Join-Path $javaHome "bin\javac.exe"
$jar = Join-Path $javaHome "bin\jar.exe"
$mixinJar = Join-Path $root "gameDir\libraries\net\fabricmc\sponge-mixin\0.17.2+mixin.0.8.7\sponge-mixin-0.17.2+mixin.0.8.7.jar"
$clientJar = Join-Path $root "gameDir\.fabric\remappedJars\minecraft-1.21.11-0.19.2\client-intermediary.jar"

Remove-Item -Recurse -Force $buildRoot -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $classesDir | Out-Null
New-Item -ItemType Directory -Force -Path $modsDir | Out-Null

$sources = Get-ChildItem $srcJava -Recurse -Filter "*.java" | Select-Object -ExpandProperty FullName
if (-not $sources) { throw "No compatibility mod sources found" }

$cp = @($clientJar, $mixinJar) -join ";"
& $javac --release 21 -proc:none -cp $cp -d $classesDir $sources
if ($LASTEXITCODE -ne 0) { throw "compatibility mod compile failed" }

Copy-Item -Recurse "$srcResources\*" $classesDir -Force

Push-Location $classesDir
& $jar cf $jarPath .
if ($LASTEXITCODE -ne 0) {
    Pop-Location
    throw "compatibility mod jar failed"
}
Pop-Location

Copy-Item $jarPath (Join-Path $modsDir "banditvault-xbox-compat-1.0.0.jar") -Force
Write-Host "Compatibility mod built -> $jarPath"
