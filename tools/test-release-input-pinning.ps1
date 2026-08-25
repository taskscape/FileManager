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
$releaseInstaller = Get-Content -LiteralPath (Join-Path $repositoryRoot 'tools\build-release-installer.ps1') -Raw
$innoProvisioner = Get-Content -LiteralPath (Join-Path $repositoryRoot 'tools\install-pinned-inno-setup.ps1') -Raw
foreach ($requiredPattern in @(
    'needs: release-tests',
    'needs: build',
    'environment: production',
    'contents: write',
    # The workflow delegates packaging to the shared helper; the helper owns
    # the pinned Inno Setup call so local and CI packaging cannot drift.
    'build-release-installer\.ps1',
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
    '/LOG=',
    '/PORTABLE=1',
    '/CURRENTUSER',
    '/NOCANCEL',
    'InstallTimeoutSeconds',
    'WaitForExit',
    'diagnosticLogPath',
    'RUNNER_TOOL_CACHE',
    'downloaded = \$false',
    'runner service account cannot write Program Files',
    'ISCC\.exe publishes a 0\.0\.0\.0 PE version'
)) {
    # The helper owns verification and user-writable installation, so both local and GitHub packaging share one immutable tool boundary.
    if ($innoProvisioner -notmatch $requiredPattern) {
        throw "Pinned Inno Setup provisioner is missing required contract: $requiredPattern"
    }
}

if ($releaseInstaller -notmatch 'install-pinned-inno-setup\.ps1') {
    throw 'The shared release installer helper must invoke the pinned Inno Setup provisioner.'
}
$preflightPosition = $releaseInstaller.IndexOf('$innoCompiler = & $innoProvisioner', [StringComparison]::Ordinal)
$releaseBuildPosition = $releaseInstaller.IndexOf('& msbuild', [StringComparison]::Ordinal)
if ($preflightPosition -lt 0 -or $releaseBuildPosition -lt 0 -or $preflightPosition -gt $releaseBuildPosition) {
    throw 'The Build Installer helper must preflight Inno Setup before the Release build.'
}

if ($releaseWorkflow -notmatch 'Preflight pinned Inno Setup' -or
    $releaseWorkflow -notmatch 'Validate release workflow contracts' -or
    $releaseWorkflow -notmatch 'NativeSafetyRegressionTests\.Root_test_runner_collects_every_documented_automated_test_layer' -or
    $releaseWorkflow -notmatch 'inno-setup-provisioning-logs-') {
    throw 'The Build Installer workflow must preflight Inno Setup, validate release contracts before compiling, and preserve provisioning logs.'
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
if ($rootRunner -notmatch 'No release mode was specified' -or $rootRunner -notmatch 'NoReleasePipeline') {
    throw 'The local aggregate runner must default to release-pipeline mode while retaining an explicit CI opt-out.'
}
if ($rootRunner -notmatch 'Testing uncommitted local changes' -or $rootRunner -match 'Use a clean checkout at the selected commit') {
    # Developers must be able to validate the uncommitted snapshot they will push; only GitHub itself requires a committed checkout.
    throw 'The local release runner must accept uncommitted changes and report the GitHub comparison boundary as a note.'
}

Write-Host "Release input pinning passed for Inno Setup $($inno.version) and $($workflowPaths.Count) workflows."
