# Configure the self-hosted runner from the verified VS 2026 Build Tools path because
# the third-party action's version range excludes the installed 18.9 release.
[CmdletBinding()]
param()

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

$command = 'call "' + $vsDevCmd + '" -arch=x64 -host_arch=x64 >nul && set'
$developerEnvironment = & $env:ComSpec /d /s /c $command
if ($LASTEXITCODE -ne 0) {
    throw "VsDevCmd failed with exit code $LASTEXITCODE."
}

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
