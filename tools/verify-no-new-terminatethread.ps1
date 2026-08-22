param(
    [Parameter(Mandatory = $true)]
    [string]$BaseCommit
)

$ErrorActionPreference = 'Stop'

$baseSourceLines = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
$sourcePathspecs = @(':(glob)**/*.c', ':(glob)**/*.cc', ':(glob)**/*.cpp', ':(glob)**/*.h', ':(glob)**/*.hpp')
# One base-revision source scan avoids treating format strings as Git revisions
# while keeping move detection fast enough for the full local test runner.
foreach ($baseLine in (& git grep -h -I -e '.' $BaseCommit -- $sourcePathspecs)) { [void]$baseSourceLines.Add($baseLine.Trim()) }

function Test-LineWasPresentInBaseRevision {
    param([Parameter(Mandatory = $true)][string]$SourceLine)

    # A mechanical move must not look like newly introduced shutdown debt; exact
    # matching still makes any modified or genuinely new call fail the ratchet.
    return $baseSourceLines.Contains($SourceLine.Trim())
}

# This is a ratchet, not a historical-baseline rewrite: only additions in native
# source files are considered. Existing calls must be removed by their owning
# subsystem's dedicated shutdown change.
$diff = git diff --no-ext-diff --unified=0 $BaseCommit HEAD -- '*.c' '*.cc' '*.cpp' '*.h' '*.hpp'
if ($LASTEXITCODE -ne 0) {
    throw "Unable to compare HEAD with $BaseCommit."
}

$newCalls = $diff | Where-Object {
    $_ -match '^\+[^+].*\bTerminateThread\s*\(' -and
    -not (Test-LineWasPresentInBaseRevision $_.Substring(1))
}

if ($newCalls) {
    Write-Error "New TerminateThread calls are prohibited. Use cooperative cancellation and a safe join instead."
    $newCalls | ForEach-Object { Write-Error $_ }
    exit 1
}

Write-Host 'No new TerminateThread calls found.'
