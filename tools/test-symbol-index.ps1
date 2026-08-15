[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string] $BuildRoot,
    [Parameter(Mandatory = $true)]
    [string] $IndexPath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$root = (Resolve-Path -LiteralPath $BuildRoot).Path.TrimEnd('\')
$index = Get-Content -LiteralPath $IndexPath -Raw | ConvertFrom-Json
if ($index.schemaVersion -ne 1 -or $null -eq $index.modules -or $index.modules.Count -eq 0) { throw 'Symbol index has no versioned module records.' }

$seenKeys = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
foreach ($entry in $index.modules) {
    # Verify immutable hashes before publication so an index never points at an unrelated PDB.
    $module = Join-Path $root $entry.module.Replace('/', '\')
    $pdb = Join-Path $root $entry.pdb.Replace('/', '\')
    if (-not (Test-Path -LiteralPath $module) -or -not (Test-Path -LiteralPath $pdb)) { throw "Symbol index references a missing module or PDB: $($entry.module)" }
    if ((Get-FileHash -LiteralPath $module -Algorithm SHA256).Hash.ToLowerInvariant() -ne $entry.moduleSha256) { throw "Module hash drifted for $($entry.module)." }
    if ((Get-FileHash -LiteralPath $pdb -Algorithm SHA256).Hash.ToLowerInvariant() -ne $entry.pdbSha256) { throw "PDB hash drifted for $($entry.pdb)." }
    if ($entry.symbolKey -notmatch '^[^/]+/[0-9A-F]{32}\d+/[^/]+\.pdb$' -or -not $seenKeys.Add($entry.symbolKey)) { throw "Invalid or duplicate symbol key: $($entry.symbolKey)" }
}
Write-Host "Verified $($index.modules.Count) indexed release symbols."
