[CmdletBinding()]
param(
    [string]$ExecutablePath,
    [string]$Filter = 'TestCategory=UI',
    [string]$Logger = 'console;verbosity=normal',
    [switch]$NoBuild,
    [switch]$ListTests,
    [switch]$ConfirmIsolatedProfile,
    [string[]]$AdditionalDotNetArguments = @()
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repositoryRoot = $PSScriptRoot
$testProject = Join-Path $repositoryRoot 'tests\FileManager.UiTests\FileManager.UiTests.csproj'

function Resolve-FileManagerExecutable {
    param([string]$RequestedPath)

    if (-not [string]::IsNullOrWhiteSpace($RequestedPath)) {
        # An explicit selection is authoritative so a typo cannot silently run a different executable.
        if (-not (Test-Path -LiteralPath $RequestedPath -PathType Leaf)) {
            throw "The requested FileManager executable was not found: $RequestedPath"
        }
        return (Resolve-Path -LiteralPath $RequestedPath).Path
    }

    $candidates = [System.Collections.Generic.List[string]]::new()
    if (-not [string]::IsNullOrWhiteSpace($env:FILEMANAGER_UI_EXE)) {
        # Preserve an explicit caller selection before checking this checkout's standard build locations.
        $candidates.Add($env:FILEMANAGER_UI_EXE)
    }
    foreach ($relativePath in @(
        'src\vcxproj\salamander\Debug_x64\salamand.exe',
        'src\vcxproj\salamander\Release_x64\salamand.exe',
        'src\vcxproj\salamander\Debug_x86\salamand.exe',
        'src\vcxproj\salamander\Release_x86\salamand.exe'
    )) {
        # Checkout-relative discovery remains portable across users, drive letters, and repository locations.
        $candidates.Add((Join-Path $repositoryRoot $relativePath))
    }

    foreach ($candidate in $candidates) {
        if (-not [string]::IsNullOrWhiteSpace($candidate) -and
            (Test-Path -LiteralPath $candidate -PathType Leaf)) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }

    # Do not silently fall back to an installed copy: a stale executable makes every native-command test fail misleadingly.
    throw 'No checkout salamand.exe was found. Build the current branch or pass -ExecutablePath explicitly.'
}

if (-not $ConfirmIsolatedProfile) {
    # Explicit acknowledgement keeps interactive UI work out of a normal unattended desktop session.
    throw 'Re-run with -ConfirmIsolatedProfile to use the guarded filesystem and registry test sandbox.'
}
if (-not (Test-Path -LiteralPath $testProject -PathType Leaf)) {
    throw "The UI test project was not found: $testProject"
}
if ($null -eq (Get-Command dotnet -ErrorAction SilentlyContinue)) {
    throw 'dotnet was not found on PATH.'
}

$resolvedExecutable = Resolve-FileManagerExecutable $ExecutablePath
$executableDirectory = Split-Path -Parent $resolvedExecutable
$crashReporter = Join-Path $executableDirectory 'salmon.exe'
# Fail once in preflight instead of letting every UI case stall on the application's missing-reporter dialog.
if (-not (Test-Path -LiteralPath $crashReporter -PathType Leaf)) {
    throw "The selected test artifact is incomplete; salmon.exe is missing beside salamand.exe: $crashReporter"
}

$sandboxParent = Join-Path ([IO.Path]::GetTempPath()) ('OpenSalamanderUiTests-' + [Guid]::NewGuid().ToString('N'))
$testDataRoot = Join-Path $sandboxParent 'filemanager-testdata'
$configurationRoot = 'Software\Open Salamander\6.0-filemanager-testdata'
$savedEnvironment = @{
    FILEMANAGER_UI_ISOLATED = $env:FILEMANAGER_UI_ISOLATED
    FILEMANAGER_UI_EXE = $env:FILEMANAGER_UI_EXE
    FILEMANAGER_UI_TESTDATA_ROOT = $env:FILEMANAGER_UI_TESTDATA_ROOT
    FILEMANAGER_UI_CONFIG_ROOT = $env:FILEMANAGER_UI_CONFIG_ROOT
}
$testExitCode = 1

try {
    $env:FILEMANAGER_UI_ISOLATED = '1'
    $env:FILEMANAGER_UI_EXE = $resolvedExecutable
    $env:FILEMANAGER_UI_TESTDATA_ROOT = $testDataRoot
    $env:FILEMANAGER_UI_CONFIG_ROOT = $configurationRoot

    $arguments = [System.Collections.Generic.List[string]]::new()
    $arguments.Add('test')
    $arguments.Add($testProject)
    if ($NoBuild) {
        # A no-build rerun also avoids restore, which would regenerate ignored NuGet intermediates.
        $arguments.Add('--no-build')
        $arguments.Add('--no-restore')
    }
    if ($ListTests) {
        $arguments.Add('--list-tests')
    }
    if (-not [string]::IsNullOrWhiteSpace($Filter)) {
        $arguments.Add('--filter')
        $arguments.Add($Filter)
    }
    if (-not [string]::IsNullOrWhiteSpace($Logger)) {
        $arguments.Add('--logger')
        $arguments.Add($Logger)
    }
    foreach ($argument in $AdditionalDotNetArguments) {
        $arguments.Add($argument)
    }

    Write-Host 'Running FileManager UI tests'
    Write-Host "  Project:    $testProject"
    Write-Host "  Executable: $resolvedExecutable"
    Write-Host "  Sandbox:    $testDataRoot"
    Write-Host "  Filter:     $Filter"

    Push-Location $repositoryRoot
    try {
        & dotnet @($arguments.ToArray())
        $testExitCode = $LASTEXITCODE
    }
    finally {
        Pop-Location
    }
}
finally {
    # Restore the caller's shell exactly so focused reruns cannot contaminate later commands.
    foreach ($name in $savedEnvironment.Keys) {
        $value = $savedEnvironment[$name]
        if ($null -eq $value) {
            Remove-Item -Path "Env:$name" -ErrorAction SilentlyContinue
        }
        else {
            Set-Item -Path "Env:$name" -Value $value
        }
    }

    # NUnit removes the marked child on a healthy run; remove only the unique empty parent created above.
    if ((Test-Path -LiteralPath $sandboxParent -PathType Container) -and
        @(Get-ChildItem -LiteralPath $sandboxParent -Force).Count -eq 0) {
        Remove-Item -LiteralPath $sandboxParent
    }
}

exit $testExitCode
