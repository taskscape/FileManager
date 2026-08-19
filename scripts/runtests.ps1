[CmdletBinding()]
param(
    [string]$BaseCommit,
    [string]$SqliteDll,
    [switch]$FailOnSkipped,
    [switch]$KeepBuildArtifacts,
    [ValidateSet('v143', 'v145')]
    [string]$PlatformToolset = 'v145',
    [string]$NUnitTrxPath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repositoryRoot = Split-Path -Parent $PSScriptRoot
$testProject = Join-Path $repositoryRoot 'tests\FileManager.UiTests\FileManager.UiTests.csproj'
$nativeSolution = Join-Path $repositoryRoot 'src\vcxproj\salamand.sln'
$nativeSafetyProject = Join-Path $repositoryRoot 'tests\NativeSafetyTests\NativeSafetyTests.vcxproj'
$pictViewEngineProject = Join-Path $repositoryRoot 'tests\PictViewEngineTests\PictViewEngineTests.vcxproj'
$failures = [System.Collections.Generic.List[string]]::new()
$passed = [System.Collections.Generic.List[string]]::new()
$skipped = [System.Collections.Generic.List[string]]::new()

function Invoke-AutomatedCheck {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Name,
        [Parameter(Mandatory = $true)]
        [scriptblock]$Action,
        [string]$SkipReason
    )

    Write-Host "`n=== Running $Name ===" -ForegroundColor Cyan
    if (-not [string]::IsNullOrWhiteSpace($SkipReason)) {
        # Optional lanes must remain visible in the aggregate report even when
        # this machine cannot safely satisfy their external prerequisites.
        $skipped.Add("${Name}: $SkipReason")
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
        $installationPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
        if (-not [string]::IsNullOrWhiteSpace($installationPath)) {
            $candidates.Add((Join-Path $installationPath.Trim() 'Common7\Tools\VsDevCmd.bat'))
        }
    }

    return $candidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
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

function Build-UiTestApplication {
    param(
        [Parameter(Mandatory = $true)]
        [string]$DeveloperCommand,
        [Parameter(Mandatory = $true)]
        [string]$BuildDirectory,
        [Parameter(Mandatory = $true)]
        [ValidateSet('v143', 'v145')]
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
    $buildCommand = 'call "' + $DeveloperCommand + '" -arch=x64 -host_arch=x64 && msbuild "' + $nativeSolution +
        '" /m /t:Build /p:Configuration=Debug /p:Platform=x64 /p:PlatformToolset=' + $Toolset + ' /p:PreferredToolArchitecture=x64 /p:OPENSAL_BUILD_DIR=' +
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
        [ValidateSet('v143', 'v145')]
        [string]$Toolset
    )

    if (-not (Test-Path -LiteralPath $nativeSafetyProject -PathType Leaf)) {
        throw "The native safety test project was not found: $nativeSafetyProject"
    }

    # Keep the native executable independent of the product solution so its
    # pure boundary checks run quickly after the main build has produced UI artifacts.
    $buildCommand = 'call "' + $DeveloperCommand + '" -arch=x64 -host_arch=x64 && msbuild "' + $nativeSafetyProject +
        '" /m /t:Build /p:Configuration=Debug /p:Platform=x64 /p:PlatformToolset=' + $Toolset + ' /nr:false'
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
        [ValidateSet('v143', 'v145')]
        [string]$Toolset
    )

    if (-not (Test-Path -LiteralPath $pictViewEngineProject -PathType Leaf)) {
        throw "The PictView engine test project was not found: $pictViewEngineProject"
    }

    # The engine links straight into a console host, so its decode, transform and
    # encode round trips run without a desktop session or the plug-in host.
    $buildCommand = 'call "' + $DeveloperCommand + '" -arch=x64 -host_arch=x64 && msbuild "' + $pictViewEngineProject +
        '" /m /t:Build /p:Configuration=Debug /p:Platform=x64 /p:PlatformToolset=' + $Toolset + ' /nr:false'
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

function Resolve-UiTestVolume {
    param(
        [Parameter(Mandatory = $true)]
        [string[]]$RequiredFileSystems,
        [Parameter(Mandatory = $true)]
        [string]$Purpose
    )

    $temporaryVolume = [System.IO.Path]::GetPathRoot([System.IO.Path]::GetTempPath())
    $volume = Get-CimInstance Win32_LogicalDisk |
        Where-Object {
            $_.DriveType -in 2, 3 -and
            -not [string]::IsNullOrWhiteSpace($_.DeviceID) -and
            ($RequiredFileSystems -contains $_.FileSystem) -and
            -not [string]::Equals("$($_.DeviceID)\", $temporaryVolume, [StringComparison]::OrdinalIgnoreCase) -and
            $_.FreeSpace -ge 1GB
        } |
        Select-Object -First 1
    if ($null -eq $volume) {
        throw "The complete UI suite requires a writable $($RequiredFileSystems -join '/') volume distinct from $temporaryVolume for $Purpose; no such volume is available."
    }

    return "$($volume.DeviceID)\"
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
        throw "The complete UI suite requires permission to create disposable directory symbolic links: $_"
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

function Resolve-FtpMenuCommand {
    param(
        [Parameter(Mandatory = $true)]
        [string]$ExecutablePath,

        [Parameter(Mandatory = $true)]
        [string]$Caption
    )

    if ($null -eq ('FileManager.NativeMenuProbe' -as [type])) {
        Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;

namespace FileManager {
    public static class NativeMenuProbe {
        [DllImport("user32.dll", CharSet = CharSet.Unicode)]
        public static extern IntPtr GetMenu(IntPtr hWnd);

        [DllImport("user32.dll")]
        public static extern int GetMenuItemCount(IntPtr hMenu);

        [DllImport("user32.dll")]
        public static extern IntPtr GetSubMenu(IntPtr hMenu, int nPos);

        [DllImport("user32.dll", CharSet = CharSet.Unicode)]
        public static extern int GetMenuString(IntPtr hMenu, uint uIDItem, char[] lpString, int cchMax, uint flags);

        [DllImport("user32.dll")]
        public static extern uint GetMenuItemID(IntPtr hMenu, int nPos);
    }
}
'@
    }

    # Inspect the menu from this freshly built executable; plug-in SUIDs change
    # with load order, so a workflow or caller cannot safely supply this value.
    $process = Start-Process -FilePath $ExecutablePath -PassThru -WindowStyle Hidden
    try {
        $deadline = [DateTime]::UtcNow.AddSeconds(20)
        do {
            $process.Refresh()
            if ($process.MainWindowHandle -ne [IntPtr]::Zero) {
                break
            }
            Start-Sleep -Milliseconds 100
        } while ([DateTime]::UtcNow -lt $deadline)

        if ($process.MainWindowHandle -eq [IntPtr]::Zero) {
            throw 'The built FileManager executable did not expose a main window for FTP command discovery.'
        }

        $rootMenu = [FileManager.NativeMenuProbe]::GetMenu($process.MainWindowHandle)
        if ($rootMenu -eq [IntPtr]::Zero) {
            throw 'The built FileManager executable did not expose a native menu for FTP command discovery.'
        }

        $findCommand = $null
        $findCommand = {
            param([IntPtr]$menu)
            for ($index = 0; $index -lt [FileManager.NativeMenuProbe]::GetMenuItemCount($menu); $index++) {
                $captionBuffer = New-Object char[] 512
                [void][FileManager.NativeMenuProbe]::GetMenuString($menu, [uint32]$index, $captionBuffer, $captionBuffer.Length, 0x400)
                $caption = -join $captionBuffer
                $caption = $caption.Trim([char]0).Replace('&', '').Split("`t")[0]
                if ($caption -eq $Caption) {
                    $command = [FileManager.NativeMenuProbe]::GetMenuItemID($menu, $index)
                    if ($command -ne [uint32]::MaxValue) {
                        return [int]$command
                    }
                }

                $submenu = [FileManager.NativeMenuProbe]::GetSubMenu($menu, $index)
                if ($submenu -ne [IntPtr]::Zero) {
                    $command = & $findCommand $submenu
                    if ($null -ne $command) {
                        return $command
                    }
                }
            }
        }

        $command = & $findCommand $rootMenu
        if ($null -eq $command -or $command -le 0) {
            throw "The built FileManager menu does not contain the FTP Client $Caption command."
        }

        return $command
    }
    finally {
        if (-not $process.HasExited) {
            Stop-Process -Id $process.Id -Force
        }
        $process.Dispose()
    }
}

function Set-UiTestEnvironment {
    param(
        [Parameter(Mandatory = $true)]
        [string]$ExecutablePath
    )

    Initialize-UiTestSandbox
    $env:FILEMANAGER_UI_EXE = $ExecutablePath
    if ([string]::IsNullOrWhiteSpace($env:FILEMANAGER_UI_CROSS_VOLUME_ROOT)) {
        try {
            # Additional-volume lanes are optional when the current host exposes only one writable drive.
            $env:FILEMANAGER_UI_CROSS_VOLUME_ROOT = Join-Path (Resolve-UiTestVolume -RequiredFileSystems @('NTFS') -Purpose 'cross-volume move tests') 'filemanager-testdata'
        }
        catch { }
    }
    if ([string]::IsNullOrWhiteSpace($env:FILEMANAGER_UI_ADS_UNSUPPORTED_TARGET_ROOT)) {
        try {
            # An ADS-unsupported target cannot be synthesized, so retain the case as an explicit NUnit skip when absent.
            $env:FILEMANAGER_UI_ADS_UNSUPPORTED_TARGET_ROOT = Join-Path (Resolve-UiTestVolume -RequiredFileSystems @('FAT', 'FAT32', 'exFAT') -Purpose 'ADS-loss tests') 'filemanager-testdata'
        }
        catch { }
    }
    Assert-UiTestSymbolicLinkSupport

    if ([string]::IsNullOrWhiteSpace($env:FILEMANAGER_UI_FTP_ORGANIZE_COMMAND)) {
        # Resolve dynamic plug-in commands from this build rather than assuming a load-order-dependent SUID.
        $env:FILEMANAGER_UI_FTP_ORGANIZE_COMMAND = Resolve-FtpMenuCommand -ExecutablePath $ExecutablePath -Caption 'Organize Bookmarks...'
    }
    if ([string]::IsNullOrWhiteSpace($env:FILEMANAGER_UI_FTP_CONNECT_COMMAND)) {
        # The protocol fixture drives the actual quick-connect dialog through the same runtime menu surface as a user.
        $env:FILEMANAGER_UI_FTP_CONNECT_COMMAND = Resolve-FtpMenuCommand -ExecutablePath $ExecutablePath -Caption 'Connect to FTP Server...'
    }
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

$testResultsDirectory = Join-Path $repositoryRoot 'TestResults'
# Prune interrupted runs before preflight checks can exit, so an unavailable toolchain cannot defer retention indefinitely.
Remove-OlderUiTestBuildResults -ResultsDirectory $testResultsDirectory

$vsDevCmd = Find-VisualStudioDeveloperCommand
if ([string]::IsNullOrWhiteSpace($vsDevCmd)) {
    throw 'Visual Studio C++ developer tools were not found; the complete UI suite cannot build the current solution.'
}
$dotnet = Get-Command dotnet.exe -ErrorAction SilentlyContinue | Select-Object -First 1
if ($null -eq $dotnet) {
    throw '.NET 8 SDK was not found; the complete UI suite cannot run the NUnit project.'
}

$uiBuildDirectory = Join-Path $testResultsDirectory ('runtests-build-' + [Guid]::NewGuid().ToString('N'))
try {
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
try {
    Set-UiTestEnvironment -ExecutablePath $builtUiExecutable
}
catch {
    # Missing disposable-profile or topology prerequisites must not hide the independent native and source checks.
    $uiTestEnvironmentSkipReason = $_.Exception.Message
}

# Fast source contracts and native compatibility probes are always collected;
# architecture-aware probes run both variants declared by their public scripts.
foreach ($relativePath in @(
    'tools\verify-operation-completion-protocol.ps1',
    'tools\verify-durable-copy-commit.ps1',
    # Exercise the diff ratchet in an isolated Git history before release publication.
    'tools\test-raw-thread-creation-verifier.ps1',
    # Keep release-input provenance enforced by the same aggregate test inventory.
    'tools\test-release-input-pinning.ps1',
    # The content-fingerprint baseline rejects unsafe calls even when a diff hunk is unavailable.
    'tools\test-unsafe-api-baseline.ps1'
)) {
    Invoke-WindowsPowerShellScript -RelativePath $relativePath
}

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
    'call "' + $vsDevCmd + '" -arch=x64 -host_arch=x64 && powershell.exe -NoProfile -ExecutionPolicy Bypass -File "' + $cmarkScript + '"'
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
# The shipped wrapper and an independent console must agree on the retained archive corpus before release.
Invoke-AutomatedCheck -Name '7-Zip wrapper/oracle compatibility corpus' -Action $sevenZipAction -SkipReason $sevenZipSkipReason

$resolvedSqliteDll = $builtSqliteDll
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

$resolvedBaseCommit = Resolve-BaseCommit -RequestedCommit $BaseCommit
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
        & $dotnet.Source test $testProject --results-directory $nunitResultsDirectory `
            --logger "trx;LogFileName=$nunitTrxName" --logger 'console;verbosity=minimal' -- NUnit.NumberOfTestWorkers=0
        $dotnetExitCode = $LASTEXITCODE
        if ($dotnetExitCode -ne 0) {
            throw "The complete NUnit project failed with exit code $dotnetExitCode."
        }

        # VSTest treats NUnit Assert.Ignore as success.  The root runner owns
        # UI prerequisites, so every ignored result is a configuration defect.
        [xml]$trx = Get-Content -LiteralPath (Join-Path $nunitResultsDirectory $nunitTrxName) -Raw
        $ignoredResults = @($trx.SelectNodes("//*[local-name()='UnitTestResult' and @outcome='NotExecuted']"))
        if ($ignoredResults.Count -ne 0) {
            $ignoredNames = $ignoredResults | Select-Object -First 10 | ForEach-Object { $_.testName }
            $ignoredNames | ForEach-Object { Write-Host "Ignored NUnit test: $_" -ForegroundColor Yellow }
            throw "$($ignoredResults.Count) NUnit tests were ignored; runtests.ps1 must configure every UI prerequisite."
        }
    }
    finally {
        if (-not $retainNunitResults -and (Test-Path -LiteralPath $nunitResultsDirectory)) {
            Remove-Item -LiteralPath $nunitResultsDirectory -Recurse -Force
        }
    }
}.GetNewClosure()
Invoke-AutomatedCheck -Name 'FileManager.UiTests (complete NUnit project)' -Action $nunitAction -SkipReason $nunitSkipReason

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

Write-Host "`n=== Automated test summary ===" -ForegroundColor Cyan
Write-Host "$($passed.Count) checks passed." -ForegroundColor Green
if ($skipped.Count -ne 0) {
    Write-Host "$($skipped.Count) checks skipped:" -ForegroundColor Yellow
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
    Remove-OlderUiTestBuildResults -ResultsDirectory $testResultsDirectory
}

exit $runnerExitCode
