# Configure the self-hosted runner from the verified VS 2026 Build Tools path because
# the third-party action's version range excludes the installed 18.9 release.
[CmdletBinding()]
param(
    [ValidateSet('x86', 'x64')]
    [string]$TargetArchitecture = 'x64',

    [ValidateSet('x86', 'x64')]
    [string]$HostArchitecture = 'x64'
)

$ErrorActionPreference = 'Stop'

# The release runner is provisioned with this specific Build Tools installation and ATL workload.
$vsDevCmd = 'C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\Tools\VsDevCmd.bat'
if (-not (Test-Path -LiteralPath $vsDevCmd)) {
    throw "VS 2026 Build Tools developer prompt was not found at '$vsDevCmd'."
}

if ([string]::IsNullOrWhiteSpace($env:GITHUB_ENV)) {
    throw 'GITHUB_ENV is required to preserve the Visual Studio developer environment across workflow steps.'
}

# Capture the inherited environment so only variables changed by VsDevCmd are exported to later steps.
$initialEnvironment = @{}
Get-ChildItem Env: | ForEach-Object { $initialEnvironment[$_.Name] = $_.Value }

# Keep host and target selection explicit so each workflow matrix row receives one internally consistent tool environment.
$command = 'call "' + $vsDevCmd + '" -arch=' + $TargetArchitecture + ' -host_arch=' + $HostArchitecture + ' >nul && set'
$developerEnvironment = & $env:ComSpec /d /s /c $command
if ($LASTEXITCODE -ne 0) {
    throw "VsDevCmd failed with exit code $LASTEXITCODE."
}

$visualStudioRoot = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $vsDevCmd))
$masmPathSuffix = "\bin\Host$HostArchitecture\x86\ml.exe"
$masm = Get-ChildItem -LiteralPath (Join-Path $visualStudioRoot 'VC\Tools\MSVC') -Filter 'ml.exe' -File -Recurse -ErrorAction SilentlyContinue |
    Where-Object { $_.FullName -like "*$masmPathSuffix" } |
    Sort-Object FullName -Descending |
    Select-Object -First 1
if ($null -eq $masm) {
    throw "The VS 2026 x86 MASM assembler was not found below '$visualStudioRoot'."
}
# MSBuild locates this assembler through its MASM tool path; exporting its directory would also shadow cl.exe and link.exe with x86 tools.

foreach ($line in $developerEnvironment) {
    $separator = $line.IndexOf('=')
    if ($separator -lt 1) {
        continue
    }

    $name = $line.Substring(0, $separator)
    $value = $line.Substring($separator + 1)

    # Preserve GitHub-managed variables while forwarding only valid developer-environment names.
    if ($name -notmatch '^[A-Za-z_][A-Za-z0-9_]*$' -or $name -match '^(GITHUB|RUNNER|ACTIONS)_') {
        continue
    }

    if (-not $initialEnvironment.ContainsKey($name) -or $initialEnvironment[$name] -ne $value) {
        Add-Content -LiteralPath $env:GITHUB_ENV -Value "$name=$value" -Encoding utf8
    }
}
