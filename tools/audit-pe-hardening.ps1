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

$resolvedBuildRoot = (Resolve-Path -LiteralPath $BuildRoot).Path.TrimEnd('\')
# CET is a Release x64 contract; rejecting a broad staging root prevents Debug helpers from creating false release failures.
if ([IO.Path]::GetFileName($resolvedBuildRoot) -cne 'Release_x64') {
    throw "Build root must be the exact Release_x64 artifact directory: $resolvedBuildRoot"
}

$dumpbin = Get-Command dumpbin.exe -ErrorAction SilentlyContinue | Select-Object -First 1
if ($null -eq $dumpbin) {
    throw 'dumpbin.exe is required to audit Release PE mitigation metadata.'
}

# These helpers deliberately link without the CRT, which cannot provide CFG's required runtime support.
$crtFreeArtifactNames = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
@('fcremote.exe', 'salextx64.dll', 'salextx86.dll', 'salopen.exe', 'salspawn.exe') | ForEach-Object {
    [void]$crtFreeArtifactNames.Add($_)
}

$binaries = @(Get-ChildItem -LiteralPath $BuildRoot -Recurse -File |
    Where-Object { $_.Extension -in '.exe', '.dll', '.spl' })
if ($binaries.Count -eq 0) {
    throw "No release PE files were found below $resolvedBuildRoot"
}

$failures = [System.Collections.Generic.List[string]]::new()
foreach ($binary in $binaries) {
    # Inspect linker-produced metadata, not project text, so the audit catches per-project overrides.
    $headers = (& $dumpbin.Source /headers $binary.FullName 2>&1) -join [Environment]::NewLine
    if ($LASTEXITCODE -ne 0) {
        $failures.Add("$($binary.FullName): dumpbin failed with exit code $LASTEXITCODE.")
        continue
    }

    $required = @('Dynamic base', 'NX compatible')
    if (-not $crtFreeArtifactNames.Contains($binary.Name)) {
        $required += 'Control Flow Guard'
    }
    if ($headers -match '(?im)^\s*[0-9A-F]+ machine \(x64\)') {
        # CET and high-entropy VA are x64-only linker contracts; x86 helpers cannot advertise either flag.
        $required += @('CET compatible', 'High Entropy Virtual Addresses')
    }
    foreach ($flag in $required) {
        if ($headers -notmatch [regex]::Escape($flag)) {
            $failures.Add("$($binary.FullName): missing $flag.")
        }
    }
}

if ($failures.Count -ne 0) {
    # Emit every missing flag before failing so one audit run identifies the complete mitigation gap.
    $failures | ForEach-Object { Write-Host $_ -ForegroundColor Red }
    throw 'Release PE hardening audit failed.'
}

Write-Host "Release PE hardening audit passed for $($binaries.Count) files."
