$files = Get-ChildItem -Path 'C:\Projects\FileManager\src' -Include *.cpp,*.h,*.c -Recurse | Where-Object { $_.FullName -notmatch '\\language\\' -and $_.Extension -ne '.slg' }
$czechFiles = @{}
foreach ($file in $files) {
    $count = (Get-Content $file.FullName -Encoding UTF8 -ErrorAction SilentlyContinue | Select-String -Pattern '//.*\b(pokud|nebo|pouze|muze|musi|bude|podle|obsahuje|adresar|soubor|cesta|jinak|vraci|prida|odstrani|konci|delka|velikost|retezec|jmeno|pripona|nastavena|promenna|dostupny|mozne|nutne|puvodni|volani|hodnot)\b' | Measure-Object).Count
    if ($count -gt 0) {
        $czechFiles[$file.FullName] = $count
    }
}

$czechFiles.GetEnumerator() | Sort-Object Value -Descending | ForEach-Object {
    Write-Output "$($_.Value) - $($_.Key)"
}

Write-Output ""
Write-Output "Total files with Czech comments: $($czechFiles.Count)"
Write-Output "Total Czech comments: $(($czechFiles.Values | Measure-Object -Sum).Sum)"
