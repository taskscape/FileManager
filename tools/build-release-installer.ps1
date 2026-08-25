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
    # Keep standalone local installer builds serialized; the workflow passes its reviewed parallel budget explicitly.
    [ValidateRange(1, 16)]
    [int]$MaxBuildNodes = 1,
    [string]$SymbolIndexPath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repositoryRoot = Split-Path -Parent $PSScriptRoot
$solutionPath = Join-Path $repositoryRoot 'src\vcxproj\salamand.sln'
$releaseBuildRoot = [IO.Path]::GetFullPath($BuildDirectory).TrimEnd('\') + '\'
$installerStagingRoot = [IO.Path]::GetFullPath($InstallerStagingDirectory)
$installerRoot = [IO.Path]::GetFullPath((Join-Path $repositoryRoot 'Installer'))
if (-not [string]::Equals((Split-Path -Parent $installerStagingRoot), $installerRoot, [StringComparison]::OrdinalIgnoreCase)) {
    # setup.iss resolves SourcePath relative to its own directory, so the isolated staging tree must remain its direct child.
    throw "InstallerStagingDirectory must be a direct child of ${installerRoot}: $installerStagingRoot"
}
$innoProvisioner = Join-Path $repositoryRoot 'tools\install-pinned-inno-setup.ps1'
$innoCompiler = $null
if ([string]::IsNullOrWhiteSpace($SymbolIndexPath)) {
    # Keep the CI symbol inventory in RUNNER_TEMP while giving local parity runs a disposable fallback.
    $symbolRoot = if ([string]::IsNullOrWhiteSpace($env:RUNNER_TEMP)) { Join-Path $releaseBuildRoot 'Intermediate' } else { $env:RUNNER_TEMP }
    $SymbolIndexPath = Join-Path $symbolRoot 'release-symbol-index.json'
}

$previousBuildRoot = $env:OPENSAL_BUILD_DIR
$previousClMpCount = $env:CL_MPCount
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
    # The solution's /MP projects share this limit with MSBuild to prevent an unbounded compiler-process burst.
    $env:CL_MPCount = [string]$MaxBuildNodes

    & msbuild $solutionPath /m:$MaxBuildNodes /t:Build /p:Configuration=Release /p:Platform=x64 /p:PlatformToolset=$PlatformToolset /p:PreferredToolArchitecture=x64
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

    # Persist the child transcript in the build tree so a staging failure remains diagnosable after the aggregate runner returns.
    $prepareInstallerLogPath = Join-Path $releaseBuildRoot 'prepare-installer.log'
    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $repositoryRoot 'tools\prepare_installer.ps1') `
        -BuildDir $releaseBuildRoot -StagingDir $installerStagingRoot -BuildNumber $BuildNumber 2>&1 |
        Tee-Object -LiteralPath $prepareInstallerLogPath
    $prepareInstallerExitCode = $LASTEXITCODE
    if ($prepareInstallerExitCode -ne 0) {
        throw "Preparing installer files failed with exit code $prepareInstallerExitCode. See $prepareInstallerLogPath"
    }
    $requiredFiles = @('salamand.exe', 'salmon.exe', 'LICENSE') | ForEach-Object { Join-Path $installerStagingRoot $_ }
    $missingFiles = @($requiredFiles | Where-Object { -not (Test-Path -LiteralPath $_ -PathType Leaf) })
    if ($missingFiles.Count -ne 0) {
        throw "Installer staging is incomplete: $($missingFiles -join ', ')"
    }

    # Match the pipeline's native PowerShell argument form; Inno resolves this leaf beside setup.iss.
    $sourcePath = Split-Path -Leaf $installerStagingRoot
    # Preserve Inno's output beside the staged inputs for the same post-cleanup diagnosis guarantee.
    $installerCompileLogPath = Join-Path $releaseBuildRoot 'installer-compile.log'
    & $innoCompiler ('/DSourcePath=' + $sourcePath) ('/DBuildNumber=' + $BuildNumber) (Join-Path $repositoryRoot 'Installer\setup.iss') 2>&1 |
        Tee-Object -LiteralPath $installerCompileLogPath
    $installerCompileExitCode = $LASTEXITCODE
    if ($installerCompileExitCode -ne 0) {
        throw "Building the installer failed with exit code $installerCompileExitCode. See $installerCompileLogPath"
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
    if ($null -eq $previousClMpCount) {
        Remove-Item Env:CL_MPCount -ErrorAction SilentlyContinue
    }
    else {
        $env:CL_MPCount = $previousClMpCount
    }
}
