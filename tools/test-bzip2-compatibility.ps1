[CmdletBinding()]
param(
    [ValidateSet('x64', 'x86')]
    [string]$Architecture = 'x64',
    [ValidateRange(1, 10000)]
    [int]$Iterations = 1
)

$ErrorActionPreference = 'Stop'
$repositoryRoot = Split-Path -Parent $PSScriptRoot
$bzip2Directory = Join-Path $repositoryRoot 'src\common\dep\bzip2'
$fixtureDirectory = Join-Path $repositoryRoot 'tests\bzip2-vectors'
$probeSource = Join-Path $PSScriptRoot 'bzip2_compatibility_probe.c'
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

$temporaryDirectory = Join-Path ([System.IO.Path]::GetTempPath()) ('OpenSalamander-bzip2-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $temporaryDirectory | Out-Null

try {
    $sourceFiles = @(
        'blocksort.c', 'bzlib.c', 'compress.c', 'crctable.c',
        'decompress.c', 'huffman.c', 'randtable.c'
    ) | ForEach-Object { '"' + (Join-Path $bzip2Directory $_) + '"' }
    $probeExecutable = Join-Path $temporaryDirectory 'bzip2_compatibility_probe.exe'
    $compilerCommand = (
        (Get-VisualStudioCleanEnvironmentPreamble) + ' && call "' + $vsDevCmd + '" -arch=' + $Architecture + ' -host_arch=x64 && cd /d "' + $temporaryDirectory + '" && ' +
        # Upstream's portability and assertion macros intentionally trigger these
        # MSVC-only constant-condition and integer-conversion diagnostics.
        'cl /nologo /TC /W4 /WX /wd4100 /wd4127 /wd4244 /wd4245 /DBZ_NO_STDIO /I"' + $bzip2Directory + '" ' +
        '/Fe"' + $probeExecutable + '" "' + $probeSource + '" ' + ($sourceFiles -join ' ')
    )

    # Build the checked-in parser, not a system DLL, before replaying retained hostile streams.
    & $env:ComSpec /d /c $compilerCommand
    if ($LASTEXITCODE -ne 0) {
        throw "bzip2 compatibility probe compilation failed with exit code $LASTEXITCODE."
    }

    $fuzzFixtures = Get-ChildItem -LiteralPath (Join-Path $fixtureDirectory 'fuzz') -Filter '*.hex' | Sort-Object Name | Select-Object -ExpandProperty FullName
    for ($iteration = 1; $iteration -le $Iterations; $iteration++) {
        # Reuse the one verified parser build so scheduled fuzz soaking measures parsing, not compiler churn.
        & $probeExecutable `
            (Join-Path $fixtureDirectory 'golden-stream.hex') `
            (Join-Path $fixtureDirectory 'legacy-stream.hex') `
            (Join-Path $fixtureDirectory 'truncated-stream.hex') `
            $fuzzFixtures
        if ($LASTEXITCODE -ne 0) {
            throw "bzip2 compatibility probe failed on iteration $iteration with exit code $LASTEXITCODE."
        }
    }
}
finally {
    if (Test-Path -LiteralPath $temporaryDirectory) {
        # The GUID-named directory belongs only to this probe and must not persist between CI runs.
        Remove-Item -LiteralPath $temporaryDirectory -Recurse -Force
    }
}
