[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repositoryRoot = Split-Path -Parent $PSScriptRoot
$copyEngine = Get-Content -Raw (Join-Path $repositoryRoot 'src\async_copy.cpp')
$copyCommit = Get-Content -Raw (Join-Path $repositoryRoot 'src\copy_commit.cpp')

function Require-Index {
    param(
        [string] $Source,
        [string] $Needle,
        [int] $StartAt = 0
    )

    $index = $Source.IndexOf($Needle, $StartAt, [StringComparison]::Ordinal)
    if ($index -lt 0) {
        throw "Missing durable-copy commit invariant: $Needle"
    }
    return $index
}

$copyStart = Require-Index $copyEngine 'BOOL DoCopyFile('
$moveStart = Require-Index $copyEngine 'BOOL DoMoveFile('
$copyBody = $copyEngine.Substring($copyStart, $moveStart - $copyStart)

if ($copyBody -notmatch 'DWORD fileAttrs\s*=\s*asyncPar->GetOverlappedFlag\(\)\s*\|\s*FILE_FLAG_SEQUENTIAL_SCAN\s*\|\s*FILE_FLAG_WRITE_THROUGH') {
    throw 'Copy targets no longer request write-through creation.'
}

# The production path uses the injectable filesystem boundary so native builds
# and deterministic fault tests exercise the same durable-flush decision.
$flush = Require-Index $copyEngine 'if (!OperationExecutionFileSystem().FlushFileBuffers(out))' $copyStart
$close = Require-Index $copyEngine 'if (!HANDLES(CloseHandle(out))' $flush
# The retry/replace tail was extracted from DoCopyFile, so verify the call edge
# and the helper's internal ordering instead of coupling this guard to one file layout.
$commitPhase = Require-Index $copyEngine 'switch (VerifyAndCommitCopyTarget(op, hProgressDlg, requestedTargetName, script, dlgData,' $close
$verify = Require-Index $copyEngine 'COperationResult verificationResult = VerifyDurableCopyCommit(op->TargetName, op->FileSize);'
$verifyRetry = Require-Index $copyEngine 'while (!verificationResult.ToLegacyBool(&verificationError))' $verify
# Structured results retain phase diagnostics while their legacy BOOL adapters
# preserve the existing retry dialogs and the durable commit ordering contract.
$replace = Require-Index $copyEngine 'COperationResult commitResult = CommitTransactionalTargetFile(requestedTargetName, op->TargetName,' $verifyRetry
$replaceRetry = Require-Index $copyEngine 'while (!commitResult.ToLegacyBool(&err))' $replace
if ($flush -ge $close -or $close -ge $commitPhase -or $verify -ge $verifyRetry -or
    $verifyRetry -ge $replace -or $replace -ge $replaceRetry) {
    throw 'Flush, successful close, metadata verification, and overwrite commit are no longer ordered durably.'
}

$moveBody = $copyEngine.Substring($moveStart)
$copyThenDelete = $moveBody.IndexOf('BOOL notError = DoCopyFile(', [StringComparison]::Ordinal)
$fullHash = $moveBody.IndexOf('while (suspiciousIoRetry && !VerifyFullFileContentSha256(op->SourceName, op->TargetName, &err))', [StringComparison]::Ordinal)
# Identity-verified deletion prevents a path replacement race after the source
# digest is accepted and before a cross-volume move removes the original file.
$deleteSource = $moveBody.IndexOf('if (DeleteFileWithVerifiedIdentity(op->SourceName, op->SourceIdentity, &err))', [StringComparison]::Ordinal)
if ($copyThenDelete -lt 0 -or $fullHash -lt 0 -or $deleteSource -lt 0 -or
    $copyThenDelete -ge $fullHash -or $fullHash -ge $deleteSource) {
    throw 'Cross-volume move no longer verifies a retried copy before deleting its source.'
}

if ($copyCommit -notmatch 'BOOL VerifyFullFileContentSha256\(' -or
    $copyCommit -notmatch 'BCRYPT_SHA256_ALGORITHM' -or
    $copyEngine -notmatch '\*suspiciousIoRetry\s*=\s*TRUE') {
    throw 'Cross-volume move full SHA-256 verification is incomplete.'
}

Write-Host 'Durable copy and cross-volume move verification checks passed.'
