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

function Test-FtpUiRuntime {
    param([Parameter(Mandatory = $true)][string]$ResolvedExecutable)

    $runtimeRoot = Split-Path -Parent $ResolvedExecutable
    $requiredFiles = @(
        (Join-Path $runtimeRoot 'salmon.exe'),
        (Join-Path $runtimeRoot 'plugins\ftp\ftp.spl'),
        (Join-Path $runtimeRoot 'plugins\ftp\lang\english.slg')
    )

    # FTP command IDs exist only after the plug-in loads, so an executable alone is not a valid runtime for the default UI suite.
    return @($requiredFiles | Where-Object { -not (Test-Path -LiteralPath $_ -PathType Leaf) }).Count -eq 0
}

function New-FtpUiTestRuntime {
    param(
        [Parameter(Mandatory = $true)][string]$ResolvedExecutable,
        [Parameter(Mandatory = $true)][string]$StagingRoot
    )

    if (Test-FtpUiRuntime $ResolvedExecutable) {
        return $ResolvedExecutable
    }

    $sourceRuntimeRoot = Split-Path -Parent $ResolvedExecutable
    $repositoryPrefix = ([IO.Path]::GetFullPath($repositoryRoot).TrimEnd('\') + '\')
    if (-not $ResolvedExecutable.StartsWith($repositoryPrefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw "The selected FileManager runtime is incomplete. Required files include salmon.exe, plugins\ftp\ftp.spl, and plugins\ftp\lang\english.slg beside: $ResolvedExecutable"
    }

    $configuration = Split-Path -Leaf $sourceRuntimeRoot
    $ftpBuildRoot = Join-Path $repositoryRoot "src\plugins\ftp\vcxproj\salamander\$configuration\plugins\ftp"
    $ftpPlugin = Join-Path $ftpBuildRoot 'ftp.spl'
    $ftpLanguage = Join-Path $ftpBuildRoot 'lang\english.slg'
    $crashReporter = Join-Path $sourceRuntimeRoot 'salmon.exe'
    $requiredBuildArtifacts = @($crashReporter, $ftpPlugin, $ftpLanguage)
    $missingBuildArtifacts = @($requiredBuildArtifacts |
        Where-Object { -not (Test-Path -LiteralPath $_ -PathType Leaf) })
    if ($missingBuildArtifacts.Count -ne 0) {
        throw "The checkout build is incomplete. Build the complete $configuration solution before running UI tests. Missing: $($missingBuildArtifacts -join ', ')"
    }

    try {
        New-Item -ItemType Directory -Path $StagingRoot -Force | Out-Null
        foreach ($fileName in @('salamand.exe', 'salmon.exe', 'salbroker.exe')) {
            $sourceFile = Join-Path $sourceRuntimeRoot $fileName
            if (Test-Path -LiteralPath $sourceFile -PathType Leaf) {
                Copy-Item -LiteralPath $sourceFile -Destination $StagingRoot -Force
            }
        }
        foreach ($directoryName in @('lang', 'toolbars', 'utils')) {
            $sourceDirectory = Join-Path $sourceRuntimeRoot $directoryName
            if (Test-Path -LiteralPath $sourceDirectory -PathType Container) {
                Copy-Item -LiteralPath $sourceDirectory -Destination $StagingRoot -Recurse -Force
            }
        }

        $stagedFtpRoot = Join-Path $StagingRoot 'plugins\ftp'
        $stagedFtpLanguageRoot = Join-Path $stagedFtpRoot 'lang'
        New-Item -ItemType Directory -Path $stagedFtpLanguageRoot -Force | Out-Null
        # Stage only runtime payloads from the matching checkout configuration; build intermediates must not leak into the test installation.
        Copy-Item -LiteralPath $ftpPlugin -Destination $stagedFtpRoot -Force
        Copy-Item -LiteralPath $ftpLanguage -Destination $stagedFtpLanguageRoot -Force

        $stagedExecutable = Join-Path $StagingRoot 'salamand.exe'
        if (-not (Test-FtpUiRuntime $stagedExecutable)) {
            throw "The temporary FileManager runtime could not be staged completely below: $StagingRoot"
        }
        return $stagedExecutable
    }
    catch {
        if (Test-Path -LiteralPath $StagingRoot -PathType Container) {
            Remove-Item -LiteralPath $StagingRoot -Recurse -Force
        }
        throw
    }
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

$sandboxParent = Join-Path ([IO.Path]::GetTempPath()) ('OpenSalamanderUiTests-' + [Guid]::NewGuid().ToString('N'))
$testDataRoot = Join-Path $sandboxParent 'filemanager-testdata'
$runtimeStagingRoot = Join-Path $sandboxParent 'runtime'
$resolvedExecutable = Resolve-FileManagerExecutable $ExecutablePath
$ftpRuntimeRequired = [string]::IsNullOrWhiteSpace($Filter) -or
    $Filter -match '(?i)(TestCategory\s*=\s*UI|BasicUiTests|UI_007|Ftp|Quick_connect)'
if ($ftpRuntimeRequired) {
    # Raw Visual Studio output scatters plug-ins by project; use a disposable coherent runtime without modifying the caller's build tree.
    $resolvedExecutable = New-FtpUiTestRuntime -ResolvedExecutable $resolvedExecutable -StagingRoot $runtimeStagingRoot
}
elseif (-not (Test-Path -LiteralPath (Join-Path (Split-Path -Parent $resolvedExecutable) 'salmon.exe') -PathType Leaf)) {
    # Every UI launch needs its sibling crash reporter even when the selected fixture does not exercise FTP.
    throw "The selected FileManager runtime is incomplete; salmon.exe is missing beside: $resolvedExecutable"
}
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

    if (Test-Path -LiteralPath $runtimeStagingRoot -PathType Container) {
        # The temporary runtime contains only files copied by this invocation below its GUID-owned parent.
        Remove-Item -LiteralPath $runtimeStagingRoot -Recurse -Force
    }

    # NUnit removes the marked child on a healthy run; remove only the unique empty parent created above.
    if ((Test-Path -LiteralPath $sandboxParent -PathType Container) -and
        @(Get-ChildItem -LiteralPath $sandboxParent -Force).Count -eq 0) {
        Remove-Item -LiteralPath $sandboxParent
    }
}

exit $testExitCode
