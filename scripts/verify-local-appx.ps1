param([Parameter(Mandatory)][string]$Path, [Parameter(Mandatory)][string]$ExpectedThumbprint)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Security.Cryptography.Pkcs
Add-Type -AssemblyName System.IO.Compression.FileSystem
$zip = [IO.Compression.ZipFile]::OpenRead($Path)
try {
    function Read-EntryBytes([string]$Name) {
        $entry = $zip.GetEntry($Name)
        if (-not $entry) { throw "Missing package entry: $Name" }
        $stream = $entry.Open(); $memory = [IO.MemoryStream]::new()
        try { $stream.CopyTo($memory); return ,$memory.ToArray() }
        finally { $stream.Dispose(); $memory.Dispose() }
    }
    $signature = Read-EntryBytes 'AppxSignature.p7x'
    if ([Text.Encoding]::ASCII.GetString($signature,0,4) -ne 'PKCX') { throw 'Invalid signature header' }
    $cms = [Security.Cryptography.Pkcs.SignedCms]::new()
    $cms.Decode([byte[]]$signature[4..($signature.Length-1)])
    $cms.CheckSignature($true)
    if ($cms.SignerInfos.Count -ne 1 -or $cms.SignerInfos[0].Certificate.Thumbprint -ne $ExpectedThumbprint) { throw 'Unexpected signer' }
    $cert = $cms.SignerInfos[0].Certificate
    if ((Get-Date) -lt $cert.NotBefore -or (Get-Date) -gt $cert.NotAfter) { throw 'Certificate outside validity period' }
    $sha = [Security.Cryptography.SHA256]::Create()
    try {
        $signedHex = [BitConverter]::ToString($cms.ContentInfo.Content).Replace('-','')
        foreach ($item in @(@('AXBM','AppxBlockMap.xml'), @('AXCT','[Content_Types].xml'))) {
            $tag = [BitConverter]::ToString([Text.Encoding]::ASCII.GetBytes($item[0])).Replace('-','')
            $hash = [BitConverter]::ToString($sha.ComputeHash((Read-EntryBytes $item[1]))).Replace('-','')
            if (-not $signedHex.Contains($tag + $hash)) { throw "Signed digest mismatch: $($item[1])" }
        }
        [xml]$map = [Text.Encoding]::UTF8.GetString((Read-EntryBytes 'AppxBlockMap.xml'))
        if ($map.BlockMap.HashMethod -ne 'http://www.w3.org/2001/04/xmlenc#sha256') { throw 'Unsupported block hash algorithm' }
        $buffer = [byte[]]::new(65536); $files = 0
        foreach ($file in $map.BlockMap.File) {
            $entry = $zip.GetEntry($file.Name.Replace('\','/'))
            if (-not $entry -or $entry.Length -ne [long]$file.Size) { throw "Missing/wrong size: $($file.Name)" }
            $stream = $entry.Open()
            try {
                foreach ($block in $file.Block) {
                    $count = 0
                    while ($count -lt $buffer.Length) {
                        $read = $stream.Read($buffer,$count,$buffer.Length-$count)
                        if ($read -eq 0) { break }; $count += $read
                    }
                    if ([Convert]::ToBase64String($sha.ComputeHash($buffer,0,$count)) -ne $block.Hash) { throw "Corrupt block: $($file.Name)" }
                }
                if ($stream.ReadByte() -ne -1) { throw "Unverified bytes: $($file.Name)" }
            } finally { $stream.Dispose() }
            $files++
        }
    } finally { $sha.Dispose() }
    [IO.File]::WriteAllBytes([IO.Path]::ChangeExtension($Path,'.cer'),$cert.Export([Security.Cryptography.X509Certificates.X509ContentType]::Cert))
    Write-Host "Local signature, signed digests and $files payload files verified. Windows trust unchanged."
} finally { $zip.Dispose() }
"$((Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash)  $([IO.Path]::GetFileName($Path))" | Set-Content "$Path.sha256"
