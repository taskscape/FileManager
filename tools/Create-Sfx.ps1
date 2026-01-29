<#
.SYNOPSIS
Creates a self-extracting executable (SFX) from a specified directory.

.DESCRIPTION
The script compiles a minimal C# stub, packs files from the source directory,
and combines them into a single .exe file. It AUTOMATICALLY includes
the 'toolbars' directory from 'src\res\toolbars'.

.PARAMETER SourceDir
Directory containing installer files (must include setup.exe).

.PARAMETER OutputPath
Path to the output .exe file.

.EXAMPLE
.\Create-Sfx.ps1 -SourceDir "Instalator" -OutputPath "Instalator_SFX.exe"
#> 
param(
    [Parameter(Mandatory=$true)]
    [string]$SourceDir,

    [Parameter(Mandatory=$true)]
    [string]$OutputPath
)

$ErrorActionPreference = "Stop"

# Check if source directory exists
if (-not (Test-Path $SourceDir)) {
    Write-Error "Source directory '$SourceDir' does not exist."
}

# Determine path to 'src\res\toolbars' relative to this script script location
$ScriptRoot = Split-Path $MyInvocation.MyCommand.Path
$ToolbarsSrcPath = Join-Path $ScriptRoot "..\src\res\toolbars"

# Normalize path
if (Test-Path $ToolbarsSrcPath) {
    # Resolve-Path returns a PathInfo object, so we specifically ask for the .Path string property
    $ToolbarsSrcPath = (Resolve-Path $ToolbarsSrcPath).Path
} else {
    Write-Warning "Toolbars directory not found at: $ToolbarsSrcPath. It will not be included."
    $ToolbarsSrcPath = $null
}

# --- 1. C# Code for the Stub ---
$csharpSource = @"
using System;
using System.Diagnostics;
using System.IO;
using System.IO.Compression;
using System.Reflection;
using System.Threading;

[assembly: AssemblyTitle("SFX Installer")]
[assembly: AssemblyProduct("SFX Installer")]
[assembly: AssemblyVersion("1.0.0.0")]

namespace SfxStub
{
    class Program
    {
        static void Main(string[] args)
        {
            string tempDir = Path.Combine(Path.GetTempPath(), "Install_" + Guid.NewGuid().ToString("N"));
            string currentExe = Process.GetCurrentProcess().MainModule.FileName;

            try
            {
                // -- Step 1: Read ZIP Data --
                byte[] zipData;
                using (var fs = new FileStream(currentExe, FileMode.Open, FileAccess.Read, FileShare.Read))
                {
                    if (fs.Length < 8) return; 

                    // Read offset (last 8 bytes)
                    fs.Seek(-8, SeekOrigin.End);
                    long zipStartOffset;
                    using (var br = new BinaryReader(fs, System.Text.Encoding.Default, true))
                    {
                        zipStartOffset = br.ReadInt64();
                    }

                    // Calculate ZIP length (File Length - Start Offset - 8 bytes footer)
                    long zipLength = fs.Length - zipStartOffset - 8;

                    if (zipLength <= 0)
                    {
                        throw new Exception("Invalid SFX file structure.");
                    }

                    // Load ONLY ZIP data into memory
                    zipData = new byte[zipLength];
                    fs.Seek(zipStartOffset, SeekOrigin.Begin);
                    fs.Read(zipData, 0, (int)zipLength);
                }

                // -- Step 2: Extract --
                // Console.WriteLine("Extracting..."); 
                Directory.CreateDirectory(tempDir);

                using (var ms = new MemoryStream(zipData))
                using (var archive = new ZipArchive(ms, ZipArchiveMode.Read))
                {
                    archive.ExtractToDirectory(tempDir);
                }

                // -- Step 3: Run setup.exe --
                string setupExe = Path.Combine(tempDir, "setup.exe");
                
                if (!File.Exists(setupExe))
                {
                    string[] exeFiles = Directory.GetFiles(tempDir, "*.exe");
                    if (exeFiles.Length > 0) setupExe = exeFiles[0];
                    else throw new FileNotFoundException("setup.exe not found.");
                }

                ProcessStartInfo psi = new ProcessStartInfo(setupExe);
                psi.WorkingDirectory = tempDir;
                if (args.Length > 0) {
                     psi.Arguments = string.Join(" ", args);
                }

                Process p = Process.Start(psi);
                if (p != null) p.WaitForExit();
            }
            catch (Exception ex)
            {
                Console.WriteLine("Error: " + ex.Message);
                Console.WriteLine("Press any key to exit...");
                Console.ReadKey();
            }
            finally
            {
                // -- Step 4: Cleanup --
                try 
                {
                    if (Directory.Exists(tempDir)) Directory.Delete(tempDir, true); 
                } 
                catch { } 
            }
        }
    }
}
"@

