[CmdletBinding()]
param(
    [string]$InstallDirectory,
    # A blocked UAC or desktop prompt must not consume an entire workflow job.
    [ValidateRange(30, 600)]
    [int]$InstallTimeoutSeconds = 120
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
$diagnosticLogPath = Join-Path $logDirectory ('preflight-' + $innoInput.version + '-' + [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ') + '-' + [Guid]::NewGuid().ToString('N') + '.log')
New-Item -ItemType Directory -Path $logDirectory -Force | Out-Null

function Add-DiagnosticLogEntry {
    param([Parameter(Mandatory = $true)][string]$Message)

    Add-Content -LiteralPath $diagnosticLogPath -Value ('[{0}] {1}' -f [DateTime]::UtcNow.ToString('o'), $Message) -Encoding utf8
}

Add-DiagnosticLogEntry "Pinned Inno Setup preflight started for version $($innoInput.version)."
Add-DiagnosticLogEntry "Install root: $installRoot"
Add-DiagnosticLogEntry "Compiler path: $compiler"
Add-DiagnosticLogEntry "Runner identity: $([Security.Principal.WindowsIdentity]::GetCurrent().Name)"
Add-DiagnosticLogEntry "PowerShell: $($PSVersionTable.PSVersion)"

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
    Add-DiagnosticLogEntry 'Verified compiler cache hit; no installer execution was required.'
    Write-Host "Using verified cached Inno Setup compiler: $compiler" -ForegroundColor Green
    Write-Output $compiler
    return
}

New-Item -ItemType Directory -Path $installRoot -Force | Out-Null
$installer = Join-Path ([IO.Path]::GetTempPath()) ('filemanager-innosetup-' + $innoInput.version + '-' + [Guid]::NewGuid().ToString('N') + '.exe')
$logPath = Join-Path $logDirectory ('install-' + $innoInput.version + '-' + [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ') + '-' + [Guid]::NewGuid().ToString('N') + '.log')
Add-DiagnosticLogEntry "Installer payload path: $installer"
Add-DiagnosticLogEntry "Inno Setup log path: $logPath"

try {
    # Retry only the network transfer; an installer exit code is deterministic input for diagnosis and must not be hidden by blind retries.
    $downloaded = $false
    for ($attempt = 1; $attempt -le 3 -and -not $downloaded; $attempt++) {
        try {
            Write-Host "Downloading pinned Inno Setup payload (attempt $attempt of 3)..."
            Add-DiagnosticLogEntry "Downloading pinned payload, attempt $attempt of 3."
            Invoke-WebRequest -Uri $innoInput.url -OutFile $installer -ErrorAction Stop
            $downloaded = $true
        }
        catch {
            if ($attempt -eq 3) {
                throw "Downloading pinned Inno Setup failed after 3 attempts: $($_.Exception.Message)"
            }
            Add-DiagnosticLogEntry "Download attempt $attempt failed: $($_.Exception.Message)"
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

    # A runner service account cannot write Program Files; portable current-user mode avoids UAC and desktop-session prompts.
    $arguments = @(
        '/PORTABLE=1',
        '/CURRENTUSER',
        '/VERYSILENT',
        '/SUPPRESSMSGBOXES',
        '/NORESTART',
        '/SP-',
        # A non-interactive runner must not allow a hidden Cancel interaction to return Inno's exit code 2.
        '/NOCANCEL',
        ('/DIR="' + $installRoot + '"'),
        ('/LOG="' + $logPath + '"')
    )
    Add-DiagnosticLogEntry "Starting portable current-user installer with a $InstallTimeoutSeconds second timeout."
    Write-Host "Installing pinned Inno Setup into $installRoot..."
    # Start asynchronously so the explicit timeout can terminate a blocked installer instead of waiting indefinitely.
    $installProcess = Start-Process -FilePath $installer -ArgumentList $arguments -PassThru -WindowStyle Hidden
    if (-not $installProcess.WaitForExit($InstallTimeoutSeconds * 1000)) {
        Add-DiagnosticLogEntry "Installer exceeded the $InstallTimeoutSeconds second timeout; terminating it."
        try {
            $installProcess.Kill()
            $installProcess.WaitForExit()
        }
        catch {
            Add-DiagnosticLogEntry "Unable to terminate timed-out installer: $($_.Exception.Message)"
        }
        throw "Inno Setup installation timed out after $InstallTimeoutSeconds seconds. Installer log: $logPath. Diagnostic log: $diagnosticLogPath"
    }
    Add-DiagnosticLogEntry "Installer exited with code $($installProcess.ExitCode)."
    if ($installProcess.ExitCode -ne 0) {
        # Inno exit code 2 commonly means cancellation; retain its log so CI can distinguish prompts from runner interference.
        throw "Inno Setup installation failed with exit code $($installProcess.ExitCode). Installer log: $logPath. Diagnostic log: $diagnosticLogPath"
    }
}
catch {
    Add-DiagnosticLogEntry "Preflight failed: $($_.Exception.Message)"
    Add-DiagnosticLogEntry "Inno log exists: $(Test-Path -LiteralPath $logPath -PathType Leaf)"
    throw
}
finally {
    if (Test-Path -LiteralPath $installer -PathType Leaf) {
        # The verified payload is temporary input, not a release artifact; retain the diagnostic log instead.
        try {
            Remove-Item -LiteralPath $installer -Force
        }
        catch {
            Add-DiagnosticLogEntry "Unable to remove temporary installer payload: $($_.Exception.Message)"
        }
    }
}

if (-not (Test-Path -LiteralPath $compiler -PathType Leaf)) {
    Add-DiagnosticLogEntry "Compiler was not found after a successful installer exit."
    throw "Pinned Inno Setup did not install the locked compiler at $compiler. Installer log: $logPath. Diagnostic log: $diagnosticLogPath"
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
Add-DiagnosticLogEntry 'Pinned compiler installed and verification marker written.'
Write-Host "Pinned Inno Setup compiler verified: $compiler" -ForegroundColor Green
Write-Output $compiler
