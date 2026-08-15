[CmdletBinding()]
param([string] $OutputPath)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
if ([string]::IsNullOrWhiteSpace($OutputPath)) { $OutputPath = Join-Path $PSScriptRoot 'unsafe-api-baseline.json' }
Import-Module (Join-Path $PSScriptRoot 'unsafe-api-baseline.psm1') -Force

$root = Split-Path $PSScriptRoot -Parent
# The manifest is content-based so line movement is not mistaken for new unsafe debt.
$baseline = [ordered]@{ schemaVersion = 1; generatedUtc = [DateTime]::UtcNow.ToString('o'); entries = @(Get-UnsafeApiEntries -RepositoryRoot $root) }
$baseline | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $OutputPath -Encoding utf8
Write-Host "Wrote $($baseline.entries.Count) unsafe API baseline entries to $OutputPath"
