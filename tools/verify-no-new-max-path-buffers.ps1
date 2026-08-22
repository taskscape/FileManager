param(
    [Parameter(Mandatory = $true)]
    [string]$BaseCommit
)

$ErrorActionPreference = 'Stop'

$exemptionsPath = Join-Path $PSScriptRoot 'max-path-buffer-exemptions.md'
if (-not (Test-Path -LiteralPath $exemptionsPath)) {
    throw "The MAX_PATH buffer exemption register is missing: $exemptionsPath"
}
$exemptions = Get-Content -LiteralPath $exemptionsPath -Raw

$baseSourceLines = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
$sourcePathspecs = @(':(glob)**/*.c', ':(glob)**/*.cc', ':(glob)**/*.cpp', ':(glob)**/*.h', ':(glob)**/*.hpp')
# One base-revision source scan avoids treating format strings as Git revisions
# while keeping move detection fast enough for the full local test runner.
foreach ($baseLine in (& git grep -h -I -e '.' $BaseCommit -- $sourcePathspecs)) { [void]$baseSourceLines.Add($baseLine.Trim()) }

function Test-ApprovedExemption([string]$Id, [string]$File) {
    $escapedId = [regex]::Escape($Id)
    $entry = [regex]::Match($exemptions, "(?ms)^###\s+$escapedId\s*$.*?(?=^###|\z)")
    if (-not $entry.Success) {
        return $false
    }

    $escapedFile = [regex]::Escape($File)
    $filePattern = '(?m)^- File:\s+`?' + $escapedFile + '`?\s*$'
    return $entry.Value -match $filePattern -and
           $entry.Value -match '(?m)^- Reason:\s+\S' -and
           $entry.Value -match '(?m)^- Removal:\s+\S'
}

function Test-LineWasPresentInBaseRevision([string]$SourceLine) {
    # Source extraction preserves the debt's history; only an actually changed
    # fixed buffer needs an exemption or a CWidePath migration.
    return $baseSourceLines.Contains($SourceLine.Trim())
}

# This is a changed-lines ratchet, not a historical cleanup. It deliberately
# leaves existing fixed buffers for their owning subsystem migrations while
# rejecting new char/WCHAR arrays whose bound includes MAX_PATH.
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
    if ($addedCode -notmatch '\b(?:char|WCHAR)\s+[A-Za-z_][A-Za-z0-9_]*\s*\[[^\]]*\bMAX_PATH\b[^\]]*\]') {
        continue
    }

    $exemption = [regex]::Match($addedCode, 'MAX_PATH-RATCHET-EXEMPT:\s*([A-Za-z0-9._-]+)')
    # A same-line match in the base revision is a relocation, not a new fixed buffer.
    if (Test-LineWasPresentInBaseRevision $addedCode) {
        continue
    }

    if ($exemption.Success -and (Test-ApprovedExemption $exemption.Groups[1].Value $currentFile)) {
        Write-Host "Approved MAX_PATH buffer exemption $($exemption.Groups[1].Value): $currentFile"
        continue
    }

    if ($exemption.Success) {
        $violations += "${currentFile}: unapproved MAX_PATH-RATCHET-EXEMPT '$($exemption.Groups[1].Value)': $addedCode"
    }
    else {
        $violations += "${currentFile}: $addedCode"
    }
}

if ($violations.Count -ne 0) {
    # Emit every offending line before the terminating error so CI identifies
    # the migration sites even under ErrorActionPreference=Stop.
    $violations | ForEach-Object { Write-Host $_ -ForegroundColor Red }
    Write-Host 'A temporary exception requires an inline MAX_PATH-RATCHET-EXEMPT ID and a matching reason/removal entry in tools/max-path-buffer-exemptions.md.' -ForegroundColor Red
    Write-Error 'New fixed char/WCHAR MAX_PATH buffers are prohibited. Migrate the boundary to CWidePath or a subsystem adapter.' -ErrorAction Continue
    exit 1
}

Write-Host 'No new fixed char/WCHAR MAX_PATH buffers found.'
