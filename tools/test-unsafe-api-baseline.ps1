[CmdletBinding()]
param([string] $BaselinePath)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
if ([string]::IsNullOrWhiteSpace($BaselinePath)) { $BaselinePath = Join-Path $PSScriptRoot 'unsafe-api-baseline.json' }
Import-Module (Join-Path $PSScriptRoot 'unsafe-api-baseline.psm1') -Force

if (-not (Test-Path -LiteralPath $BaselinePath -PathType Leaf)) { throw "Unsafe API baseline is missing: $BaselinePath" }
$root = Split-Path $PSScriptRoot -Parent
$baseline = Get-Content -LiteralPath $BaselinePath -Raw | ConvertFrom-Json
if ($baseline.schemaVersion -ne 1 -or $null -eq $baseline.entries) { throw 'Unsafe API baseline has an unsupported schema.' }

$allowed = @{}
foreach ($entry in $baseline.entries) {
    $key = "$($entry.path)|$($entry.api)|$($entry.fingerprint)"
    if ($allowed.ContainsKey($key)) { throw "Unsafe API baseline has a duplicate entry: $key" }
    $allowed[$key] = [int]$entry.count
}

$violations = @()
foreach ($entry in Get-UnsafeApiEntries -RepositoryRoot $root) {
    $key = "$($entry.path)|$($entry.api)|$($entry.fingerprint)"
    if (-not $allowed.ContainsKey($key) -or [int]$entry.count -gt $allowed[$key]) {
        $violations += "$($entry.path): $($entry.api) ($($entry.count) occurrence(s))"
    }
}
if ($violations.Count -ne 0) {
    $violations | ForEach-Object { Write-Error "New unsafe API debt: $_" }
    throw 'Unsafe API baseline rejected new repository call sites. Replace the call or regenerate the reviewed baseline after proving its bounded invariant.'
}
Write-Host "Unsafe API baseline accepted $($baseline.entries.Count) existing call-site fingerprints."
