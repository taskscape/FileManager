[CmdletBinding()]
param(
    [string]$BaseCommit,
    [string]$SqliteDll,
    [switch]$FailOnSkipped
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repositoryRoot = $PSScriptRoot
$testProject = Join-Path $repositoryRoot 'tests\FileManager.UiTests\FileManager.UiTests.csproj'
$nativeSolution = Join-Path $repositoryRoot 'src\vcxproj\salamand.sln'
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
    $command = Get-Command appverif.exe -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($null -ne $command) {
        return $command.Source
    }

    # Windows SDK installations do not always add the debugger tools to PATH,
    # so the strict release lane also searches their standard installation root.
    $debuggerRoot = Join-Path ([Environment]::GetFolderPath([Environment+SpecialFolder]::ProgramFilesX86)) 'Windows Kits\10\Debuggers'
    if (Test-Path -LiteralPath $debuggerRoot -PathType Container) {
        return Get-ChildItem -LiteralPath $debuggerRoot -Filter appverif.exe -Recurse -ErrorAction SilentlyContinue |
            Select-Object -First 1 -ExpandProperty FullName
    }

    return $null
}

function Build-UiTestApplication {
    param(
        [Parameter(Mandatory = $true)]
        [string]$DeveloperCommand,
        [Parameter(Mandatory = $true)]
        [string]$BuildDirectory
    )

    if (-not (Test-Path -LiteralPath $nativeSolution -PathType Leaf)) {
        throw "The native FileManager solution was not found: $nativeSolution"
    }

    New-Item -ItemType Directory -Path $BuildDirectory -Force | Out-Null
    # Build the complete Debug x64 solution into a per-run directory so UI tests
    # always exercise this checkout rather than a caller-provided executable.
    $buildCommand = 'call "' + $DeveloperCommand + '" -arch=x64 -host_arch=x64 && msbuild "' + $nativeSolution +
        '" /m /t:Build /p:Configuration=Debug /p:Platform=x64 /p:PlatformToolset=v145 /p:PreferredToolArchitecture=x64 /p:OPENSAL_BUILD_DIR="' +
        $BuildDirectory + '\" /nr:false'
    & $env:ComSpec /d /s /c $buildCommand
    if ($LASTEXITCODE -ne 0) {
        throw "Building the Debug x64 FileManager solution failed with exit code $LASTEXITCODE."
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

function Assert-UiTestSymbolicLinkSupport {
    $root = Join-Path ([System.IO.Path]::GetTempPath()) ('FileManager-runtests-symlink-' + [Guid]::NewGuid().ToString('N'))
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
    }
}

function Resolve-FtpOrganizeBookmarksCommand {
    param(
        [Parameter(Mandatory = $true)]
        [string]$ExecutablePath
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
                if ($caption -eq 'Organize Bookmarks...') {
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
            throw 'The built FileManager menu does not contain the FTP Client Organize Bookmarks command.'
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

    $env:FILEMANAGER_UI_ISOLATED = '1'
    $env:FILEMANAGER_UI_EXE = $ExecutablePath
    $env:FILEMANAGER_UI_CONFIG_FAULT_INJECTION = '1'
    $env:FILEMANAGER_UI_RECYCLE_BIN = '1'
    $env:FILEMANAGER_UI_CROSS_VOLUME_ROOT = Resolve-UiTestVolume -RequiredFileSystems @('NTFS') -Purpose 'cross-volume move tests'
    $env:FILEMANAGER_UI_ADS_UNSUPPORTED_TARGET_ROOT = Resolve-UiTestVolume -RequiredFileSystems @('FAT', 'FAT32', 'exFAT') -Purpose 'ADS-loss tests'
    Assert-UiTestSymbolicLinkSupport

    $env:FILEMANAGER_UI_FTP_ORGANIZE_COMMAND = Resolve-FtpOrganizeBookmarksCommand -ExecutablePath $ExecutablePath
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

$vsDevCmd = Find-VisualStudioDeveloperCommand
if ([string]::IsNullOrWhiteSpace($vsDevCmd)) {
    throw 'Visual Studio C++ developer tools were not found; the complete UI suite cannot build the current solution.'
}
$dotnet = Get-Command dotnet.exe -ErrorAction SilentlyContinue | Select-Object -First 1
if ($null -eq $dotnet) {
    throw '.NET 8 SDK was not found; the complete UI suite cannot run the NUnit project.'
}

$uiBuildDirectory = Join-Path (Join-Path $repositoryRoot 'TestResults') ('runtests-build-' + [Guid]::NewGuid().ToString('N'))
Build-UiTestApplication -DeveloperCommand $vsDevCmd -BuildDirectory $uiBuildDirectory
$builtUiExecutable = Resolve-UiTestArtifact -BuildDirectory $uiBuildDirectory -FileName 'salamand.exe'
$builtSqliteDll = Resolve-UiTestArtifact -BuildDirectory $uiBuildDirectory -FileName 'sqlite.dll'
Set-UiTestEnvironment -ExecutablePath $builtUiExecutable

# Fast source contracts and native compatibility probes are always collected;
# architecture-aware probes run both variants declared by their public scripts.
foreach ($relativePath in @(
    'tools\verify-operation-completion-protocol.ps1',
    'tools\verify-durable-copy-commit.ps1',
    # Keep release-input provenance enforced by the same aggregate test inventory.
    'tools\test-release-input-pinning.ps1'
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
    'tools\verify-no-new-max-path-buffers.ps1',
    'tools\verify-no-new-unsafe-string-calls.ps1'
)) {
    $arguments = if ($null -ne $resolvedBaseCommit) { @('-BaseCommit', $resolvedBaseCommit) } else { @() }
    Invoke-WindowsPowerShellScript -RelativePath $relativePath -ScriptArguments $arguments -SkipReason $ratchetSkipReason
}

Invoke-WindowsPowerShellScript -RelativePath 'tools\verify-fluent-icon-coverage.ps1'

$nunitSkipReason = $null
$nunitResultsDirectory = Join-Path ([System.IO.Path]::GetTempPath()) ('FileManager-runtests-' + [Guid]::NewGuid().ToString('N'))
$nunitTrxName = 'runtests.trx'
$nunitAction = {
    # Running the complete project discovers every fixture after this runner
    # has supplied the executable and every prerequisite UI-test environment value.
    New-Item -ItemType Directory -Path $nunitResultsDirectory | Out-Null
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
        if (Test-Path -LiteralPath $nunitResultsDirectory) {
            Remove-Item -LiteralPath $nunitResultsDirectory -Recurse -Force
        }
    }
}.GetNewClosure()
Invoke-AutomatedCheck -Name 'FileManager.UiTests (complete NUnit project)' -Action $nunitAction -SkipReason $nunitSkipReason

$isolatedUi = [string]::Equals($env:FILEMANAGER_UI_ISOLATED, '1', [StringComparison]::Ordinal)
$uiExecutable = $env:FILEMANAGER_UI_EXE
$appVerifierPath = Find-ApplicationVerifier
$lockStressSkipReason = $null
if (-not $isolatedUi) {
    $lockStressSkipReason = 'FILEMANAGER_UI_ISOLATED=1 is required for the Application Verifier lane.'
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
if ($failures.Count -ne 0) {
    Write-Host "$($failures.Count) checks failed:" -ForegroundColor Red
    $failures | ForEach-Object { Write-Host "  - $_" -ForegroundColor Red }
    exit 1
}
if ($FailOnSkipped -and $skipped.Count -ne 0) {
    Write-Host 'Skipped checks are prohibited by -FailOnSkipped.' -ForegroundColor Red
    exit 1
}

Write-Host 'All available automated checks passed.' -ForegroundColor Green
exit 0
