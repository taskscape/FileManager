param(
    [Parameter(Mandatory = $true)]
    [string]$BaseCommit
)

$ErrorActionPreference = 'Stop'

# This is a ratchet, not a historical-baseline rewrite: only additions in native
# source files are considered. Existing calls must be removed by their owning
# subsystem's dedicated shutdown change.
$diff = git diff --no-ext-diff --unified=0 $BaseCommit HEAD -- '*.c' '*.cc' '*.cpp' '*.h' '*.hpp'
if ($LASTEXITCODE -ne 0) {
    throw "Unable to compare HEAD with $BaseCommit."
}

$newCalls = $diff | Where-Object {
    $_ -match '^\+[^+].*\bTerminateThread\s*\('
}

if ($newCalls) {
    Write-Error "New TerminateThread calls are prohibited. Use cooperative cancellation and a safe join instead."
    $newCalls | ForEach-Object { Write-Error $_ }
    exit 1
}

Write-Host 'No new TerminateThread calls found.'
