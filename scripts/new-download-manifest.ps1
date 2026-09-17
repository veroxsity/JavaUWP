param(
    [string]$MinecraftVersion,
    [string]$Loader,
    [string]$LoaderVersion,
    [string]$FabricLoaderVersion,
    [string]$OutputPath
)

$ErrorActionPreference = "Stop"

. (Join-Path $PSScriptRoot "common.ps1")

if (-not $MinecraftVersion) {
    $MinecraftVersion = $ProjectConfig.MinecraftVersion
}
if (-not $Loader) {
    $Loader = "fabric"
}
$Loader = $Loader.ToLowerInvariant()
if (-not $LoaderVersion -and $FabricLoaderVersion) {
    $LoaderVersion = $FabricLoaderVersion
}
if (-not $LoaderVersion) {
    $FabricLoaderVersion = $ProjectConfig.FabricLoaderVersion
    $LoaderVersion = $FabricLoaderVersion
}
if (-not $FabricLoaderVersion) {
    $FabricLoaderVersion = $LoaderVersion
}
if (-not $OutputPath) {
    $OutputPath = Join-Path (Get-ConfigPath "PackageContentDir") "download_manifest.tsv"
}

function Get-Json([string]$Url) {
    $lastError = $null
    for ($attempt = 1; $attempt -le 4; $attempt++) {
        try {
            if ($attempt -gt 1) {
                Write-Host "Retry $attempt for $Url"
            }
            return Get-CachedRemoteJson -Uri $Url -TimeoutSec 60
        } catch {
            $lastError = $_
            if ($attempt -ge 4) {
                break
            }
            Start-Sleep -Seconds ([Math]::Min(2 * $attempt, 6))
        }
    }
    throw $lastError
}

function Convert-MavenNameToPath([string]$Name) {
    $parts = $Name.Split(":")
    if ($parts.Length -lt 3) {
        throw "Unsupported Maven coordinate: $Name"
    }

    $group = $parts[0].Replace(".", "/")
    $artifact = $parts[1]
    $version = $parts[2]
    $classifier = if ($parts.Length -ge 4) { "-$($parts[3])" } else { "" }
    return "$group/$artifact/$version/$artifact-$version$classifier.jar"
}

function Get-RemoteTextOrThrow([string]$Url) {
    $lastError = $null
    for ($attempt = 1; $attempt -le 4; $attempt++) {
        try {
            $content = (Invoke-WebRequest -UseBasicParsing -Uri $Url -TimeoutSec 30).Content
            $text = if ($content -is [byte[]]) {
                [System.Text.Encoding]::ASCII.GetString($content).Trim()
            } else {
                ([string]$content).Trim()
            }
            if ($text) {
                return $text
            }
            $lastError = "empty response"
        } catch {
            $lastError = $_
        }

        if ($attempt -lt 4) {
            Start-Sleep -Seconds ([Math]::Min(2 * $attempt, 6))
        }
    }

    throw "Could not fetch $Url after 4 attempts: $lastError"
}

function Get-RemoteSizeOrZero([string]$Url) {
    try {
        $response = Invoke-WebRequest -UseBasicParsing -Method Head -Uri $Url -TimeoutSec 30
        $length = $response.Headers["Content-Length"]
        if ($length) {
            return [UInt64]$length
        }
    } catch {
    }

    return [UInt64]0
}

