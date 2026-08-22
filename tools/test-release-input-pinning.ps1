[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repositoryRoot = Split-Path -Parent $PSScriptRoot
$lockPath = Join-Path $PSScriptRoot 'release-inputs.json'
if (-not (Test-Path -LiteralPath $lockPath -PathType Leaf)) {
    throw "Release input lock file is missing: $lockPath"
}

# Treat release-tool coordinates as data so reviewing an update requires all identity fields.
$lock = Get-Content -LiteralPath $lockPath -Raw | ConvertFrom-Json
if ($lock.schemaVersion -ne 1) {
    throw "Unsupported release-input lock schema version: $($lock.schemaVersion)"
}

$inno = $lock.inputs.innoSetup
if ([string]::IsNullOrWhiteSpace($inno.version) -or
    $inno.url -notmatch '^https://github\.com/jrsoftware/issrc/releases/download/' -or
    $inno.sha256 -notmatch '^[0-9a-f]{64}$' -or
    [string]::IsNullOrWhiteSpace($inno.publisher)) {
    throw 'The Inno Setup release input must declare version, immutable release URL, SHA-256, and publisher.'
}

$workflowPaths = Get-ChildItem -LiteralPath (Join-Path $repositoryRoot '.github\workflows') -Filter '*.yml' -File
if ($workflowPaths.Count -eq 0) {
    throw 'No GitHub Actions workflows were found to verify.'
}

$mutableReferences = [System.Collections.Generic.List[string]]::new()
foreach ($workflowPath in $workflowPaths) {
    $lineNumber = 0
    foreach ($line in Get-Content -LiteralPath $workflowPath.FullName) {
        $lineNumber++
        if ($line -match '^\s*uses:\s*[^@\s]+@([^\s#]+)') {
            $revision = $Matches[1]
            # Full commit identifiers make action execution independent of moved tags and branches.
            if ($revision -notmatch '^[0-9a-f]{40}$') {
                $mutableReferences.Add("$($workflowPath.Name):$lineNumber uses mutable revision '$revision'.")
            }
        }
    }
}

if ($mutableReferences.Count -ne 0) {
    $mutableReferences | ForEach-Object { Write-Error $_ }
    throw 'All GitHub Actions references must use reviewed full commit SHAs.'
}

$releaseWorkflow = Get-Content -LiteralPath (Join-Path $repositoryRoot '.github\workflows\build-installer.yml') -Raw
$innoProvisioner = Get-Content -LiteralPath (Join-Path $repositoryRoot 'tools\install-pinned-inno-setup.ps1') -Raw
foreach ($requiredPattern in @(
    'needs: release-tests',
    'needs: build',
    'environment: production',
    'contents: write',
    'install-pinned-inno-setup\.ps1',
    'release-installer-\$\{\{ github\.sha \}\}',
    'private-symbols-\$\{\{ github\.sha \}\}'
)) {
    if ($releaseWorkflow -notmatch $requiredPattern) {
        throw "Release workflow is missing required immutable-release contract: $requiredPattern"
    }
}

foreach ($requiredPattern in @(
    'Get-FileHash',
    'Get-AuthenticodeSignature',
    '/DIR=',
    'runner service account cannot write Program Files',
    'ISCC\.exe publishes a 0\.0\.0\.0 PE version'
)) {
    # The helper owns verification and user-writable installation, so both local and GitHub packaging share one immutable tool boundary.
    if ($innoProvisioner -notmatch $requiredPattern) {
        throw "Pinned Inno Setup provisioner is missing required contract: $requiredPattern"
    }
}

foreach ($scriptPath in @(
    (Join-Path $repositoryRoot '.github\workflows\build-installer.yml'),
    (Join-Path $repositoryRoot 'scripts\build-installer.ps1'),
    (Join-Path $repositoryRoot 'scripts\runtests.ps1')
)) {
    $scriptText = Get-Content -LiteralPath $scriptPath -Raw
    # The compiler provisioner is a PowerShell script, so LASTEXITCODE would reject a successful return on a fresh shell.
    if ($scriptText -match 'install-pinned-inno-setup\.ps1[\s\S]{0,400}\$LASTEXITCODE') {
        throw "Inno Setup provisioner caller incorrectly checks LASTEXITCODE: $scriptPath"
    }
}

$rootRunner = Get-Content -LiteralPath (Join-Path $repositoryRoot 'scripts\runtests.ps1') -Raw
if ($rootRunner -notmatch 'test-release-input-pinning\.ps1') {
    throw 'The aggregate test runner must execute the release-input pinning contract.'
}

Write-Host "Release input pinning passed for Inno Setup $($inno.version) and $($workflowPaths.Count) workflows."
