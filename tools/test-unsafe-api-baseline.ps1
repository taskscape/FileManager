[CmdletBinding()]
param(
    [string] $BaselinePath,
    [string] $BaseCommit
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
if ([string]::IsNullOrWhiteSpace($BaselinePath)) { $BaselinePath = Join-Path $PSScriptRoot 'unsafe-api-baseline.json' }
Import-Module (Join-Path $PSScriptRoot 'unsafe-api-baseline.psm1') -Force

if (-not (Test-Path -LiteralPath $BaselinePath -PathType Leaf)) { throw "Unsafe API baseline is missing: $BaselinePath" }
$root = Split-Path $PSScriptRoot -Parent
$baseline = Get-Content -LiteralPath $BaselinePath -Raw | ConvertFrom-Json
if ($baseline.schemaVersion -ne 1 -or $null -eq $baseline.entries) { throw 'Unsafe API baseline has an unsupported schema.' }

if ([string]::IsNullOrWhiteSpace($BaseCommit)) {
    # A normal local run has no pull-request base; use HEAD^ only to recognize pre-existing calls mechanically relocated by the current change.
    # Anchor Git to the repository because runtests.ps1 may be launched from its scripts directory.
    & git -C $root rev-parse --verify --quiet 'HEAD^' *> $null
    if ($LASTEXITCODE -eq 0) {
        $BaseCommit = 'HEAD^'
    }
}

$allowed = @{}
foreach ($entry in $baseline.entries) {
    $key = "$($entry.path)|$($entry.api)|$($entry.fingerprint)"
    if ($allowed.ContainsKey($key)) { throw "Unsafe API baseline has a duplicate entry: $key" }
    $allowed[$key] = [int]$entry.count
}

$baseSourceLines = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
if (-not [string]::IsNullOrWhiteSpace($BaseCommit)) {
    $sourcePathspecs = @(':(glob)**/*.c', ':(glob)**/*.cc', ':(glob)**/*.cpp', ':(glob)**/*.h', ':(glob)**/*.hpp')
    # One base-revision source scan avoids treating format strings as Git revisions
    # while keeping move detection fast enough for the full local test runner.
    # Pathspecs must be evaluated from the repository root so local and Actions scans cover the same source tree.
    foreach ($baseSourceLine in (& git -C $root grep -h -I -e '.' $BaseCommit -- $sourcePathspecs)) { [void]$baseSourceLines.Add($baseSourceLine.Trim()) }
}

function Test-LineWasPresentInBaseRevision {
    param([Parameter(Mandatory = $true)][string]$SourceLine)

    # Baseline fingerprints include paths, so retain equivalent pre-refactor
    # calls only when the exact normalized source line existed in the base revision.
    return $baseSourceLines.Contains($SourceLine.Trim())
}

$violations = @()
foreach ($entry in Get-UnsafeApiEntries -RepositoryRoot $root -IncludeSourceLine) {
    $key = "$($entry.path)|$($entry.api)|$($entry.fingerprint)"
    if (-not $allowed.ContainsKey($key) -or [int]$entry.count -gt $allowed[$key]) {
        if (-not [string]::IsNullOrWhiteSpace($BaseCommit) -and
            (Test-LineWasPresentInBaseRevision $entry.sourceLine)) {
            continue
        }
        $violations += "$($entry.path): $($entry.api) ($($entry.count) occurrence(s))"
    }
}
if ($violations.Count -ne 0) {
    $violations | ForEach-Object { Write-Error "New unsafe API debt: $_" }
    throw 'Unsafe API baseline rejected new repository call sites. Replace the call or regenerate the reviewed baseline after proving its bounded invariant.'
}
Write-Host "Unsafe API baseline accepted $($baseline.entries.Count) existing call-site fingerprints."
