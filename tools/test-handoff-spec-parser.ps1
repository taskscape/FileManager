[CmdletBinding()]
param(
    [ValidateSet('x64', 'x86')]
    [string]$Architecture = 'x64',
    [ValidateRange(0, 10000)]
    [int]$Iterations = 1
)

# Delivery Handoff specification parser probe (handoff-spec.md C.12.2): builds the
# plug-in's host-independent parser with cl.exe, replays the checked-in corpus
# against its expected findings, and soaks deterministic mutations of every input.

$ErrorActionPreference = 'Stop'
$repositoryRoot = Split-Path -Parent $PSScriptRoot
$engineDirectory = Join-Path $repositoryRoot 'src\plugins\handoff\engine'
$templateDirectory = Join-Path $repositoryRoot 'src\plugins\handoff\templates'
$corpusDirectory = Join-Path $repositoryRoot 'tests\handoff-specs'
$probeSource = Join-Path $PSScriptRoot 'handoff_spec_parser_probe.cpp'
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vsDevCmdCandidates = @(
    # Prefer the repository's authoritative VS 2026 environment when it is installed.
    (Join-Path ${env:ProgramW6432} 'Microsoft Visual Studio\18\Insiders\Common7\Tools\VsDevCmd.bat')
)
if (Test-Path -LiteralPath $vswhere) {
    $vsInstall = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not [string]::IsNullOrWhiteSpace($vsInstall)) {
        $vsDevCmdCandidates += Join-Path $vsInstall.Trim() 'Common7\Tools\VsDevCmd.bat'
    }
}
$vsDevCmd = $vsDevCmdCandidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
if ([string]::IsNullOrWhiteSpace($vsDevCmd)) {
    throw 'No Visual Studio developer command environment was found.'
}

# Clear a previously imported VS shell inside the probe's cmd.exe so VsDevCmd does not duplicate compiler paths beyond cmd.exe's line limit.
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

$temporaryDirectory = Join-Path ([System.IO.Path]::GetTempPath()) ('OpenSalamander-handoff-spec-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $temporaryDirectory | Out-Null

try {
    # Only the specification parser and the units it depends on; no WIC, WinRT, or file I/O.
    $sourceFiles = @(
        'json.cpp', 'spec.cpp', 'patterns.cpp', 'name_template.cpp', 'findings.cpp', 'formats.cpp', 'text_util.cpp'
    ) | ForEach-Object { '"' + (Join-Path $engineDirectory $_) + '"' }
    $probeExecutable = Join-Path $temporaryDirectory 'handoff_spec_parser_probe.exe'
    $compilerCommand = (
        (Get-VisualStudioCleanEnvironmentPreamble) + ' && call "' + $vsDevCmd + '" -arch=' + $Architecture + ' -host_arch=x64 && cd /d "' + $temporaryDirectory + '" && ' +
        # The probe compiles without /J, the opposite of the plug-in build, so engine
        # code that depended on char signedness would diverge between the two.
        # The VS 2026 STL bounds regex backtracking itself; an older hosted toolset
        # (VS 2022) honours these limits instead, so hostile patterns stay bounded there too.
        'cl /nologo /std:c++latest /EHsc /W4 /WX /sdl /guard:cf /utf-8 /DWINVER=0x0A00 /D_WIN32_WINNT=0x0A00 /D_CRT_SECURE_NO_WARNINGS ' +
        '/D_REGEX_MAX_COMPLEXITY_COUNT=10000000 /D_REGEX_MAX_STACK_COUNT=1000 ' +
        '/I"' + $engineDirectory + '" /I"' + (Join-Path $repositoryRoot 'src') + '" ' +
        '/Fe"' + $probeExecutable + '" "' + $probeSource + '" ' + ($sourceFiles -join ' ') + ' Normaliz.lib'
    )

    # Build the checked-in parser before replaying hostile specifications.
    & $env:ComSpec /d /c $compilerCommand
    if ($LASTEXITCODE -ne 0) {
        throw "Handoff specification parser probe compilation failed with exit code $LASTEXITCODE."
    }

    # Templates must validate cleanly; corpus files compare with their .expected findings.
    & $probeExecutable `
        --valid $templateDirectory `
        --valid (Join-Path $corpusDirectory 'valid') `
        --invalid (Join-Path $corpusDirectory 'invalid') `
        --hostile (Join-Path $corpusDirectory 'hostile') `
        --iterations $Iterations
    if ($LASTEXITCODE -ne 0) {
        throw "Handoff specification parser probe failed with exit code $LASTEXITCODE."
    }
}
finally {
    if (Test-Path -LiteralPath $temporaryDirectory) {
        # The GUID-named directory belongs only to this probe and must not persist between CI runs.
        Remove-Item -LiteralPath $temporaryDirectory -Recurse -Force
    }
}
