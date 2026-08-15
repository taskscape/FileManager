[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string] $BuildRoot,
    [Parameter(Mandatory = $true)]
    [string] $OutputPath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$root = (Resolve-Path -LiteralPath $BuildRoot).Path.TrimEnd('\')

function Convert-RvaToFileOffset {
    param([uint32] $Rva, [object[]] $Sections, [string] $Path)

    foreach ($section in $Sections) {
        $span = [Math]::Max($section.VirtualSize, $section.RawSize)
        if ($Rva -ge $section.VirtualAddress -and $Rva -lt ($section.VirtualAddress + $span)) {
            return [int64]$section.RawOffset + ($Rva - $section.VirtualAddress)
        }
    }
    throw "$Path has a debug directory RVA outside its section table."
}

function Get-CodeViewRecord {
    param([string] $Path)

    $stream = [System.IO.File]::OpenRead($Path)
    try {
        $reader = [System.IO.BinaryReader]::new($stream)
        if ($reader.ReadUInt16() -ne 0x5A4D) { throw "$Path is not a PE file." }
        $stream.Position = 0x3C
        $peOffset = $reader.ReadInt32()
        $stream.Position = $peOffset
        if ($reader.ReadUInt32() -ne 0x00004550) { throw "$Path has an invalid PE signature." }
        [void]$reader.ReadUInt16() # machine
        $sectionCount = $reader.ReadUInt16()
        $stream.Position += 12 # timestamp, symbol table fields
        $optionalHeaderSize = $reader.ReadUInt16()
        $stream.Position += 2 # characteristics
        $optionalHeaderOffset = $stream.Position
        $optionalMagic = $reader.ReadUInt16()
        $dataDirectoryOffset = if ($optionalMagic -eq 0x20B) { 112 } elseif ($optionalMagic -eq 0x10B) { 96 } else { throw "$Path has an unsupported PE optional-header format." }
        if ($optionalHeaderSize -lt ($dataDirectoryOffset + (8 * 7))) { throw "$Path has no complete debug data directory." }
        $stream.Position = $optionalHeaderOffset + $dataDirectoryOffset + (8 * 6)
        $debugRva = $reader.ReadUInt32()
        $debugSize = $reader.ReadUInt32()
        if ($debugRva -eq 0 -or $debugSize -lt 28) { throw "$Path has no CodeView debug directory." }

        $stream.Position = $optionalHeaderOffset + $optionalHeaderSize
        $sections = @()
        for ($index = 0; $index -lt $sectionCount; $index++) {
            $stream.Position += 8 # name
            $virtualSize = $reader.ReadUInt32()
            $virtualAddress = $reader.ReadUInt32()
            $rawSize = $reader.ReadUInt32()
            $rawOffset = $reader.ReadUInt32()
            $stream.Position += 16 # relocations, line numbers, counts, characteristics
            $sections += [pscustomobject]@{ VirtualSize = $virtualSize; VirtualAddress = $virtualAddress; RawSize = $rawSize; RawOffset = $rawOffset }
        }

        $debugOffset = Convert-RvaToFileOffset -Rva $debugRva -Sections $sections -Path $Path
        for ($index = 0; $index -lt [Math]::Floor($debugSize / 28); $index++) {
            $stream.Position = $debugOffset + (28 * $index)
            $stream.Position += 12
            $type = $reader.ReadUInt32()
            $dataSize = $reader.ReadUInt32()
            [void]$reader.ReadUInt32() # RVA is unnecessary because PointerToRawData is authoritative for the file.
            $dataOffset = $reader.ReadUInt32()
            if ($type -ne 2 -or $dataSize -lt 24) { continue }

            $stream.Position = $dataOffset
            $signature = [Text.Encoding]::ASCII.GetString($reader.ReadBytes(4))
            if ($signature -ne 'RSDS') { continue }
            $guid = [Guid]::new($reader.ReadBytes(16)).ToString('N').ToUpperInvariant()
            $age = $reader.ReadUInt32()
            $pathBytes = [Collections.Generic.List[byte]]::new()
            while ($stream.Position -lt ($dataOffset + $dataSize)) {
                $next = $reader.ReadByte()
                if ($next -eq 0) { break }
                $pathBytes.Add($next)
            }
            $pdbName = [IO.Path]::GetFileName([Text.Encoding]::UTF8.GetString($pathBytes.ToArray()))
            if ([string]::IsNullOrWhiteSpace($pdbName)) { throw "$Path has an empty CodeView PDB name." }
            return [pscustomobject]@{ Guid = $guid; Age = $age; PdbName = $pdbName }
        }
        throw "$Path has no RSDS CodeView record."
    }
    finally {
        $reader.Dispose()
        $stream.Dispose()
    }
}

$binaries = @(Get-ChildItem -LiteralPath $root -Recurse -File | Where-Object { $_.Extension -in '.exe', '.dll', '.spl' } | Sort-Object FullName)
$pdbFiles = @(Get-ChildItem -LiteralPath $root -Recurse -File -Filter '*.pdb')
if ($binaries.Count -eq 0 -or $pdbFiles.Count -eq 0) { throw "Expected Release binaries and PDBs below $root." }

$entries = foreach ($binary in $binaries) {
    $record = Get-CodeViewRecord -Path $binary.FullName
    # Prefer a sibling PDB; fall back only when the CodeView basename is unique in the build tree.
    $candidates = @($pdbFiles | Where-Object { $_.Name -ieq $record.PdbName })
    $sibling = $candidates | Where-Object { $_.DirectoryName -ieq $binary.DirectoryName } | Select-Object -First 1
    $pdb = if ($null -ne $sibling) { $sibling } elseif ($candidates.Count -eq 1) { $candidates[0] } else { throw "Cannot unambiguously locate $($record.PdbName) for $($binary.FullName)." }
    [ordered]@{
        module = $binary.FullName.Substring($root.Length).TrimStart('\').Replace('\', '/')
        moduleSha256 = (Get-FileHash -LiteralPath $binary.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
        pdb = $pdb.FullName.Substring($root.Length).TrimStart('\').Replace('\', '/')
        pdbSha256 = (Get-FileHash -LiteralPath $pdb.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
        pdbGuid = $record.Guid
        pdbAge = $record.Age
        symbolKey = "$($record.PdbName)/$($record.Guid)$($record.Age)/$($record.PdbName)"
    }
}

$directory = Split-Path -Parent $OutputPath
if (-not [string]::IsNullOrWhiteSpace($directory)) { New-Item -ItemType Directory -Path $directory -Force | Out-Null }
[ordered]@{ schemaVersion = 1; generatedUtc = [DateTime]::UtcNow.ToString('o'); modules = @($entries) } |
    ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $OutputPath -Encoding utf8
Write-Host "Wrote a symbol index for $($entries.Count) modules to $OutputPath"
