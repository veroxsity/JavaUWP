$ErrorActionPreference = "Stop"

. (Join-Path $PSScriptRoot "config.ps1")

# /external:W0 needs /external:anglebrackets or the sdk headers drown out ours at /W4
$CommonClFlags = @(
    "/W4",
    "/external:anglebrackets",
    "/external:W0"
)

function Resolve-RepoRoot {
    return (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
}

$script:JavaHomeMajorCache = @{}

function Get-SmallCacheValue {
    param(
        [Parameter(Mandatory = $true)][string]$FileName,
        [Parameter(Mandatory = $true)][string]$Key
    )

    $path = Join-Path (Get-ConfigPath "MetadataCacheDir") $FileName
    if (-not (Test-Path $path)) { return $null }
    foreach ($line in [System.IO.File]::ReadAllLines($path)) {
        $parts = $line -split "`t"
        if ($parts.Count -eq 2 -and $parts[0] -eq $Key) { return $parts[1] }
    }
    return $null
}

function Set-SmallCacheValue {
    param(
        [Parameter(Mandatory = $true)][string]$FileName,
        [Parameter(Mandatory = $true)][string]$Key,
        [Parameter(Mandatory = $true)][string]$Value
    )

    $dir = Get-ConfigPath "MetadataCacheDir"
    Ensure-Dir $dir
    [System.IO.File]::AppendAllText((Join-Path $dir $FileName), "$Key`t$Value`r`n")
}

function Get-JavaHomeMajorVersion {
    param(
        [Parameter(Mandatory = $true)]
        [string]$JavaHome
    )

    $javaExe = Join-Path $JavaHome "bin\java.exe"
    $javacExe = Join-Path $JavaHome "bin\javac.exe"
    if (-not (Test-Path $javaExe) -or -not (Test-Path $javacExe)) {
        return $null
    }

    # every build script resolves a java home and the per-version mod loop runs about a hundred of
    # them, so the answer is cached on disk, keyed on java.exe so a jdk upgrade in place still busts it
    $item = Get-Item -LiteralPath $javaExe
    $cacheKey = "$JavaHome|$($item.Length)|$($item.LastWriteTimeUtc.Ticks)"
    if ($script:JavaHomeMajorCache.ContainsKey($cacheKey)) {
        return $script:JavaHomeMajorCache[$cacheKey]
    }
    $cached = Get-SmallCacheValue -FileName "java-majors.tsv" -Key $cacheKey
    if ($cached) {
        $script:JavaHomeMajorCache[$cacheKey] = [int]$cached
        return [int]$cached
    }

    # java -version writes to stderr, and $ErrorActionPreference = 'Stop' in
    # the parent scope turns any captured ErrorRecord into a terminating throw.
    # PowerShell 7's $PSNativeCommandUseErrorActionPreference doesn't exist on
    # 5.1, so relax the error pref itself for just this native call.
    $versionOutput = $null
    try {
        $prevPref = $ErrorActionPreference
        $ErrorActionPreference = 'Continue'
        $versionOutput = (& $javaExe -version 2>&1 | Select-Object -First 1).ToString()
    } catch {
        return $null
    } finally {
        $ErrorActionPreference = $prevPref
    }
    if ($versionOutput -match '"(?<major>\d+)(?:\.(?<minor>\d+))?') {
        $major = [int]$Matches.major
        if ($major -eq 1 -and $Matches.minor) {
            $major = [int]$Matches.minor
        }
        $script:JavaHomeMajorCache[$cacheKey] = $major
        Set-SmallCacheValue -FileName "java-majors.tsv" -Key $cacheKey -Value $major
        return $major
    }

    return $null
}

function Test-JavaHomeMinimumVersion {
    param(
        [Parameter(Mandatory = $true)]
        [string]$JavaHome,

        [Parameter(Mandatory = $true)]
        [int]$MajorVersion
    )

    $major = Get-JavaHomeMajorVersion -JavaHome $JavaHome
    if ($null -eq $major) {
        return $false
    }
    return ($major -ge $MajorVersion)
}

function Test-JavaHomeExactVersion {
    param(
        [Parameter(Mandatory = $true)]
        [string]$JavaHome,

        [Parameter(Mandatory = $true)]
        [int]$MajorVersion
    )

    $major = Get-JavaHomeMajorVersion -JavaHome $JavaHome
    if ($null -eq $major) {
        return $false
    }
    return ($major -eq $MajorVersion)
}

function Get-JavaHomeCandidates {
    param(
        [Parameter(Mandatory = $true)]
        [int]$MajorVersion
    )

    $candidates = @()
    foreach ($envName in @("JAVA${MajorVersion}_HOME", "JDK${MajorVersion}_HOME", "JAVA_HOME_${MajorVersion}_X64")) {
        $value = [Environment]::GetEnvironmentVariable($envName)
        if ($value) {
            $candidates += Get-Item $value -ErrorAction SilentlyContinue
        }
    }

    $directRoots = @(
        "$env:SystemDrive\ms-jdk$MajorVersion",
        "$env:SystemDrive\Program Files\Java",
        "$env:SystemDrive\Program Files\Eclipse Adoptium",
        "$env:SystemDrive\Program Files\Amazon Corretto",
        "$env:SystemDrive\Program Files\Microsoft",
        "$env:SystemDrive\"
    )

    foreach ($root in $directRoots | Select-Object -Unique) {
        if (-not (Test-Path $root)) { continue }

        if (Test-Path (Join-Path $root "bin\javac.exe")) {
            $candidates += Get-Item $root
            continue
        }

        $candidates += Get-ChildItem $root -Directory -ErrorAction SilentlyContinue |
            Where-Object {
                $_.Name -like "graalvm-community-openjdk-*" -or
                $_.Name -like "jdk-$MajorVersion*" -or
                $_.Name -like "jdk$MajorVersion*" -or
                $_.Name -like "msopenjdk-$MajorVersion*" -or
                $_.Name -like "microsoft-jdk-$MajorVersion*"
            }
    }

    return @($candidates | Where-Object { $_ } | Select-Object -Unique)
}

function Resolve-JavaHomeExact {
    param(
        [Parameter(Mandatory = $true)]
        [int]$MajorVersion
    )

    $candidates = @()
    if ($env:JAVA_HOME) {
        $candidates += Get-Item $env:JAVA_HOME -ErrorAction SilentlyContinue
    }
    $candidates += Get-JavaHomeCandidates -MajorVersion $MajorVersion

    $match = $candidates |
        Where-Object { Test-JavaHomeExactVersion -JavaHome $_.FullName -MajorVersion $MajorVersion } |
        Select-Object -First 1

    if ($match) {
        return $match.FullName
    }

    throw "No exact Java $MajorVersion installation found. Set JAVA${MajorVersion}_HOME or JDK${MajorVersion}_HOME to a JDK $MajorVersion install."
}

function Resolve-JavaHome {
    param(
        [int]$MajorVersion = $ProjectConfig.JavaRelease
    )

    if ($env:JAVA_HOME) {
        if (Test-JavaHomeMinimumVersion -JavaHome $env:JAVA_HOME -MajorVersion $MajorVersion) {
            return $env:JAVA_HOME
        }

        Write-Warning "JAVA_HOME is set but is older than JDK ${MajorVersion}: $env:JAVA_HOME"
    }

    $match = Get-JavaHomeCandidates -MajorVersion $MajorVersion |
        Where-Object { Test-JavaHomeMinimumVersion -JavaHome $_.FullName -MajorVersion $MajorVersion } |
        Select-Object -First 1

    if ($match) {
        return $match.FullName
    }

    throw "No suitable Java installation found. Set JAVA_HOME to a JDK $MajorVersion or newer install."
}

$script:MinecraftJavaMajorCache = @{}
$script:RemoteJsonCache = @{}

# only version_manifest_v2 is mutable, the rest is content addressed, hence the ttl
$script:MutableMetadataUrls = @(
    "https://piston-meta.mojang.com/mc/game/version_manifest_v2.json"
)
$script:MutableMetadataTtlHours = 24

function Get-CachedRemoteJson {
    param(
        [Parameter(Mandatory = $true)][string]$Uri,
        [int]$TimeoutSec = 60
    )

    if ($script:RemoteJsonCache.ContainsKey($Uri)) {
        return $script:RemoteJsonCache[$Uri]
    }

    $cacheDir = Get-ConfigPath "MetadataCacheDir"
    $sha = [System.Security.Cryptography.SHA256]::Create()
    try {
        $hash = [System.BitConverter]::ToString($sha.ComputeHash([System.Text.Encoding]::UTF8.GetBytes($Uri))).Replace("-", "")
    } finally {
        $sha.Dispose()
    }
    $cacheFile = Join-Path $cacheDir ($hash.Substring(0, 32) + ".json")

    $mutable = $script:MutableMetadataUrls -contains $Uri
    if (Test-Path $cacheFile) {
        $stale = $false
        if ($mutable) {
            $age = (Get-Date) - (Get-Item $cacheFile).LastWriteTime
            $stale = $age.TotalHours -ge $script:MutableMetadataTtlHours
        }
        if (-not $stale) {
            try {
                $cached = [System.IO.File]::ReadAllText($cacheFile) | ConvertFrom-Json
                $script:RemoteJsonCache[$Uri] = $cached
                return $cached
            } catch {
                Remove-Item -Force $cacheFile -ErrorAction SilentlyContinue
            }
        }
    }

    Write-Host "Fetch $Uri"
    $raw = Invoke-WebRequest -UseBasicParsing -TimeoutSec $TimeoutSec -Uri $Uri
    $text = if ($raw.Content -is [byte[]]) { [System.Text.Encoding]::UTF8.GetString($raw.Content) } else { [string]$raw.Content }
    $parsed = $text | ConvertFrom-Json

    Ensure-Dir $cacheDir
    [System.IO.File]::WriteAllText($cacheFile, $text, (New-Object System.Text.UTF8Encoding($false)))
    $script:RemoteJsonCache[$Uri] = $parsed
    return $parsed
}

function Get-MinecraftVersionManifest {
    return Get-CachedRemoteJson -Uri "https://piston-meta.mojang.com/mc/game/version_manifest_v2.json"
}

# one copy of every loader installer jar, shared by the manifest generator and the patched client scripts
function Get-LoaderInstallerCacheDir {
    $dir = Join-Path (Get-ConfigPath "CacheDir") "loader-installers"
    Ensure-Dir $dir
    return $dir
}

# every forge and neoforge target installs into one launcher-shaped tree so the vanilla libraries download once
function Get-LoaderInstallRoot([string]$Loader) {
    $dir = Join-Path (Get-ConfigPath "CacheDir") "$Loader\install"
    Ensure-Dir $dir
    return $dir
}

function New-BuildStamp {
    param(
        [string[]]$Values = @(),
        # content hashed so a checkout rewriting mtimes does not force a rebuild
        [string[]]$ContentFiles = @(),
        # build.ps1 repatches the loader jars every run, so dating them would bust every stamp
        [string[]]$DependencyFiles = @(),
        # sized only, the client jar alone is 40 MB per target
        [string[]]$ImmutableFiles = @()
    )

    $lines = New-Object System.Collections.Generic.List[string]
    foreach ($v in $Values) { $lines.Add("v|$v") }

    $sha = [System.Security.Cryptography.SHA256]::Create()
    try {
        foreach ($f in @($ContentFiles | Sort-Object -Unique)) {
            if (-not (Test-Path -LiteralPath $f -PathType Leaf)) { $lines.Add("c|$f|missing"); continue }
            $bytes = [System.IO.File]::ReadAllBytes($f)
            $hash = [System.BitConverter]::ToString($sha.ComputeHash($bytes)).Replace("-", "")
            $lines.Add("c|$f|$hash")
        }

        foreach ($f in @($DependencyFiles | Sort-Object -Unique)) {
            if (-not (Test-Path -LiteralPath $f -PathType Leaf)) { $lines.Add("d|$f|missing"); continue }
            $item = Get-Item -LiteralPath $f
            $lines.Add("d|$f|$($item.Length)|$($item.LastWriteTimeUtc.Ticks)")
        }

        foreach ($f in @($ImmutableFiles | Sort-Object -Unique)) {
            if (-not (Test-Path -LiteralPath $f -PathType Leaf)) { $lines.Add("i|$f|missing"); continue }
            $lines.Add("i|$f|$((Get-Item -LiteralPath $f).Length)")
        }

        $joined = [string]::Join("`n", $lines)
        return [System.BitConverter]::ToString($sha.ComputeHash([System.Text.Encoding]::UTF8.GetBytes($joined))).Replace("-", "")
    } finally {
        $sha.Dispose()
    }
}

function Test-BuildStampCurrent {
    param(
        [Parameter(Mandatory = $true)][string]$StampPath,
        [Parameter(Mandatory = $true)][string]$Stamp,
        [string[]]$RequiredOutputs = @()
    )

    if ($env:BANDIT_FORCE_REBUILD) { return $false }
    if (-not (Test-Path -LiteralPath $StampPath -PathType Leaf)) { return $false }
    foreach ($o in $RequiredOutputs) {
        if (-not (Test-Path -LiteralPath $o -PathType Leaf)) { return $false }
    }

    try {
        return ([System.IO.File]::ReadAllText($StampPath).Trim() -eq $Stamp)
    } catch {
        return $false
    }
}

function Set-BuildStamp {
    param(
        [Parameter(Mandatory = $true)][string]$StampPath,
        [Parameter(Mandatory = $true)][string]$Stamp
    )

    Ensure-Dir (Split-Path $StampPath -Parent)
    [System.IO.File]::WriteAllText($StampPath, $Stamp, (New-Object System.Text.UTF8Encoding($false)))
}

function Get-MinecraftJavaMajorVersion {
    param(
        [Parameter(Mandatory = $true)]
        [string]$MinecraftVersion
    )

    if ($script:MinecraftJavaMajorCache.ContainsKey($MinecraftVersion)) {
        return $script:MinecraftJavaMajorCache[$MinecraftVersion]
    }

    # parsing the 570 KB version manifest per script invocation costs more than the answer is worth,
    # and a released version never changes the java it wants
    $cached = Get-SmallCacheValue -FileName "minecraft-java-majors.tsv" -Key $MinecraftVersion
    if ($cached) {
        $script:MinecraftJavaMajorCache[$MinecraftVersion] = [int]$cached
        return [int]$cached
    }

    $major = [int]$ProjectConfig.JavaRelease
    $resolved = $false
    try {
        $manifest = Get-MinecraftVersionManifest
        $entry = $manifest.versions | Where-Object { $_.id -eq $MinecraftVersion } | Select-Object -First 1
        if ($entry) {
            $versionJson = Get-CachedRemoteJson -Uri $entry.url
            if ($versionJson.javaVersion -and $versionJson.javaVersion.majorVersion) {
                $major = [int]$versionJson.javaVersion.majorVersion
                $resolved = $true
            }
        }
    } catch {
        Write-Warning "Could not read the required Java version for Minecraft ${MinecraftVersion}: $($_.Exception.Message)"
    }

    $script:MinecraftJavaMajorCache[$MinecraftVersion] = $major
    if ($resolved) {
        Set-SmallCacheValue -FileName "minecraft-java-majors.tsv" -Key $MinecraftVersion -Value $major
    }
    return $major
}

function Resolve-JavaHomeForMinecraft {
    param(
        [Parameter(Mandatory = $true)]
        [string]$MinecraftVersion
    )

    # javac and the remap launch both read the client jar, so the JDK has to be new enough
    # to load its class files. 26.2 is class file 69, which JDK 21 refuses.
    $required = [Math]::Max(
        (Get-MinecraftJavaMajorVersion -MinecraftVersion $MinecraftVersion),
        [int]$ProjectConfig.JavaRelease)
    return Resolve-JavaHome -MajorVersion $required
}

function Resolve-SpongeMixinJar {
    param(
        [Parameter(Mandatory = $true)]
        [string]$GameDir,

        [string]$MinecraftVersion,

        [string]$LoaderVersion
    )

    if ($MinecraftVersion -and $LoaderVersion) {
        $profilePath = Join-Path $GameDir "versions\fabric-loader-$LoaderVersion-$MinecraftVersion\fabric-loader-$LoaderVersion-$MinecraftVersion.json"
        if (Test-Path $profilePath) {
            $profileJson = Get-Content -Raw -Path $profilePath | ConvertFrom-Json
            foreach ($library in $profileJson.libraries) {
                $name = [string]$library.name
                if ($name -notlike "net.fabricmc:sponge-mixin:*") { continue }
                $mixinVersion = $name.Split(":")[2]
                $candidate = Join-Path $GameDir "libraries\net\fabricmc\sponge-mixin\$mixinVersion\sponge-mixin-$mixinVersion.jar"
                if (Test-Path $candidate) { return $candidate }
            }
        }
    }

    $configured = Join-Path $GameDir "libraries\net\fabricmc\sponge-mixin\$($ProjectConfig.MixinVersion)\sponge-mixin-$($ProjectConfig.MixinVersion).jar"
    if (Test-Path $configured) { return $configured }

    $newest = Get-ChildItem -LiteralPath (Join-Path $GameDir "libraries\net\fabricmc\sponge-mixin") `
        -Recurse -Filter "sponge-mixin-*.jar" -ErrorAction SilentlyContinue |
        Sort-Object Name -Descending |
        Select-Object -First 1
    if ($newest) { return $newest.FullName }

    throw "sponge-mixin jar not found under $GameDir. Run scripts\setup.ps1 for this target first."
}

function Test-FabricTargetHasIntermediary {
    param(
        [Parameter(Mandatory = $true)][string]$GameDir,
        [Parameter(Mandatory = $true)][string]$MinecraftVersion,
        [Parameter(Mandatory = $true)][string]$LoaderVersion
    )

    $profilePath = Join-Path $GameDir "versions\fabric-loader-$LoaderVersion-$MinecraftVersion\fabric-loader-$LoaderVersion-$MinecraftVersion.json"
    if (-not (Test-Path $profilePath)) { return $true }

    $profileJson = Get-Content -Raw -Path $profilePath | ConvertFrom-Json
    foreach ($library in $profileJson.libraries) {
        if ([string]$library.name -like "net.fabricmc:intermediary:*") { return $true }
    }

    return $false
}

function Resolve-FabricClientJar {
    param(
        [Parameter(Mandatory = $true)][string]$GameDir,
        [Parameter(Mandatory = $true)][string]$MinecraftVersion,
        [Parameter(Mandatory = $true)][string]$LoaderVersion
    )

    # 26.x ships unobfuscated, so Fabric has no intermediary for it and never writes a
    # remapped jar. Mods for those targets compile straight against the client jar.
    $remapped = Join-Path $GameDir ".fabric\remappedJars\minecraft-$MinecraftVersion-$LoaderVersion\client-intermediary.jar"
    if (Test-Path $remapped) { return $remapped }

    return (Join-Path $GameDir "versions\$MinecraftVersion\$MinecraftVersion.jar")
}

function Resolve-VSTools {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path $vswhere)) {
        throw "vswhere.exe not found. Install Visual Studio Build Tools or Visual Studio with C++ tools."
    }

    $installPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $installPath) {
        throw "Visual Studio with C++ tools not found."
    }

    $msvcRoot = Get-ChildItem (Join-Path $installPath "VC\Tools\MSVC") -Directory |
        Sort-Object Name -Descending |
        Select-Object -First 1 -ExpandProperty FullName
    if (-not $msvcRoot) {
        throw "MSVC tools directory not found."
    }

    $clExe = Join-Path $msvcRoot "bin\Hostx64\x64\cl.exe"
    if (-not (Test-Path $clExe)) {
        throw "cl.exe not found at $clExe"
    }

    return @{
        MsvcRoot = $msvcRoot
        ClExe    = $clExe
    }
}

function Resolve-WindowsSdk {
    $sdkRoot = (Get-ItemProperty "HKLM:\SOFTWARE\Microsoft\Windows Kits\Installed Roots").KitsRoot10
    if (-not $sdkRoot) {
        throw "Windows 10/11 SDK not found."
    }

    $sdkVer = Get-ChildItem (Join-Path $sdkRoot "Include") -Directory |
        Sort-Object Name |
        Select-Object -Last 1 -ExpandProperty Name
    if (-not $sdkVer) {
        throw "Windows SDK include directory not found under $sdkRoot."
    }

    return @{
        Root    = $sdkRoot
        Version = $sdkVer
    }
}

function Get-MesaRuntimeDllNames {
    return @(
        "opengl32.dll",
        "libgallium_wgl.dll",
        "spirv_to_dxil.dll",
        "vulkan_dzn.dll",
        "dxil.dll",
        "z-1.dll"
    )
}

function Test-MesaRuntimeDir {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path
    )

    if (-not (Test-Path $Path)) {
        return $false
    }

    $required = @("opengl32.dll", "libgallium_wgl.dll", "dxil.dll", "spirv_to_dxil.dll", "z-1.dll")
    foreach ($dll in $required) {
        if (-not (Test-Path (Join-Path $Path $dll))) {
            return $false
        }
    }

    return $true
}

function Resolve-MesaRuntimeDir {
    param(
        [string]$MesaRuntimeDir
    )

    $candidates = @()

    if ($MesaRuntimeDir) {
        $candidates += $MesaRuntimeDir
    }
    if ($env:MESA_UWP_DIR) {
        $candidates += $env:MESA_UWP_DIR
    }

    $localMesaDir = Get-ConfigPath "MesaRuntimeDir"
    if (Test-Path $localMesaDir) {
        $candidates += $localMesaDir
    }

    $cachedMesaDir = Join-Path (Resolve-RepoRoot) "staging\cache\mesa-runtime"
    if (Test-Path $cachedMesaDir) {
        $candidates += $cachedMesaDir
    }

    # Backward-compatible convenience for users who source Mesa DLLs from
    # RetroArch UWP. RetroArch is not required by the project.
    if ($env:RETROARCH_UWP_DIR) {
        $candidates += $env:RETROARCH_UWP_DIR
    }

    $searchRoots = @()
    foreach ($drive in Get-PSDrive -PSProvider FileSystem -ErrorAction SilentlyContinue) {
        if ($drive.Name.Length -ne 1) { continue }
        $searchRoots += "$($drive.Name):\WindowsApps"
        $searchRoots += "$($drive.Name):\Program Files\WindowsApps"
    }

    foreach ($root in $searchRoots) {
        if (-not (Test-Path $root)) { continue }

        $candidates += Get-ChildItem $root -Directory -ErrorAction SilentlyContinue |
            Select-Object -ExpandProperty FullName
    }

    foreach ($candidate in $candidates | Where-Object { $_ } | Select-Object -Unique) {
        if (Test-MesaRuntimeDir -Path $candidate) {
            return (Resolve-Path $candidate).Path
        }
    }

    throw "Mesa UWP runtime DLLs not found. Set MESA_UWP_DIR, pass -MesaRuntimeDir, or place the DLLs in the tracked mesa-runtime folder."
}

function Get-ProjectPath {
    param(
        [Parameter(Mandatory = $true)]
        [string]$RelativePath
    )

    return (Join-Path (Resolve-RepoRoot) $RelativePath)
}

function Get-ConfigPath {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Name
    )

    if (-not $ProjectConfig.Contains($Name)) {
        throw "Unknown project path config key: $Name"
    }

    return (Get-ProjectPath $ProjectConfig[$Name])
}

function Test-MinecraftVersionAtLeast {
    param(
        [Parameter(Mandatory = $true)][string]$Version,
        [Parameter(Mandatory = $true)][string]$Minimum
    )

    $versionParts = $Version.Split('.') | ForEach-Object { [int]$_ }
    $minimumParts = $Minimum.Split('.') | ForEach-Object { [int]$_ }
    $count = [Math]::Max($versionParts.Length, $minimumParts.Length)
    for ($i = 0; $i -lt $count; $i++) {
        $value = if ($i -lt $versionParts.Length) { $versionParts[$i] } else { 0 }
        $minimumValue = if ($i -lt $minimumParts.Length) { $minimumParts[$i] } else { 0 }
        if ($value -gt $minimumValue) { return $true }
        if ($value -lt $minimumValue) { return $false }
    }
    return $true
}

function Ensure-Dir {
    param(
        [Parameter(Mandatory = $true)]
        [string[]]$Path
    )

    New-Item -ItemType Directory -Force -Path $Path | Out-Null
}
