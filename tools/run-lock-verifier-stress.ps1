[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string] $ExecutablePath,
    [Parameter(Mandatory = $true)]
    [string] $TestProject,
    [Parameter(Mandatory = $true)]
    [string] $AppVerifierPath,
    [Parameter(Mandatory = $true)]
    [string] $LogOutputDirectory
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

try {
    # Lock checks are process-persistent settings, so always remove them even when the stress suite fails.
    & $AppVerifierPath -enable Locks -for $targetName
    if ($LASTEXITCODE -ne 0) {
        throw "Application Verifier failed to enable Locks for $targetName (exit code $LASTEXITCODE)."
    }

    # The tagged UI loop repeatedly starts and restarts the native process under the isolated test profile.
    dotnet test $TestProject --filter 'TestCategory=LockStress' -- NUnit.NumberOfTestWorkers=0
    if ($LASTEXITCODE -ne 0) {
        throw "The Application Verifier lock stress suite failed (exit code $LASTEXITCODE)."
    }
}
finally {
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
