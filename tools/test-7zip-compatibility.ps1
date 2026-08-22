[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$WrapperPath,
    [Parameter(Mandatory = $true)]
    [string]$EnginePath,
    [string]$SevenZipPath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repositoryRoot = Split-Path -Parent $PSScriptRoot
$corpusRoot = Join-Path $repositoryRoot 'tests\7zip-vectors'
$expectedManifestPath = Join-Path $corpusRoot 'expected-manifest.txt'

function Resolve-SevenZipOracle {
    param([string]$RequestedPath)

    if (-not [string]::IsNullOrWhiteSpace($RequestedPath)) {
        if (-not (Test-Path -LiteralPath $RequestedPath -PathType Leaf)) {
            throw "The requested 7-Zip oracle does not exist: $RequestedPath"
        }
        return (Resolve-Path -LiteralPath $RequestedPath).Path
    }

    $command = Get-Command 7z.exe -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($null -eq $command) {
        throw 'A 7z.exe-compatible oracle is required for the 7-Zip compatibility snapshot test.'
    }
    return $command.Source
}

function Invoke-SevenZipOracle {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Executable,
        [Parameter(Mandatory = $true)]
        [string[]]$Arguments
    )

    & $Executable @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "7-Zip oracle failed ($LASTEXITCODE): $($Arguments -join ' ')"
    }
}

function Get-Sha256Hex {
    param([Parameter(Mandatory = $true)][string]$Path)

    # Use the BCL directly because minimal Windows PowerShell hosts can omit the Get-FileHash cmdlet.
    $stream = [System.IO.File]::OpenRead($Path)
    try {
        $bytes = [System.Security.Cryptography.SHA256]::Create().ComputeHash($stream)
        return (($bytes | ForEach-Object { $_.ToString('x2') }) -join '')
    }
    finally {
        $stream.Dispose()
    }
}

