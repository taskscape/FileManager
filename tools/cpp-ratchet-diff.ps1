# Match comments only outside ordinary C/C++ string and character literals: a URL
# or escaped quote in executable code must remain part of the comparison key.
function Remove-CppTrailingComment {
    param([AllowEmptyString()][string]$Line)

    # Stay conservative on syntax requiring multiline lexical state; a comment
    # marker inside a raw string or block comment must never hide following code.
    if ($Line -match 'R"|/\*|\*/') { return $Line.TrimEnd() }

    $tokens = '"(?:\\.|[^"\\])*"|''(?:\\.|[^''\\])*''|(?<comment>//.*$)'
    return ([regex]::Replace($Line, $tokens, {
        param($match)
        if ($match.Groups['comment'].Success) { return '' }
        return $match.Value
    })).TrimEnd()
}

function Remove-CppCommentOnlyDiffChanges {
    param([string[]]$Diff)

    # Match removed and added code within the same file, consuming each removal
    # once. Extra additions still reach the owning ratchet's existing policy.
    $removed = [System.Collections.Generic.Dictionary[string, int]]::new([StringComparer]::Ordinal)
    $file = $null
    foreach ($line in $Diff) {
        if ($line.StartsWith('diff --git ')) { $file = $null; continue }
        if ($line -match '^\+\+\+ b/(.+)$') { $file = $Matches[1]; continue }
        if ($null -ne $file -and $line.StartsWith('-') -and -not $line.StartsWith('---')) {
            $key = $file + "`0" + (Remove-CppTrailingComment $line.Substring(1))
            if (-not $removed.ContainsKey($key)) { $removed[$key] = 0 }
            $removed[$key]++
        }
    }

    $file = $null
    foreach ($line in $Diff) {
        if ($line.StartsWith('diff --git ')) { $file = $null; $line; continue }
        if ($line -match '^\+\+\+ b/(.+)$') { $file = $Matches[1] }
        elseif ($null -ne $file -and $line.StartsWith('+') -and -not $line.StartsWith('+++')) {
            $key = $file + "`0" + (Remove-CppTrailingComment $line.Substring(1))
            if ($removed.ContainsKey($key) -and $removed[$key] -gt 0) {
                $removed[$key]--
                continue
            }
        }
        $line
    }
}
