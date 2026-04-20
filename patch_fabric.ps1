# patch_fabric.ps1 - Patch fabric-loader to fix Xbox toRealPath() issue
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

$root   = $PSScriptRoot
$java   = Resolve-JavaHome
$loader = Join-Path $root "gameDir\libraries\net\fabricmc\fabric-loader\0.19.2\fabric-loader-0.19.2.jar"
$patch  = Join-Path $root "patch"
$tmp    = Join-Path $patch "tmp"

New-Item -ItemType Directory -Force $tmp | Out-Null

# Compile the patched class against the original JAR
Write-Host "Compiling patched LoaderUtil..."
& (Join-Path $java "bin\javac.exe") -cp $loader -d $tmp (Join-Path $patch "LoaderUtil.java")
if ($LASTEXITCODE -ne 0) { throw "Compile failed" }

# Inject the patched class into the JAR
Write-Host "Patching JAR..."
$classFile = "net\fabricmc\loader\impl\util\LoaderUtil.class"
Push-Location $tmp
& (Join-Path $java "bin\jar.exe") uf $loader $classFile
if ($LASTEXITCODE -ne 0) { throw "JAR patch failed" }
Pop-Location

Write-Host "Done - fabric-loader-0.19.2.jar patched"
Write-Host "Class injected: $classFile"
