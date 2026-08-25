# SPDX-FileCopyrightText: 2026 Taskscape Ltd
# SPDX-License-Identifier: GPL-2.0-or-later

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$BuildDirectory,
    [Parameter(Mandatory = $true)]
    [string]$InstallerStagingDirectory,
    [Parameter(Mandatory = $true)]
    [ValidatePattern('^\d+$')]
    [string]$BuildNumber,
    [ValidateSet('v145')]
    [string]$PlatformToolset = 'v145',
    [string]$SymbolIndexPath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repositoryRoot = Split-Path -Parent $PSScriptRoot
$solutionPath = Join-Path $repositoryRoot 'src\vcxproj\salamand.sln'
$releaseBuildRoot = [IO.Path]::GetFullPath($BuildDirectory).TrimEnd('\') + '\'
$installerStagingRoot = [IO.Path]::GetFullPath($InstallerStagingDirectory)
$innoProvisioner = Join-Path $repositoryRoot 'tools\install-pinned-inno-setup.ps1'
$innoCompiler = $null
if ([string]::IsNullOrWhiteSpace($SymbolIndexPath)) {
    # Keep the CI symbol inventory in RUNNER_TEMP while giving local parity runs a disposable fallback.
    $symbolRoot = if ([string]::IsNullOrWhiteSpace($env:RUNNER_TEMP)) { Join-Path $releaseBuildRoot 'Intermediate' } else { $env:RUNNER_TEMP }
    $SymbolIndexPath = Join-Path $symbolRoot 'release-symbol-index.json'
}

$previousBuildRoot = $env:OPENSAL_BUILD_DIR
try {
    # The staging helper uses repository-relative inputs, so both CI and local parity execute from the same root.
    Push-Location $repositoryRoot
    # Provision the external compiler before the long Release build so packaging failures surface as a fast prerequisite failure.
    $innoCompiler = & $innoProvisioner
    if (-not (Test-Path -LiteralPath $innoCompiler -PathType Leaf)) {
        throw 'Pinned Inno Setup preflight did not produce ISCC.exe.'
    }
    New-Item -ItemType Directory -Force -Path $releaseBuildRoot | Out-Null
    # Project property sheets consume this exact environment variable; do not override OutDir for local parity.
    $env:OPENSAL_BUILD_DIR = $releaseBuildRoot

    & msbuild $solutionPath /m /t:Build /p:Configuration=Release /p:Platform=x64 /p:PlatformToolset=$PlatformToolset /p:PreferredToolArchitecture=x64
    if ($LASTEXITCODE -ne 0) {
        throw "Building the Release x64 FileManager solution failed with exit code $LASTEXITCODE."
    }

    $releaseArtifactRoot = Join-Path $releaseBuildRoot 'salamander\Release_x64'
    # Keep the installer-bound PE audit immediately after the exact Release build that produced its inputs.
    & (Join-Path $repositoryRoot 'tools\audit-pe-hardening.ps1') -BuildRoot $releaseArtifactRoot
    if ($LASTEXITCODE -ne 0) {
        throw "Release PE hardening audit failed with exit code $LASTEXITCODE."
    }

    & dotnet test (Join-Path $repositoryRoot 'tests\FileManager.UiTests\FileManager.UiTests.csproj') --filter 'FullyQualifiedName~NativeSafetyRegressionTests'
    if ($LASTEXITCODE -ne 0) {
        throw "The v145 native regression subset failed with exit code $LASTEXITCODE."
    }

    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $SymbolIndexPath) | Out-Null
    # Index the exact Release outputs before staging so symbol verification remains a Build Installer gate.
    & (Join-Path $repositoryRoot 'tools\new-symbol-index.ps1') -BuildRoot $releaseBuildRoot -OutputPath $SymbolIndexPath
    if ($LASTEXITCODE -ne 0) {
        throw "Generating the release symbol index failed with exit code $LASTEXITCODE."
    }
    & (Join-Path $repositoryRoot 'tools\test-symbol-index.ps1') -BuildRoot $releaseBuildRoot -IndexPath $SymbolIndexPath
    if ($LASTEXITCODE -ne 0) {
        throw "Verifying the release symbol index failed with exit code $LASTEXITCODE."
    }

    # Use the same Windows PowerShell staging command and required-file gate as the Actions job.
    & powershell.exe -File (Join-Path $repositoryRoot 'tools\prepare_installer.ps1') -BuildDir $releaseBuildRoot -StagingDir $installerStagingRoot -BuildNumber $BuildNumber
    if ($LASTEXITCODE -ne 0) {
        throw "Preparing installer files failed with exit code $LASTEXITCODE."
    }
    $requiredFiles = @('salamand.exe', 'salmon.exe', 'LICENSE') | ForEach-Object { Join-Path $installerStagingRoot $_ }
    $missingFiles = @($requiredFiles | Where-Object { -not (Test-Path -LiteralPath $_ -PathType Leaf) })
    if ($missingFiles.Count -ne 0) {
        throw "Installer staging is incomplete: $($missingFiles -join ', ')"
    }

    # Match the pipeline's native PowerShell argument form; Inno resolves this leaf beside setup.iss.
    $sourcePath = Split-Path -Leaf $installerStagingRoot
    # Compile the same setup script with the same source and build-number definitions used by Actions.
    & $innoCompiler ('/DSourcePath=' + $sourcePath) ('/DBuildNumber=' + $BuildNumber) (Join-Path $repositoryRoot 'Installer\setup.iss')
    if ($LASTEXITCODE -ne 0) {
        throw "Building the installer failed with exit code $LASTEXITCODE."
    }

    $installerPath = Join-Path $repositoryRoot ('Installer\Output\OpenSalamander_6.0.' + $BuildNumber + '.exe')
    if (-not (Test-Path -LiteralPath $installerPath -PathType Leaf)) {
        throw "The expected installer was not produced: $installerPath"
    }
}
finally {
    Pop-Location
    if ($null -eq $previousBuildRoot) {
        Remove-Item Env:OPENSAL_BUILD_DIR -ErrorAction SilentlyContinue
    }
    else {
        $env:OPENSAL_BUILD_DIR = $previousBuildRoot
    }
}
