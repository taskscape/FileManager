param(
    [Parameter(Mandatory = $true)]
    [string]$BaseCommit
)

$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot 'cpp-ratchet-diff.ps1')

# Keep legacy compatibility uses visible while requiring new timeout decisions to use CMonotonicClock.
$diff = git diff --no-ext-diff --unified=0 $BaseCommit HEAD -- '*.c' '*.cc' '*.cpp' '*.h' '*.hpp'
if ($LASTEXITCODE -ne 0) {
    throw "Unable to compare HEAD with $BaseCommit."
}
# Comment translations must not turn unchanged legacy calls into new debt.
$diff = @(Remove-CppCommentOnlyDiffChanges -Diff $diff)

$violations = @()
$currentFile = $null
foreach ($line in $diff) {
    if ($line -match '^\+\+\+ b/(.+)$') {
        $currentFile = $Matches[1]
        continue
    }
    if ($line -notmatch '^\+[^+]' -or $currentFile -eq $null) {
        continue
    }

    $addedCode = Remove-CppTrailingComment $line.Substring(1)
    if ($addedCode -match '^\s*(//|\*|/\*)') {
        continue
    }
    if ($addedCode -match '\bGetTickCount\s*\(') {
        $violations += "${currentFile}: $addedCode"
    }
}

if ($violations.Count -ne 0) {
    # Keep every location visible before the explicit failure under Stop preference.
    $message = @(
        'New wrap-prone GetTickCount calls are prohibited. Use CMonotonicClock for timeout and scheduling decisions; keep an exception narrowly documented when an API requires a DWORD value.'
        $violations
    ) -join [Environment]::NewLine
    Write-Error $message -ErrorAction Continue
    exit 1
}

Write-Host 'No new wrap-prone GetTickCount calls found.'
