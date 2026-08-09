param(
    [string]$BuildDir = "build_stage",
    [string]$StagingDir = "Installer_Staging",
    [string]$BuildNumber = "0"
)

Write-Host "=== Open Salamander Installer Staging Script ===" -ForegroundColor Cyan
Write-Host "Build Number: $BuildNumber" -ForegroundColor Cyan
Write-Host "Staging files for Inno Setup..."

if (Test-Path $StagingDir) { Remove-Item $StagingDir -Recurse -Force }
New-Item -ItemType Directory -Path $StagingDir | Out-Null
New-Item -ItemType Directory -Path "$StagingDir\plugins" | Out-Null
New-Item -ItemType Directory -Path "$StagingDir\lang" | Out-Null
New-Item -ItemType Directory -Path "$StagingDir\convert" | Out-Null
New-Item -ItemType Directory -Path "$StagingDir\toolbars" | Out-Null
New-Item -ItemType Directory -Path "$StagingDir\utils" | Out-Null

# Write build info file for traceability
@"
Build Number: $BuildNumber
Build Date: $(Get-Date -Format "yyyy-MM-dd HH:mm:ss UTC" -AsUTC)
"@ | Out-File -FilePath "$StagingDir\build_info.txt" -Encoding utf8

# 1. Copy license file
Copy-Item "Installer\LICENSE" "$StagingDir\" -ErrorAction SilentlyContinue

# 2. Copy main executables and DLLs
function Copy-Exe($srcPatterns, $fileName, $dest) {
    foreach ($pattern in $srcPatterns) {
        $found = Get-ChildItem -Path $pattern -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($found) {
            Copy-Item $found.FullName $dest
            Write-Host "Found $fileName at: $($found.FullName)" -ForegroundColor Green
            return $true
        }
    }
    # Fallback: search recursively in BuildDir and src
    $found = Get-ChildItem -Path $BuildDir, "src" -Filter $fileName -Recurse -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($found) {
        Copy-Item $found.FullName $dest
        Write-Host "Found $fileName (recursive) at: $($found.FullName)" -ForegroundColor Green
        return $true
    }
    Write-Warning "$fileName not found in primary locations or recursively."
    return $false
}

# Main exes
$salamandCopied = Copy-Exe @("$BuildDir\Release_x64\salamand.exe", "src\vcxproj\salamander\Release_x64\salamand.exe") "salamand.exe" "$StagingDir\"
$salmonCopied = Copy-Exe @("$BuildDir\Release_x64\salmon.exe", "src\vcxproj\salmon\salamander\Release_x64\utils\salmon.exe") "salmon.exe" "$StagingDir\"

if (-not $salamandCopied) { Write-Error "Could not find salamand.exe" }
if (-not $salmonCopied) { Write-Error "Could not find salmon.exe" }

# Shell extensions are installed from the utils directory and registered by Salamander itself.
Copy-Exe @("$BuildDir\Release_x64\salextx64.dll", "$BuildDir\shellext\Release_x64\salextx64.dll", "src\vcxproj\shellext\salamander\Release_x64\plugins\Intermediate\salextx64\salextx64.dll", "src\vcxproj\shellext\salamander\Release_x64\salextx64.dll") "salextx64.dll" "$StagingDir\utils\"
Copy-Exe @("$BuildDir\Release_Win32\salextx86.dll", "$BuildDir\shellext\Release_Win32\salextx86.dll", "$BuildDir\Release_x64\salextx86.dll", "src\vcxproj\shellext\salamander\Release_x86\plugins\Intermediate\salextx86\salextx86.dll", "src\vcxproj\shellext\salamander\Release_x86\salextx86.dll") "salextx86.dll" "$StagingDir\utils\"

# Utils (Note: salpvenv.exe excluded - depends on proprietary PVW32Cnv.lib)
Copy-Exe @("$BuildDir\Release_x64\salopen.exe") "salopen.exe" "$StagingDir\"
Copy-Exe @("$BuildDir\Release_x64\salspawn.exe") "salspawn.exe" "$StagingDir\"
Copy-Exe @("$BuildDir\Release_x64\tserver.exe", "$BuildDir\Release_Win32\tserver.exe") "tserver.exe" "$StagingDir\"
Copy-Exe @("$BuildDir\Release_x64\sfx7zip.exe", "$BuildDir\Release_Win32\sfx7zip.exe") "sfx7zip.exe" "$StagingDir\"
Copy-Exe @("$BuildDir\Release_x64\zip2sfx.exe", "$BuildDir\Release_Win32\zip2sfx.exe") "zip2sfx.exe" "$StagingDir\"
Copy-Exe @("$BuildDir\Release_x64\translator.exe", "$BuildDir\Release_Win32\translator.exe") "translator.exe" "$StagingDir\"
Copy-Exe @("$BuildDir\Release_x64\fcremote.exe") "fcremote.exe" "$StagingDir\"
Copy-Exe @("$BuildDir\Release_x64\7zwrapper.exe") "7zwrapper.exe" "$StagingDir\"

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
    Write-Host "Found plugin $pluginName at: $($file.FullName)" -ForegroundColor Green
    
    $stagedLangDir = $null
    Get-ChildItem -Path $file.DirectoryName -Filter "*.slg" -Recurse | 
        Where-Object { $_.FullName -notmatch "\\Intermediate\\" } | ForEach-Object {
            if ($null -eq $stagedLangDir) {
                $stagedLangDir = New-Item -ItemType Directory -Path "$pluginDestDir\lang" -Force
            }
            Copy-Item $_.FullName "$stagedLangDir\"
            Write-Host "  Found lang file: $($_.Name)" -ForegroundColor Gray
        }
}

Write-Host "`n=== Staging Complete ===" -ForegroundColor Cyan
Write-Host "Files staged in: $StagingDir"
Write-Host "Ready for Inno Setup compilation."
