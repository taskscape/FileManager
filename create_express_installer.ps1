# Skrypt do tworzenia instalatora przy użyciu IExpress - wersja poprawiona (debug)

$sourceFolder = "Installer"
$outputName = "FileManager_Setup.exe"
$setupFile = "setup.exe"
$sedFile = "installer.sed"

# Ścieżka bezwzględna do folderu roboczego
$workDir = $PWD.Path

# 1. Spakuj folder Installer do ZIP (zachowanie struktury)
Write-Host "Pakowanie folderu $sourceFolder do ZIP..." -ForegroundColor Cyan
$zipName = "install.zip"
$zipPath = Join-Path $workDir $zipName
if (Test-Path $zipPath) { Remove-Item $zipPath }
Compress-Archive -Path "$sourceFolder\*" -DestinationPath $zipPath -Force

# 2. Utwórz launcher.bat
$batName = "run.bat"
$batPath = Join-Path $workDir $batName
# Używamy prostszego polecenia tar (dostępne w Win10/11) lub powershell do rozpakowania
$batContent = @"
@echo off
cd /d "%~dp0"
powershell -NoProfile -Command "Expand-Archive -Path 'install.zip' -DestinationPath . -Force"
start "" "$setupFile"
"@ 
Set-Content -Path $batPath -Value $batContent -Encoding ASCII

# 3. Przygotuj plik SED
# IExpress wymaga ścieżek w sekcji [SourceFiles] bez cudzysłowów, ale w [Strings] z cudzysłowami jeśli są spacje.
# Najbezpieczniej jest trzymać pliki źródłowe w katalogu bez spacji, ale spróbujemy to obejść.

$targetExe = Join-Path $workDir $outputName

$sedContent = @"
[Version]
Class=IExpress
SEDVersion=3
[Options]
PackagePurpose=InstallApp
ShowInstallProgramWindow=0
HideExtractAnimation=0
UseLongFileName=1
InsideCompressed=0
CAB_FixedSize=0
CAB_ResvCodeSigning=0
RebootMode=N
InstallPrompt=
DisplayLicense=
FinishMessage=
TargetName=$targetExe
FriendlyName=FileManager Installer
AppLaunched=cmd.exe /c $batName
PostInstallCmd=<None>
AdminQuietInstCmd=
UserQuietInstCmd=
SourceFiles=SourceFiles

[Strings]
InstallPrompt=
DisplayLicense=
FinishMessage=
TargetName=$targetExe
FriendlyName=FileManager Installer
AppLaunched=cmd.exe /c $batName
PostInstallCmd=<None>
AdminQuietInstCmd=
UserQuietInstCmd=

[SourceFiles]
SourceFiles0=$workDir\
[SourceFiles0]
%FILE0%=
%FILE1%=
"@

# Podmieniamy zmienne
$finalSed = $sedContent.Replace("%FILE0%", $zipName).Replace("%FILE1%", $batName)
Set-Content -Path $sedFile -Value $finalSed -Encoding ASCII

Write-Host "Uruchamianie IExpress..." -ForegroundColor Cyan
# Uruchamiamy bez /Q żeby zobaczyć błędy jeśli są, ale z /N żeby nie otwierał GUI kreatora
iexpress /N $sedFile

# Czekamy chwilę, bo IExpress działa asynchronicznie w tle
Start-Sleep -Seconds 2

if (Test-Path $targetExe) {
    Write-Host "SUKCES! Utworzono plik: $targetExe" -ForegroundColor Green
    # Sprzątanie tylko przy sukcesie
    Remove-Item $zipPath
    Remove-Item $batPath
    Remove-Item $sedFile
} else {
    Write-Error "Nie udało się utworzyć pliku $targetExe. Sprawdź ewentualne okna błędów IExpress."
}