[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrWhiteSpace([Environment]::GetEnvironmentVariable('MOJERZEC_USERNAME')) -or
    [string]::IsNullOrWhiteSpace([Environment]::GetEnvironmentVariable('MOJERZEC_PASSWORD'))) {
    # Keep the separate FTPS command successful on machines without secrets; NUnit records the same passed non-execution.
    Write-Host 'FTP UI tests have not been performed due to missing credentials (MOJERZEC_USERNAME and/or MOJERZEC_PASSWORD).'
}

# The live-server lane must never share the user's Open Salamander profile while it accepts a certificate exception.
$env:FILEMANAGER_UI_ISOLATED = '1'
$repositoryRoot = Split-Path -Parent $PSScriptRoot
# Retain Debug-only native dialog evidence after the disposable UI profile is deleted by the test harness.
$env:FILEMANAGER_UI_FTP_DEBUG_ERROR_LOG_DIRECTORY = Join-Path $repositoryRoot 'TestResults\ftp-debug-error-dialogs'
$runner = Join-Path $PSScriptRoot 'runtests.ps1'
& $runner -NoReleasePipeline -NUnitFilter 'TestCategory=LiveFtp' -SkipLockVerifier
