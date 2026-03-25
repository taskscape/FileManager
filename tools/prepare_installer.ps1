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
New-Item -ItemType Directory -Path "$StagingDir\utils"

# 1. Copy base installer files
Copy-Item "Installer\setup.exe" "$StagingDir\" -ErrorAction SilentlyContinue
Copy-Item "Installer\LICENSE" "$StagingDir\" -ErrorAction SilentlyContinue
Copy-Item "Installer\x64" "$StagingDir\" -ErrorAction SilentlyContinue

# 2. Copy main executables and DLLs
function Copy-Exe($srcPatterns, $fileName, $dest) {
    foreach ($pattern in $srcPatterns) {
        $found = Get-ChildItem -Path $pattern -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($found) {
            Copy-Item $found.FullName $dest
            Write-Host "Found $fileName at: $($found.FullName)"
            return $true
        }
    }
    # Fallback: search recursively in BuildDir and src
    $found = Get-ChildItem -Path $BuildDir, "src" -Filter $fileName -Recurse -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($found) {
        Copy-Item $found.FullName $dest
        Write-Host "Found $fileName (recursive) at: $($found.FullName)"
        return $true
    }
    Write-Warning "$fileName not found in primary locations or recursively."
    return $false
}

# Main exes
$salamandCopied = Copy-Exe @("$BuildDir\Release_x64\salamand.exe", "src\vcxproj\salamander\Release_x64\salamand.exe") "salamand.exe" "$StagingDir\"
$salmonCopied = Copy-Exe @("$BuildDir\Release_x64\salmon.exe", "src\vcxproj\salmon\salamander\Release_x64\utils\salmon.exe") "salmon.exe" "$StagingDir\"
$removeCopied = Copy-Exe @("$BuildDir\Release_x64\remove.exe", "src\vcxproj\setup\remove\Release_x64\remove.exe") "remove.exe" "$StagingDir\"

if (-not $salamandCopied) { Write-Error "Could not find salamand.exe" }
if (-not $salmonCopied) { Write-Error "Could not find salmon.exe" }
if (-not $removeCopied) { Write-Error "Could not find remove.exe" }

# Shell extensions
Copy-Exe @("$BuildDir\Release_x64\salextx64.dll", "$BuildDir\shellext\Release_x64\salextx64.dll", "src\vcxproj\shellext\salamander\Release_x64\plugins\Intermediate\salextx64\salextx64.dll", "src\vcxproj\shellext\salamander\Release_x64\salextx64.dll") "salextx64.dll" "$StagingDir\"
Copy-Exe @("$BuildDir\Release_Win32\salextx86.dll", "$BuildDir\shellext\Release_Win32\salextx86.dll", "$BuildDir\Release_x64\salextx86.dll", "src\vcxproj\shellext\salamander\Release_x86\plugins\Intermediate\salextx86\salextx86.dll", "src\vcxproj\shellext\salamander\Release_x86\salextx86.dll") "salextx86.dll" "$StagingDir\"

# Utils

# OpenSSL
$opensslCopied1 = Copy-Exe @("utils\libeay32.dll", "external\openssl\libeay32.dll", "libeay32.dll") "libeay32.dll" "$StagingDir\utils\"
$opensslCopied2 = Copy-Exe @("utils\ssleay32.dll", "external\openssl\ssleay32.dll", "ssleay32.dll") "ssleay32.dll" "$StagingDir\utils\"

if (-not $opensslCopied1 -or -not $opensslCopied2) {
    Write-Warning "OpenSSL DLLs not found. FTP encryption might not work."
}

# 3. Copy lang (main app)
Copy-Item "Installer\lang\*" "$StagingDir\lang\" -Recurse -ErrorAction SilentlyContinue

# 4. Copy convert
Copy-Item "convert\*" "$StagingDir\convert\" -Recurse -ErrorAction SilentlyContinue

# 5. Copy toolbars
Copy-Item "src\res\toolbars\*" "$StagingDir\toolbars\" -Recurse -ErrorAction SilentlyContinue

# 6. Copy plugins
$splFiles = Get-ChildItem -Path "src\plugins", $BuildDir -Recurse -Filter "*.spl" -ErrorAction SilentlyContinue | 
            Where-Object { $_.FullName -match "Release_x64" -and $_.FullName -notmatch "\\Intermediate\\" }

$processedSpl = New-Object System.Collections.Generic.HashSet[string]

foreach ($file in $splFiles) {
    if ($processedSpl.Contains($file.Name)) { continue }
    $processedSpl.Add($file.Name) | Out-Null

    $pluginName = $file.BaseName
    $pluginDestDir = New-Item -ItemType Directory -Path "$StagingDir\plugins\$pluginName" -Force
    Copy-Item $file.FullName "$pluginDestDir\"
    Write-Host "Found plugin $pluginName at: $($file.FullName)"
    
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

function Add-FileToSetupInf($fileRelPath) {
    $script:setupInf += "`n$fileRelPath,%1\$fileRelPath,0"
}

$rootFiles = @("salextx64.dll", "salextx86.dll")
foreach ($rf in $rootFiles) {
    if (Test-Path "$StagingDir\$rf") {
        Add-FileToSetupInf $rf
    }
}

function Add-ToSetupInf($path) {
    if (Test-Path "$StagingDir\$path") {
        $files = Get-ChildItem -Path "$StagingDir\$path" -File -Recurse
        foreach ($f in $files) {
            $relPath = $f.FullName.Substring((Get-Item $StagingDir).FullName.Length + 1)
            if ($relPath -notmatch "\\Intermediate\\") {
                Add-FileToSetupInf $relPath
            }
        }
    }
}

Add-ToSetupInf "lang"
Add-ToSetupInf "convert"
Add-ToSetupInf "toolbars"
Add-ToSetupInf "plugins"
Add-ToSetupInf "utils"

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
