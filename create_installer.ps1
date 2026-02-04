# Skrypt do tworzenia instalatora SFX (Self-Extracting Executable)
# Wymaga zainstalowanego 7-Zip.

$sourceFolder = "Installer"
$outputName = "FileManager_Setup.exe"
$setupFile = "setup.exe" # Plik wewnątrz folderu Installer, który ma się uruchomić

# 1. Znajdź 7-Zip
$7z = "$env:ProgramFiles\7-Zip\7z.exe"
if (-not (Test-Path $7z)) {
    $7z = "$env:ProgramFiles(x86)\7-Zip\7z.exe"
}

if (-not (Test-Path $7z)) {
    Write-Error "Nie znaleziono 7-Zip! Zainstaluj 7-Zip, aby utworzyć instalator SFX."
    return
}

# 2. Znajdź moduł SFX (7z.sfx)
$sfxModule = Join-Path (Split-Path $7z) "7z.sfx"
if (-not (Test-Path $sfxModule)) {
    Write-Error "Nie znaleziono modułu 7z.sfx w folderze 7-Zip."
    return
}

Write-Host "Znaleziono 7-Zip: $7z" -ForegroundColor Green

# 3. Utwórz plik konfiguracyjny dla SFX
$configContent = @"
;!@Install@!UTF-8!
Title="FileManager Setup"
RunProgram="$sourceFolder\$setupFile"
GUIMode="1"
;!@InstallEnd@!
"@
Set-Content -Path "config.txt" -Value $configContent -Encoding UTF8

# 4. Spakuj folder Installer do tymczasowego archiwum .7z
$tempArchive = "temp_installer.7z"
if (Test-Path $tempArchive) { Remove-Item $tempArchive }

Write-Host "Pakowanie plików..." -ForegroundColor Cyan
& $7z a $tempArchive $sourceFolder -mx9

if (-not (Test-Path $tempArchive)) {
    Write-Error "Błąd podczas pakowania plików."
    return
}

# 5. Połącz SFX + Config + Archiwum w jeden plik .exe
Write-Host "Tworzenie pliku wykonywalnego $outputName..." -ForegroundColor Cyan
cmd /c "copy /b `"$sfxModule`" + config.txt + $tempArchive `"$outputName`""

# 6. Sprzątanie
Remove-Item "config.txt"
Remove-Item $tempArchive

if (Test-Path $outputName) {
    Write-Host "SUKCES! Utworzono plik: $PWD\$outputName" -ForegroundColor Green
} else {
    Write-Error "Nie udało się utworzyć pliku wynikowego."
}
