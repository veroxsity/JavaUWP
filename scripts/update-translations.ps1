# rewrites MC.Xbox\lang\en.json from the strings marked in the source, then reports what each language file is missing
# -Fill adds the missing entries to each language file with the English text, so untranslated ones are easy to spot
param(
    [switch]$Fill
)

$ErrorActionPreference = "Stop"
$root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$langDir = Join-Path $root "MC.Xbox\lang"
$utf8 = New-Object System.Text.UTF8Encoding($false)

function Unescape-C([string]$text) {
    [regex]::Replace($text, '\\(u[0-9A-Fa-f]{4}|.)', {
        param($m)
        $c = $m.Groups[1].Value
        if ($c.Length -eq 5) { return [string][char][Convert]::ToInt32($c.Substring(1), 16) }
        switch ($c) { 'n' { "`n" } 't' { "`t" } default { $c } }
    })
}

# the text of one or more adjacent string literals, which the compiler joins into one
function Read-Literal([string]$run, [string]$prefix) {
    $parts = [regex]::Matches($run, $prefix + '"((?:[^"\\]|\\.)*)"') | ForEach-Object { $_.Groups[1].Value }
    Unescape-C ($parts -join '')
}

function Decode-Html([string]$text) {
    $text.Replace('&lt;', '<').Replace('&gt;', '>').Replace('&quot;', '"').Replace('&#39;', "'").Replace('&amp;', '&')
}

