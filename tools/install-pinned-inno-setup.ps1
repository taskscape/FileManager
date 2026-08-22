[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$InstallDirectory
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repositoryRoot = Split-Path -Parent $PSScriptRoot
$input = (Get-Content -LiteralPath (Join-Path $PSScriptRoot 'release-inputs.json') -Raw | ConvertFrom-Json).inputs.innoSetup
$installRoot = [IO.Path]::GetFullPath($InstallDirectory)
$compiler = Join-Path $installRoot 'ISCC.exe'

if (Test-Path -LiteralPath $compiler -PathType Leaf) {
    # ISCC.exe publishes a 0.0.0.0 PE version, so the versioned job cache plus the verified installer payload are the authoritative identity.
    Write-Output $compiler
    return
}

New-Item -ItemType Directory -Path $installRoot -Force | Out-Null
$installer = Join-Path ([IO.Path]::GetTempPath()) ('filemanager-innosetup-' + $input.version + '-' + [Guid]::NewGuid().ToString('N') + '.exe')
try {
    Invoke-WebRequest -Uri $input.url -OutFile $installer
    $actualHash = (Get-FileHash -LiteralPath $installer -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($actualHash -ne $input.sha256) {
        throw "Pinned Inno Setup SHA-256 mismatch: expected $($input.sha256), got $actualHash."
    }

    $signature = Get-AuthenticodeSignature -LiteralPath $installer
    if ($signature.Status -ne 'Valid' -or $signature.SignerCertificate.Subject -notlike "*CN=$($input.publisher)*") {
        throw "Pinned Inno Setup Authenticode verification failed: $($signature.Status) $($signature.SignerCertificate.Subject)"
    }

    # A runner service account cannot write Program Files; install the verified compiler into this job-owned directory instead.
    $installProcess = Start-Process -FilePath $installer -ArgumentList @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', '/SP-', ('/DIR="' + $installRoot + '"')) -Wait -PassThru -WindowStyle Hidden
    if ($installProcess.ExitCode -ne 0) {
        throw "Inno Setup installation failed with exit code $($installProcess.ExitCode)."
    }
}
finally {
    if (Test-Path -LiteralPath $installer -PathType Leaf) {
        # The verified payload is temporary input, not a release artifact; do not retain executable installers across jobs.
        Remove-Item -LiteralPath $installer -Force
    }
}

if (-not (Test-Path -LiteralPath $compiler -PathType Leaf)) {
    # The release asset hash identifies 6.7.3; ISCC.exe's PE resource cannot provide an additional meaningful version check.
    throw "Pinned Inno Setup did not install the locked compiler at $compiler."
}

Write-Output $compiler
