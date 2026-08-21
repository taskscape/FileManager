[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string] $ExecutablePath,
    [Parameter(Mandatory = $true)]
    [string] $TestProject,
    [Parameter(Mandatory = $true)]
    [string] $AppVerifierPath,
    [Parameter(Mandatory = $true)]
    [string] $LogOutputDirectory,
    [ValidateSet('Locks', 'Full')]
    [string] $VerifierProfile = 'Locks',
    # Individual layers are run before the full profile so a startup failure identifies the first incompatible verifier provider.
    [ValidateSet('Locks', 'Handles', 'Heaps', 'Exceptions')]
    [string[]] $VerifierLayers,
    [string] $TestFilter = 'TestCategory=LockStress'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if (-not (Test-Path -LiteralPath $ExecutablePath)) {
    throw "FileManager executable was not found: $ExecutablePath"
}
if (-not (Test-Path -LiteralPath $TestProject)) {
    throw "FileManager UI test project was not found: $TestProject"
}
if (-not (Test-Path -LiteralPath $AppVerifierPath)) {
    throw "Application Verifier was not found: $AppVerifierPath"
}

$targetName = Split-Path -Leaf $ExecutablePath
$gflagsPath = $null
if ($VerifierProfile -eq 'Full') {
    # Full PageHeap is deliberately opt-in because it is process-persistent and expensive.
    $gflagsCommand = Get-Command gflags.exe -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($null -eq $gflagsCommand) {
        throw 'gflags.exe is required for the Full Application Verifier profile.'
    }
    $gflagsPath = $gflagsCommand.Source
}

try {
    # Verifier configuration is process-persistent, so every enabled layer is removed in finally.
    # The unary comma keeps a single layer as an array; without it, @('Locks')
    # unrolls to a scalar string and @layers splats as the character 'L'.
    $layers = if ($PSBoundParameters.ContainsKey('VerifierLayers')) { @($VerifierLayers) } elseif ($VerifierProfile -eq 'Full') { @('Heaps', 'Handles', 'Locks', 'Exceptions') } else { , @('Locks') }
    # Define the automatic variable before launching the GUI-subsystem CLI so a non-elevated host reports the real prerequisite error under StrictMode.
    $global:LASTEXITCODE = $null
    & $AppVerifierPath -enable @layers -for $targetName
    if ($null -eq $LASTEXITCODE) {
        throw "Application Verifier could not be launched to enable $($layers -join ', ') for $targetName (the command did not set an exit code; it may require an elevated console)."
    }
    if ($LASTEXITCODE -ne 0) {
        throw "Application Verifier failed to enable $($layers -join ', ') for $targetName (exit code $LASTEXITCODE)."
    }

    if ($null -ne $gflagsPath) {
        & $gflagsPath /p /enable $targetName /full
        if ($LASTEXITCODE -ne 0) {
            throw "gflags failed to enable full PageHeap for $targetName (exit code $LASTEXITCODE)."
        }
    }

    # Preserve the probe result beside verifier XML so CI diagnosis does not depend on an ephemeral console log.
    $testResultsDirectory = Join-Path $LogOutputDirectory 'nunit-results'
    New-Item -ItemType Directory -Path $testResultsDirectory -Force | Out-Null
    dotnet test $TestProject --filter $TestFilter --results-directory $testResultsDirectory `
        --logger 'trx;LogFileName=verifier-tests.trx' --logger 'console;verbosity=normal' -- NUnit.NumberOfTestWorkers=0
    if ($LASTEXITCODE -ne 0) {
        throw "The Application Verifier lock stress suite failed (exit code $LASTEXITCODE)."
    }
}
finally {
    if ($null -ne $gflagsPath) {
        & $gflagsPath /p /disable $targetName
        if ($LASTEXITCODE -ne 0) {
            Write-Warning "gflags PageHeap settings could not be removed for $targetName (exit code $LASTEXITCODE)."
        }
    }

    & $AppVerifierPath -delete settings -for $targetName
    if ($LASTEXITCODE -ne 0) {
        Write-Warning "Application Verifier settings could not be removed for $targetName (exit code $LASTEXITCODE)."
    }

    $verifierLogDirectory = Join-Path $env:USERPROFILE 'AppVerifierLogs'
    if (Test-Path -LiteralPath $verifierLogDirectory) {
        # Copy per-user verifier output into the workspace so the nightly artifact is independent of the runner account name.
        $verifierLogs = Get-ChildItem -LiteralPath $verifierLogDirectory -Force
        if ($verifierLogs.Count -ne 0) {
            New-Item -ItemType Directory -Path $LogOutputDirectory -Force | Out-Null
            Copy-Item -Path (Join-Path $verifierLogDirectory '*') -Destination $LogOutputDirectory -Recurse -Force
        }
    }
}
