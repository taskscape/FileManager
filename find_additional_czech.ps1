$additionalWords = @('ktery', 'kde', 'jak', 'vsechny', 'kazdy', 'tento', 'tato', 'pouzije', 'pouziva', 'napr', 'atribut', 'funkce', 'parametr', 'vrati')
$files = Get-ChildItem -Path 'C:\Projects\FileManager\src' -Include *.cpp,*.h,*.c -Recurse | Where-Object { $_.FullName -notmatch '\\language\\' }
$count = 0
$pattern = '//.*\b(' + ($additionalWords -join '|') + ')\b'

foreach ($file in $files) {
    $matches = (Get-Content $file.FullName -Encoding UTF8 -ErrorAction SilentlyContinue | Select-String -Pattern $pattern | Measure-Object).Count
    $count += $matches
}

Write-Output "Additional Czech keyword matches in comments: $count"
Write-Output ""
Write-Output "Sample pattern used: $pattern"