# --- 2. Compile Stub ---
$stubPath = Join-Path $env:TEMP "sfx_stub.exe"
Write-Host "Compiling C# code..." -ForegroundColor Cyan

$assemblies = @("System.IO.Compression", "System.IO.Compression.FileSystem")

try {
    Add-Type -TypeDefinition $csharpSource -OutputAssembly $stubPath -OutputType ConsoleApplication -ReferencedAssemblies $assemblies
}
catch {
    Write-Error "Compilation error. Ensure .NET Framework is installed."
}

# --- 3. Pack Files ---
$tempZip = [System.IO.Path]::GetTempFileName()
if (Test-Path $tempZip) { Remove-Item $tempZip }
$tempZip = $tempZip + ".zip"

# Create a staging directory to combine SourceDir and Toolbars
$stagingDir = Join-Path $env:TEMP ("sfx_stage_" + [Guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Path $stagingDir | Out-Null

try {
    Write-Host "Preparing files in staging area..." -ForegroundColor Cyan
    
    # Copy SourceDir content
    Copy-Item -Path "$SourceDir\*" -Destination $stagingDir -Recurse -Force

    # Copy Toolbars (Hardcoded)
    if ($ToolbarsSrcPath) {
        $destPath = Join-Path $stagingDir "toolbars"
        Write-Host "Auto-including toolbars from: $ToolbarsSrcPath" -ForegroundColor Cyan
        
        # Explicitly create destination directory
        if (-not (Test-Path $destPath)) {
            New-Item -ItemType Directory -Path $destPath | Out-Null
        }

        # Copy CONTENT of toolbars into destPath
        Copy-Item -Path "$ToolbarsSrcPath\*" -Destination $destPath -Recurse -Force
    }

    Write-Host "Compressing contents of staging area:" -ForegroundColor Yellow
    # List files relative to staging dir so user sees structure
    Get-ChildItem -Path $stagingDir -Recurse | Select-Object -ExpandProperty FullName | ForEach-Object { $_.Substring($stagingDir.Length) }
    
    Write-Host "`nCompressing..." -ForegroundColor Cyan
    Compress-Archive -Path "$stagingDir\*" -DestinationPath $tempZip -CompressionLevel Optimal
}
finally {
    # Cleanup staging dir
    if (Test-Path $stagingDir) { Remove-Item $stagingDir -Recurse -Force }
}

# --- 4. Combine (Stub + Zip + Offset) ---
Write-Host "Creating output file: $OutputPath" -ForegroundColor Cyan

try {
    $stubBytes = [System.IO.File]::ReadAllBytes($stubPath)
    $zipBytes = [System.IO.File]::ReadAllBytes($tempZip)

    $offset = $stubBytes.Length
    $offsetBytes = [System.BitConverter]::GetBytes([long]$offset)

    $fs = [System.IO.File]::Create($OutputPath)
    $fs.Write($stubBytes, 0, $stubBytes.Length)
    $fs.Write($zipBytes, 0, $zipBytes.Length)
    $fs.Write($offsetBytes, 0, $offsetBytes.Length)
    $fs.Close()

    Write-Host "Done! Created: $OutputPath" -ForegroundColor Green
}
catch {
    Write-Error "Error combining files: $_ "
}
finally {
    if (Test-Path $stubPath) { Remove-Item $stubPath }
    if (Test-Path $tempZip) { Remove-Item $tempZip }
}