[CmdletBinding()]
param(
    [ValidateSet('x64', 'x86')]
    [string]$Architecture = 'x64'
)

$ErrorActionPreference = 'Stop'
$repositoryRoot = Split-Path -Parent $PSScriptRoot
$zlibDirectory = Join-Path $repositoryRoot 'src\common\dep\zlib'
$fixtureDirectory = Join-Path $repositoryRoot 'tests\zlib-vectors'
$probeSource = Join-Path $PSScriptRoot 'zlib_compatibility_probe.c'
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

$temporaryDirectory = Join-Path ([System.IO.Path]::GetTempPath()) ('OpenSalamander-zlib-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $temporaryDirectory | Out-Null

try {
    $sourceFiles = @(
        'adler32.c', 'compress.c', 'crc32.c', 'deflate.c', 'infback.c',
        'inffast.c', 'inflate.c', 'inftrees.c', 'trees.c', 'uncompr.c', 'zutil.c'
    ) | ForEach-Object { '"' + (Join-Path $zlibDirectory $_) + '"' }
    $probeExecutable = Join-Path $temporaryDirectory 'zlib_compatibility_probe.exe'
    $compilerCommand = (
        'call "' + $vsDevCmd + '" -arch=' + $Architecture + ' -host_arch=x64 && cd /d "' + $temporaryDirectory + '" && ' +
        'cl /nologo /TC /W4 /I"' + $zlibDirectory + '" ' +
        '/Fe"' + $probeExecutable + '" "' + $probeSource + '" ' + ($sourceFiles -join ' ')
    )

    # Build the checked-in sources, not a system DLL, so compatibility follows the product's vendor boundary.
    & $env:ComSpec /d /c $compilerCommand
    if ($LASTEXITCODE -ne 0) {
        throw "zlib compatibility probe compilation failed with exit code $LASTEXITCODE."
    }

    & $probeExecutable `
        (Join-Path $fixtureDirectory 'legacy-zlib-1.2.11.hex') `
        (Join-Path $fixtureDirectory 'truncated-zlib-stream.hex') `
        (Join-Path $fixtureDirectory 'bad-adler32-zlib-stream.hex') `
        (Join-Path $fixtureDirectory 'invalid-deflate-zlib-stream.hex')
    if ($LASTEXITCODE -ne 0) {
        throw "zlib compatibility probe failed with exit code $LASTEXITCODE."
    }
}
finally {
    if (Test-Path -LiteralPath $temporaryDirectory) {
        Remove-Item -LiteralPath $temporaryDirectory -Recurse -Force
    }
}
