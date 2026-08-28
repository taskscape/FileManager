[CmdletBinding()]
param(
    [string]$BaseCommit,
    [string]$SqliteDll,
    [switch]$FailOnSkipped,
    [switch]$KeepBuildArtifacts,
    # The release workflow uses the host's fixed D: volume when it is available instead of provisioning test media.
    [switch]$SkipLockVerifier,
    # Keep external-server tests out of the blocking inventory; their launcher opts in with the LiveFtp category.
    [string]$NUnitFilter = 'TestCategory!=Quarantined&TestCategory!=LiveFtp',
    # The complete UI harness must match the repository-wide VS 2026 compiler contract.
    [ValidateSet('v145')]
    [string]$PlatformToolset = 'v145',
    # Local VS linkers can fail while concurrent resource DLLs share base-address state; CI supplies its reviewed node count explicitly.
    [ValidateRange(1, 16)]
    [int]$MaxBuildNodes = 1,
    [string]$NUnitTrxPath,
    # Run the local equivalent of both release-test and installer-build jobs, excluding the GitHub-only publish job.
    [switch]$ReleasePipeline,
    # CI callers can explicitly retain the ordinary test inventory; local invocations default to release parity.
    [switch]$NoReleasePipeline,
    # Inspect the blocking release environment without changing disks, building binaries, or changing installed tools.
    [switch]$PrerequisiteOnly,
    # GitHub supplies its monotonically increasing run number; local pipeline runs use 0 unless callers provide one.
    [string]$BuildNumber = $env:GITHUB_RUN_NUMBER
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# A plain local invocation must provision the same deterministic UI capabilities as the release workflow.
if ([string]::IsNullOrWhiteSpace($env:FILEMANAGER_UI_ISOLATED)) {
    $env:FILEMANAGER_UI_ISOLATED = '1'
}
if ([string]::IsNullOrWhiteSpace($env:FILEMANAGER_UI_CONFIG_ROOT)) {
    $env:FILEMANAGER_UI_CONFIG_ROOT = 'Software\Open Salamander\6.0-filemanager-testdata'
}
if ([string]::IsNullOrWhiteSpace($env:FILEMANAGER_UI_CONFIG_FAULT_INJECTION)) {
    $env:FILEMANAGER_UI_CONFIG_FAULT_INJECTION = '1'
}
if ([string]::IsNullOrWhiteSpace($env:FILEMANAGER_UI_RECYCLE_BIN)) {
    $env:FILEMANAGER_UI_RECYCLE_BIN = '1'
}
# The project files enable CL /MP, so keep compiler-child fan-out aligned with the bounded MSBuild node count.
$env:CL_MPCount = [string]$MaxBuildNodes

if ($ReleasePipeline -and $NoReleasePipeline) {
    throw '-ReleasePipeline and -NoReleasePipeline cannot be used together.'
}
if (-not $ReleasePipeline -and -not $NoReleasePipeline -and -not [System.String]::Equals($env:GITHUB_ACTIONS, 'true', [System.StringComparison]::OrdinalIgnoreCase)) {
    # A no-argument developer run must exercise the same release gate and installer pass as the pipeline.
    $ReleasePipeline = $true
    Write-Host 'No release mode was specified; using local release-pipeline defaults.' -ForegroundColor Cyan
}

$repositoryRoot = Split-Path -Parent $PSScriptRoot
$testProject = Join-Path $repositoryRoot 'tests\FileManager.UiTests\FileManager.UiTests.csproj'
$nativeSolution = Join-Path $repositoryRoot 'src\vcxproj\salamand.sln'
$nativeSafetyProject = Join-Path $repositoryRoot 'tests\NativeSafetyTests\NativeSafetyTests.vcxproj'
$pictViewEngineProject = Join-Path $repositoryRoot 'tests\PictViewEngineTests\PictViewEngineTests.vcxproj'
$failures = [System.Collections.Generic.List[string]]::new()
$passed = [System.Collections.Generic.List[string]]::new()
$skipped = [System.Collections.Generic.List[string]]::new()
$nonBlockingSkipped = [System.Collections.Generic.List[string]]::new()
# Keep known whole-lane host limitations distinct from individual capability-gated NUnit cases.
$nonBlockingEnvironmentSkipped = [System.Collections.Generic.List[string]]::new()
# These assertions describe optional capabilities whose absence is an explicit, documented local skip.
$optionalUiIgnoreMessagePrefixes = @(
    'Set FILEMANAGER_UI_CONFIG_FAULT_INJECTION=1 ',
    'Set FILEMANAGER_UI_RECYCLE_BIN=1 '
)
$nonBlockingUiIgnoreMessagePrefixes = @(
    'Second-volume UI tests skipped: ',
    'Second-volume ADS-unsupported UI tests skipped: ',
    # These characterization cases need optional plug-in or language-specific Help deployment that the base artifact does not guarantee.
    'Install and enable the Zip plug-in, then set FILEMANAGER_UI_ZIP_PLUGIN=1 ',
    'Deploy salamand.chm and set FILEMANAGER_UI_HELP_SEARCH_TERM plus FILEMANAGER_UI_HELP_EXPECTED_RESULT ',
    'Deploy help/<language>/salamand.chm beside the tested executable to run Help Search characterization.'
)

if ($ReleasePipeline) {
    # The local parity mode uses the blocking release inventory, not the diagnostic verifier or quarantined monitoring lane.
    $FailOnSkipped = $true
    $SkipLockVerifier = $true
    if ([string]::IsNullOrWhiteSpace($NUnitFilter)) {
        $NUnitFilter = 'TestCategory!=Quarantined&TestCategory!=LiveFtp'
    }
    elseif ($NUnitFilter -cne 'TestCategory!=Quarantined&TestCategory!=LiveFtp') {
        # A custom filter would make the local inventory differ from the one that blocks the GitHub release gate.
        throw '-ReleasePipeline requires the workflow NUnit filter: TestCategory!=Quarantined&TestCategory!=LiveFtp.'
    }
    if ([string]::IsNullOrWhiteSpace($BuildNumber)) {
        $BuildNumber = '0'
    }
    if ($BuildNumber -notmatch '^\d+$') {
        throw 'BuildNumber must be a non-negative integer so the installer version matches the GitHub release contract.'
    }
}

function Invoke-AutomatedCheck {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Name,
        [Parameter(Mandatory = $true)]
        [scriptblock]$Action,
        [string]$SkipReason,
        [switch]$NonBlockingSkip
    )

    Write-Host "`n=== Running $Name ===" -ForegroundColor Cyan
    if (-not [string]::IsNullOrWhiteSpace($SkipReason)) {
        # Optional lanes must remain visible in the aggregate report even when
        # this machine cannot safely satisfy their external prerequisites.
        $skipEntry = "${Name}: $SkipReason"
        if ($NonBlockingSkip) {
            $nonBlockingEnvironmentSkipped.Add($skipEntry)
        }
        else {
            $skipped.Add($skipEntry)
        }
        Write-Host "SKIPPED: $SkipReason" -ForegroundColor Yellow
        return
    }

    try {
        & $Action
        $exitCode = $LASTEXITCODE
        if ($null -eq $exitCode) {
            $exitCode = 0
        }

        if ($exitCode -ne 0) {
            $failures.Add($Name)
            Write-Host "FAILED: $Name (exit code $exitCode)" -ForegroundColor Red
        }
        else {
            $passed.Add($Name)
            Write-Host "PASSED: $Name" -ForegroundColor Green
        }
    }
    catch {
        # Isolate each check so one terminating error cannot hide failures or
        # skips from the remainder of the complete automated test inventory.
        $failures.Add($Name)
        Write-Host "FAILED: $Name" -ForegroundColor Red
        Write-Host $_ -ForegroundColor Red
    }
}

function Invoke-WindowsPowerShellScript {
    param(
        [Parameter(Mandatory = $true)]
        [string]$RelativePath,
        [string[]]$ScriptArguments = @(),
        [string]$DisplayName = $RelativePath,
        [string]$SkipReason
    )

    $scriptPath = Join-Path $repositoryRoot $RelativePath
    $action = {
        & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $scriptPath @ScriptArguments
    }.GetNewClosure()
    Invoke-AutomatedCheck -Name $DisplayName -Action $action -SkipReason $SkipReason
}

