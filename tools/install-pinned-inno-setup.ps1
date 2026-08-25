[CmdletBinding()]
param(
    [string]$InstallDirectory
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'

$innoInput = (Get-Content -LiteralPath (Join-Path $PSScriptRoot 'release-inputs.json') -Raw | ConvertFrom-Json).inputs.innoSetup
$cacheKey = 'FileManager\InnoSetup\' + $innoInput.version + '-' + $innoInput.sha256.Substring(0, 16)
$cacheRoot = if (-not [string]::IsNullOrWhiteSpace($env:RUNNER_TOOL_CACHE)) {
    $env:RUNNER_TOOL_CACHE
}
elseif (-not [string]::IsNullOrWhiteSpace($env:LOCALAPPDATA)) {
    Join-Path $env:LOCALAPPDATA 'FileManager\tool-cache'
}
else {
    Join-Path ([IO.Path]::GetTempPath()) 'FileManager\tool-cache'
}
$installRoot = if ([string]::IsNullOrWhiteSpace($InstallDirectory)) {
    Join-Path $cacheRoot $cacheKey
}
else {
    [IO.Path]::GetFullPath($InstallDirectory)
}
$compiler = Join-Path $installRoot 'ISCC.exe'
$verificationMarker = Join-Path $installRoot '.filemanager-inno-setup.json'
$logRoot = if (-not [string]::IsNullOrWhiteSpace($env:RUNNER_TEMP)) {
    Join-Path $env:RUNNER_TEMP 'FileManager\InnoSetup'
}
else {
    Join-Path ([IO.Path]::GetTempPath()) 'FileManager\InnoSetup'
}
$logDirectory = Join-Path $logRoot 'logs'

function Test-VerifiedCompiler {
    if (-not (Test-Path -LiteralPath $compiler -PathType Leaf) -or
        -not (Test-Path -LiteralPath $verificationMarker -PathType Leaf)) {
        return $false
    }

    try {
        $record = Get-Content -LiteralPath $verificationMarker -Raw | ConvertFrom-Json
        return $record.version -eq $innoInput.version -and
            $record.sha256 -eq $innoInput.sha256 -and
            $record.compiler -eq $compiler
    }
    catch {
        return $false
    }
}

if (Test-VerifiedCompiler) {
    # ISCC.exe publishes a 0.0.0.0 PE version, so the versioned cache key and verification marker are the authoritative compiler identity.
    Write-Host "Using verified cached Inno Setup compiler: $compiler" -ForegroundColor Green
    Write-Output $compiler
    return
}

New-Item -ItemType Directory -Path $installRoot -Force | Out-Null
New-Item -ItemType Directory -Path $logDirectory -Force | Out-Null
$installer = Join-Path ([IO.Path]::GetTempPath()) ('filemanager-innosetup-' + $innoInput.version + '-' + [Guid]::NewGuid().ToString('N') + '.exe')
$logPath = Join-Path $logDirectory ('install-' + $innoInput.version + '-' + [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ') + '-' + [Guid]::NewGuid().ToString('N') + '.log')

try {
    # Retry only the network transfer; an installer exit code is deterministic input for diagnosis and must not be hidden by blind retries.
    $downloaded = $false
    for ($attempt = 1; $attempt -le 3 -and -not $downloaded; $attempt++) {
        try {
            Write-Host "Downloading pinned Inno Setup payload (attempt $attempt of 3)..."
            Invoke-WebRequest -Uri $innoInput.url -OutFile $installer -ErrorAction Stop
            $downloaded = $true
        }
        catch {
            if ($attempt -eq 3) {
                throw "Downloading pinned Inno Setup failed after 3 attempts: $($_.Exception.Message)"
            }
            Start-Sleep -Seconds ([int]([math]::Pow(2, $attempt - 1)))
        }
    }

    $actualHash = (Get-FileHash -LiteralPath $installer -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($actualHash -ne $innoInput.sha256) {
        throw "Pinned Inno Setup SHA-256 mismatch: expected $($innoInput.sha256), got $actualHash."
    }

    $signature = Get-AuthenticodeSignature -LiteralPath $installer
    if ($signature.Status -ne 'Valid' -or $signature.SignerCertificate.Subject -notlike "*CN=$($innoInput.publisher)*") {
        throw "Pinned Inno Setup Authenticode verification failed: $($signature.Status) $($signature.SignerCertificate.Subject)"
    }

    # A runner service account cannot write Program Files; install the verified compiler into this job-owned cache directory instead.
    $arguments = @(
        '/VERYSILENT',
        '/SUPPRESSMSGBOXES',
        '/NORESTART',
        '/SP-',
        # A non-interactive runner must not allow a hidden Cancel interaction to return Inno's exit code 2.
        '/NOCANCEL',
        ('/DIR="' + $installRoot + '"'),
        ('/LOG="' + $logPath + '"')
    )
    Write-Host "Installing pinned Inno Setup into $installRoot..."
    $installProcess = Start-Process -FilePath $installer -ArgumentList $arguments -Wait -PassThru -WindowStyle Hidden
    if ($installProcess.ExitCode -ne 0) {
        # Inno exit code 2 commonly means cancellation; retain its log so CI can distinguish prompts from runner interference.
        throw "Inno Setup installation failed with exit code $($installProcess.ExitCode). Installer log: $logPath"
    }
}
finally {
    if (Test-Path -LiteralPath $installer -PathType Leaf) {
        # The verified payload is temporary input, not a release artifact; retain the diagnostic log instead.
        Remove-Item -LiteralPath $installer -Force
    }
}

if (-not (Test-Path -LiteralPath $compiler -PathType Leaf)) {
    throw "Pinned Inno Setup did not install the locked compiler at $compiler. Installer log: $logPath"
}

$verificationRecord = [ordered]@{
    version = $innoInput.version
    sha256 = $innoInput.sha256
    url = $innoInput.url
    publisher = $innoInput.publisher
    compiler = $compiler
    verifiedUtc = [DateTime]::UtcNow.ToString('o')
}
# Persist the verification identity beside ISCC.exe so later jobs can reuse only this exact pinned tool.
$verificationRecord | ConvertTo-Json | Set-Content -LiteralPath $verificationMarker -Encoding utf8
Write-Host "Pinned Inno Setup compiler verified: $compiler" -ForegroundColor Green
Write-Output $compiler
