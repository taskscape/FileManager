[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string] $BuildRoot,
    [Parameter(Mandatory = $true)]
    [string] $IndexPath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Get-Sha256Hex {
    param([Parameter(Mandatory = $true)][string]$Path)

    # Use the BCL directly so symbol verification matches indexing on minimal Windows PowerShell hosts.
    $stream = [System.IO.File]::OpenRead($Path)
    $hasher = [System.Security.Cryptography.SHA256]::Create()
    try {
        $bytes = $hasher.ComputeHash($stream)
        return (($bytes | ForEach-Object { $_.ToString('x2') }) -join '')
    }
    finally {
        $hasher.Dispose()
        $stream.Dispose()
    }
}

$root = (Resolve-Path -LiteralPath $BuildRoot).Path.TrimEnd('\')
$index = Get-Content -LiteralPath $IndexPath -Raw | ConvertFrom-Json
if ($index.schemaVersion -ne 1 -or $null -eq $index.modules -or $index.modules.Count -eq 0) { throw 'Symbol index has no versioned module records.' }

$symbolContent = [Collections.Generic.Dictionary[string, string]]::new([StringComparer]::OrdinalIgnoreCase)
foreach ($entry in $index.modules) {
    # Verify immutable hashes before publication so an index never points at an unrelated PDB.
    $module = Join-Path $root $entry.module.Replace('/', '\')
    $pdb = Join-Path $root $entry.pdb.Replace('/', '\')
    if (-not (Test-Path -LiteralPath $module) -or -not (Test-Path -LiteralPath $pdb)) { throw "Symbol index references a missing module or PDB: $($entry.module)" }
    if ((Get-Sha256Hex -Path $module) -ne $entry.moduleSha256) { throw "Module hash drifted for $($entry.module)." }
    if ((Get-Sha256Hex -Path $pdb) -ne $entry.pdbSha256) { throw "PDB hash drifted for $($entry.pdb)." }
    if ($entry.symbolKey -notmatch '^[^/]+/[0-9A-F]{32}\d+/[^/]+\.pdb$') {
        throw "Invalid symbol key: $($entry.symbolKey)"
    }

    # Release staging deliberately mirrors helper binaries; a shared CodeView key is safe only when both immutable artifacts are identical.
    $contentIdentity = "$($entry.moduleSha256)|$($entry.pdbSha256)"
    [string]$knownContentIdentity = $null
    if ($symbolContent.TryGetValue($entry.symbolKey, [ref]$knownContentIdentity)) {
        if ($knownContentIdentity -ne $contentIdentity) {
            throw "Symbol key resolves to inconsistent release content: $($entry.symbolKey)"
        }
    }
    else {
        $symbolContent.Add($entry.symbolKey, $contentIdentity)
    }
}
Write-Host "Verified $($index.modules.Count) indexed release symbols."
