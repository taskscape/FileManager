[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repositoryRoot = Split-Path -Parent $PSScriptRoot
$copyEngine = Get-Content -Raw (Join-Path $repositoryRoot 'src\async_copy.cpp')

function Require-Index {
    param(
        [string] $Needle,
        [int] $StartAt = 0
    )

    $index = $copyEngine.IndexOf($Needle, $StartAt, [StringComparison]::Ordinal)
    if ($index -lt 0) {
        throw "Missing durable-copy commit invariant: $Needle"
    }
    return $index
}

$copyStart = Require-Index 'BOOL DoCopyFile('
$moveStart = Require-Index 'BOOL DoMoveFile('
$copyBody = $copyEngine.Substring($copyStart, $moveStart - $copyStart)

if ($copyBody -notmatch 'DWORD fileAttrs\s*=\s*asyncPar->GetOverlappedFlag\(\)\s*\|\s*FILE_FLAG_SEQUENTIAL_SCAN\s*\|\s*FILE_FLAG_WRITE_THROUGH') {
    throw 'Copy targets no longer request write-through creation.'
}

$flush = Require-Index 'if (!FlushFileBuffers(out))' $copyStart
$close = Require-Index 'if (!HANDLES(CloseHandle(out))' $flush
$verify = Require-Index 'while (!VerifyDurableCopyCommit(op->TargetName, op->FileSize, &verificationError))' $close
$replace = Require-Index 'while (!CommitTransactionalTargetFile(requestedTargetName, op->TargetName, &err))' $verify
if ($flush -ge $close -or $close -ge $verify -or $verify -ge $replace) {
    throw 'Flush, successful close, metadata verification, and overwrite commit are no longer ordered durably.'
}

$moveBody = $copyEngine.Substring($moveStart)
$copyThenDelete = $moveBody.IndexOf('BOOL notError = DoCopyFile(', [StringComparison]::Ordinal)
$fullHash = $moveBody.IndexOf('while (suspiciousIoRetry && !VerifyFullFileContentSha256(op->SourceName, op->TargetName, &err))', [StringComparison]::Ordinal)
$deleteSource = $moveBody.IndexOf('if (DeleteFileUtf8(op->SourceName))', [StringComparison]::Ordinal)
if ($copyThenDelete -lt 0 -or $fullHash -lt 0 -or $deleteSource -lt 0 -or
    $copyThenDelete -ge $fullHash -or $fullHash -ge $deleteSource) {
    throw 'Cross-volume move no longer verifies a retried copy before deleting its source.'
}

if ($copyEngine -notmatch 'static BOOL VerifyFullFileContentSha256\(' -or
    $copyEngine -notmatch 'BCRYPT_SHA256_ALGORITHM' -or
    $copyEngine -notmatch '\*suspiciousIoRetry\s*=\s*TRUE') {
    throw 'Cross-volume move full SHA-256 verification is incomplete.'
}

Write-Host 'Durable copy and cross-volume move verification checks passed.'
