[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repositoryRoot = Split-Path -Parent $PSScriptRoot
$operationsCore = Get-Content -Raw (Join-Path $repositoryRoot 'src\operations_core.cpp')
$progressDialog = Get-Content -Raw (Join-Path $repositoryRoot 'src\dialogs_file_ops.cpp')
$progressDialogArray = Get-Content -Raw (Join-Path $repositoryRoot 'src\file_enumeration.cpp')

function Get-CaseBody {
    param(
        [string] $Source,
        [string] $CaseLabel,
        [string] $NextCaseLabel
    )

    $start = $Source.IndexOf($CaseLabel, [StringComparison]::Ordinal)
    if ($start -lt 0) {
        throw "Missing $CaseLabel."
    }

    $end = $Source.IndexOf($NextCaseLabel, $start + $CaseLabel.Length, [StringComparison]::Ordinal)
    if ($end -lt 0) {
        throw "Missing $NextCaseLabel after $CaseLabel."
    }

    return $Source.Substring($start, $end - $start)
}

# A close/cancel request must only request worker cancellation.  The dialog stays
# alive for the posted completion message, so it cannot destroy worker-owned state.
$cancelCase = Get-CaseBody $progressDialog 'case WM_USER_CANCELPROGRDLG:' 'case WM_USER_PROGRDLG_WORKERCOMPLETE:'
if ($cancelCase -notmatch 'CancelWorker\s*=\s*TRUE' -or
    $cancelCase -notmatch 'SetEvent\(WorkerNotSuspended\)' -or
    $cancelCase -match 'EndDialog\(|DestroyWindow\(') {
    throw 'Cancel/close no longer preserves the asynchronous completion lifecycle.'
}

# The worker must release the script before it posts its small owned result.  It
# must not synchronously enter the UI or wait for the UI continuation event.
$completionStart = $operationsCore.IndexOf('CWorkerCompletion* completion', [StringComparison]::Ordinal)
if ($completionStart -lt 0) {
    throw 'Worker completion result was not created.'
}
$completionTail = $operationsCore.Substring($completionStart)
$completionEnd = $completionTail.IndexOf('TRACE_I("End");', [StringComparison]::Ordinal)
if ($completionEnd -lt 0) {
    throw 'Worker completion body has no end marker.'
}
$completionBody = $completionTail.Substring(0, $completionEnd)
$freeScript = $completionBody.IndexOf('FreeScript(script)', [StringComparison]::Ordinal)
$postCompletion = $completionBody.IndexOf('PostMessage(hProgressDlg, WM_USER_PROGRDLG_WORKERCOMPLETE', [StringComparison]::Ordinal)
if ($freeScript -lt 0 -or $postCompletion -lt 0 -or $freeScript -gt $postCompletion -or
    $completionBody -match 'SendMessage\(hProgressDlg, WM_COMMAND, IDOK' -or
    $completionBody -match 'WaitForSingleObject\(wContinue, INFINITE\)') {
    throw 'Worker completion can again wait for the UI or retain the operation script.'
}

# Modal close, explicit cancellation, and shutdown all converge here.  The UI
# consumes the owned result after the worker cleanup, acknowledges by scheduling
# the normal delayed close, and never opens the old continuation gate.
$completionCase = Get-CaseBody $progressDialog 'case WM_USER_PROGRDLG_WORKERCOMPLETE:' 'case WM_USER_PROGRDLG_UPDATEICON:'
if ($completionCase -notmatch 'delete completion' -or
    $completionCase -notmatch 'wParam\s*=\s*cancelled\s*\?\s*IDCANCEL\s*:\s*IDOK' -or
    $completionCase -notmatch 'PostMessage\(HWindow, WM_USER_PROGRDLGEND' -or
    $completionCase -match 'SetEvent\(WContinue\)' -or
    $completionCase -match 'ReplyMessage\(') {
    throw 'The completion acknowledgement is no longer asynchronous and owned.'
}

# Shutdown uses CProgressDlgArray::PostCancelToAllDlgs, which must remain a posted
# cancellation rather than a synchronous destroy while the worker can still run.
if ($progressDialogArray -notmatch 'void CProgressDlgArray::PostCancelToAllDlgs\(\)' -or
    $progressDialogArray -notmatch 'PostMessage\([^\r\n]*WM_USER_CANCELPROGRDLG') {
    throw 'Shutdown no longer routes through the safe progress-dialog cancellation message.'
}

Write-Host 'Operation completion protocol checks passed (close, cancel, shutdown).'
