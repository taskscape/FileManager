[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string] $V143Manifest,
    [Parameter(Mandatory = $true)]
    [string] $V145Manifest
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$v143 = Get-Content -LiteralPath $V143Manifest -Raw | ConvertFrom-Json
$v145 = Get-Content -LiteralPath $V145Manifest -Raw | ConvertFrom-Json
if ($v143.toolset -ne 'v143' -or $v145.toolset -ne 'v145') {
    throw 'Toolset manifests must identify the expected v143 and v145 producers.'
}

$left = @{}
$right = @{}
foreach ($entry in @($v143.files)) { $left[$entry.path] = $entry }
foreach ($entry in @($v145.files)) { $right[$entry.path] = $entry }
$failures = [System.Collections.Generic.List[string]]::new()

foreach ($path in @($left.Keys + $right.Keys | Sort-Object -Unique)) {
    if (-not $left.ContainsKey($path) -or -not $right.ContainsKey($path)) {
        $failures.Add("$path is not produced by both toolsets.")
        continue
    }
    $v143Entry = $left[$path]
    $v145Entry = $right[$path]
    # Compiler output hashes may differ; shipped ABI, architecture, and version identity must not.
    foreach ($field in @('machine', 'fileVersion', 'productVersion')) {
        if ([string]$v143Entry.$field -ne [string]$v145Entry.$field) {
            $failures.Add("$path has mismatched ${field}: v143='$($v143Entry.$field)', v145='$($v145Entry.$field)'.")
        }
    }
}

if ($failures.Count -ne 0) {
    $failures | ForEach-Object { Write-Error $_ }
    throw 'v143/v145 Release artifact compatibility comparison failed.'
}

Write-Host "v143/v145 artifact parity passed for $($left.Count) files; hashes are retained for provenance."
