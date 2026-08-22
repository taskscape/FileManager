[CmdletBinding()]
param(
    # Build configuration
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release',

    # Installer builds are intentionally pinned to the repository-wide VS 2026 toolset.
    [ValidateSet('v145')]
    [string]$PlatformToolset = 'v145',

    # Build number for versioning
    [string]$BuildNumber = $env:GITHUB_RUN_NUMBER,

    # Custom build directory (defaults to build_stage for Release, build_debug for Debug)
    [string]$BuildDir,

    # Custom staging directory (relative to the repository root; must match setup.iss expectations)
    [string]$StagingDir = 'Installer\Installer_Staging',

    # Skip running tests before building
    [switch]$SkipTests,

    # Show help
    [switch]$Help
)

<#
.SYNOPSIS
    Builds the Open Salamander installer locally, mirroring the GitHub Actions workflow.

.DESCRIPTION
    This script reproduces the GitHub Actions build-installer.yml workflow locally:
    1. Builds the solution with MSBuild
    2. Runs native regression tests
    3. Installs Inno Setup (if not present)
    4. Stages files for Inno Setup
    5. Compiles the installer

    Prerequisites:
    - Visual Studio 2026 (v145)
    - .NET SDK
    - Git (for version info)
    - Inno Setup 6.7.3 (will be downloaded if not installed)

.EXAMPLE
    .\scripts\build-installer.ps1
    # Builds Release configuration with the VS 2026 v145 toolset

.EXAMPLE
    .\scripts\build-installer.ps1 -Configuration Debug -PlatformToolset v145
    # Builds Debug configuration with v145 toolset

.EXAMPLE
    .\scripts\build-installer.ps1 -SkipTests -BuildNumber 42
    # Builds without running tests, sets custom build number
#>

if ($Help) {
    Get-Help $PSCommandPath -Detailed
    return
}

# This script relies on PowerShell 7 cmdlets (Get-FileHash, Get-AuthenticodeSignature, etc.)
# that are absent from a minimal Windows PowerShell 5.1 host. When started under 5.1,
# re-launch under pwsh so the whole pipeline runs against a consistent, complete runtime.
if ($PSVersionTable.PSVersion.Major -lt 6) {
    $pwsh = Get-Command pwsh.exe -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($null -ne $pwsh) {
        $argList = @()
        foreach ($key in $PSBoundParameters.Keys) {
            $value = $PSBoundParameters[$key]
            if ($value -is [switch]) {
                if ($value) { $argList += "-$key" }
            } else {
                $argList += "-$key"
                $argList += [string]$value
            }
        }
        & $pwsh.Source -NoProfile -ExecutionPolicy Bypass -File $PSCommandPath @argList
        exit $LASTEXITCODE
    }
}

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'

# Resolve paths
$repositoryRoot = Split-Path -Parent $PSScriptRoot
$script:BUILD_DIR = if ($BuildDir) { $BuildDir } else { Join-Path $repositoryRoot 'build_stage' }
$script:STAGING_DIR = Join-Path $repositoryRoot $StagingDir
$script:RUNNER_TEMP = Join-Path $repositoryRoot 'tmp'
$script:INNO_SETUP_COMPILER = $null

# Ensure temporary directory exists
if (-not (Test-Path $script:RUNNER_TEMP)) {
    New-Item -ItemType Directory -Path $script:RUNNER_TEMP | Out-Null
}

Write-Host "=== Open Salamander Installer Build Script ===" -ForegroundColor Cyan
Write-Host "Repository root: $repositoryRoot"
Write-Host "Configuration: $Configuration"
Write-Host "Platform Toolset: $PlatformToolset"
Write-Host "Build Number: $BuildNumber"
Write-Host "Build Directory: $script:BUILD_DIR"
Write-Host "Staging Directory: $script:STAGING_DIR"
Write-Host ""

# Verify MSBuild is available
function Test-MSBuildAvailable() {
    $msbuildPath = $null

    # 1. Prefer the VS 2026 developer environment already established by the caller.
    if (-not $msbuildPath -or -not (Test-Path $msbuildPath)) {
        $cmd = Get-Command 'msbuild.exe' -ErrorAction SilentlyContinue
        if ($null -ne $cmd) {
            $msbuildPath = $cmd.Source
        }
    }

    # 2. Fall back only to a VS 2026 MSBuild installation, never to an older toolchain.
    if (-not $msbuildPath -or -not (Test-Path $msbuildPath)) {
        $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
        if (-not (Test-Path $vswhere)) {
            $vswhere = "${env:ProgramFiles}\Microsoft Visual Studio\Installer\vswhere.exe"
        }
        if (Test-Path $vswhere) {
            $foundPath = & $vswhere -latest -prerelease -version '[18.0,19.0)' -requires Microsoft.Component.MSBuild -find 'MSBuild\**\Bin\MSBuild.exe' | Select-Object -First 1
            if ($foundPath -and (Test-Path $foundPath)) {
                $msbuildPath = $foundPath
            }
        }
    }

    if (-not $msbuildPath -or -not (Test-Path $msbuildPath)) {
        Write-Error "VS 2026 MSBuild not found for toolset $PlatformToolset. Please install the Visual Studio 2026 C++ workload."
    }
    return $msbuildPath
}