function Quote([string]$text) {
    $escaped = $text.Replace('\', '\\').Replace('"', '\"').Replace("`n", '\n').Replace("`t", '\t').Replace("`r", '')
    '"' + $escaped + '"'
}

function Write-Json([string]$path, [string]$name, $entries) {
    $lines = @('  "_name": ' + (Quote $name))
    foreach ($key in $entries.Keys) { $lines += '  ' + (Quote $key) + ': ' + (Quote $entries[$key]) }
    [IO.File]::WriteAllText($path, "{`n" + ($lines -join ",`n") + "`n}`n", $utf8)
}

# keys that differ only by case are different strings, so the dictionaries have to compare exactly
function New-Map { New-Object System.Collections.Specialized.OrderedDictionary ([StringComparer]::Ordinal) }

function Read-Map([string]$path) {
    $text = Get-Content $path -Raw -Encoding UTF8
    if ($PSVersionTable.PSVersion.Major -ge 6) { return ConvertFrom-Json $text -AsHashtable }
    Add-Type -AssemblyName System.Web.Extensions
    (New-Object System.Web.Script.Serialization.JavaScriptSerializer).DeserializeObject($text)
}

# the text of the Remote Files page as the compiler sees it. literals that are separate in the source are split
# by a control character, so text that is built from pieces at run time can be told apart from fixed text
function Get-PageHtml([string]$source) {
    $pattern = '(?s)(?<raw>R"(?<d>[A-Za-z0-9_]*)\((?<body>.*?)\)\k<d>")|(?<cmt>//[^\n]*|/\*.*?\*/)|(?<wlit>L"(?:[^"\\\n]|\\.)*")|(?<chr>''(?:[^''\\\n]|\\.)+'')|(?<nlit>"(?<str>(?:[^"\\\n]|\\.)*)")'
    $page = New-Object System.Text.StringBuilder
    $prevEnd = -1
    foreach ($m in [regex]::Matches($source, $pattern)) {
        if ($m.Groups['cmt'].Success -or $m.Groups['wlit'].Success -or $m.Groups['chr'].Success) { continue }
        if ($m.Groups['raw'].Success) {
            $body = $m.Groups['body'].Value
            # style sheets and the page script are not markup
            if ($m.Groups['d'].Value -eq 'RFSTYLE' -or $body -match '^\s*(var |async function |function )') { $prevEnd = -1; continue }
        } else {
            $body = Unescape-C $m.Groups['str'].Value
        }
        $between = if ($prevEnd -ge 0) { $source.Substring($prevEnd, $m.Index - $prevEnd) } else { '' }
        # a stream insertion between two literals still joins them into one piece of text
        if ($prevEnd -lt 0 -or ($between -match '\S' -and $between -notmatch '^\s*<<\s*$')) { [void]$page.Append([char]1) }
        [void]$page.Append($body)
        $prevEnd = $m.Index + $m.Length
    }
    $page.ToString()
}

$english = New-Map
function Add-Key([string]$key) { if ($key -and -not $english.Contains($key)) { $english[$key] = $key } }

$sources = Get-ChildItem (Join-Path $root "MC.Xbox") -Recurse -Include *.cpp, *.h |
    Where-Object { $_.FullName -notmatch '\\third_party\\' } | Sort-Object FullName
$dynamic = @()
foreach ($file in $sources) {
    $source = [IO.File]::ReadAllText($file.FullName)
    foreach ($m in [regex]::Matches($source, '\b(?:Tr|TrFormat|TR_NOOP)\(\s*((?:L"(?:[^"\\]|\\.)*"\s*)+)')) {
        Add-Key (Read-Literal $m.Groups[1].Value 'L')
    }
    foreach ($m in [regex]::Matches($source, '\b(?:TrU|TrFormatU)\(\s*((?:"(?:[^"\\]|\\.)*"\s*)+)')) {
        Add-Key (Read-Literal $m.Groups[1].Value '')
    }
    if ($file.Name -ne 'remote_file_server.cpp') { continue }

    # the page script calls tr('English') and trf('English {0}', [values])
    foreach ($m in [regex]::Matches($source, "\btrf?\(\s*'((?:[^'\\]|\\.)*)'")) { Add-Key (Unescape-C $m.Groups[1].Value) }

    # fixed text in the page, found the same way html_translate.cpp looks it up
    $page = Get-PageHtml $source
    $page = [regex]::Replace($page, '(?is)<(script|style|code|pre|textarea)\b[^>]*>.*?</\1>', '')
    $nodes = @([regex]::Matches($page, '>([^<>]+)<') | ForEach-Object { $_.Groups[1].Value }) +
        @([regex]::Matches($page, '\s(?:title|placeholder|aria-label)="([^"<>]*)"') | ForEach-Object { $_.Groups[1].Value })
    foreach ($node in $nodes) {
        if ($node -notmatch '[A-Za-z]') { continue }
        if ($node.Contains([string][char]1)) {
            # a sentence with a few pieces in it, not code or unrelated strings that ended up side by side
            if ($node -match '\S\s+\S' -and $node -notmatch '[;{}=+$\[\]"]' -and ([regex]::Matches($node, [string][char]1)).Count -le 3) { $dynamic += ($node -replace [string][char]1, '{...}').Trim() }
            continue
        }
        Add-Key (Decode-Html $node.Trim())
    }
}

New-Item -ItemType Directory -Force $langDir | Out-Null
Write-Json (Join-Path $langDir "en.json") "English" $english
Write-Host "en.json: $($english.Count) strings"
if ($dynamic.Count -gt 0) {
    Write-Host "text on the web page that is built from pieces and cannot be translated yet:"
    $dynamic | Sort-Object -Unique | ForEach-Object { Write-Host "  $_" }
}

foreach ($file in Get-ChildItem $langDir -Filter *.json | Where-Object { $_.Name -ne "en.json" }) {
    $current = New-Map
    $name = $file.BaseName
    $parsed = Read-Map $file.FullName
    foreach ($key in $parsed.Keys) {
        if ($key -ceq "_name") { $name = $parsed[$key] } else { $current[$key] = $parsed[$key] }
    }

    $missing = @($english.Keys | Where-Object { -not $current.Contains($_) })
    $unused = @($current.Keys | Where-Object { -not $english.Contains($_) })
    Write-Host "$($file.Name): $($missing.Count) missing, $($unused.Count) no longer in the source"
    if ($Fill -and $missing.Count -gt 0) {
        foreach ($key in $missing) { $current[$key] = $key }
        Write-Json $file.FullName $name $current
    }
}
