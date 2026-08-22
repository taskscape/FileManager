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

function Test-LineWasPresentInBaseRevision([string]$SourceLine) {
    # Relocating an unchanged legacy call is not new debt; content changes still
    # reach the migration gate because the exact prior source line will not match.
    return $baseSourceLines.Contains($SourceLine.Trim())
}

# This is intentionally a changed-lines ratchet. Existing call sites remain
# visible debt, while every new call must use the checked common helpers.
$diff = git diff --no-ext-diff --unified=0 $BaseCommit HEAD -- '*.c' '*.cc' '*.cpp' '*.h' '*.hpp'
if ($LASTEXITCODE -ne 0) {
    throw "Unable to compare HEAD with $BaseCommit."
}

$unsafeCall = '\b(?:strcpy|strcat|sprintf|lstrcpy(?:A|W)?|lstrcat(?:A|W)?|wsprintf(?:A|W)?)\s*\('
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
    if ($addedCode -match $unsafeCall) {
        if (Test-LineWasPresentInBaseRevision $addedCode) {
            continue
        }
        $violations += "${currentFile}: $addedCode"
    }
}

if ($violations.Count -ne 0) {
    Write-Error 'New unchecked strcpy, strcat, sprintf, lstrcpy, lstrcat, and wsprintf calls are prohibited. Use CopyStringChecked, FormatStringChecked, or a boundary-specific adapter that reports failure.'
    $violations | ForEach-Object { Write-Error $_ }
    exit 1
}

Write-Host 'No new unchecked string-copy or formatting calls found.'
