param(
    [Parameter(Mandatory = $true)]
    [string]$BaseCommit
)

$ErrorActionPreference = 'Stop'

# Keep legacy compatibility uses visible while requiring new timeout decisions to use CMonotonicClock.
$diff = git diff --no-ext-diff --unified=0 $BaseCommit HEAD -- '*.c' '*.cc' '*.cpp' '*.h' '*.hpp'
if ($LASTEXITCODE -ne 0) {
    throw "Unable to compare HEAD with $BaseCommit."
}

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

    $addedCode = $line.Substring(1)
    if ($addedCode -match '^\s*(//|\*|/\*)') {
        continue
    }
    if ($addedCode -match '\bGetTickCount\s*\(') {
        $violations += "${currentFile}: $addedCode"
    }
}

if ($violations.Count -ne 0) {
    Write-Error 'New wrap-prone GetTickCount calls are prohibited. Use CMonotonicClock for timeout and scheduling decisions; keep an exception narrowly documented when an API requires a DWORD value.'
    $violations | ForEach-Object { Write-Error $_ }
    exit 1
}

Write-Host 'No new wrap-prone GetTickCount calls found.'
