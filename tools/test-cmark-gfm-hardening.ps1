[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$repositoryRoot = Split-Path -Parent $PSScriptRoot
$workDirectory = Join-Path ([System.IO.Path]::GetTempPath()) ('cmark-gfm-hardening-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $workDirectory | Out-Null

# Compile the same vendor sources and bounded renderer used by the IE Viewer.
$cmarkRoot = Join-Path $repositoryRoot 'src\plugins\ieviewer\cmark-gfm'
$sourceFiles = @(
    (Get-ChildItem -LiteralPath (Join-Path $cmarkRoot 'src') -Filter '*.c' | Where-Object Name -ne 'main.c' | Select-Object -ExpandProperty FullName),
    (Get-ChildItem -LiteralPath (Join-Path $cmarkRoot 'extensions') -Filter '*.c' | Select-Object -ExpandProperty FullName),
    (Join-Path $repositoryRoot 'src\plugins\ieviewer\markdown_rendering.cpp'),
    (Join-Path $repositoryRoot 'tools\cmark_gfm_hardening_probe.cpp')
)
$includeDirectories = @(
    (Join-Path $repositoryRoot 'src\plugins\ieviewer'),
    (Join-Path $cmarkRoot 'src'),
    (Join-Path $cmarkRoot 'extensions'),
    (Join-Path $cmarkRoot 'build\src'),
    (Join-Path $cmarkRoot 'build\extensions')
)
$arguments = @('/nologo', '/std:c++latest', '/EHsc', '/W4', '/WX', '/wd4100', '/D_CRT_SECURE_NO_WARNINGS', '/DCMARK_GFM_STATIC_DEFINE', '/DCMARK_GFM_EXTENSIONS_STATIC_DEFINE')
$arguments += $includeDirectories | ForEach-Object { '/I' + $_ }
$arguments += $sourceFiles
$arguments += '/Fe:' + (Join-Path $workDirectory 'cmark_gfm_hardening_probe.exe')

Push-Location $workDirectory
try
{
    & cl.exe @arguments
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    & (Join-Path $workDirectory 'cmark_gfm_hardening_probe.exe') $repositoryRoot
    exit $LASTEXITCODE
}
finally
{
    Pop-Location
    # The GUID-named directory belongs only to this probe and must not persist between CI runs.
    Remove-Item -LiteralPath $workDirectory -Recurse -Force
}