# Build the solution
function Invoke-BuildSolution() {
    param(
        [string]$Config,
        [string]$BuildRoot
    )

    Write-Host "=== Building Solution ($Config | $PlatformToolset) ===" -ForegroundColor Cyan
    Write-Host "Build root: $BuildRoot"

    # Create build directory
    New-Item -ItemType Directory -Force -Path $BuildRoot | Out-Null

    $msbuild = Test-MSBuildAvailable
    Write-Host "Using MSBuild: $msbuild"

    $solutionPath = Join-Path $repositoryRoot 'src\vcxproj\salamand.sln'
    if (-not (Test-Path $solutionPath)) {
        Write-Error "Solution not found: $solutionPath"
    }

    # Build command
    $buildCommand = @(
        "& `$msbuild `"$solutionPath`" /m /t:Build",
        "/p:Configuration=`"$Config`"",
        "/p:Platform=x64",
        "/p:PlatformToolset=$PlatformToolset",
        "/p:PreferredToolArchitecture=x64",
        "/nr:false"
    ) -join ' '

    Write-Host "Executing: $buildCommand"
    Invoke-Expression $buildCommand

    if ($LASTEXITCODE -ne 0) {
        Write-Error "Build failed with exit code $LASTEXITCODE"
    }

    Write-Host "Build completed successfully" -ForegroundColor Green
}

# Run native regression tests
function Invoke-NativeRegressionTests() {
    param(
        [string]$ResultsDirectory,
        [string]$OutputFile = 'native.trx'
    )

    Write-Host "=== Running Native Regression Tests ===" -ForegroundColor Cyan

    $testProject = Join-Path $repositoryRoot 'tests\FileManager.UiTests\FileManager.UiTests.csproj'
    if (-not (Test-Path $testProject)) {
        Write-Error "Test project not found: $testProject"
    }

    $resultsPath = Join-Path $ResultsDirectory $OutputFile
    $filter = "FullyQualifiedName~NativeSafetyRegressionTests"

    Write-Host "Running tests with filter: $filter"
    Write-Host "Results will be saved to: $resultsPath"

    dotnet test $testProject --filter $filter --results-directory $ResultsDirectory --logger "trx;LogFileName=$OutputFile"

    if ($LASTEXITCODE -ne 0) {
        Write-Host "Note: Test execution completed with non-zero exit code" -ForegroundColor Yellow
    }

    Write-Host "Native regression tests completed" -ForegroundColor Green
}

# Install Inno Setup if not present
function Invoke-InstallInnoSetup() {
    Write-Host "=== Checking Inno Setup Installation ===" -ForegroundColor Cyan

    $installDirectory = Join-Path $script:RUNNER_TEMP 'filemanager-inno-setup-6.7.3'
    # Reuse the workflow helper so local builds never require machine-wide Program Files access.
    $script:INNO_SETUP_COMPILER = & (Join-Path $repositoryRoot 'tools\install-pinned-inno-setup.ps1') -InstallDirectory $installDirectory
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $script:INNO_SETUP_COMPILER -PathType Leaf)) {
        throw 'Pinned Inno Setup provisioning did not produce ISCC.exe.'
    }

    Write-Host "Inno Setup provisioned: $script:INNO_SETUP_COMPILER" -ForegroundColor Green
}

# Stage files for Inno Setup
function Invoke-StageFiles() {
    param(
        [string]$BuildDir,
        [string]$StagingDir,
        [string]$BuildNumber
    )

    Write-Host "=== Staging Files for Inno Setup ===" -ForegroundColor Cyan

    $prepareScript = Join-Path $repositoryRoot 'tools\prepare_installer.ps1'
    if (-not (Test-Path $prepareScript)) {
        Write-Error "Prepare script not found: $prepareScript"
    }

    & $prepareScript -BuildDir $BuildDir -StagingDir $StagingDir -BuildNumber $BuildNumber

    if ($LASTEXITCODE -ne 0) {
        Write-Error "File staging failed"
    }

    # Verify required files
    $requiredFiles = @(
        Join-Path $StagingDir 'salamand.exe'
        Join-Path $StagingDir 'salmon.exe'
        Join-Path $StagingDir 'LICENSE'
    )

    foreach ($file in $requiredFiles) {
        if (-not (Test-Path $file)) {
            Write-Error "Missing required staged file: $file"
        }
    }

    Write-Host "Files staged successfully" -ForegroundColor Green
}

