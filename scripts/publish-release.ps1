param(
    [ValidateSet("nightly", "nightly-native", "stable")][string]$Channel = "nightly",
    [string]$Tag,
    [Parameter(Mandatory = $true)][string]$Sha,
    [string]$AppxVersion,
    [string]$RunUrl,
    [Parameter(Mandatory = $true)][string[]]$Asset,
    [switch]$DryRun
)

$ErrorActionPreference = "Stop"

if (-not $Tag) { $Tag = $Channel }

$shortSha = if ($Sha.Length -ge 7) { $Sha.Substring(0, 7) } else { $Sha }
$runLine = if ($RunUrl) { "Workflow run: $RunUrl" } else { "Build: local" }

$licenceNote = "Redistribution of the APPX package is not permitted without prior written permission, as described in the project license. Videos, streams, tutorials, and install guides are allowed when they follow the creator content rules in the license, including crediting and linking back to veroxsity / BanditVault."

# the version bump greps this body for "Package version: X.Y.Z.R", do not reword that line
if ($Channel -eq "stable") {
    $headline = "Stable release build from $shortSha."
    $channelNote = "Stable releases are the supported builds."
    $prerelease = $false
} elseif ($Channel -eq "nightly-native") {
    $headline = "Native mouse build from $shortSha."
    $channelNote = "Native mouse was tested on Xbox by the project owner. This separate package has its own LocalState. Use the matching public certificate from this release."
    $prerelease = $true
} else {
    $headline = "Automated nightly build from $shortSha."
    $channelNote = "Nightly releases are highly experimental builds for testing current development work. They are not full game releases, and support is not provided for nightly builds."
    $prerelease = $true
}

$notes = @"
$headline

Package version: $AppxVersion

$channelNote

$licenceNote

Commit: $Sha
$runLine
"@

$notesFile = Join-Path ([System.IO.Path]::GetTempPath()) "release-notes-$([guid]::NewGuid().ToString('N')).md"
[System.IO.File]::WriteAllText($notesFile, $notes, (New-Object System.Text.UTF8Encoding($false)))

function Invoke-Gh {
    param([string[]]$GhArgs)

    if ($DryRun) {
        Write-Host "gh $($GhArgs -join ' ')"
        return 0
    }

    # gh prints the release url on success, and anything on the success stream becomes the return value.
    # it also writes progress to stderr, which Stop turns into a terminating NativeCommandError
    $old = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        & gh @GhArgs | Out-Host
        return $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $old
    }
}

try {
    foreach ($a in $Asset) {
        if (-not (Test-Path -LiteralPath $a -PathType Leaf)) { throw "Asset not found: $a" }
    }

    # gh writes to stderr when the release is absent, so check the exit code
    $probe = 'Continue'
    $old = $ErrorActionPreference
    $ErrorActionPreference = $probe
    & gh release view $Tag *> $null
    $exists = ($LASTEXITCODE -eq 0)
    $ErrorActionPreference = $old

    if ($DryRun) { Write-Host "gh release view $Tag  ->  exists=$exists" }

    $verb = if ($exists) { "edit" } else { "create" }
    $ghArgs = @("release", $verb, $Tag, "--target", $Sha, "--title", $Tag, "--notes-file", $notesFile)
    if ($prerelease) {
        $ghArgs += "--prerelease"
    } elseif ($verb -eq "edit") {
        $ghArgs += @("--prerelease=false")
    }
    $code = Invoke-Gh $ghArgs
    if ($code -ne 0) { throw "gh release $verb failed with exit code $code" }

    $code = Invoke-Gh (@("release", "upload", $Tag) + $Asset + @("--clobber"))
    if ($code -ne 0) { throw "gh release upload failed with exit code $code" }

    Write-Host "Published $Tag from $shortSha with $($Asset.Count) asset(s)"
} finally {
    Remove-Item -LiteralPath $notesFile -Force -ErrorAction SilentlyContinue
}
