param(
    [Parameter(Mandatory = $true)]
    [string]$BaseCommit
)

$ErrorActionPreference = 'Stop'

# This changed-lines ratchet leaves legacy workers visible while requiring every
# new CRT-backed worker to declare its ownership through CThreadOwner.
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
    if ($addedCode -match '\b(?:CreateThread|_beginthreadex)\s*\(' -and
        $currentFile -notin @('src/common/thread_owner.h', 'src/plugins/shared/plugin_thread_owner.h', 'src/common/handles.cpp', 'src/common/handles.h')) {
        $violations += "${currentFile}: $addedCode"
    }
}

if ($violations.Count -ne 0) {
    Write-Error 'New raw CreateThread and _beginthreadex calls are prohibited. Use CThreadOwner or CPluginThreadOwner so handle, stop, completion, naming, and exception policy stay together.'
    $violations | ForEach-Object { Write-Error $_ }
    exit 1
}

Write-Host 'No new raw thread-creation calls found.'