function Resolve-BaseCommit {
    param([string]$RequestedCommit)

    $candidates = [System.Collections.Generic.List[string]]::new()
    if (-not [string]::IsNullOrWhiteSpace($RequestedCommit)) {
        $candidates.Add($RequestedCommit)
    }
    elseif (-not [string]::IsNullOrWhiteSpace($env:GITHUB_BASE_REF)) {
        $candidates.Add("origin/$($env:GITHUB_BASE_REF)")
    }

    foreach ($candidate in $candidates | Select-Object -Unique) {
        & git rev-parse --verify --quiet "$candidate^{commit}" *> $null
        if ($LASTEXITCODE -eq 0) {
            return $candidate
        }
    }

    return $null
}

function Find-VisualStudioDeveloperCommand {
    $candidates = [System.Collections.Generic.List[string]]::new()
    if (-not [string]::IsNullOrWhiteSpace($env:ProgramW6432)) {
        $candidates.Add((Join-Path $env:ProgramW6432 'Microsoft Visual Studio\18\Insiders\Common7\Tools\VsDevCmd.bat'))
    }

    $programFilesX86 = [Environment]::GetFolderPath([Environment+SpecialFolder]::ProgramFilesX86)
    $vswhere = Join-Path $programFilesX86 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path -LiteralPath $vswhere) {
        # Restrict discovery to VS 2026 so an older installation cannot supply the test compiler.
        $installationPath = & $vswhere -latest -products * -version '[18.0,19.0)' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
        if (-not [string]::IsNullOrWhiteSpace($installationPath)) {
            $candidates.Add((Join-Path $installationPath.Trim() 'Common7\Tools\VsDevCmd.bat'))
        }
    }

    return $candidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
}

