param([string]$VsDevCmd)
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$output = Join-Path $repo 'TestResults\fluent-rendering'
New-Item -ItemType Directory -Force $output | Out-Null

# The same VS 2026 toolchain as the application is required; never install tools on a runner.
if (-not $VsDevCmd) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path $vswhere)) { throw 'Provide -VsDevCmd from an installed Visual Studio 2026 instance.' }
    $installation = & $vswhere -latest -version '[18.0,19.0)' -products '*' -requires Microsoft.Component.MSBuild -property installationPath
    if (-not $installation) { throw 'Visual Studio 2026 was not found.' }
    $VsDevCmd = Join-Path $installation 'Common7\Tools\VsDevCmd.bat'
}
if (-not (Test-Path $VsDevCmd)) { throw 'The supplied VS 2026 developer environment does not exist.' }

# Compile the current production functions verbatim so the check cannot silently test a stale renderer copy.
$source = [IO.File]::ReadAllText((Join-Path $repo 'src\svg.cpp'))
$loaderStart = $source.IndexOf('static char* ReadToolbarSVG(')
$loaderEnd = $source.IndexOf('// Use one identity map', $loaderStart)
$paintStart = $source.IndexOf('void RenderSVGImage(')
$paintEnd = $source.IndexOf('//*****************************************************************************', $paintStart)
if ($loaderStart -lt 0 -or $loaderEnd -lt 0 -or $paintStart -lt 0 -or $paintEnd -lt 0) { throw 'Unable to locate production SVG functions.' }
[IO.File]::WriteAllText((Join-Path $output 'svg-functions.inc'), $source.Substring($loaderStart, $loaderEnd-$loaderStart) + $source.Substring($paintStart, $paintEnd-$paintStart))
$test = Join-Path $repo 'tests\FluentIconRenderingTests.cpp'
$exe = Join-Path $output 'FluentIconRenderingTests.exe'
$compile = 'call "' + $VsDevCmd + '" -arch=x64 -host_arch=x64 && cl /nologo /std:c++17 /EHsc /W3 /I"' + $output + '" /I"' + (Join-Path $repo 'src') + '" /I"' + (Join-Path $repo 'src\common\dep\nanosvg') + '" "' + $test + '" /Fo:"' + (Join-Path $output 'tests.obj') + '" /Fe:"' + $exe + '" user32.lib gdi32.lib comctl32.lib msimg32.lib'
& $env:ComSpec /d /s /c $compile
if ($LASTEXITCODE -ne 0) { throw 'Native icon test compilation failed.' }
& $exe (Join-Path $repo 'src\res\toolbars') $output
if ($LASTEXITCODE -ne 0) { throw 'Native icon rendering verification failed.' }
