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

function Get-CodeWithoutCommentsAndLiterals {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Line,
        [Parameter(Mandatory = $true)]
        [ref]$InBlockComment
    )

    # Scan the complete target file so added lines inside an existing block comment
    # cannot be mistaken for code, while pointer expressions such as *thread remain visible.
    $code = [System.Text.StringBuilder]::new()
    $quote = [char]0
    $escaped = $false
    for ($index = 0; $index -lt $Line.Length; $index++) {
        $character = $Line[$index]
        $next = if ($index + 1 -lt $Line.Length) { $Line[$index + 1] } else { [char]0 }

        if ($InBlockComment.Value) {
            if ($character -eq '*' -and $next -eq '/') {
                $InBlockComment.Value = $false
                $index++
            }
            continue
        }

        if ($quote -ne [char]0) {
            if ($escaped) {
                $escaped = $false
            }
            elseif ($character -eq '\') {
                $escaped = $true
            }
            elseif ($character -eq $quote) {
                $quote = [char]0
            }
            continue
        }

        if ($character -eq '/' -and $next -eq '/') {
            break
        }
        if ($character -eq '/' -and $next -eq '*') {
            $InBlockComment.Value = $true
            $index++
            continue
        }
        if ($character -eq '"' -or $character -eq "'") {
            $quote = $character
            continue
        }

        [void]$code.Append($character)
    }

    return $code.ToString()
}

$exemptFiles = @(
    'src/common/thread_owner.h',
    'src/plugins/shared/plugin_thread_owner.h',
    'src/common/handles.cpp',
    'src/common/handles.h'
)
$exemptPrefixes = @(
    # The pinned upstream 7-Zip implementation owns threads through its portable
    # CThread layer; first-party adapters outside this subtree remain ratcheted.
    'src/plugins/7zip/7za/'
)

$candidates = @()
$currentFile = $null
$currentNewLine = 0
foreach ($line in $diff) {
    if ($line -match '^\+\+\+ b/(.+)$') {
        $currentFile = $Matches[1]
        continue
    }
    if ($line -match '^@@ -\d+(?:,\d+)? \+(\d+)(?:,\d+)? @@') {
        $currentNewLine = [int]$Matches[1]
        continue
    }
    if ($currentFile -eq $null -or $line -match '^\\ No newline at end of file$') {
        continue
    }

    if ($line -match '^\+[^+]') {
        $addedCode = $line.Substring(1)
        $isExempt = $currentFile -in $exemptFiles
        foreach ($prefix in $exemptPrefixes) {
            $isExempt = $isExempt -or $currentFile.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)
        }
        if (-not $isExempt -and $addedCode -match '\b(?:CreateThread|_beginthreadex)\s*\(') {
            $candidates += [pscustomobject]@{
                File = $currentFile
                LineNumber = $currentNewLine
                AddedCode = $addedCode
            }
        }
        $currentNewLine++
        continue
    }
    if ($line -notmatch '^-') {
        $currentNewLine++
    }
}

$violations = @()
foreach ($fileGroup in ($candidates | Group-Object File)) {
    $sourceLines = @(git show "HEAD:$($fileGroup.Name)")
    if ($LASTEXITCODE -ne 0) {
        throw "Unable to inspect $($fileGroup.Name) at HEAD."
    }

    $candidateByLine = @{}
    foreach ($candidate in $fileGroup.Group) {
        $candidateByLine[$candidate.LineNumber] = $candidate
    }

    $inBlockComment = $false
    for ($index = 0; $index -lt $sourceLines.Count; $index++) {
        $code = Get-CodeWithoutCommentsAndLiterals -Line $sourceLines[$index] -InBlockComment ([ref]$inBlockComment)
        $lineNumber = $index + 1
        if ($candidateByLine.ContainsKey($lineNumber) -and $code -match '\b(?:CreateThread|_beginthreadex)\s*\(') {
            $candidate = $candidateByLine[$lineNumber]
            $violations += "$($candidate.File):$lineNumber`: $($candidate.AddedCode)"
        }
    }
}

if ($violations.Count -ne 0) {
    # Keep the collected locations in one non-terminating record so Stop preference
    # cannot suppress the actionable diagnostics before the explicit failing exit.
    $message = @(
        'New raw CreateThread and _beginthreadex calls are prohibited. Use CThreadOwner or CPluginThreadOwner so handle, stop, completion, naming, and exception policy stay together.'
        $violations
    ) -join [Environment]::NewLine
    Write-Error $message -ErrorAction Continue
    exit 1
}

Write-Host 'No new raw thread-creation calls found.'
