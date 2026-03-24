
param(
    [string]$BuildDir = "build_stage",
    [string]$StagingDir = "Installer_Staging",
    [string]$OutputPath = "OpenSalamander_v5.exe"
)

if (Test-Path $StagingDir) { Remove-Item $StagingDir -Recurse -Force }
New-Item -ItemType Directory -Path $StagingDir
New-Item -ItemType Directory -Path "$StagingDir\plugins"
New-Item -ItemType Directory -Path "$StagingDir\lang"
New-Item -ItemType Directory -Path "$StagingDir\convert"
New-Item -ItemType Directory -Path "$StagingDir\toolbars"

# 1. Copy base installer files
Copy-Item "Installer\setup.exe" "$StagingDir\"
Copy-Item "Installer\LICENSE" "$StagingDir\"
Copy-Item "Installer\x64" "$StagingDir\"

# 2. Copy main executables (from Release_x64)
Copy-Item "src\vcxproj\salamander\Release_x64\salamand.exe" "$StagingDir\"
Copy-Item "src\vcxproj\salmon\salamander\Release_x64\utils\salmon.exe" "$StagingDir\"
Copy-Item "$BuildDir\remove\Release_x64\remove.exe" "$StagingDir\"

# 3. Copy lang (main app)
Copy-Item "Installer\lang\*" "$StagingDir\lang\" -Recurse

# 4. Copy convert
Copy-Item "convert\*" "$StagingDir\convert\" -Recurse

# 5. Copy toolbars
Copy-Item "src\res\toolbars\*" "$StagingDir\toolbars\" -Recurse

# 6. Copy plugins
# Find all .spl files in Release_x64 directories
$splFiles = Get-ChildItem -Path "src\plugins" -Recurse -Filter "*.spl" | Where-Object { $_.FullName -match "Release_x64" }
foreach ($file in $splFiles) {
    # Extract plugin name from path (usually ...\plugins\<name>\<name>.spl)
    $pluginName = $file.Directory.Name
    $pluginDestDir = New-Item -ItemType Directory -Path "$StagingDir\plugins\$pluginName" -Force
    Copy-Item $file.FullName "$pluginDestDir\"
    
    # Copy plugin's lang files (only .slg, exclude Intermediate)
    $pluginLangDir = Join-Path $file.DirectoryName "lang"
    if (Test-Path $pluginLangDir) {
        $stagedLangDir = New-Item -ItemType Directory -Path "$pluginDestDir\lang" -Force
        Get-ChildItem -Path $pluginLangDir -Filter "*.slg" | Copy-Item -Destination "$stagedLangDir\"
    }
}

# 7. Generate setup.inf
$setupInf = @"
[Private]
ApplicationName=Open Salamander 5.0
ApplicationNameVer=Open Salamander 5.0
DefaultDirectory=%4%\Open Salamander 5.0
LicenseFile=LICENSE
SkipChooseDirectory=0
SaveRemoveLog=%1%\uninstall.log
UninstallRunProgramQuietPath=%1%\remove.exe

[CopyFiles]
salamand.exe,%1\salamand.exe,0
salmon.exe,%1\salmon.exe,0
remove.exe,%1\remove.exe,0
"@

# Function to add files to setup.inf
function Add-ToSetupInf($path) {
    $files = Get-ChildItem -Path "$StagingDir\$path" -File -Recurse
    foreach ($f in $files) {
        $relPath = $f.FullName.Substring((Get-Item $StagingDir).FullName.Length + 1)
        # Check if the file is in an Intermediate directory (shouldn't be there, but just in case)
        if ($relPath -notmatch "\\Intermediate\\") {
            $script:setupInf += "`n$relPath,%1\$relPath,0"
        }
    }
}

Add-ToSetupInf "lang"
Add-ToSetupInf "convert"
Add-ToSetupInf "toolbars"
Add-ToSetupInf "plugins"

$setupInf += @"

[CreateShortcuts]
0,Open Salamander 5.0,%1\salamand.exe,
1,Open Salamander 5.0,%1\salamand.exe,
"@

$setupInf | Out-File -FilePath "$StagingDir\setup.inf" -Encoding utf8

# 8. Create SFX using the existing tool
$ScriptRoot = Split-Path $MyInvocation.MyCommand.Path
$SfxTool = Join-Path $ScriptRoot "Create-Sfx.ps1"

powershell.exe -File "$SfxTool" -SourceDir "$StagingDir" -OutputPath "$OutputPath"

Write-Host "Installer created: $OutputPath"