# Keep cmd.exe's initial environment bounded so PowerShell callers with oversized PATH values can still start VsDevCmd.
function Get-VisualStudioBootstrapPath {
    $paths = [System.Collections.Generic.List[string]]::new()
    foreach ($candidate in @(
        (Join-Path $env:SystemRoot 'System32'),
        $env:SystemRoot,
        (Join-Path $env:SystemRoot 'System32\Wbem'),
        (Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0'),
        (Join-Path $env:SystemRoot 'System32\OpenSSH'),
        (Join-Path $env:ProgramFiles 'dotnet'),
        (Join-Path $env:ProgramFiles 'PowerShell\7'),
        (Join-Path $env:ProgramFiles 'Git\cmd')
    )) {
        if (-not [string]::IsNullOrWhiteSpace($candidate) -and (Test-Path -LiteralPath $candidate -PathType Container)) {
            $paths.Add($candidate)
        }
    }
    foreach ($commandName in @('git.exe', 'pwsh.exe')) {
        $command = Get-Command $commandName -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($null -ne $command -and -not [string]::IsNullOrWhiteSpace($command.Source)) {
            $paths.Add((Split-Path -Parent $command.Source))
        }
    }
    return (($paths | Select-Object -Unique) -join ';')
}

# Start each developer-command shell without state left by an earlier VsDevCmd invocation.
# VsDevCmd appends its tool paths to these variables; preserving an imported environment can make its batch expansion exceed cmd.exe's line limit.
function Get-VisualStudioCleanEnvironmentPreamble {
    $variables = @(
        'INCLUDE', 'EXTERNAL_INCLUDE', 'LIB', 'LIBPATH',
        'VSINSTALLDIR', 'VCINSTALLDIR', 'VCToolsInstallDir', 'VCToolsRedistDir', 'VCToolsVersion',
        'WindowsSdkDir', 'WindowsSDKVersion', 'WindowsSDKLibVersion', 'UniversalCRTSdkDir', 'UCRTVersion',
        'DevEnvDir', 'VisualStudioVersion', 'VS180COMNTOOLS',
        'VSCMD_ARG_TGT_ARCH', 'VSCMD_ARG_HOST_ARCH', 'VSCMD_VER',
        '__VSCMD_PREINIT_PATH', '__VSCMD_PREINIT_INCLUDE', '__VSCMD_PREINIT_LIB',
        '__VSCMD_PREINIT_LIBPATH', '__VSCMD_PREINIT_EXTERNAL_INCLUDE'
    )
    return (($variables | ForEach-Object { 'set "' + $_ + '="' }) -join ' && ')
}

function Import-VisualStudioDeveloperEnvironment {
    param([Parameter(Mandatory = $true)][string]$DeveloperCommand)

    # Release audit scripts call dumpbin directly, so retain the VS 2026 environment across local steps just as GITHUB_ENV does in Actions.
    $bootstrapPath = Get-VisualStudioBootstrapPath
    # Start VsDevCmd from a clean, bounded environment so previous local runs cannot compound its tool paths.
    # Request both target architectures so x86 MASM is available while PreferredToolArchitecture selects x64 C++.
    $environmentPreamble = Get-VisualStudioCleanEnvironmentPreamble
    $developerEnvironment = & $env:ComSpec /d /s /c ($environmentPreamble + ' && set "PATH=' + $bootstrapPath + '" && call "' + $DeveloperCommand + '" -arch=x86 -host_arch=x64 >nul && set')
    if ($LASTEXITCODE -ne 0) {
        throw "Visual Studio 2026 developer environment setup failed with exit code $LASTEXITCODE."
    }

    foreach ($line in $developerEnvironment) {
        $separator = $line.IndexOf('=')
        if ($separator -lt 1) {
            continue
        }
        $name = $line.Substring(0, $separator)
        if ($name -match '^[A-Za-z_][A-Za-z0-9_]*$') {
            Set-Item -Path ("Env:" + $name) -Value $line.Substring($separator + 1)
        }
    }
}

function Find-ApplicationVerifier {
    # Prefer the SDK's Application Verifier CLI, then fall back to the in-box
    # C:\Windows\System32\appverif.exe (which is a full tool on Windows 11).
    $debuggerRoot = Join-Path ([Environment]::GetFolderPath([Environment+SpecialFolder]::ProgramFilesX86)) 'Windows Kits\10\Debuggers'
    if (Test-Path -LiteralPath $debuggerRoot -PathType Container) {
        $sdk = Get-ChildItem -LiteralPath $debuggerRoot -Filter appverif.exe -Recurse -ErrorAction SilentlyContinue |
            Select-Object -First 1
        if ($null -ne $sdk) {
            return $sdk.FullName
        }
    }

    $command = Get-Command appverif.exe -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($null -ne $command) {
        return $command.Source
    }

    return $null
}

function Resolve-ReleasePipelineBaseCommit {
    param([string]$RequestedCommit)

    # Mirror the workflow's push payload first, then use the selected revision's first parent for manual local runs.
    $candidate = $RequestedCommit
    if ([string]::IsNullOrWhiteSpace($candidate)) {
        $candidate = $env:PUSH_BASE_COMMIT
    }
    if ([string]::IsNullOrWhiteSpace($candidate) -or $candidate -match '^0+$') {
        $candidate = 'HEAD^'
    }

    & git rev-parse --verify --quiet "$candidate^{commit}" *> $null
    if ($LASTEXITCODE -ne 0) {
        throw "The release pipeline base commit is unavailable: $candidate. Fetch complete history or pass -BaseCommit explicitly."
    }

    return $candidate
}

function Test-ProcessIsElevated {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = [Security.Principal.WindowsPrincipal]::new($identity)
    # Application Verifier mutates system-wide image settings, so a normal desktop token cannot run this lane.
    return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Test-Dotnet10Sdk {
    param([Parameter(Mandatory = $true)][string]$DotnetPath)

    # A net10.0 project cannot be restored or built reliably with an older SDK even when dotnet.exe is present.
    $installedSdks = @(& $DotnetPath --list-sdks)
    return @($installedSdks | Where-Object { $_ -match '^10\.\d+\.\d+\s+\[' }).Count -ne 0
}

function Get-Dotnet10SdkCommand {
    $dotnet = Get-Command dotnet.exe -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($null -eq $dotnet -or -not (Test-Dotnet10Sdk -DotnetPath $dotnet.Source)) {
        # Both developer and self-hosted CI hosts are provisioned out of band; this runner never attempts a privileged SDK installation.
        throw '.NET 10 SDK must already be installed to run the net10.0 NUnit project.'
    }

    return $dotnet
}

# Fail before build/test work so a host-provisioning problem cannot masquerade as a repository failure.
$dotnet = Get-Dotnet10SdkCommand

function Get-ReleasePipelinePrerequisiteFailures {
    $missing = [System.Collections.Generic.List[string]]::new()

    if ([string]::IsNullOrWhiteSpace((Find-VisualStudioDeveloperCommand))) {
        $missing.Add('Install Visual Studio 2026 C++ tools with the v145 x64/x86 toolset.')
    }
    if ($null -eq (Get-Command powershell.exe -ErrorAction SilentlyContinue | Select-Object -First 1)) {
        $missing.Add('Windows PowerShell is required for the repository PowerShell probes.')
    }
    if ($null -eq (Get-Command pwsh.exe -ErrorAction SilentlyContinue | Select-Object -First 1)) {
        $missing.Add('Install 64-bit PowerShell 7.4 or newer for the SQLite recovery probe.')
    }

    # Preserve the mutable collection for callers that add checkout-specific failures after this generic audit.
    return (, $missing)
}

function Get-ReleasePipelineProvisioningNotes {
    $notes = [System.Collections.Generic.List[string]]::new()
    $innoCompiler = 'C:\Program Files (x86)\Inno Setup 6\ISCC.exe'
    if (-not (Test-Path -LiteralPath $innoCompiler -PathType Leaf)) {
        # The workflow acquires the locked compiler during packaging, so report the local gap without rejecting a runnable elevated pipeline.
        $notes.Add('Pinned Inno Setup 6.7.3 is not installed in the machine-wide location; the release preflight will provision or reuse the verified local tool cache from tools\\release-inputs.json.')
    }

    $buildToolsDeveloperCommand = Join-Path ([Environment]::GetFolderPath([Environment+SpecialFolder]::ProgramFilesX86)) 'Microsoft Visual Studio\18\BuildTools\Common7\Tools\VsDevCmd.bat'
    if (-not (Test-Path -LiteralPath $buildToolsDeveloperCommand -PathType Leaf)) {
        # The repository permits the installed VS 2026 IDE, while Actions itself is provisioned with Build Tools at this fixed path.
        $notes.Add('The workflow-specific VS 2026 Build Tools installation is absent; the local runner will use the installed VS 2026 developer environment instead.')
    }

    # Keep provisioning notes as one collection instead of letting PowerShell enumerate it into scalar pipeline output.
    return (, $notes)
}

function Build-UiTestApplication {
    param(
        [Parameter(Mandatory = $true)]
        [string]$DeveloperCommand,
        [Parameter(Mandatory = $true)]
        [string]$BuildDirectory,
        [Parameter(Mandatory = $true)]
        # Build the disposable UI executable with the only supported native toolset.
        [ValidateSet('v145')]
        [string]$Toolset
    )

    if (-not (Test-Path -LiteralPath $nativeSolution -PathType Leaf)) {
        throw "The native FileManager solution was not found: $nativeSolution"
    }

    New-Item -ItemType Directory -Path $BuildDirectory -Force | Out-Null
    # Build the complete Debug x64 solution into a per-run directory so UI tests
    # always exercise this checkout rather than a caller-provided executable.
    # Keep the toolset explicit so parity jobs test the executable they built.
    # The generated workspace path has no spaces; avoid a trailing backslash escaping the MSBuild property quote.
    $bootstrapPath = Get-VisualStudioBootstrapPath
    $environmentPreamble = Get-VisualStudioCleanEnvironmentPreamble
    # Use both target architectures so the x64 solution can assemble its x86 sfx7zip dependency without inheriting a large user PATH.
    $buildCommand = $environmentPreamble + ' && set "PATH=' + $bootstrapPath + '" && call "' + $DeveloperCommand + '" -arch=x86 -host_arch=x64 && msbuild "' + $nativeSolution +
        '" /m:' + $MaxBuildNodes + ' /t:Build /p:Configuration=Debug /p:Platform=x64 /p:PlatformToolset=' + $Toolset + ' /p:PreferredToolArchitecture=x64 /p:OPENSAL_BUILD_DIR=' +
        ($BuildDirectory.TrimEnd('\') + '\') + ' /nr:false'
    & $env:ComSpec /d /s /c $buildCommand
    if ($LASTEXITCODE -ne 0) {
        throw "Building the Debug x64 FileManager solution failed with exit code $LASTEXITCODE."
    }
}

function Invoke-NativeSafetyTests {
    param(
        [Parameter(Mandatory = $true)]
        [string]$DeveloperCommand,
        [Parameter(Mandatory = $true)]
        # Keep native safety probes on the same VS 2026 toolset as the UI executable.
        [ValidateSet('v145')]
        [string]$Toolset
    )

    if (-not (Test-Path -LiteralPath $nativeSafetyProject -PathType Leaf)) {
        throw "The native safety test project was not found: $nativeSafetyProject"
    }

    # Keep the native executable independent of the product solution so its
    # pure boundary checks run quickly after the main build has produced UI artifacts.
    $bootstrapPath = Get-VisualStudioBootstrapPath
    $environmentPreamble = Get-VisualStudioCleanEnvironmentPreamble
    # Keep the safety build on the same bounded dual-architecture environment as the product build.
    $buildCommand = $environmentPreamble + ' && set "PATH=' + $bootstrapPath + '" && call "' + $DeveloperCommand + '" -arch=x86 -host_arch=x64 && msbuild "' + $nativeSafetyProject +
        '" /m:' + $MaxBuildNodes + ' /t:Build /p:Configuration=Debug /p:Platform=x64 /p:PlatformToolset=' + $Toolset + ' /nr:false'
    & $env:ComSpec /d /s /c $buildCommand
    if ($LASTEXITCODE -ne 0) {
        throw "Building the native safety tests failed with exit code $LASTEXITCODE."
    }

    $testExecutable = Join-Path $repositoryRoot 'tests\NativeSafetyTests\x64\Debug\NativeSafetyTests.exe'
    if (-not (Test-Path -LiteralPath $testExecutable -PathType Leaf)) {
        throw "The native safety test executable was not produced: $testExecutable"
    }
    & $testExecutable
    if ($LASTEXITCODE -ne 0) {
        throw "The native safety tests failed with exit code $LASTEXITCODE."
    }
}

function Invoke-PictViewEngineTests {
    param(
        [Parameter(Mandatory = $true)]
        [string]$DeveloperCommand,
        [Parameter(Mandatory = $true)]
        # PictView coverage shares the single supported VS 2026 toolchain.
        [ValidateSet('v145')]
        [string]$Toolset
    )

    if (-not (Test-Path -LiteralPath $pictViewEngineProject -PathType Leaf)) {
        throw "The PictView engine test project was not found: $pictViewEngineProject"
    }

    # The engine links straight into a console host, so its decode, transform and
    # encode round trips run without a desktop session or the plug-in host.
    $bootstrapPath = Get-VisualStudioBootstrapPath
    $environmentPreamble = Get-VisualStudioCleanEnvironmentPreamble
    # Keep the PictView build on the same bounded dual-architecture environment as the product build.
    $buildCommand = $environmentPreamble + ' && set "PATH=' + $bootstrapPath + '" && call "' + $DeveloperCommand + '" -arch=x86 -host_arch=x64 && msbuild "' + $pictViewEngineProject +
        '" /m:' + $MaxBuildNodes + ' /t:Build /p:Configuration=Debug /p:Platform=x64 /p:PlatformToolset=' + $Toolset + ' /nr:false'
    & $env:ComSpec /d /s /c $buildCommand
    if ($LASTEXITCODE -ne 0) {
        throw "Building the PictView engine tests failed with exit code $LASTEXITCODE."
    }

    $testExecutable = Join-Path $repositoryRoot 'tests\PictViewEngineTests\x64\Debug\PictViewEngineTests.exe'
    if (-not (Test-Path -LiteralPath $testExecutable -PathType Leaf)) {
        throw "The PictView engine test executable was not produced: $testExecutable"
    }
    & $testExecutable
    if ($LASTEXITCODE -ne 0) {
        throw "The PictView engine tests failed with exit code $LASTEXITCODE."
    }
}

function Resolve-UiTestArtifact {
    param(
        [Parameter(Mandatory = $true)]
        [string]$BuildDirectory,
        [Parameter(Mandatory = $true)]
        [string]$FileName
    )

    $artifact = Get-ChildItem -LiteralPath $BuildDirectory -Filter $FileName -File -Recurse |
        Where-Object { $_.FullName -like '*Debug_x64*' } |
        Select-Object -First 1
    if ($null -eq $artifact) {
        throw "The Debug x64 build did not produce $FileName below $BuildDirectory."
    }

    return $artifact.FullName
}

function Stage-UiTestCrashReporter {
    param(
        [Parameter(Mandatory = $true)]
        [string]$ExecutablePath,
        [Parameter(Mandatory = $true)]
        [string]$BuildDirectory
    )

    $destination = Join-Path (Split-Path -Parent $ExecutablePath) 'salmon.exe'
    if (Test-Path -LiteralPath $destination -PathType Leaf) {
        return
    }

    $reporter = Get-ChildItem -LiteralPath $BuildDirectory -Filter 'salmon.exe' -File -Recurse |
        Where-Object { $_.FullName -like '*Debug_x64*' } |
        Select-Object -First 1
    if ($null -eq $reporter) {
        throw 'The Debug x64 build did not produce salmon.exe for the UI test application.'
    }

    # salmon.exe must sit beside salamand.exe because the crash-client constructs that sibling path at startup.
    Copy-Item -LiteralPath $reporter.FullName -Destination $destination -Force
}

function Resolve-WritableFixedDDrive {
    # Probe the exact requested drive with a disposable file so the suite never assumes that a visible D: root is writable.
    $disk = Get-CimInstance Win32_LogicalDisk -Filter "DeviceID='D:'" -ErrorAction SilentlyContinue
    if ($null -eq $disk -or $disk.DriveType -ne 3 -or [string]::IsNullOrWhiteSpace($disk.FileSystem)) {
        return $null
    }

    $probeDirectory = Join-Path 'D:\' ('.filemanager-ui-write-probe-' + [Guid]::NewGuid().ToString('N'))
    try {
        New-Item -ItemType Directory -Path $probeDirectory -Force -ErrorAction Stop | Out-Null
        [IO.File]::WriteAllText((Join-Path $probeDirectory 'probe.txt'), 'FileManager UI test capability probe')
        return [pscustomobject]@{
            Root = 'D:\'
            FileSystem = [string]$disk.FileSystem
        }
    }
    catch {
        return $null
    }
    finally {
        if (Test-Path -LiteralPath $probeDirectory -PathType Container) {
            Remove-Item -LiteralPath $probeDirectory -Recurse -Force -ErrorAction SilentlyContinue
        }
    }
}

function Initialize-UiTestSandbox {
    $configuredRoot = $env:FILEMANAGER_UI_TESTDATA_ROOT
    if ([string]::IsNullOrWhiteSpace($configuredRoot)) {
        # Keep current-user test data below a plainly named directory that the harness owns and removes.
        $configuredRoot = Join-Path $env:USERPROFILE 'filemanager-testdata'
    }
    $fullRoot = [System.IO.Path]::GetFullPath($configuredRoot)
    if ([System.IO.Path]::GetFileName($fullRoot.TrimEnd([System.IO.Path]::DirectorySeparatorChar, [System.IO.Path]::AltDirectorySeparatorChar)) -ine 'filemanager-testdata') {
        throw 'FILEMANAGER_UI_TESTDATA_ROOT must name a filemanager-testdata directory.'
    }
    $env:FILEMANAGER_UI_TESTDATA_ROOT = $fullRoot
    $env:FILEMANAGER_UI_CONFIG_ROOT = 'Software\Open Salamander\6.0-filemanager-testdata'

    $ownershipMarker = Join-Path $fullRoot '.filemanager-testdata-owner'
    $ownershipMarkerContents = 'Open Salamander UI test sandbox'
    if (Test-Path -LiteralPath $fullRoot -PathType Container) {
        $entries = @(Get-ChildItem -LiteralPath $fullRoot -Force)
        if (Test-Path -LiteralPath $ownershipMarker -PathType Leaf) {
            if ([IO.File]::ReadAllText($ownershipMarker) -cne $ownershipMarkerContents) {
                throw "Refusing to reuse an unowned UI test data directory '$fullRoot'."
            }
        }
        elseif ($entries.Count -ne 0) {
            throw "Refusing to reuse an unowned UI test data directory '$fullRoot'."
        }
    }
    else {
        New-Item -ItemType Directory -Path $fullRoot | Out-Null
    }

    # Command discovery runs before NUnit creates its sandbox, so establish the
    # same ownership contract now and let NUnit reparse-safely reset it later.
    [IO.File]::WriteAllText($ownershipMarker, $ownershipMarkerContents)
    New-Item -ItemType Directory -Path (Join-Path $fullRoot 'temp') -Force | Out-Null
    New-Item -ItemType Directory -Path (Join-Path $fullRoot 'appdata') -Force | Out-Null
}

function Build-ReleaseGateDebugArtifacts {
    param(
        [Parameter(Mandatory = $true)]
        [string]$DeveloperCommand,
        [Parameter(Mandatory = $true)]
        [string]$BuildDirectory,
        [Parameter(Mandatory = $true)]
        [ValidateSet('v145')]
        [string]$Toolset
    )

    New-Item -ItemType Directory -Path $BuildDirectory -Force | Out-Null
    # This intentionally precedes runtests' disposable build, matching the workflow's staged Debug artifact step and its strict input resolution.
    $buildRoot = $BuildDirectory.TrimEnd('\') + '\'
    $bootstrapPath = Get-VisualStudioBootstrapPath
    $environmentPreamble = Get-VisualStudioCleanEnvironmentPreamble
    # Use both target architectures so the staged release build can assemble x86 sfx7zip while retaining x64 C++ tools.
    $buildCommand = $environmentPreamble + ' && set "PATH=' + $bootstrapPath + '" && call "' + $DeveloperCommand + '" -arch=x86 -host_arch=x64 && set "OPENSAL_BUILD_DIR=' + $buildRoot +
        '" && msbuild "' + $nativeSolution + '" /m:' + $MaxBuildNodes + ' /t:Build /p:Configuration=Debug /p:Platform=x64 /p:PlatformToolset=' +
        $Toolset + ' /p:PreferredToolArchitecture=x64 /nr:false'
    & $env:ComSpec /d /s /c $buildCommand
    if ($LASTEXITCODE -ne 0) {
        throw "Building the staged Debug x64 release-gate artifacts failed with exit code $LASTEXITCODE."
    }
}

function Invoke-ReleasePipelineCheck {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Name,
        [Parameter(Mandatory = $true)]
        [scriptblock]$Action
    )

    $failureCount = $failures.Count
    Invoke-AutomatedCheck -Name $Name -Action $Action
    # GitHub stops the installer job at its first failed step, so local parity must not stage an unverified artifact.
    return $failures.Count -eq $failureCount
}

function Invoke-ReleasePipelinePackaging {
    param(
        [Parameter(Mandatory = $true)]
        [string]$DeveloperCommand,
        [Parameter(Mandatory = $true)]
        [string]$BuildDirectory,
        [Parameter(Mandatory = $true)]
        [ValidateSet('v145')]
        [string]$Toolset,
        [Parameter(Mandatory = $true)]
        [ValidateRange(1, 16)]
        [int]$MaxBuildNodes,
        [Parameter(Mandatory = $true)]
        [string]$ReleaseBuildNumber
    )

    $buildInstallerScript = Join-Path $repositoryRoot 'tools\build-release-installer.ps1'
    # A previous local installer can hold its staged executable open, so give this run an isolated sibling directory that Inno can still resolve beside setup.iss.
    $stagingDirectory = Join-Path $repositoryRoot ('Installer\Installer_Staging-runtests-' + [Guid]::NewGuid().ToString('N'))
    $buildInstallerAction = {
        # The local release gate delegates the entire CI Build Installer job to the same script.
        & $buildInstallerScript -BuildDirectory $BuildDirectory -InstallerStagingDirectory $stagingDirectory -BuildNumber $ReleaseBuildNumber -PlatformToolset $Toolset -MaxBuildNodes $MaxBuildNodes
        if ($LASTEXITCODE -ne 0) {
            throw "The Build Installer pass failed with exit code $LASTEXITCODE."
        }
    }.GetNewClosure()
    [void](Invoke-ReleasePipelineCheck -Name 'Build Installer (x64 Release)' -Action $buildInstallerAction)
}

function Invoke-PinnedInnoSetupPreflight {
    $provisioner = Join-Path $repositoryRoot 'tools\install-pinned-inno-setup.ps1'
    Write-Host "`n=== Preflight pinned Inno Setup ===" -ForegroundColor Cyan
    # Use the exact provisioner consumed by the separate Build Installer pass so local failures are actionable before compilation.
    $compiler = & $provisioner
    if (-not (Test-Path -LiteralPath $compiler -PathType Leaf)) {
        throw 'Pinned Inno Setup preflight did not produce ISCC.exe.'
    }
    Write-Host "Pinned Inno Setup preflight passed: $compiler" -ForegroundColor Green
}

function Invoke-ReleaseWorkflowContractValidation {
    param(
        [Parameter(Mandatory = $true)]
        [string]$PowerShellPath,
        [Parameter(Mandatory = $true)]
        [string]$DotnetPath
    )

    # This is a distinct Build Installer step in Actions, so keep its ordering
    # here to surface the same source-contract failure before the Release build.
    & $PowerShellPath -NoProfile -ExecutionPolicy Bypass -File (Join-Path $repositoryRoot 'tools\test-release-input-pinning.ps1')
    if ($LASTEXITCODE -ne 0) {
        throw "The release workflow input-contract validation failed with exit code $LASTEXITCODE."
    }

    & $DotnetPath test $testProject `
        --filter 'FullyQualifiedName~NativeSafetyRegressionTests.Root_test_runner_collects_every_documented_automated_test_layer' `
        --logger 'console;verbosity=minimal'
    if ($LASTEXITCODE -ne 0) {
        throw "The focused release workflow contract test failed with exit code $LASTEXITCODE."
    }
}

function Assert-UiTestSymbolicLinkSupport {
    # Reparse-point setup stays below the same cleanup boundary as every UI operation.
    $root = Join-Path $env:FILEMANAGER_UI_TESTDATA_ROOT ('symlink-preflight-' + [Guid]::NewGuid().ToString('N'))
    $target = Join-Path $root 'target'
    $link = Join-Path $root 'link'
    try {
        New-Item -ItemType Directory -Path $target -Force | Out-Null
        # Reparse-point tests must execute, so reject hosts that would make NUnit ignore them.
        New-Item -ItemType SymbolicLink -Path $link -Target $target -ErrorAction Stop | Out-Null
    }
    catch {
        # Preserve the documented runner contract: without this host privilege, report an honest whole-lane skip instead of partial UI coverage.
        return "Required privilege SeCreateSymbolicLinkPrivilege is unavailable. The complete UI test suite has not been completed because required privileges are missing; all UI tests were skipped. Details: $($_.Exception.Message)"
    }
    finally {
        if (Test-Path -LiteralPath $root) {
            Remove-Item -LiteralPath $root -Recurse -Force
        }
        # Building the preflight subtree can create the test-data root as a side
        # effect, and an interrupted earlier run can leave an empty markerless root
        # behind. Either way the NUnit sandbox refuses such a directory, so remove
        # it whenever it is truly empty and let the sandbox create it with its
        # ownership marker.
        if (Test-Path -LiteralPath $env:FILEMANAGER_UI_TESTDATA_ROOT) {
            $remaining = @(Get-ChildItem -LiteralPath $env:FILEMANAGER_UI_TESTDATA_ROOT -Force)
            if ($remaining.Count -eq 0) {
                Remove-Item -LiteralPath $env:FILEMANAGER_UI_TESTDATA_ROOT -Force
            }
        }
    }
}

function Set-UiTestEnvironment {
    param(
        [Parameter(Mandatory = $true)]
        [string]$ExecutablePath
    )

    Initialize-UiTestSandbox
    $env:FILEMANAGER_UI_EXE = $ExecutablePath
    Remove-Item Env:FILEMANAGER_UI_CROSS_VOLUME_ROOT -ErrorAction SilentlyContinue
    Remove-Item Env:FILEMANAGER_UI_ADS_UNSUPPORTED_TARGET_ROOT -ErrorAction SilentlyContinue
    Remove-Item Env:FILEMANAGER_UI_SECOND_VOLUME_SKIP_REASON -ErrorAction SilentlyContinue
    Remove-Item Env:FILEMANAGER_UI_ADS_UNSUPPORTED_TARGET_SKIP_REASON -ErrorAction SilentlyContinue

    $secondVolume = Resolve-WritableFixedDDrive
    $sourceVolume = [IO.Path]::GetPathRoot($env:FILEMANAGER_UI_TESTDATA_ROOT)
    if ($null -eq $secondVolume -or [string]::Equals($sourceVolume, $secondVolume.Root, [StringComparison]::OrdinalIgnoreCase)) {
        # A missing or unusable D: capability must produce a successful, explicit NUnit skip instead of requiring elevation or disk provisioning.
        $env:FILEMANAGER_UI_SECOND_VOLUME_SKIP_REASON = 'Second-volume UI tests skipped: fixed writable D:\ is unavailable or is the same volume as the primary test sandbox; all tests that depend on a second volume have been skipped.'
        Write-Host $env:FILEMANAGER_UI_SECOND_VOLUME_SKIP_REASON -ForegroundColor Yellow
    }
    else {
        $env:FILEMANAGER_UI_CROSS_VOLUME_ROOT = Join-Path $secondVolume.Root 'filemanager-testdata'
        if ($secondVolume.FileSystem -in @('FAT', 'FAT32', 'exFAT')) {
            # An ADS-unsupported D: volume can serve both the ordinary cross-volume and metadata-loss fixtures.
            $env:FILEMANAGER_UI_ADS_UNSUPPORTED_TARGET_ROOT = $env:FILEMANAGER_UI_CROSS_VOLUME_ROOT
        }
        else {
            # NTFS D: supports cross-volume moves but cannot represent the ADS-loss target, so keep that limitation explicit.
            $env:FILEMANAGER_UI_ADS_UNSUPPORTED_TARGET_SKIP_REASON = "Second-volume ADS-unsupported UI tests skipped: D:\ uses $($secondVolume.FileSystem), which supports alternate data streams."
            Write-Host $env:FILEMANAGER_UI_ADS_UNSUPPORTED_TARGET_SKIP_REASON -ForegroundColor Yellow
        }
    }
    $privilegeSkipReason = Assert-UiTestSymbolicLinkSupport
    if (-not [string]::IsNullOrWhiteSpace($privilegeSkipReason)) {
        # Stop before launching UI fixtures so an unavailable host cannot report a misleading partial result.
        return $privilegeSkipReason
    }

    # The fixture reads dynamic FTP menu IDs from the exact process it launches, avoiding a cross-process SUID hand-off.
}

function Resolve-SqliteDll {
    param([string]$RequestedPath)

    if (-not [string]::IsNullOrWhiteSpace($RequestedPath)) {
        if (-not (Test-Path -LiteralPath $RequestedPath -PathType Leaf)) {
            throw "The requested SQLite test DLL does not exist: $RequestedPath"
        }
        return (Resolve-Path -LiteralPath $RequestedPath).Path
    }

    # Prefer the authoritative local Debug x64 artifact, then accept another
    # x64 configuration only when it was already produced by a solution build.
    $candidates = @(
        (Join-Path $repositoryRoot 'src\vcxproj\sqlite\salamander\Debug_x64\utils\sqlite.dll'),
        (Join-Path $repositoryRoot 'src\vcxproj\sqlite\salamander\Release_x64\utils\sqlite.dll')
    )
    return $candidates | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } | Select-Object -First 1
}

function Remove-OlderUiTestBuildResults {
    param(
        [Parameter(Mandatory = $true)]
        [string]$ResultsDirectory
    )

    if (-not (Test-Path -LiteralPath $ResultsDirectory -PathType Container)) {
        return
    }

    $buildResults = @(Get-ChildItem -LiteralPath $ResultsDirectory -Directory -Filter 'runtests-build-*' |
        Sort-Object LastWriteTimeUtc -Descending)
    $buildResults | Select-Object -Skip 1 | ForEach-Object {
        # A compiler can release a PCH shortly after an interrupted build; do
        # not turn that transient stale-artifact cleanup race into a test skip.
        $staleDirectory = $_.FullName
        $removed = $false
        for ($attempt = 1; $attempt -le 3 -and -not $removed; $attempt++) {
            try {
                Remove-Item -LiteralPath $staleDirectory -Recurse -Force -ErrorAction Stop
                $removed = $true
            }
            catch {
                if ($attempt -lt 3) {
                    Start-Sleep -Seconds 1
                }
                else {
                    Write-Warning "Could not remove locked stale UI build directory '$staleDirectory': $($_.Exception.Message)"
                }
            }
        }
    }
}

if ($PrerequisiteOnly -and -not $ReleasePipeline) {
    throw '-PrerequisiteOnly is reserved for -ReleasePipeline because the ordinary runner intentionally allows optional diagnostic lanes.'
}

if ($ReleasePipeline) {
    # Keep Git's base revision deterministic while allowing the local worktree snapshot under test to be uncommitted.
    $BaseCommit = Resolve-ReleasePipelineBaseCommit -RequestedCommit $BaseCommit
    $releasePrerequisiteFailures = Get-ReleasePipelinePrerequisiteFailures
    $releaseProvisioningNotes = Get-ReleasePipelineProvisioningNotes
    $workingTreeChanges = @(& git status --porcelain)
    if ($workingTreeChanges.Count -ne 0) {
        # Local parity must test the developer's current snapshot; GitHub exercises the committed form of that same change after push.
        $releaseProvisioningNotes.Add('Testing uncommitted local changes; push this snapshot to compare its committed form with GitHub Actions.')
    }

    Write-Host "`n=== Release pipeline prerequisite report ===" -ForegroundColor Cyan
    Write-Host "Base commit: $BaseCommit"
    if ($releasePrerequisiteFailures.Count -eq 0) {
        Write-Host 'Blocking prerequisites: satisfied.' -ForegroundColor Green
    }
    else {
        Write-Host 'Blocking prerequisites:' -ForegroundColor Red
        $releasePrerequisiteFailures | ForEach-Object { Write-Host "  - $_" -ForegroundColor Red }
    }
    if ($releaseProvisioningNotes.Count -ne 0) {
        Write-Host 'Pipeline provisioning:' -ForegroundColor Yellow
        $releaseProvisioningNotes | ForEach-Object { Write-Host "  - $_" -ForegroundColor Yellow }
    }

    if ($PrerequisiteOnly) {
        exit $(if ($releasePrerequisiteFailures.Count -eq 0) { 0 } else { 1 })
    }
    if ($releasePrerequisiteFailures.Count -ne 0) {
        throw 'Release-pipeline prerequisites are not satisfied. Run with -PrerequisiteOnly after correcting the listed items.'
    }
}

$testResultsDirectory = Join-Path $repositoryRoot 'TestResults'
# Keep per-test execution evidence outside disposable UI roots and build directories so failures remain diagnosable.
if ([string]::IsNullOrWhiteSpace($env:FILEMANAGER_UI_TRANSCRIPT_ROOT)) {
    $env:FILEMANAGER_UI_TRANSCRIPT_ROOT = Join-Path $testResultsDirectory ('ui-test-transcripts-' + [Guid]::NewGuid().ToString('N'))
}
Write-Host "UI execution transcripts: $env:FILEMANAGER_UI_TRANSCRIPT_ROOT" -ForegroundColor Cyan
# Prune interrupted runs before preflight checks can exit, so an unavailable toolchain cannot defer retention indefinitely.
Remove-OlderUiTestBuildResults -ResultsDirectory $testResultsDirectory

$releaseGateBuildDirectory = $null
$releaseGateResultDirectory = $null
if ($ReleasePipeline) {
    # Keep the release-gate artifacts and TRX outside the disposable runner build, just as Actions carries them across individual steps.
    $releaseGateResultDirectory = Join-Path $testResultsDirectory ('runtests-release-gate-' + [Guid]::NewGuid().ToString('N'))
    $releaseGateBuildDirectory = Join-Path $releaseGateResultDirectory 'build_stage'
    if ([string]::IsNullOrWhiteSpace($NUnitTrxPath)) {
        $NUnitTrxPath = Join-Path $releaseGateResultDirectory 'runtests-v145.trx'
    }
}

$vsDevCmd = Find-VisualStudioDeveloperCommand
if ([string]::IsNullOrWhiteSpace($vsDevCmd)) {
    throw 'Visual Studio 2026 C++ developer tools were not found; the complete UI suite cannot build the current solution.'
}
$uiBuildDirectory = Join-Path $testResultsDirectory ('runtests-build-' + [Guid]::NewGuid().ToString('N'))
$releaseBuildDirectory = $null
try {
if ($ReleasePipeline) {
    Build-ReleaseGateDebugArtifacts -DeveloperCommand $vsDevCmd -BuildDirectory $releaseGateBuildDirectory -Toolset $PlatformToolset
    # Resolve the staged artifacts before invoking the aggregate runner, matching the workflow's strict hand-off between steps.
    $SqliteDll = Resolve-UiTestArtifact -BuildDirectory $releaseGateBuildDirectory -FileName 'sqlite.dll'
    $env:FILEMANAGER_UI_EXE = Resolve-UiTestArtifact -BuildDirectory $releaseGateBuildDirectory -FileName 'salamand.exe'
}
Build-UiTestApplication -DeveloperCommand $vsDevCmd -BuildDirectory $uiBuildDirectory -Toolset $PlatformToolset
# Keep the script scope so Invoke-AutomatedCheck can resolve the helper function.
$nativeSafetyAction = {
    Invoke-NativeSafetyTests -DeveloperCommand $vsDevCmd -Toolset $PlatformToolset
}
Invoke-AutomatedCheck -Name 'NativeSafetyTests (Debug x64)' -Action $nativeSafetyAction
$pictViewEngineAction = {
    Invoke-PictViewEngineTests -DeveloperCommand $vsDevCmd -Toolset $PlatformToolset
}
Invoke-AutomatedCheck -Name 'PictViewEngineTests (Debug x64)' -Action $pictViewEngineAction
$builtUiExecutable = Resolve-UiTestArtifact -BuildDirectory $uiBuildDirectory -FileName 'salamand.exe'
$null = Stage-UiTestCrashReporter -ExecutablePath $builtUiExecutable -BuildDirectory $uiBuildDirectory
$builtSqliteDll = Resolve-UiTestArtifact -BuildDirectory $uiBuildDirectory -FileName 'sqlite.dll'
$built7zWrapper = Resolve-UiTestArtifact -BuildDirectory $uiBuildDirectory -FileName '7zwrapper.dll'
$built7zEngine = Resolve-UiTestArtifact -BuildDirectory $uiBuildDirectory -FileName '7za.dll'
$uiTestEnvironmentSkipReason = $null
$uiTestEnvironmentSkipIsNonBlocking = $false
try {
    $uiTestEnvironmentSkipReason = Set-UiTestEnvironment -ExecutablePath $builtUiExecutable
    $uiTestEnvironmentSkipIsNonBlocking = -not [string]::IsNullOrWhiteSpace($uiTestEnvironmentSkipReason)
}
catch {
    # Keep source and native diagnostics visible; known privilege failures remain an allowed whole-lane skip.
    $uiTestEnvironmentSkipReason = $_.Exception.Message
    $uiTestEnvironmentSkipIsNonBlocking = $uiTestEnvironmentSkipReason -match '(?i)(privilege|permission|access is denied|administrator)'
}

# Fast source contracts and native compatibility probes are always collected;
# architecture-aware probes run both variants declared by their public scripts.
$resolvedBaseCommit = Resolve-BaseCommit -RequestedCommit $BaseCommit
foreach ($relativePath in @(
    'tools\verify-operation-completion-protocol.ps1',
    'tools\verify-durable-copy-commit.ps1',
    # Exercise the diff ratchet in an isolated Git history before release publication.
    'tools\test-raw-thread-creation-verifier.ps1',
    # Keep release-input provenance enforced by the same aggregate test inventory.
    'tools\test-release-input-pinning.ps1'
)) {
    Invoke-WindowsPowerShellScript -RelativePath $relativePath
}
# Give the global unsafe baseline the same base revision as the diff ratchets,
# so pure source relocations do not masquerade as newly introduced API debt.
$unsafeBaselineArguments = if ($null -ne $resolvedBaseCommit) { @('-BaseCommit', $resolvedBaseCommit) } else { @() }
Invoke-WindowsPowerShellScript -RelativePath 'tools\test-unsafe-api-baseline.ps1' -ScriptArguments $unsafeBaselineArguments

foreach ($architecture in @('x64', 'x86')) {
    Invoke-WindowsPowerShellScript -RelativePath 'tools\test-zlib-compatibility.ps1' `
        -ScriptArguments @('-Architecture', $architecture) `
        -DisplayName "tools\test-zlib-compatibility.ps1 ($architecture)"
    Invoke-WindowsPowerShellScript -RelativePath 'tools\test-bzip2-compatibility.ps1' `
        -ScriptArguments @('-Architecture', $architecture) `
        -DisplayName "tools\test-bzip2-compatibility.ps1 ($architecture)"
}

$cmarkSkipReason = $null
$cmarkScript = Join-Path $repositoryRoot 'tools\test-cmark-gfm-hardening.ps1'
$cmarkCommand = if ($null -ne $vsDevCmd) {
    # Keep this later developer shell independent of an inherited IDE prompt just as the native build shells are.
    (Get-VisualStudioCleanEnvironmentPreamble) + ' && set "PATH=' + (Get-VisualStudioBootstrapPath) + '" && call "' + $vsDevCmd + '" -arch=x64 -host_arch=x64 && powershell.exe -NoProfile -ExecutionPolicy Bypass -File "' + $cmarkScript + '"'
} else { $null }
$cmarkAction = {
    & $env:ComSpec /d /s /c $cmarkCommand
}.GetNewClosure()
Invoke-AutomatedCheck -Name 'tools\test-cmark-gfm-hardening.ps1' -Action $cmarkAction -SkipReason $cmarkSkipReason

$sevenZipOracle = Get-Command 7z.exe -ErrorAction SilentlyContinue | Select-Object -First 1
$sevenZipSkipReason = if ($null -eq $sevenZipOracle) {
    'A 7z.exe-compatible oracle is required for the bundled 7-Zip differential compatibility test.'
} else { $null }
$sevenZipAction = {
    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $repositoryRoot 'tools\test-7zip-compatibility.ps1') `
        -WrapperPath $built7zWrapper -EnginePath $built7zEngine -SevenZipPath $sevenZipOracle.Source
}.GetNewClosure()
# The 7-Zip executable is an optional external oracle, so its absence must not block
# releases that bundle no 7-Zip installation requirement; run the comparison whenever it is available.
if ($null -eq $sevenZipOracle) {
    Write-Host "NOT APPLICABLE: 7-Zip wrapper/oracle compatibility corpus - $sevenZipSkipReason" -ForegroundColor Yellow
}
else {
    Invoke-AutomatedCheck -Name '7-Zip wrapper/oracle compatibility corpus' -Action $sevenZipAction
}

# The workflow passes its staged Debug DLL explicitly; ordinary local runs retain the freshly built disposable artifact.
$resolvedSqliteDll = if ([string]::IsNullOrWhiteSpace($SqliteDll)) { $builtSqliteDll } else { Resolve-SqliteDll -RequestedPath $SqliteDll }
$pwsh = Get-Command pwsh.exe -ErrorAction SilentlyContinue | Select-Object -First 1
$sqliteSkipReason = $null
if ([string]::IsNullOrWhiteSpace($resolvedSqliteDll)) {
    $sqliteSkipReason = 'Build the Debug x64 sqlite.vcxproj target or pass -SqliteDll.'
}
elseif ($null -eq $pwsh) {
    $sqliteSkipReason = '64-bit PowerShell 7.4 or newer was not found.'
}
$sqliteAction = {
    & $pwsh.Source -NoProfile -ExecutionPolicy Bypass -File (Join-Path $repositoryRoot 'tools\test-sqlite-recovery.ps1') -SqliteDll $resolvedSqliteDll
}.GetNewClosure()
Invoke-AutomatedCheck -Name 'tools\test-sqlite-recovery.ps1' -Action $sqliteAction -SkipReason $sqliteSkipReason

$ratchetSkipReason = if ([string]::IsNullOrWhiteSpace($resolvedBaseCommit)) {
    'No authoritative pull-request base was supplied; pass -BaseCommit explicitly.'
} else { $null }
foreach ($relativePath in @(
    'tools\verify-no-new-terminatethread.ps1',
    'tools\verify-no-new-raw-thread-creation.ps1',
    'tools\verify-no-new-gettickcount.ps1',
    'tools\verify-no-new-max-path-buffers.ps1',
    'tools\verify-no-new-unsafe-string-calls.ps1'
)) {
    $arguments = if ($null -ne $resolvedBaseCommit) { @('-BaseCommit', $resolvedBaseCommit) } else { @() }
    Invoke-WindowsPowerShellScript -RelativePath $relativePath -ScriptArguments $arguments -SkipReason $ratchetSkipReason
}

Invoke-WindowsPowerShellScript -RelativePath 'tools\verify-fluent-icon-coverage.ps1'
Invoke-WindowsPowerShellScript -RelativePath 'tools\verify-ui-test-quarantine.ps1'

$networkFixtureAction = {
    # These loopback fixtures are safe in every profile and keep protocol-edge
    # coverage visible when the destructive UI suite must be skipped.
    & $dotnet.Source test $testProject --filter 'FullyQualifiedName~DeterministicNetworkFixtureTests' `
        --logger 'console;verbosity=minimal'
    if ($LASTEXITCODE -ne 0) {
        throw "The deterministic network fixture tests failed with exit code $LASTEXITCODE."
    }
}.GetNewClosure()
Invoke-AutomatedCheck -Name 'Deterministic FTP/FTPS/HTTP fixture tests' -Action $networkFixtureAction

$nunitSkipReason = $uiTestEnvironmentSkipReason
$retainNunitResults = -not [string]::IsNullOrWhiteSpace($NUnitTrxPath)
if ($retainNunitResults) {
    $nunitTrxPath = [System.IO.Path]::GetFullPath($NUnitTrxPath)
    if (Test-Path -LiteralPath $nunitTrxPath) {
        throw "The requested NUnit TRX path already exists: $nunitTrxPath"
    }
    $nunitResultsDirectory = Split-Path -Parent $nunitTrxPath
    $nunitTrxName = Split-Path -Leaf $nunitTrxPath
}
else {
    $nunitResultsDirectory = Join-Path ([System.IO.Path]::GetTempPath()) ('FileManager-runtests-' + [Guid]::NewGuid().ToString('N'))
    $nunitTrxName = 'runtests.trx'
}
$nunitAction = {
    # Running the complete project discovers every fixture after this runner
    # has supplied the executable and every prerequisite UI-test environment value.
    New-Item -ItemType Directory -Path $nunitResultsDirectory -Force | Out-Null
    try {
        $nunitArguments = @(
            'test',
            $testProject,
            '--results-directory', $nunitResultsDirectory,
            '--logger', "trx;LogFileName=$nunitTrxName",
            '--logger', 'console;verbosity=minimal'
        )
        if (-not [string]::IsNullOrWhiteSpace($NUnitFilter)) {
            # A caller-supplied category filter can route manifest-backed quarantines to monitoring without weakening unexpected-skip enforcement.
            $nunitArguments += @('--filter', $NUnitFilter)
        }
        $nunitArguments += @('--', 'NUnit.NumberOfTestWorkers=0')
        & $dotnet.Source @nunitArguments
        $dotnetExitCode = $LASTEXITCODE
        if ($dotnetExitCode -ne 0) {
            throw "The complete NUnit project failed with exit code $dotnetExitCode."
        }

        # VSTest treats NUnit Ignore as success. Only the documented optional
        # capability gates may be skipped locally; every other ignored result remains a harness defect.
        [xml]$trx = Get-Content -LiteralPath (Join-Path $nunitResultsDirectory $nunitTrxName) -Raw
        $ignoredResults = @($trx.SelectNodes("//*[local-name()='UnitTestResult' and @outcome='NotExecuted']"))
        if ($ignoredResults.Count -ne 0) {
            $expectedCapabilitySkips = @()
            $unexpectedIgnoredResults = @()
            foreach ($result in $ignoredResults) {
                $messageNode = $result.SelectSingleNode("./*[local-name()='Output']/*[local-name()='ErrorInfo']/*[local-name()='Message']")
                $message = if ($null -eq $messageNode) { '' } else { $messageNode.InnerText }
                $isExpectedCapabilitySkip = $false
                $isNonBlockingCapabilitySkip = $false
                foreach ($prefix in $optionalUiIgnoreMessagePrefixes) {
                    if ($message.StartsWith($prefix, [StringComparison]::Ordinal)) {
                        $isExpectedCapabilitySkip = $true
                        break
                    }
                }
                foreach ($prefix in $nonBlockingUiIgnoreMessagePrefixes) {
                    if ($message.StartsWith($prefix, [StringComparison]::Ordinal)) {
                        $isExpectedCapabilitySkip = $true
                        $isNonBlockingCapabilitySkip = $true
                        break
                    }
                }
                if ($isExpectedCapabilitySkip) {
                    $expectedCapabilitySkips += [pscustomobject]@{ Result = $result; Message = $message; NonBlocking = $isNonBlockingCapabilitySkip }
                }
                else {
                    $unexpectedIgnoredResults += $result
                }
            }
            foreach ($result in $expectedCapabilitySkips) {
                # Preserve missing local capabilities in the summary instead of misclassifying their explicitly gated tests as failures.
                $skipEntry = "FileManager.UiTests (complete NUnit project): $($result.Result.testName) - $($result.Message)"
                if ($result.NonBlocking) {
                    $nonBlockingSkipped.Add($skipEntry)
                }
                else {
                    $skipped.Add($skipEntry)
                }
                Write-Host "Skipped NUnit test: $($result.Result.testName)" -ForegroundColor Yellow
            }
            if ($unexpectedIgnoredResults.Count -ne 0) {
                $unexpectedIgnoredResults | Select-Object -First 10 | ForEach-Object { Write-Host "Ignored NUnit test: $($_.testName)" -ForegroundColor Yellow }
                throw "$($unexpectedIgnoredResults.Count) NUnit tests were unexpectedly ignored; quarantined tests must be routed with -NUnitFilter."
            }
            $blockingCapabilitySkipCount = @($expectedCapabilitySkips | Where-Object { -not $_.NonBlocking }).Count
            if ($FailOnSkipped -and $blockingCapabilitySkipCount -ne 0) {
                throw "$blockingCapabilitySkipCount capability-gated NUnit tests were skipped despite -FailOnSkipped. Provision the release test environment before retrying."
            }
        }
    }
    finally {
        if (-not $retainNunitResults -and (Test-Path -LiteralPath $nunitResultsDirectory)) {
            Remove-Item -LiteralPath $nunitResultsDirectory -Recurse -Force
        }
    }
}.GetNewClosure()
Invoke-AutomatedCheck -Name 'FileManager.UiTests (complete NUnit project)' -Action $nunitAction `
    -SkipReason $nunitSkipReason -NonBlockingSkip:$uiTestEnvironmentSkipIsNonBlocking

if ($SkipLockVerifier) {
    # The release workflow owns verifier execution as a separate fresh-volume phase, not a skipped test result.
    Write-Host 'Deferring Application Verifier lock stress to the caller-owned isolated phase.' -ForegroundColor Cyan
}
else {
    $sandboxedUi = -not [string]::IsNullOrWhiteSpace($env:FILEMANAGER_UI_TESTDATA_ROOT) -and
                   [string]::Equals($env:FILEMANAGER_UI_CONFIG_ROOT, 'Software\Open Salamander\6.0-filemanager-testdata', [StringComparison]::OrdinalIgnoreCase)
    $uiExecutable = $env:FILEMANAGER_UI_EXE
    $appVerifierPath = Find-ApplicationVerifier
    $lockStressSkipReason = $null
    if (-not $sandboxedUi) {
        $lockStressSkipReason = 'FILEMANAGER_UI_TESTDATA_ROOT and the suffixed configuration key are required for the Application Verifier lane.'
    }
    elseif ([string]::IsNullOrWhiteSpace($uiExecutable) -or -not (Test-Path -LiteralPath $uiExecutable -PathType Leaf)) {
        $lockStressSkipReason = 'FILEMANAGER_UI_EXE must identify the built executable.'
    }
    elseif ([string]::IsNullOrWhiteSpace($appVerifierPath)) {
        $lockStressSkipReason = 'Application Verifier was not found.'
    }
    elseif (-not (Test-ProcessIsElevated)) {
        # Do not misreport an unavailable privileged diagnostic lane as a product failure on a developer desktop.
        $lockStressSkipReason = 'Application Verifier requires an elevated console to configure Locks for the built executable.'
    }
    $lockStressLogDirectory = Join-Path $repositoryRoot 'TestResults\application-verifier-logs'
    $lockStressArguments = if ($null -eq $lockStressSkipReason) {
        @(
            '-ExecutablePath', $uiExecutable,
            '-TestProject', $testProject,
            '-AppVerifierPath', $appVerifierPath,
            '-LogOutputDirectory', $lockStressLogDirectory
        )
    } else { @() }
    Invoke-WindowsPowerShellScript -RelativePath 'tools\run-lock-verifier-stress.ps1' `
        -ScriptArguments $lockStressArguments -SkipReason $lockStressSkipReason
}

if ($ReleasePipeline) {
    if ($failures.Count -eq 0 -and $skipped.Count -eq 0) {
        # Actions provisions Inno Setup only after the release-test job passes; preserve that boundary so local failures occur in the same phase.
        $preflightAction = {
            Invoke-PinnedInnoSetupPreflight
        }
        $preflightPassed = Invoke-ReleasePipelineCheck -Name 'Preflight pinned Inno Setup' -Action $preflightAction
        # Keep a failed preflight from evaluating an uninitialized downstream result under StrictMode.
        $contractValidationPassed = $false

        if ($preflightPassed) {
            # Keep the workflow's fast post-preflight contract gate ahead of the lengthy Release compilation.
            $contractValidationAction = {
                Invoke-ReleaseWorkflowContractValidation -PowerShellPath $pwsh.Source -DotnetPath $dotnet.Source
            }
            $contractValidationPassed = Invoke-ReleasePipelineCheck -Name 'Validate release workflow contracts' -Action $contractValidationAction
        }

        if ($preflightPassed -and $contractValidationPassed) {
            # Delay the process-wide import until direct Release tools need it; every Debug build enters its own
            # bounded developer shell, and re-entering VsDevCmd after an import can exceed cmd.exe's line limit.
            Import-VisualStudioDeveloperEnvironment -DeveloperCommand $vsDevCmd
            # Keep Release outputs separate from Debug UI artifacts so the PE audit sees the same tree as the installer job.
            $releaseBuildDirectory = Join-Path $testResultsDirectory ('runtests-release-' + [Guid]::NewGuid().ToString('N'))
            Invoke-ReleasePipelinePackaging -DeveloperCommand $vsDevCmd -BuildDirectory $releaseBuildDirectory `
                -Toolset $PlatformToolset -MaxBuildNodes $MaxBuildNodes -ReleaseBuildNumber $BuildNumber
        }
    }
    else {
        # GitHub's build job depends on the release gate, so do not package when its prerequisites were not verified.
        Write-Host 'Skipping Release build and installer because the release-test gate did not pass.' -ForegroundColor Yellow
    }
}

Write-Host "`n=== Automated test summary ===" -ForegroundColor Cyan
Write-Host "$($passed.Count) checks passed." -ForegroundColor Green
$totalSkippedCount = $skipped.Count + $nonBlockingSkipped.Count + $nonBlockingEnvironmentSkipped.Count
if ($nonBlockingEnvironmentSkipped.Count -ne 0) {
    Write-Host "$($nonBlockingEnvironmentSkipped.Count) UI test environment checks skipped as an allowed privilege limitation:" -ForegroundColor Yellow
    $nonBlockingEnvironmentSkipped | ForEach-Object { Write-Host "  - $_" -ForegroundColor Yellow }
}
if ($nonBlockingSkipped.Count -ne 0) {
    Write-Host "$($nonBlockingSkipped.Count) second-volume-dependent tests skipped as an allowed capability limitation:" -ForegroundColor Yellow
    $nonBlockingSkipped | ForEach-Object { Write-Host "  - $_" -ForegroundColor Yellow }
}
if ($skipped.Count -ne 0) {
    Write-Host "$($skipped.Count) blocking checks skipped:" -ForegroundColor Yellow
    $skipped | ForEach-Object { Write-Host "  - $_" -ForegroundColor Yellow }
}
$runnerExitCode = 0
if ($failures.Count -ne 0) {
    Write-Host "$($failures.Count) checks failed:" -ForegroundColor Red
    $failures | ForEach-Object { Write-Host "  - $_" -ForegroundColor Red }
    # Exit only after finally removes this run's disposable build tree.
    $runnerExitCode = 1
}
if ($FailOnSkipped -and $skipped.Count -ne 0) {
    Write-Host 'Skipped checks are prohibited by -FailOnSkipped.' -ForegroundColor Red
    # Preserve a failure result while still executing the cleanup boundary below.
    $runnerExitCode = 1
}

if ($runnerExitCode -eq 0) {
    Write-Host 'All available automated checks passed.' -ForegroundColor Green
}
}
finally {
    if (-not $KeepBuildArtifacts -and (Test-Path -LiteralPath $uiBuildDirectory -PathType Container)) {
        # This per-run GUID directory is owned by the runner; remove it on every exit path to avoid exhausting the test host.
        Remove-Item -LiteralPath $uiBuildDirectory -Recurse -Force
    }
    if (-not $KeepBuildArtifacts -and -not [string]::IsNullOrWhiteSpace($releaseGateBuildDirectory) -and
        (Test-Path -LiteralPath $releaseGateBuildDirectory -PathType Container)) {
        # Retain the TRX artifact but discard the workflow-equivalent staged Debug tree once it is no longer needed.
        Remove-Item -LiteralPath $releaseGateBuildDirectory -Recurse -Force
    }
    if (-not [string]::IsNullOrWhiteSpace($releaseBuildDirectory) -and
        (Test-Path -LiteralPath $releaseBuildDirectory -PathType Container)) {
        if ($KeepBuildArtifacts -or $failures.Count -ne 0) {
            # A release packaging failure must retain its child logs and staged inputs instead of leaving only a summarized exit code.
            Write-Host "Retaining failed Release installer build directory: $releaseBuildDirectory" -ForegroundColor Yellow
        }
        else {
            # Successful Release outputs are disposable per-run artifacts unless the caller explicitly asks to inspect them.
            Remove-Item -LiteralPath $releaseBuildDirectory -Recurse -Force
        }
    }
    Remove-OlderUiTestBuildResults -ResultsDirectory $testResultsDirectory
}

exit $runnerExitCode
