Set-StrictMode -Version Latest

$script:UnsafeApiPattern = '\b(?<api>strcpy|strcat|strncpy|strncat|sprintf|vsprintf|_snprintf|_vsnprintf|lstrcpy(?:A|W)?|lstrcat(?:A|W)?|wsprintf(?:A|W)?|gets|scanf|fscanf|sscanf)\s*\('

function Get-UnsafeApiEntries {
    param([Parameter(Mandatory = $true)][string] $RepositoryRoot)

    $root = (Resolve-Path -LiteralPath $RepositoryRoot).Path.TrimEnd('\')
    $entries = @{}
    $files = Get-ChildItem -LiteralPath (Join-Path $root 'src') -Recurse -File |
        Where-Object { $_.Extension -in '.c', '.cc', '.cpp', '.h', '.hpp' } | Sort-Object FullName
    foreach ($file in $files) {
        $relative = $file.FullName.Substring($root.Length).TrimStart('\').Replace('\', '/')
        $lineNumber = 0
        foreach ($line in [IO.File]::ReadLines($file.FullName)) {
            $lineNumber++
            # A leading comment is documentation rather than an executable API call.
            if ($line -match '^\s*(//|/\*|\*)') { continue }
            $matches = [regex]::Matches($line, $script:UnsafeApiPattern, [Text.RegularExpressions.RegexOptions]::IgnoreCase)
            foreach ($match in $matches) {
                $api = $match.Groups['api'].Value.ToLowerInvariant()
                $normalized = ($line.Trim() -replace '\s+', ' ')
                $material = "$relative`n$api`n$normalized"
                $bytes = [Text.Encoding]::UTF8.GetBytes($material)
                $fingerprint = ([Security.Cryptography.SHA256]::Create().ComputeHash($bytes) | ForEach-Object { $_.ToString('x2') }) -join ''
                $key = "$relative|$api|$fingerprint"
                if (-not $entries.ContainsKey($key)) {
                    $entries[$key] = [ordered]@{ path = $relative; api = $api; fingerprint = $fingerprint; count = 0 }
                }
                $entries[$key].count++
            }
        }
    }
    return @($entries.Values | Sort-Object path, api, fingerprint)
}

Export-ModuleMember -Function Get-UnsafeApiEntries