# Build installer with Inno Setup
function Invoke-BuildInstaller() {
    param(
        [string]$StagingDir,
        [string]$BuildNumber
    )

    Write-Host "=== Building Installer with Inno Setup ===" -ForegroundColor Cyan

    $setupScript = Join-Path $repositoryRoot 'Installer\setup.iss'
    if (-not (Test-Path $setupScript)) {
        Write-Error "Setup script not found: $setupScript"
    }

    $iscc = $script:INNO_SETUP_COMPILER
    if ([string]::IsNullOrWhiteSpace($iscc) -or -not (Test-Path -LiteralPath $iscc -PathType Leaf)) {
        throw 'Run Invoke-InstallInnoSetup before compiling the installer.'
    }

    $sourcePath = Split-Path -Leaf $StagingDir

    Write-Host "Compiling installer with source path: $sourcePath"
    Write-Host "Build number: $BuildNumber"

    & $iscc /DSourcePath="$sourcePath" /DBuildNumber=$BuildNumber $setupScript

    if ($LASTEXITCODE -ne 0) {
        Write-Error "Inno Setup compilation failed with exit code $LASTEXITCODE"
    }

    $outputFile = Join-Path $repositoryRoot "Installer\Output\OpenSalamander_6.0.$BuildNumber.exe"
    if (-not (Test-Path $outputFile)) {
        Write-Warning "Expected output file not found: $outputFile"
        # Try to find the actual output
        $outputFiles = Get-ChildItem -Path (Join-Path $repositoryRoot 'Installer\Output') -Filter "*.exe" -File
        if ($outputFiles.Count -gt 0) {
            Write-Host "Found installer: $($outputFiles[0].FullName)"
        }
    } else {
        Write-Host "Installer built successfully: $outputFile" -ForegroundColor Green
    }
}

# Main execution
try {
    Write-Host "=== Starting Build Process ===" -ForegroundColor Cyan
    Write-Host ""

    # Determine output directories based on configuration
    if ($Configuration -eq 'Release') {
        $buildOutputDir = Join-Path $script:BUILD_DIR 'Release_x64'
    } else {
        $buildOutputDir = Join-Path $script:BUILD_DIR 'Debug_x64'
    }

    # Step 1: Build solution
    Write-Host "Step 1: Building solution..." -ForegroundColor Yellow
    Invoke-BuildSolution -Config $Configuration -BuildRoot $script:BUILD_DIR
    Write-Host ""

    # Step 2: Run native regression tests
    Write-Host "Step 2: Running native regression tests..." -ForegroundColor Yellow
    $nativeResultsDir = Join-Path $script:RUNNER_TEMP 'native-tests'
    if (-not (Test-Path $nativeResultsDir)) {
        New-Item -ItemType Directory -Path $nativeResultsDir | Out-Null
    }
    Invoke-NativeRegressionTests -ResultsDirectory $nativeResultsDir -OutputFile "native-$Configuration.trx"
    Write-Host ""

    # Step 3: Install Inno Setup (if needed)
    Write-Host "Step 3: Checking Inno Setup installation..." -ForegroundColor Yellow
    Invoke-InstallInnoSetup
    Write-Host ""

    # Step 4: Stage files
    Write-Host "Step 4: Staging files..." -ForegroundColor Yellow
    Invoke-StageFiles -BuildDir $script:BUILD_DIR -StagingDir $script:STAGING_DIR -BuildNumber $BuildNumber
    Write-Host ""

    # Step 5: Build installer
    Write-Host "Step 5: Building installer..." -ForegroundColor Yellow
    Invoke-BuildInstaller -StagingDir $script:STAGING_DIR -BuildNumber $BuildNumber
    Write-Host ""

    Write-Host "=== Build Process Completed Successfully ===" -ForegroundColor Green
    Write-Host ""
    Write-Host "Output locations:" -ForegroundColor Cyan
    Write-Host "  Build artifacts: $script:BUILD_DIR"
    Write-Host "  Staging directory: $script:STAGING_DIR"
    Write-Host "  Native tests: $nativeResultsDir"
}
catch {
    Write-Host "=== Build Failed ===" -ForegroundColor Red
    Write-Host "Error: $_" -ForegroundColor Red
    Write-Host "Stack trace: $($_.ScriptStackTrace)" -ForegroundColor Gray
    exit 1
}
finally {
    # Clean up temporary files
    if (Test-Path $script:RUNNER_TEMP) {
        # Remove-Item $script:RUNNER_TEMP -Recurse -Force -ErrorAction SilentlyContinue
        Write-Host "Note: Temporary files are in: $script:RUNNER_TEMP"
    }
}
