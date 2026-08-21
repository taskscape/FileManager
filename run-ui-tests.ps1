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

    $candidates = [System.Collections.Generic.List[string]]::new()
    if (-not [string]::IsNullOrWhiteSpace($RequestedPath)) {
        # An explicit path is authoritative so a typo cannot silently test a different checkout build.
        if (-not (Test-Path -LiteralPath $RequestedPath -PathType Leaf)) {
            throw "The requested FileManager executable was not found: $RequestedPath"
        }
        return (Resolve-Path -LiteralPath $RequestedPath).Path
    }
    if (-not [string]::IsNullOrWhiteSpace($env:FILEMANAGER_UI_EXE)) {
        # Preserve an explicit caller selection before trying checkout-relative or installed locations.
        $candidates.Add($env:FILEMANAGER_UI_EXE)
    }

    # Repository-relative candidates make the runner portable across drive letters and checkout directories.
    foreach ($relativePath in @(
        'src\vcxproj\salamander\Debug_x64\salamand.exe',
        'src\vcxproj\salamander\Release_x64\salamand.exe',
        'src\vcxproj\salamander\Debug_x86\salamand.exe',
        'src\vcxproj\salamander\Release_x86\salamand.exe'
    )) {
        $candidates.Add((Join-Path $repositoryRoot $relativePath))
    }

    foreach ($appPathKey in @(
        'HKCU:\Software\Microsoft\Windows\CurrentVersion\App Paths\salamand.exe',
        'HKLM:\Software\Microsoft\Windows\CurrentVersion\App Paths\salamand.exe',
        'HKLM:\Software\WOW6432Node\Microsoft\Windows\CurrentVersion\App Paths\salamand.exe'
    )) {
        if (Test-Path -LiteralPath $appPathKey) {
            # App Paths is the authoritative Windows registration when Salamander was installed outside this checkout.
            $registeredPath = (Get-Item -LiteralPath $appPathKey).GetValue('')
            if (-not [string]::IsNullOrWhiteSpace($registeredPath)) {
                $candidates.Add($registeredPath)
            }
        }
    }

    foreach ($candidate in $candidates) {
        if (-not [string]::IsNullOrWhiteSpace($candidate) -and
            (Test-Path -LiteralPath $candidate -PathType Leaf)) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }

    throw 'salamand.exe was not found. Build Debug x64 or pass -ExecutablePath with the installed application path.'
}

if (-not $ConfirmIsolatedProfile) {
    # The application writes HKCU configuration, so require an explicit acknowledgement before enabling the test gate.
    throw 'UI tests modify the current Windows profile. Re-run with -ConfirmIsolatedProfile under a dedicated disposable test account.'
}
if (-not (Test-Path -LiteralPath $testProject -PathType Leaf)) {
    throw "The UI test project was not found: $testProject"
}
if ($null -eq (Get-Command dotnet -ErrorAction SilentlyContinue)) {
    throw 'dotnet was not found on PATH.'
}

$resolvedExecutable = Resolve-FileManagerExecutable $ExecutablePath
$previousIsolated = $env:FILEMANAGER_UI_ISOLATED
$previousExecutable = $env:FILEMANAGER_UI_EXE
$testExitCode = 1

try {
    $env:FILEMANAGER_UI_ISOLATED = '1'
    $env:FILEMANAGER_UI_EXE = $resolvedExecutable

    $arguments = [System.Collections.Generic.List[string]]::new()
    $arguments.Add('test')
    $arguments.Add($testProject)
    if ($NoBuild) {
        # A no-build rerun must also avoid restore, which would regenerate ignored NuGet obj metadata unnecessarily.
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

    Write-Host "Running FileManager UI tests"
    Write-Host "  Project:    $testProject"
    Write-Host "  Executable: $resolvedExecutable"
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
    # Restore the caller's process environment so focused reruns do not affect later shell commands.
    $env:FILEMANAGER_UI_ISOLATED = $previousIsolated
    $env:FILEMANAGER_UI_EXE = $previousExecutable
}

exit $testExitCode
