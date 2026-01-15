$modifiedFiles = @(
    'src\common\handles.cpp',
    'src\common\lstrfix.h',
    'src\common\messages.cpp',
    'src\common\moore.cpp',
    'src\common\multimon.cpp',
    'src\common\regexp.cpp',
    'src\common\sheets.cpp',
    'src\common\str.cpp',
    'src\common\strutils.h',
    'src\common\trace.cpp',
    'src\common\winlib.cpp',
    'src\common\winlib.h',
    'src\plugins\shared\auxtools.h',
    'src\plugins\shared\dbg.h',
    'src\plugins\shared\lukas\messages.cpp',
    'src\plugins\shared\lukas\utilaux.cpp',
    'src\plugins\shared\lukas\utilbase.cpp',
    'src\plugins\shared\lukas\utildlg.cpp',
    'src\plugins\shared\mhandles.h',
    'src\plugins\shared\spl_arc.h',
    'src\plugins\shared\spl_base.h',
    'src\plugins\shared\spl_com.h',
    'src\plugins\shared\spl_file.h',
    'src\plugins\shared\spl_fs.h',
    'src\plugins\shared\spl_gen.h',
    'src\plugins\shared\spl_gui.h',
    'src\plugins\shared\spl_menu.h',
    'src\plugins\shared\spl_thum.h',
    'src\plugins\shared\spl_vers.h',
    'src\plugins\shared\spl_view.h',
    'src\plugins\shared\winliblt.h'
)

Write-Output "Checking modified files for Czech comments..."
Write-Output ""

$translatedFiles = @()
$stillCzechFiles = @()

foreach ($file in $modifiedFiles) {
    $fullPath = Join-Path 'C:\Projects\FileManager' $file
    if (Test-Path $fullPath) {
        $count = (Get-Content $fullPath -Encoding UTF8 -ErrorAction SilentlyContinue |
                  Select-String -Pattern '//.*\b(pokud|nebo|pouze|muze|musi|bude|podle|obsahuje|adresar|soubor|cesta|jinak|vraci|prida|odstrani|konci|delka|velikost|retezec|jmeno|pripona|nastavena|promenna|dostupny|mozne|nutne|puvodni|volani|hodnot)\b' |
                  Measure-Object).Count

        if ($count -gt 0) {
            $stillCzechFiles += [PSCustomObject]@{File=$file; Count=$count}
            Write-Output "$count Czech comments - $file"
        } else {
            $translatedFiles += $file
            Write-Output "TRANSLATED - $file"
        }
    }
}

Write-Output ""
Write-Output "=========================================="
Write-Output "Summary:"
Write-Output "  Translated files: $($translatedFiles.Count)"
Write-Output "  Still contains Czech: $($stillCzechFiles.Count)"
Write-Output "=========================================="
