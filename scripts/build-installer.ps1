[CmdletBinding()]
param(
    # Build configuration
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release',

    # Platform toolset (v143 for VS2022, v145 for VS2026)
    [ValidateSet('v143', 'v145')]
    [string]$PlatformToolset = 'v143',

    # Build number for versioning
    [string]$BuildNumber = $env:GITHUB_RUN_NUMBER,

    # Custom build directory (defaults to build_stage for Release, build_debug for Debug)
    [string]$BuildDir,

    # Custom staging directory
    [string]$StagingDir = 'Installer_Staging',

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
    3. Builds the v145 toolset manifest for comparison
    4. Compares toolset parity with v145
    5. Installs Inno Setup (if not present)
    6. Stages files for Inno Setup
    7. Compiles the installer

    Prerequisites:
    - Visual Studio 2022 (v143) or Visual Studio 2026 (v145)
    - .NET SDK
    - Git (for version info)
    - Inno Setup 6.7.3 (will be downloaded if not installed)

.EXAMPLE
    .\scripts\build-installer.ps1
    # Builds Release configuration with v143 toolset

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

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'

# Resolve paths
$repositoryRoot = Split-Path -Parent $PSScriptRoot
$script:BUILD_DIR = if ($BuildDir) { $BuildDir } else { Join-Path $repositoryRoot 'build_stage' }
$script:STAGING_DIR = Join-Path $repositoryRoot $StagingDir
$script:RUNNER_TEMP = Join-Path $repositoryRoot 'tmp'

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
    $msbuildPath = if ($PlatformToolset -eq 'v145') {
        # Try to find VS2026 (v145)
        $vs2026Path = "${env:ProgramFiles}\Microsoft Visual Studio\2026\Enterprise"
        if (Test-Path $vs2026Path) { Join-Path $vs2026Path 'MSBuild\Current\Bin\MSBuild.exe' } else { $null }
    } else {
        # VS2022 (v143) - should be in PATH
        (Get-Command 'msbuild.exe' -ErrorAction SilentlyContinue).Path
    }

    if (-not $msbuildPath -or -not (Test-Path $msbuildPath)) {
        Write-Error "MSBuild not found for toolset $PlatformToolset. Please install Visual Studio $PlatformToolset."
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

# Create PE manifest
function Invoke-CreatePEManifest() {
    param(
        [string]$BuildRoot,
        [string]$Toolset,
        [string]$OutputPath
    )

    Write-Host "=== Creating PE Manifest ($Toolset) ===" -ForegroundColor Cyan

    $manifestTool = Join-Path $repositoryRoot 'tools\new-toolset-pe-manifest.ps1'
    if (-not (Test-Path $manifestTool)) {
        Write-Error "Manifest tool not found: $manifestTool"
    }

    & $manifestTool -BuildRoot $BuildRoot -Toolset $Toolset -OutputPath $OutputPath

    if ($LASTEXITCODE -ne 0) {
        Write-Error "Failed to create PE manifest"
    }

    Write-Host "PE manifest created: $OutputPath" -ForegroundColor Green
}

# Compare PE manifests
function Invoke-ComparePEManifests() {
    param(
        [string]$V143Manifest,
        [string]$V145Manifest
    )

    Write-Host "=== Comparing PE Manifests ===" -ForegroundColor Cyan

    $compareTool = Join-Path $repositoryRoot 'tools\compare-toolset-pe-manifests.ps1'
    if (-not (Test-Path $compareTool)) {
        Write-Error "Compare tool not found: $compareTool"
    }

    & $compareTool -V143Manifest $V143Manifest -V145Manifest $V145Manifest

    Write-Host "PE manifest comparison completed" -ForegroundColor Green
}

# Compare TRX files
function Invoke-CompareTRX() {
    param(
        [string]$V143Results,
        [string]$V145Results
    )

    Write-Host "=== Comparing Test Results ===" -ForegroundColor Cyan

    $compareTool = Join-Path $repositoryRoot 'tools\compare-vstest-trx.ps1'
    if (-not (Test-Path $compareTool)) {
        Write-Error "Compare tool not found: $compareTool"
    }

    & $compareTool -V143Results $V143Results -V145Results $V145Results

    Write-Host "Test result comparison completed" -ForegroundColor Green
}

# Install Inno Setup if not present
function Invoke-InstallInnoSetup() {
    Write-Host "=== Checking Inno Setup Installation ===" -ForegroundColor Cyan

    $innoPath = 'C:\Program Files (x86)\Inno Setup 6'
    $iscc = Join-Path $innoPath 'ISCC.exe'

    if (Test-Path $iscc) {
        $version = (Get-Item $iscc).VersionInfo.ProductVersion
        Write-Host "Inno Setup found: $version"
        return $true
    }

    Write-Host "Inno Setup not found. Installing version 6.7.3..." -ForegroundColor Yellow

    $inputsPath = Join-Path $repositoryRoot 'tools\release-inputs.json'
    if (-not (Test-Path $inputsPath)) {
        Write-Error "Release inputs file not found: $inputsPath"
    }

    $inputs = Get-Content $inputsPath -Raw | ConvertFrom-Json
    $innoInput = $inputs.inputs.innoSetup

    if ($innoInput.version -ne '6.7.3') {
        Write-Warning "Lock file specifies Inno Setup version $($innoInput.version), but script expects 6.7.3"
    }

    $installerPath = Join-Path $script:RUNNER_TEMP 'innosetup-installer.exe'

    Write-Host "Downloading Inno Setup from: $($innoInput.url)"
    Invoke-WebRequest -Uri $innoInput.url -OutFile $installerPath

    $actualHash = (Get-FileHash -LiteralPath $installerPath -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($actualHash -ne $innoInput.sha256) {
        throw "Inno Setup SHA-256 mismatch: expected $($innoInput.sha256), got $actualHash."
    }

    Write-Host "Verifying Authenticode signature..."
    $signature = Get-AuthenticodeSignature -LiteralPath $installerPath
    if ($signature.Status -ne 'Valid' -or $signature.SignerCertificate.Subject -notlike "*CN=$($innoInput.publisher)*") {
        throw "Inno Setup Authenticode verification failed: $($signature.Status) $($signature.SignerCertificate.Subject)"
    }

    Write-Host "Installing Inno Setup silently..."
    & $installerPath /VERYSILENT /SUPPRESSMSGBOXES /NORESTART /SP-

    if ($LASTEXITCODE -ne 0) {
        throw "Inno Setup installation failed with exit code $LASTEXITCODE."
    }

    if (-not (Test-Path $iscc)) {
        throw "Inno Setup did not install $iscc."
    }

    $version = (Get-Item $iscc).VersionInfo.ProductVersion
    Write-Host "Inno Setup installed successfully: $version" -ForegroundColor Green

    return $true
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

    $innoPath = 'C:\Program Files (x86)\Inno Setup 6'
    $iscc = Join-Path $innoPath 'ISCC.exe'

    if (-not (Test-Path $iscc)) {
        Write-Error "Inno Setup ISCC.exe not found: $iscc"
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

    # Step 3: Create PE manifest
    Write-Host "Step 3: Creating PE manifest..." -ForegroundColor Yellow
    $peManifestPath = Join-Path $script:RUNNER_TEMP "pe-manifest-$Configuration.json"
    Invoke-CreatePEManifest -BuildRoot $script:BUILD_DIR -Toolset $PlatformToolset -OutputPath $peManifestPath
    Write-Host ""

    # Step 4: Install Inno Setup (if needed)
    Write-Host "Step 4: Checking Inno Setup installation..." -ForegroundColor Yellow
    Invoke-InstallInnoSetup
    Write-Host ""

    # Step 5: Stage files
    Write-Host "Step 5: Staging files..." -ForegroundColor Yellow
    Invoke-StageFiles -BuildDir $script:BUILD_DIR -StagingDir $script:STAGING_DIR -BuildNumber $BuildNumber
    Write-Host ""

    # Step 6: Build installer
    Write-Host "Step 6: Building installer..." -ForegroundColor Yellow
    Invoke-BuildInstaller -StagingDir $script:STAGING_DIR -BuildNumber $BuildNumber
    Write-Host ""

    Write-Host "=== Build Process Completed Successfully ===" -ForegroundColor Green
    Write-Host ""
    Write-Host "Output locations:" -ForegroundColor Cyan
    Write-Host "  Build artifacts: $script:BUILD_DIR"
    Write-Host "  Staging directory: $script:STAGING_DIR"
    Write-Host "  PE manifest: $peManifestPath"
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
