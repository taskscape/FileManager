[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string] $BuildRoot
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if (-not (Test-Path -LiteralPath $BuildRoot -PathType Container)) {
    throw "Build root does not exist: $BuildRoot"
}

$dumpbin = Get-Command dumpbin.exe -ErrorAction SilentlyContinue | Select-Object -First 1
if ($null -eq $dumpbin) {
    throw 'dumpbin.exe is required to audit Release PE mitigation metadata.'
}

$binaries = @(Get-ChildItem -LiteralPath $BuildRoot -Recurse -File |
    Where-Object { $_.Extension -in '.exe', '.dll', '.spl' })
if ($binaries.Count -eq 0) {
    throw "No release PE files were found below $BuildRoot"
}

$failures = [System.Collections.Generic.List[string]]::new()
foreach ($binary in $binaries) {
    # Inspect linker-produced metadata, not project text, so the audit catches per-project overrides.
    $headers = (& $dumpbin.Source /headers $binary.FullName 2>&1) -join [Environment]::NewLine
    if ($LASTEXITCODE -ne 0) {
        $failures.Add("$($binary.FullName): dumpbin failed with exit code $LASTEXITCODE.")
        continue
    }

    $required = @('Dynamic base', 'NX compatible', 'Control Flow Guard', 'CET compatible')
    if ($binary.Extension -ne '.spl' -or $binary.Length -gt 0) {
        $required += 'High Entropy Virtual Addresses'
    }
    foreach ($flag in $required) {
        if ($headers -notmatch [regex]::Escape($flag)) {
            $failures.Add("$($binary.FullName): missing $flag.")
        }
    }
}

if ($failures.Count -ne 0) {
    $failures | ForEach-Object { Write-Error $_ }
    throw 'Release PE hardening audit failed.'
}

Write-Host "Release PE hardening audit passed for $($binaries.Count) files."
