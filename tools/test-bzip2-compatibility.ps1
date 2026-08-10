[CmdletBinding()]
param(
    [ValidateSet('x64', 'x86')]
    [string]$Architecture = 'x64'
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

$temporaryDirectory = Join-Path ([System.IO.Path]::GetTempPath()) ('OpenSalamander-bzip2-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $temporaryDirectory | Out-Null

try {
    $sourceFiles = @(
        'blocksort.c', 'bzlib.c', 'compress.c', 'crctable.c',
        'decompress.c', 'huffman.c', 'randtable.c'
    ) | ForEach-Object { '"' + (Join-Path $bzip2Directory $_) + '"' }
    $probeExecutable = Join-Path $temporaryDirectory 'bzip2_compatibility_probe.exe'
    $compilerCommand = (
        'call "' + $vsDevCmd + '" -arch=' + $Architecture + ' -host_arch=x64 && cd /d "' + $temporaryDirectory + '" && ' +
        # Upstream's portable integer aliases intentionally trigger these MSVC-only conversion diagnostics.
        'cl /nologo /TC /W4 /WX /wd4100 /wd4244 /wd4245 /DBZ_NO_STDIO /I"' + $bzip2Directory + '" ' +
        '/Fe"' + $probeExecutable + '" "' + $probeSource + '" ' + ($sourceFiles -join ' ')
    )

    # Build the checked-in parser, not a system DLL, before replaying retained hostile streams.
    & $env:ComSpec /d /c $compilerCommand
    if ($LASTEXITCODE -ne 0) {
        throw "bzip2 compatibility probe compilation failed with exit code $LASTEXITCODE."
    }

    $fuzzFixtures = Get-ChildItem -LiteralPath (Join-Path $fixtureDirectory 'fuzz') -Filter '*.hex' | Sort-Object Name | Select-Object -ExpandProperty FullName
    & $probeExecutable `
        (Join-Path $fixtureDirectory 'golden-stream.hex') `
        (Join-Path $fixtureDirectory 'legacy-stream.hex') `
        (Join-Path $fixtureDirectory 'truncated-stream.hex') `
        $fuzzFixtures
    if ($LASTEXITCODE -ne 0) {
        throw "bzip2 compatibility probe failed with exit code $LASTEXITCODE."
    }
}
finally {
    if (Test-Path -LiteralPath $temporaryDirectory) {
        # The GUID-named directory belongs only to this probe and must not persist between CI runs.
        Remove-Item -LiteralPath $temporaryDirectory -Recurse -Force
    }
}