function Get-ExtractionManifest {
    param([Parameter(Mandatory = $true)][string]$Root)

    return @(Get-ChildItem -LiteralPath $Root -File -Recurse | ForEach-Object {
        $relative = $_.FullName.Substring($Root.Length).TrimStart('\', '/') -replace '\\', '/'
        $hash = Get-Sha256Hex $_.FullName
        "$relative|$($_.Length)|$hash"
    } | Sort-Object)
}

foreach ($path in @($WrapperPath, $EnginePath, $corpusRoot, $expectedManifestPath)) {
    if (-not (Test-Path -LiteralPath $path)) {
        throw "7-Zip compatibility input does not exist: $path"
    }
}

$oracle = Resolve-SevenZipOracle $SevenZipPath
$workRoot = Join-Path ([System.IO.Path]::GetTempPath()) ('filemanager-7zip-compat-' + [Guid]::NewGuid().ToString('N'))
try {
    $binaryDirectory = Join-Path $workRoot 'bin'
    $sourceDirectory = Join-Path $workRoot 'source'
    $wrapperExtraction = Join-Path $workRoot 'wrapper-extracted'
    $oracleExtraction = Join-Path $workRoot 'oracle-extracted'
    $wrapperArchive = Join-Path $workRoot 'wrapper.7z'
    $oracleArchive = Join-Path $workRoot 'oracle.7z'
    New-Item -ItemType Directory -Path $binaryDirectory, $sourceDirectory, $wrapperExtraction, $oracleExtraction -Force | Out-Null
    Copy-Item -LiteralPath $WrapperPath -Destination (Join-Path $binaryDirectory '7zwrapper.dll')
    Copy-Item -LiteralPath $EnginePath -Destination (Join-Path $binaryDirectory '7za.dll')
    Copy-Item -LiteralPath (Join-Path $corpusRoot 'alpha.txt'), (Join-Path $corpusRoot 'bravo.txt') -Destination $sourceDirectory
    # An empty member catches the zero-length stream path without relying on a generated archive fixture.
    [System.IO.File]::WriteAllBytes((Join-Path $sourceDirectory 'zero.bin'), [byte[]]@())

    $expectedManifest = @(Get-Content -LiteralPath $expectedManifestPath | Where-Object { $_ -and -not $_.StartsWith('#') })
    $sourceManifest = Get-ExtractionManifest $sourceDirectory
    if ($expectedManifest.Count -eq 0) {
        throw 'The retained 7-Zip expected manifest is empty.'
    }
    if (Compare-Object -ReferenceObject $expectedManifest -DifferenceObject $sourceManifest) {
        throw 'The retained 7-Zip corpus no longer matches its expected manifest snapshot.'
    }

    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
using System.Text;
public static class FileManagerSevenZipCompatibilityNative
{
    [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
    public static extern IntPtr LoadLibrary(string fileName);

    [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Ansi)]
    public static extern IntPtr GetProcAddress(IntPtr module, string procedureName);

    [DllImport("kernel32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static extern bool FreeLibrary(IntPtr module);

    [UnmanagedFunctionPointer(CallingConvention.StdCall, CharSet = CharSet.Ansi)]
    [return: MarshalAs(UnmanagedType.Bool)]
    public delegate bool CompressFilesDelegate(string archiveName, string sourceDirectory, string filter, StringBuilder errorMessage, int errorMessageSize);
}
'@
    # Load explicitly so this process can release the wrapper before removing its isolated binary directory.
    $wrapperModule = [FileManagerSevenZipCompatibilityNative]::LoadLibrary((Join-Path $binaryDirectory '7zwrapper.dll'))
    if ($wrapperModule -eq [IntPtr]::Zero) { throw 'Could not load the isolated 7-Zip wrapper DLL.' }
    Push-Location $sourceDirectory
    try {
        $export = [FileManagerSevenZipCompatibilityNative]::GetProcAddress($wrapperModule, 'CompressFiles')
        if ($export -eq [IntPtr]::Zero) { throw 'The isolated 7-Zip wrapper does not export CompressFiles.' }
        $compressFiles = [Runtime.InteropServices.Marshal]::GetDelegateForFunctionPointer($export, [type][FileManagerSevenZipCompatibilityNative+CompressFilesDelegate])
        $message = [System.Text.StringBuilder]::new(4096)
        if (-not $compressFiles.Invoke($wrapperArchive, $sourceDirectory, 'alpha.txt|bravo.txt|zero.bin', $message, $message.Capacity)) {
            throw "The bundled 7-Zip wrapper could not create the corpus archive: $message"
        }
        Invoke-SevenZipOracle -Executable $oracle -Arguments @('a', '-t7z', '-y', $oracleArchive, 'alpha.txt', 'bravo.txt', 'zero.bin')
    }
    finally {
        Pop-Location
        if ($wrapperModule -ne [IntPtr]::Zero) { [void][FileManagerSevenZipCompatibilityNative]::FreeLibrary($wrapperModule) }
    }

    Invoke-SevenZipOracle -Executable $oracle -Arguments @('t', '-y', $wrapperArchive)
    Invoke-SevenZipOracle -Executable $oracle -Arguments @('x', '-y', "-o$wrapperExtraction", $wrapperArchive)
    Invoke-SevenZipOracle -Executable $oracle -Arguments @('t', '-y', $oracleArchive)
    Invoke-SevenZipOracle -Executable $oracle -Arguments @('x', '-y', "-o$oracleExtraction", $oracleArchive)
    foreach ($manifest in @((Get-ExtractionManifest $wrapperExtraction), (Get-ExtractionManifest $oracleExtraction))) {
        if (Compare-Object -ReferenceObject $expectedManifest -DifferenceObject $manifest) {
            throw 'A 7-Zip archive extraction did not match the retained corpus snapshot.'
        }
    }

    [byte[]]$archiveBytes = [System.IO.File]::ReadAllBytes($wrapperArchive)
    if ($archiveBytes.Length -lt 33) {
        throw 'The wrapper corpus archive is unexpectedly too short to corrupt.'
    }
    $mutations = @(
        @{ Name = 'header-bitflip'; Offset = 0 },
        @{ Name = 'payload-bitflip'; Offset = [int]($archiveBytes.Length / 2) },
        @{ Name = 'footer-bitflip'; Offset = $archiveBytes.Length - 1 }
    )
    foreach ($mutation in $mutations) {
        [byte[]]$corruptBytes = $archiveBytes.Clone()
        # These named byte mutations retain every discovered parser failure as a deterministic regression case.
        $corruptBytes[$mutation.Offset] = $corruptBytes[$mutation.Offset] -bxor 0xFF
        $corruptArchive = Join-Path $workRoot ("$($mutation.Name).7z")
        [System.IO.File]::WriteAllBytes($corruptArchive, $corruptBytes)
        & $oracle 't' '-y' $corruptArchive
        if ($LASTEXITCODE -eq 0) {
            throw "The 7-Zip oracle accepted the $($mutation.Name) corpus mutation."
        }
    }

    Write-Host '7-Zip wrapper/oracle differential corpus, extraction snapshots, and corrupt-archive regression passed.'
}
finally {
    if (Test-Path -LiteralPath $workRoot) {
        # This test only removes its resolved GUID temporary root after all archive handles have closed.
        Remove-Item -LiteralPath $workRoot -Recurse -Force
    }
}
