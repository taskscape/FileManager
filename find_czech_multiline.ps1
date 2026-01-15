$files = Get-ChildItem -Path 'C:\Projects\FileManager\src' -Include *.cpp,*.h,*.c -Recurse | Where-Object { $_.FullName -notmatch '\\language\\' -and $_.Extension -ne '.slg' }
$czechFiles = @{}
foreach ($file in $files) {
    $content = Get-Content $file.FullName -Raw -Encoding UTF8 -ErrorAction SilentlyContinue
    if ($content) {
        # Find multiline comments with Czech words
        $matches = [regex]::Matches($content, '/\*[\s\S]*?\*/')
        $czechCount = 0
        foreach ($match in $matches) {
            if ($match.Value -match '\b(pokud|nebo|pouze|muze|musi|bude|podle|obsahuje|adresar|soubor|cesta|jinak|vraci|prida|odstrani|konci|delka|velikost|retezec|jmeno|pripona|nastavena|promenna|dostupny|mozne|nutne|puvodni|volani|hodnot)\b') {
                $czechCount++
            }
        }
        if ($czechCount -gt 0) {
            $czechFiles[$file.FullName] = $czechCount
        }
    }
}

$czechFiles.GetEnumerator() | Sort-Object Value -Descending | ForEach-Object {
    Write-Output "$($_.Value) - $($_.Key)"
}

Write-Output ""
Write-Output "Total files with Czech multiline comments: $($czechFiles.Count)"
Write-Output "Total Czech multiline comments: $(($czechFiles.Values | Measure-Object -Sum).Sum)"
