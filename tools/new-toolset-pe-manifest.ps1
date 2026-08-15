[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string] $BuildRoot,
    [Parameter(Mandatory = $true)]
    [string] $Toolset,
    [Parameter(Mandatory = $true)]
    [string] $OutputPath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$root = (Resolve-Path -LiteralPath $BuildRoot).Path.TrimEnd('\')
$files = @(Get-ChildItem -LiteralPath $root -Recurse -File |
    Where-Object { $_.Extension -in '.exe', '.dll', '.spl' } |
    Sort-Object FullName)
if ($files.Count -eq 0) {
    throw "No PE outputs were found below $root"
}

function Get-PeMachine {
    param([string] $Path)

    $stream = [System.IO.File]::OpenRead($Path)
    try {
        $reader = [System.IO.BinaryReader]::new($stream)
        if ($reader.ReadUInt16() -ne 0x5A4D) { throw "$Path is not a PE file." }
        $stream.Position = 0x3C
        $peOffset = $reader.ReadInt32()
        $stream.Position = $peOffset
        if ($reader.ReadUInt32() -ne 0x00004550) { throw "$Path has an invalid PE signature." }
        return ('0x{0:X4}' -f $reader.ReadUInt16())
    }
    finally {
        $stream.Dispose()
    }
}

$entries = foreach ($file in $files) {
    # Record exact bytes as evidence while comparing compatibility metadata separately across compilers.
    [ordered]@{
        path = $file.FullName.Substring($root.Length).TrimStart('\').Replace('\', '/')
        machine = Get-PeMachine -Path $file.FullName
        fileVersion = $file.VersionInfo.FileVersion
        productVersion = $file.VersionInfo.ProductVersion
        sha256 = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
        length = $file.Length
    }
}

$manifest = [ordered]@{
    schemaVersion = 1
    toolset = $Toolset
    generatedUtc = [DateTime]::UtcNow.ToString('o')
    files = @($entries)
}

$directory = Split-Path -Parent $OutputPath
if (-not [string]::IsNullOrWhiteSpace($directory)) {
    New-Item -ItemType Directory -Path $directory -Force | Out-Null
}
$manifest | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $OutputPath -Encoding utf8
Write-Host "Wrote $Toolset PE manifest for $($files.Count) files to $OutputPath"
