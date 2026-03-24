
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

# 2. Copy main executables
function Copy-Exe($srcPatterns, $exeName, $dest) {
    foreach ($pattern in $srcPatterns) {
        $found = Get-ChildItem -Path $pattern -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($found) {
            Copy-Item $found.FullName $dest
            Write-Host "Found $exeName at: $($found.FullName)"
            return $true
        }
    }
    # Fallback: search recursively in BuildDir
    Write-Warning "$exeName not found in primary locations, searching recursively in $BuildDir..."
    $found = Get-ChildItem -Path $BuildDir -Filter $exeName -Recurse -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($found) {
        Copy-Item $found.FullName $dest
        Write-Host "Found $exeName (recursive) at: $($found.FullName)"
        return $true
    }
    return $false
}

$salamandCopied = Copy-Exe @(
    "$BuildDir\salamander\Release_x64\salamand.exe",
    "$BuildDir\Release_x64\salamand.exe",
    "$BuildDir\salamand.exe",
    "src\vcxproj\salamander\Release_x64\salamand.exe"
) "salamand.exe" "$StagingDir\"

$salmonCopied = Copy-Exe @(
    "$BuildDir\salamander\Release_x64\utils\salmon.exe",
    "$BuildDir\salmon\Release_x64\utils\salmon.exe",
    "$BuildDir\Release_x64\salmon.exe",
    "$BuildDir\salmon.exe",
    "src\vcxproj\salmon\salamander\Release_x64\utils\salmon.exe"
) "salmon.exe" "$StagingDir\"

$removeCopied = Copy-Exe @(
    "$BuildDir\remove\Release_x64\remove.exe",
    "$BuildDir\Release_x64\remove.exe",
    "$BuildDir\remove.exe"
) "remove.exe" "$StagingDir\"

if (-not $salamandCopied) { Write-Error "Could not find salamand.exe" }
if (-not $salmonCopied) { Write-Error "Could not find salmon.exe" }
if (-not $removeCopied) { Write-Error "Could not find remove.exe" }

# 3. Copy lang (main app)
Copy-Item "Installer\lang\*" "$StagingDir\lang\" -Recurse

# 4. Copy convert
Copy-Item "convert\*" "$StagingDir\convert\" -Recurse

# 5. Copy toolbars
Copy-Item "src\res\toolbars\*" "$StagingDir\toolbars\" -Recurse

# 6. Copy plugins
# Find all .spl files in both src\plugins and BuildDir
$splFiles = Get-ChildItem -Path "src\plugins", $BuildDir -Recurse -Filter "*.spl" -ErrorAction SilentlyContinue | 
            Where-Object { $_.FullName -match "Release_x64" -and $_.FullName -notmatch "\\Intermediate\\" }

# Use a hash set to avoid duplicates if same file is found in both places
$processedSpl = New-Object System.Collections.Generic.HashSet[string]

foreach ($file in $splFiles) {
    if ($processedSpl.Contains($file.Name)) { continue }
    $processedSpl.Add($file.Name) | Out-Null

    # Extract plugin name from filename (e.g. 7zip.spl -> 7zip)
    $pluginName = $file.BaseName
    $pluginDestDir = New-Item -ItemType Directory -Path "$StagingDir\plugins\$pluginName" -Force
    Copy-Item $file.FullName "$pluginDestDir\"
    Write-Host "Found plugin $pluginName at: $($file.FullName)"
    
    # Copy plugin's lang files (only .slg, exclude Intermediate)
    # Search in the same directory as .spl and its subdirectories
    $stagedLangDir = $null
    Get-ChildItem -Path $file.DirectoryName -Filter "*.slg" -Recurse | 
        Where-Object { $_.FullName -notmatch "\\Intermediate\\" } | ForEach-Object {
            if ($null -eq $stagedLangDir) {
                $stagedLangDir = New-Item -ItemType Directory -Path "$pluginDestDir\lang" -Force
            }
            Copy-Item $_.FullName "$stagedLangDir\"
            Write-Host "  Found lang file: $($_.Name)"
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
