param([string]$Root = $PSScriptRoot)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$packageRoot = [IO.Path]::GetFullPath($Root).TrimEnd('\', '/')
$prefix = $packageRoot + [IO.Path]::DirectorySeparatorChar
$checkedDirectories = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)

function Assert-OrdinaryDirectory([string]$Path) {
    if ($checkedDirectories.Contains($Path)) { return }
    $directory = [IO.DirectoryInfo]::new($Path)
    if (!$directory.Exists -or ($directory.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
        throw "Missing directory or reparse point: $Path"
    }
    if (!$Path.Equals($packageRoot, [StringComparison]::OrdinalIgnoreCase)) {
        if (!$Path.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) { throw 'Directory outside package.' }
        Assert-OrdinaryDirectory $directory.Parent.FullName
    }
    [void]$checkedDirectories.Add($Path)
}

function Get-OrdinaryHash([string]$Path) {
    $item = [IO.FileInfo]::new($Path)
    Assert-OrdinaryDirectory $item.DirectoryName
    if (!$item.Exists -or ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
        throw "Missing file or reparse point: $Path"
    }
    $stream = [IO.File]::Open($Path, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read)
    $hasher = [Security.Cryptography.SHA256]::Create()
    try { return [BitConverter]::ToString($hasher.ComputeHash($stream)).Replace('-', '').ToLowerInvariant() }
    finally { $hasher.Dispose(); $stream.Dispose() }
}

Assert-OrdinaryDirectory $packageRoot
$manifestPath = Join-Path $packageRoot 'MANIFEST.json'
$checksumPath = Join-Path $packageRoot 'MANIFEST.sha256'
[void](Get-OrdinaryHash $checksumPath)
$checksum = [IO.File]::ReadAllText($checksumPath, [Text.Encoding]::UTF8).Trim()
if ($checksum -notmatch '^([0-9a-f]{64})\s+MANIFEST\.json$') { throw 'Malformed manifest checksum.' }
$expectedManifest = $Matches[1]
$actualManifest = Get-OrdinaryHash $manifestPath
if ($actualManifest -cne $expectedManifest) { throw 'MANIFEST.json checksum mismatch.' }
$manifest = [IO.File]::ReadAllText($manifestPath, [Text.Encoding]::UTF8) | ConvertFrom-Json
if ($manifest.schema -ne 1 -or @($manifest.files).Count -eq 0) { throw 'Unsupported or empty manifest.' }
$seen = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
[long]$totalBytes = 0
$count = 0
foreach ($entry in $manifest.files) {
    $relative = [string]$entry.path
    if ([string]::IsNullOrWhiteSpace($relative) -or $relative.Contains(':') -or
        [IO.Path]::IsPathRooted($relative) -or $entry.sha256 -cnotmatch '^[0-9a-f]{64}$' -or
        [long]$entry.bytes -lt 0) { throw "Invalid manifest entry: $relative" }
    $target = [IO.Path]::GetFullPath((Join-Path $packageRoot $relative))
    if (!$target.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase) -or !$seen.Add($target)) {
        throw "Escaping or duplicate manifest path: $relative"
    }
    $actual = Get-OrdinaryHash $target
    $length = [IO.FileInfo]::new($target).Length
    if ($actual -cne $entry.sha256 -or $length -ne [long]$entry.bytes) {
        throw "File checksum/length mismatch: $relative"
    }
    $totalBytes += $length
    $count++
    if ($count % 5000 -eq 0) { Write-Host "Verified $count files..." }
}
if ($count -ne $manifest.file_count -or $totalBytes -ne $manifest.total_bytes) { throw 'Manifest totals mismatch.' }
foreach ($directory in [IO.Directory]::EnumerateDirectories($packageRoot, '*', [IO.SearchOption]::AllDirectories)) {
    Assert-OrdinaryDirectory $directory
}
foreach ($file in [IO.Directory]::EnumerateFiles($packageRoot, '*', [IO.SearchOption]::AllDirectories)) {
    if ($file -eq $manifestPath -or $file -eq $checksumPath) { continue }
    if (!$seen.Contains($file)) { throw "Unlisted file: $file" }
}
[ordered]@{
    verified = $true
    files = $count
    bytes = $totalBytes
    manifest_sha256 = $actualManifest
    scope = 'File integrity only; no GPU/boot changes and no proof of physical recoverability.'
} | ConvertTo-Json