function Add-Entry(
    [System.Collections.Generic.List[object]]$Entries,
    [string]$Path,
    [string]$Sha1,
    [UInt64]$Size,
    [string]$Url) {
    if (-not $Path -or -not $Url) {
        return
    }
    if (-not $Sha1.Trim()) {
        throw "No sha1 for manifest entry $Path from $Url"
    }

    $Entries.Add([pscustomobject]@{
        Path = $Path.Replace("\", "/")
        Sha1 = $Sha1.Trim().ToLowerInvariant()
        Size = $Size
        Url = $Url
    })
}

function Test-LibraryAllowed($Library) {
    if (-not $Library.rules) {
        return $true
    }

    $allowed = $false
    foreach ($rule in $Library.rules) {
        $applies = $true
        if ($rule.os) {
            if ($rule.os.name -and $rule.os.name -ne "windows") {
                $applies = $false
            }
            if ($rule.os.arch -and $rule.os.arch -notin @("x64", "amd64")) {
                $applies = $false
            }
            # ignoring an unhandled os.version rule would put the wrong libraries on the classpath
            if ($rule.os.version) {
                throw "Library $($Library.name) has an os.version rule ($($rule.os.version)), which is not implemented."
            }
        }

        if ($applies) {
            $allowed = $rule.action -eq "allow"
        }
    }

    return $allowed
}

function Add-MinecraftLibraries($VersionJson, [System.Collections.Generic.List[object]]$Entries) {
    foreach ($library in $VersionJson.libraries) {
        if (-not (Test-LibraryAllowed $library)) {
            continue
        }

        if ($library.downloads.artifact) {
            $artifact = $library.downloads.artifact
            Add-Entry $Entries "game/libraries/$($artifact.path)" $artifact.sha1 ([UInt64]$artifact.size) $artifact.url
        }

        if ($library.natives -and $library.natives.windows -and $library.downloads.classifiers) {
            $classifier = $library.natives.windows.Replace('${arch}', '64')
            $native = $library.downloads.classifiers.$classifier
            if ($native) {
                Add-Entry $Entries "game/libraries/$($native.path)" $native.sha1 ([UInt64]$native.size) $native.url
            }
        }
    }
}

function Add-FabricLibraries($FabricProfile, [System.Collections.Generic.List[object]]$Entries) {
    foreach ($library in $FabricProfile.libraries) {
        if ($library.name -like "net.fabricmc:fabric-loader:*") {
            continue
        }

        $path = Convert-MavenNameToPath $library.name
        $baseUrl = if ($library.url) { $library.url } else { "https://maven.fabricmc.net/" }
        if (-not $baseUrl.EndsWith("/")) {
            $baseUrl += "/"
        }

        $url = "$baseUrl$path"
        $sha1 = if ($library.sha1) { $library.sha1 } else { Get-RemoteTextOrThrow "$url.sha1" }
        $size = if ($library.size) { [UInt64]$library.size } else { Get-RemoteSizeOrZero $url }
        Add-Entry $Entries "game/libraries/$path" $sha1 $size $url
    }
}

function Add-LoaderLibraries($LoaderProfile, [System.Collections.Generic.List[object]]$Entries, [string]$DefaultBaseUrl) {
    foreach ($library in $LoaderProfile.libraries) {
        if (-not (Test-LibraryAllowed $library)) {
            continue
        }

        if ($library.downloads.artifact) {
            $artifact = $library.downloads.artifact
            Add-Entry $Entries "game/libraries/$($artifact.path)" $artifact.sha1 ([UInt64]$artifact.size) $artifact.url
            continue
        }

        $path = Convert-MavenNameToPath $library.name
        $baseUrl = if ($library.url) { $library.url } else { $DefaultBaseUrl }
        if (-not $baseUrl.EndsWith("/")) {
            $baseUrl += "/"
        }

        $url = "$baseUrl$path"
        $sha1 = if ($library.sha1) { $library.sha1 } else { Get-RemoteTextOrThrow "$url.sha1" }
        $size = if ($library.size) { [UInt64]$library.size } else { Get-RemoteSizeOrZero $url }
        Add-Entry $Entries "game/libraries/$path" $sha1 $size $url
    }
}

function Get-LibraryCoordinate([string]$Path) {
    if ($Path -notlike "game/libraries/*" -or $Path -notlike "*.jar") {
        return $null
    }
    $segments = $Path.Split("/")
    if ($segments.Count -lt 4) {
        return $null
    }
    $file = $segments[-1]
    $version = $segments[-2]
    $artifact = $segments[-3]
    $prefix = "$artifact-$version"
    if (-not $file.StartsWith($prefix)) {
        return $null
    }
    return (($segments[0..($segments.Count - 3)] -join "/") + $file.Substring($prefix.Length))
}

# vanilla ships its own asm on 1.21.2 to 1.21.10 and fabric will not start with two on the classpath
function Resolve-LibraryVersionConflicts([System.Collections.Generic.List[object]]$Entries) {
    $winners = @{}
    foreach ($entry in $Entries) {
        $coordinate = Get-LibraryCoordinate $entry.Path
        if (-not $coordinate) {
            continue
        }
        if ($winners.ContainsKey($coordinate) -and $winners[$coordinate].Path -ne $entry.Path) {
            Write-Host "Library version conflict on ${coordinate}: keeping $($entry.Path), dropping $($winners[$coordinate].Path)"
        }
        $winners[$coordinate] = $entry
    }

    $kept = [System.Collections.Generic.List[object]]::new()
    foreach ($entry in $Entries) {
        $coordinate = Get-LibraryCoordinate $entry.Path
        if ($coordinate -and $winners[$coordinate].Path -ne $entry.Path) {
            continue
        }
        $kept.Add($entry)
    }
    return ,$kept
}

function Get-ZipJson([string]$JarPath, [string]$EntryName) {
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $zip = [System.IO.Compression.ZipFile]::OpenRead($JarPath)
    try {
        $entry = $zip.GetEntry($EntryName)
        if (-not $entry) {
            throw "$EntryName not found in $JarPath"
        }
        $reader = [System.IO.StreamReader]::new($entry.Open())
        try {
            return $reader.ReadToEnd() | ConvertFrom-Json
        } finally {
            $reader.Dispose()
        }
    } finally {
        $zip.Dispose()
    }
}

function Save-ZipEntry([string]$JarPath, [string]$EntryName, [string]$OutputPath) {
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $zip = [System.IO.Compression.ZipFile]::OpenRead($JarPath)
    try {
        $entry = $zip.GetEntry($EntryName)
        if (-not $entry) {
            throw "$EntryName not found in $JarPath"
        }
        Ensure-Dir (Split-Path $OutputPath -Parent)
        [System.IO.Compression.ZipFileExtensions]::ExtractToFile($entry, $OutputPath, $true)
    } finally {
        $zip.Dispose()
    }
}

function Add-InstallerMavenEntries(
    [string]$JarPath,
    [string]$BaseUrl,
    [System.Collections.Generic.List[object]]$Entries) {
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    if (-not $BaseUrl.EndsWith("/")) {
        $BaseUrl += "/"
    }

    $zip = [System.IO.Compression.ZipFile]::OpenRead($JarPath)
    try {
        foreach ($entry in $zip.Entries) {
            if (-not $entry.FullName.StartsWith("maven/") -or -not $entry.FullName.EndsWith(".jar")) {
                continue
            }

            $path = $entry.FullName.Substring("maven/".Length)
            $url = "$BaseUrl$path"
            $sha1 = Get-RemoteTextOrThrow "$url.sha1"
            Add-Entry $Entries "game/libraries/$path" $sha1 ([UInt64]$entry.Length) $url
        }
    } finally {
        $zip.Dispose()
    }
}

function Convert-ArgumentArrayToList($Arguments) {
    $out = @()
    if (-not $Arguments) {
        return @()
    }

    foreach ($arg in $Arguments) {
        if ($arg -is [string]) {
            $out += $arg
            continue
        }

        if ($arg.value) {
            if ($arg.value -is [array]) {
                foreach ($value in $arg.value) {
                    $out += [string]$value
                }
            } else {
                $out += [string]$arg.value
            }
        }
    }
    return $out
}

function Convert-JvmArgsForEmbeddedJvm($Values) {
    $out = @()
    for ($i = 0; $i -lt $Values.Count; $i++) {
        $arg = [string]$Values[$i]
        if (($arg -eq "-p" -or $arg -eq "--module-path") -and ($i + 1) -lt $Values.Count) {
            $out += "--module-path=$($Values[$i + 1])"
            $i++
            continue
        }
        if (($arg -eq "--add-modules" -or $arg -eq "--add-opens" -or $arg -eq "--add-exports") -and ($i + 1) -lt $Values.Count) {
            $out += "$arg=$($Values[$i + 1])"
            $i++
            continue
        }
        $out += $arg
    }
    return $out
}

function Join-ManifestArgumentList($Values) {
    return ($Values | Where-Object { $_ }) -join ([char]0x1f)
}

function Get-ForgeInstallerVersion([string]$MinecraftVersion, [string]$Version) {
    if ($Version.StartsWith("$MinecraftVersion-")) {
        return $Version
    }
    return "$MinecraftVersion-$Version"
}

function Get-LoaderProfile([string]$Loader, [string]$MinecraftVersion, [string]$LoaderVersion) {
    if ($Loader -eq "fabric") {
        return Get-Json "https://meta.fabricmc.net/v2/versions/loader/$MinecraftVersion/$LoaderVersion/profile/json"
    }

    $tmp = Get-LoaderInstallerCacheDir
    Ensure-Dir $tmp
    if ($Loader -eq "forge") {
        $forgeVersion = Get-ForgeInstallerVersion $MinecraftVersion $LoaderVersion
        $jar = Join-Path $tmp "forge-$forgeVersion-installer.jar"
        $url = "https://maven.minecraftforge.net/net/minecraftforge/forge/$forgeVersion/forge-$forgeVersion-installer.jar"
        if (-not (Test-Path $jar)) {
            Write-Host "Fetch $url"
            Invoke-WebRequest -UseBasicParsing -Uri $url -OutFile $jar -TimeoutSec 120
        }
        return Get-ZipJson $jar "version.json"
    }

    if ($Loader -eq "neoforge") {
        $jar = Join-Path $tmp "neoforge-$LoaderVersion-installer.jar"
        $url = "https://maven.neoforged.net/releases/net/neoforged/neoforge/$LoaderVersion/neoforge-$LoaderVersion-installer.jar"
        if (-not (Test-Path $jar)) {
            Write-Host "Fetch $url"
            Invoke-WebRequest -UseBasicParsing -Uri $url -OutFile $jar -TimeoutSec 120
        }
        return Get-ZipJson $jar "version.json"
    }

    if ($Loader -eq "vanilla") {
        return $null
    }

    throw "Unsupported loader '$Loader'"
}

function Get-LoaderInstallProfile([string]$Loader, [string]$MinecraftVersion, [string]$LoaderVersion) {
    if ($Loader -ne "forge" -and $Loader -ne "neoforge") {
        return $null
    }

    $installerJar = Get-LoaderInstallerJarPath $Loader $MinecraftVersion $LoaderVersion
    if ($installerJar -and (Test-Path $installerJar)) {
        return Get-ZipJson $installerJar "install_profile.json"
    }

    return $null
}

function Get-LoaderInstallerJarPath([string]$Loader, [string]$MinecraftVersion, [string]$LoaderVersion) {
    $tmp = Get-LoaderInstallerCacheDir
    if ($Loader -eq "forge") {
        $forgeVersion = Get-ForgeInstallerVersion $MinecraftVersion $LoaderVersion
        return Join-Path $tmp "forge-$forgeVersion-installer.jar"
    }
    if ($Loader -eq "neoforge") {
        return Join-Path $tmp "neoforge-$LoaderVersion-installer.jar"
    }
    return $null
}

function Add-LoaderInstallerJarEntry(
    [string]$Loader,
    [string]$MinecraftVersion,
    [string]$LoaderVersion,
    [System.Collections.Generic.List[object]]$Entries) {
    if ($Loader -eq "forge") {
        $forgeVersion = Get-ForgeInstallerVersion $MinecraftVersion $LoaderVersion
        $path = "net/minecraftforge/forge/$forgeVersion/forge-$forgeVersion-installer.jar"
        $url = "https://maven.minecraftforge.net/$path"
        $sha1 = Get-RemoteTextOrThrow "$url.sha1"
        $size = Get-RemoteSizeOrZero $url
        Add-Entry $Entries "game/libraries/$path" $sha1 $size $url
        return
    }

    if ($Loader -eq "neoforge") {
        $path = "net/neoforged/neoforge/$LoaderVersion/neoforge-$LoaderVersion-installer.jar"
        $url = "https://maven.neoforged.net/releases/$path"
        $sha1 = Get-RemoteTextOrThrow "$url.sha1"
        $size = Get-RemoteSizeOrZero $url
        Add-Entry $Entries "game/libraries/$path" $sha1 $size $url
    }
}

function Get-GameArgumentValue($Arguments, [string]$Name) {
    if (-not $Arguments) {
        return ""
    }

    for ($i = 0; $i -lt $Arguments.Count - 1; $i++) {
        if ([string]$Arguments[$i] -eq $Name) {
            return [string]$Arguments[$i + 1]
        }
    }

    return ""
}

function Get-LibraryVersion($Profile, [string]$Prefix) {
    if (-not $Profile -or -not $Profile.libraries) {
        return ""
    }

    foreach ($library in $Profile.libraries) {
        $name = [string]$library.name
        if ($name.StartsWith($Prefix)) {
            $parts = $name.Split(":")
            if ($parts.Length -ge 3) {
                return $parts[2]
            }
        }
    }

    return ""
}

function Get-NeoFormVersionFromProfile($Profile) {
    if (-not $Profile -or -not $Profile.arguments -or -not $Profile.arguments.game) {
        return ""
    }

    $arguments = @($Profile.arguments.game)
    for ($i = 0; $i -lt $arguments.Count - 1; $i++) {
        if ([string]$arguments[$i] -eq "--fml.neoFormVersion") {
            return [string]$arguments[$i + 1]
        }
    }

    return ""
}

function Add-NeoFormArchiveEntry(
    [string]$MinecraftVersion,
    [string]$NeoFormVersion,
    [System.Collections.Generic.List[object]]$Entries) {
    if (-not $NeoFormVersion) {
        return
    }

    # on-device prep feeds this zip to installertools MCP_DATA. profiles up to 21.9 declare it as a
    # library and it arrives with the rest, newer ones stopped, so add it from the version instead
    $coordinate = "$MinecraftVersion-$NeoFormVersion"
    $path = "net/neoforged/neoform/$coordinate/neoform-$coordinate.zip"
    $manifestPath = "game/libraries/$path"
    foreach ($entry in $Entries) {
        if ($entry.Path -eq $manifestPath) {
            return
        }
    }

    $url = "https://maven.neoforged.net/releases/$path"
    $sha1 = Get-RemoteTextOrThrow "$url.sha1"
    $size = Get-RemoteSizeOrZero $url
    Add-Entry $Entries $manifestPath $sha1 $size $url
}

function Add-Assets($VersionJson, [System.Collections.Generic.List[object]]$Entries) {
    $assetIndex = $VersionJson.assetIndex
    Add-Entry $Entries "assets/indexes/$($assetIndex.id).json" $assetIndex.sha1 ([UInt64]$assetIndex.size) $assetIndex.url

    $assetJson = Get-Json $assetIndex.url
    foreach ($property in $assetJson.objects.PSObject.Properties) {
        $hash = [string]$property.Value.hash
        $size = if ($property.Value.size) { [UInt64]$property.Value.size } else { [UInt64]0 }
        $prefix = $hash.Substring(0, 2)
        Add-Entry $Entries "assets/objects/$prefix/$hash" $hash $size "https://resources.download.minecraft.net/$prefix/$hash"
    }
}

$manifest = Get-Json "https://piston-meta.mojang.com/mc/game/version_manifest_v2.json"
$version = $manifest.versions | Where-Object { $_.id -eq $MinecraftVersion } | Select-Object -First 1
if (-not $version) {
    throw "Minecraft version '$MinecraftVersion' was not found in the official version manifest."
}

$versionJson = Get-Json $version.url
$loaderProfile = Get-LoaderProfile $Loader $MinecraftVersion $LoaderVersion
$installProfile = Get-LoaderInstallProfile $Loader $MinecraftVersion $LoaderVersion

$entries = [System.Collections.Generic.List[object]]::new()
Add-Entry $entries "game/versions/$MinecraftVersion/$MinecraftVersion.json" $version.sha1 ([UInt64]0) $version.url
Add-Entry $entries "game/versions/$MinecraftVersion/$MinecraftVersion.jar" $versionJson.downloads.client.sha1 ([UInt64]$versionJson.downloads.client.size) $versionJson.downloads.client.url
if ($versionJson.downloads.client_mappings) {
    $clientMappings = $versionJson.downloads.client_mappings
    Add-Entry $entries "game/libraries/net/minecraft/client/$MinecraftVersion/client-$MinecraftVersion-mappings.txt" $clientMappings.sha1 ([UInt64]$clientMappings.size) $clientMappings.url
}
Add-MinecraftLibraries $versionJson $entries
if ($Loader -eq "fabric") {
    Add-FabricLibraries $loaderProfile $entries
    # fabric only, forge and neoforge add their installer's older dupes last and would win here
    $entries = Resolve-LibraryVersionConflicts $entries
} elseif ($Loader -eq "forge") {
    Add-LoaderLibraries $loaderProfile $entries "https://maven.minecraftforge.net/"
    if ($installProfile) {
        Add-LoaderLibraries $installProfile $entries "https://maven.minecraftforge.net/"
    }
    Add-LoaderInstallerJarEntry $Loader $MinecraftVersion $LoaderVersion $entries
    $installerJar = Get-LoaderInstallerJarPath $Loader $MinecraftVersion $LoaderVersion
    if ($installerJar -and (Test-Path $installerJar)) {
        Add-InstallerMavenEntries $installerJar "https://maven.minecraftforge.net/" $entries
    }
} elseif ($Loader -eq "neoforge") {
    Add-LoaderLibraries $loaderProfile $entries "https://maven.neoforged.net/releases/"
    if ($installProfile) {
        Add-LoaderLibraries $installProfile $entries "https://maven.neoforged.net/releases/"
    }
    Add-LoaderInstallerJarEntry $Loader $MinecraftVersion $LoaderVersion $entries
    $installerJar = Get-LoaderInstallerJarPath $Loader $MinecraftVersion $LoaderVersion
    if ($installerJar -and (Test-Path $installerJar)) {
        Add-InstallerMavenEntries $installerJar "https://maven.neoforged.net/releases/" $entries
    }
    Add-NeoFormArchiveEntry $MinecraftVersion (Get-NeoFormVersionFromProfile $loaderProfile) $entries
}
Add-Assets $versionJson $entries

$deduped = $entries |
    Group-Object Path |
    ForEach-Object { $_.Group | Select-Object -First 1 } |
    Sort-Object Path

Ensure-Dir (Split-Path $OutputPath -Parent)
$lines = [System.Collections.Generic.List[string]]::new()
$lines.Add("# MinecraftJavaUWP official download manifest")
$lines.Add("# minecraftVersion`t$MinecraftVersion")
$lines.Add("# loader`t$Loader")
$lines.Add("# loaderVersion`t$LoaderVersion")
if ($Loader -eq "fabric") {
    $lines.Add("# fabricLoaderVersion`t$LoaderVersion")
}
$lines.Add("# assetIndex`t$($versionJson.assetIndex.id)")
$launchVersion = if ($loaderProfile -and $loaderProfile.PSObject.Properties["id"] -and $loaderProfile.PSObject.Properties["id"].Value) { $loaderProfile.PSObject.Properties["id"].Value } else { $MinecraftVersion }
$mainClass = if ($loaderProfile -and $loaderProfile.PSObject.Properties["mainClass"] -and $loaderProfile.PSObject.Properties["mainClass"].Value) { $loaderProfile.PSObject.Properties["mainClass"].Value } else { $versionJson.mainClass }
$jvmArgs = [System.Collections.Generic.List[string]]::new()
$gameArgs = [System.Collections.Generic.List[string]]::new()
$argumentsProperty = if ($loaderProfile -and $loaderProfile.PSObject.Properties["arguments"]) { $loaderProfile.PSObject.Properties["arguments"].Value } else { $null }
if ($argumentsProperty -and $Loader -ne "fabric") {
    $jvmArgs = Convert-JvmArgsForEmbeddedJvm (Convert-ArgumentArrayToList $argumentsProperty.jvm)
    $gameArgs = Convert-ArgumentArrayToList $argumentsProperty.game
}
$lines.Add("# launchVersion`t$launchVersion")
$lines.Add("# mainClass`t$mainClass")
if ($jvmArgs.Count -gt 0) {
    $lines.Add("# jvmArgs`t$(Join-ManifestArgumentList $jvmArgs)")
}
if ($gameArgs.Count -gt 0) {
    $lines.Add("# gameArgs`t$(Join-ManifestArgumentList $gameArgs)")
}
if ($Loader -eq "neoforge") {
    $neoFormVersion = Get-GameArgumentValue $gameArgs "--fml.neoFormVersion"
    if ($neoFormVersion) {
        $lines.Add("# neoFormVersion`t$neoFormVersion")
    }
    $installToolsVersion = Get-LibraryVersion $installProfile "net.neoforged.installertools:installertools:"
    $jarSplitterVersion = Get-LibraryVersion $installProfile "net.neoforged.installertools:jarsplitter:"
    $binaryPatcherVersion = Get-LibraryVersion $installProfile "net.neoforged.installertools:binarypatcher:"
    $autoRenamingToolVersion = Get-LibraryVersion $installProfile "net.neoforged:AutoRenamingTool:"
    if ($installToolsVersion) { $lines.Add("# neoForgeInstallToolsVersion`t$installToolsVersion") }
    if ($jarSplitterVersion) { $lines.Add("# neoForgeJarSplitterVersion`t$jarSplitterVersion") }
    if ($binaryPatcherVersion) { $lines.Add("# neoForgeBinaryPatcherVersion`t$binaryPatcherVersion") }
    if ($autoRenamingToolVersion) { $lines.Add("# neoForgeAutoRenamingToolVersion`t$autoRenamingToolVersion") }
} elseif ($Loader -eq "forge") {
    $neoFormVersion = Get-GameArgumentValue $gameArgs "--fml.neoFormVersion"
    $mcpVersion = Get-GameArgumentValue $gameArgs "--fml.mcpVersion"
    if ($neoFormVersion) {
        $lines.Add("# neoFormVersion`t$neoFormVersion")
    } elseif ($mcpVersion) {
        $lines.Add("# forgeMcpVersion`t$mcpVersion")
    }
    $installToolsVersion = Get-LibraryVersion $installProfile "net.minecraftforge:installertools:"
    if (-not $installToolsVersion) {
        $installToolsVersion = Get-LibraryVersion $installProfile "net.neoforged.installertools:installertools:"
    }
    $jarSplitterVersion = Get-LibraryVersion $installProfile "net.minecraftforge:jarsplitter:"
    if (-not $jarSplitterVersion) {
        $jarSplitterVersion = Get-LibraryVersion $installProfile "net.neoforged.installertools:jarsplitter:"
    }
    $binaryPatcherVersion = Get-LibraryVersion $installProfile "net.minecraftforge:binarypatcher:"
    if (-not $binaryPatcherVersion) {
        $binaryPatcherVersion = Get-LibraryVersion $installProfile "net.neoforged.installertools:binarypatcher:"
    }
    $autoRenamingToolVersion = Get-LibraryVersion $installProfile "net.minecraftforge:AutoRenamingTool:"
    if (-not $autoRenamingToolVersion) {
        $autoRenamingToolVersion = Get-LibraryVersion $installProfile "net.neoforged:AutoRenamingTool:"
    }
    if ($installToolsVersion) { $lines.Add("# neoForgeInstallToolsVersion`t$installToolsVersion") }
    if ($jarSplitterVersion) { $lines.Add("# neoForgeJarSplitterVersion`t$jarSplitterVersion") }
    if ($binaryPatcherVersion) { $lines.Add("# neoForgeBinaryPatcherVersion`t$binaryPatcherVersion") }
    if ($autoRenamingToolVersion) { $lines.Add("# neoForgeAutoRenamingToolVersion`t$autoRenamingToolVersion") }
}
$lines.Add("# path`tsha1`tsize`turl")
foreach ($entry in $deduped) {
    $lines.Add("$($entry.Path)`t$($entry.Sha1)`t$($entry.Size)`t$($entry.Url)")
}

[System.IO.File]::WriteAllLines($OutputPath, $lines, [System.Text.UTF8Encoding]::new($false))
Write-Host "Wrote $($deduped.Count) download entries to $OutputPath"
