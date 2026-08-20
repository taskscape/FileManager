[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('Setup', 'Cleanup')]
    [string]$Action,

    [Parameter(Mandatory = $true)]
    [string]$WorkingDirectory,

    [string]$EnvironmentFile
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$managedDirectory = [IO.Path]::GetFullPath($WorkingDirectory)
# Cleanup is intentionally restricted to one fixed leaf below the caller-selected runner temp directory.
if ([IO.Path]::GetFileName($managedDirectory.TrimEnd([IO.Path]::DirectorySeparatorChar)) -cne 'filemanager-ui-volumes') {
    throw "WorkingDirectory must end with the exact managed leaf 'filemanager-ui-volumes': $managedDirectory"
}

# Match the NUnit sandbox contract so each freshly provisioned volume is safe to reset and remove.
$ownershipMarkerName = '.filemanager-testdata-owner'
$ownershipMarkerContents = 'Open Salamander UI test sandbox'

function Invoke-ManagedDiskPart {
    param(
        [Parameter(Mandatory = $true)]
        [string[]]$Commands
    )

    New-Item -ItemType Directory -Force -Path $managedDirectory | Out-Null
    $scriptPath = Join-Path $managedDirectory ("diskpart-{0}.txt" -f [Guid]::NewGuid().ToString('N'))
    try {
        # DiskPart is available on the self-hosted Windows runner even when the Hyper-V PowerShell module is absent.
        Set-Content -LiteralPath $scriptPath -Value @($Commands + 'exit') -Encoding ascii
        & diskpart.exe /s $scriptPath | Write-Host
        if ($LASTEXITCODE -ne 0) {
            throw "DiskPart failed with exit code $LASTEXITCODE."
        }
    }
    finally {
        Remove-Item -LiteralPath $scriptPath -Force -ErrorAction SilentlyContinue
    }
}

function Remove-ManagedUiTestVolumes {
    if (-not (Test-Path -LiteralPath $managedDirectory -PathType Container)) {
        return
    }

    foreach ($image in @(Get-ChildItem -LiteralPath $managedDirectory -Filter '*.vhdx' -File)) {
        # Select by the exact owned image path so cleanup cannot detach an unrelated runner disk.
        Invoke-ManagedDiskPart -Commands @(
            "select vdisk file=`"$($image.FullName)`"",
            'detach vdisk noerr'
        )
        Remove-Item -LiteralPath $image.FullName -Force -ErrorAction SilentlyContinue
    }

    # Only managed DiskPart scripts and images may exist here; keep broad runner directories out of deletion scope.
    Get-ChildItem -LiteralPath $managedDirectory -File -ErrorAction SilentlyContinue |
        Remove-Item -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $managedDirectory -Force -ErrorAction SilentlyContinue
}

if ($Action -eq 'Cleanup') {
    Remove-ManagedUiTestVolumes
    return
}

if ([string]::IsNullOrWhiteSpace($EnvironmentFile)) {
    throw 'EnvironmentFile is required for Setup so later workflow steps receive the mounted roots.'
}

# A prior cancelled job may leave only images owned by this exact runner-temp leaf; clear those before reusing drive letters.
Remove-ManagedUiTestVolumes
New-Item -ItemType Directory -Force -Path $managedDirectory | Out-Null

$usedLetters = @(Get-Volume | Where-Object DriveLetter | ForEach-Object { $_.DriveLetter.ToString().ToUpperInvariant() })
$freeLetters = @('V', 'W', 'X', 'Y', 'Z') | Where-Object { $_ -notin $usedLetters } | Select-Object -First 3
if ($freeLetters.Count -ne 3) {
    throw 'Three free drive letters from V: through Z: are required for isolated UI test volumes.'
}

$volumes = @(
    [pscustomobject]@{ Name = 'primary'; FileSystem = 'NTFS'; Letter = $freeLetters[0] },
    [pscustomobject]@{ Name = 'cross'; FileSystem = 'NTFS'; Letter = $freeLetters[1] },
    [pscustomobject]@{ Name = 'no-ads'; FileSystem = 'exFAT'; Letter = $freeLetters[2] }
)

try {
    foreach ($volume in $volumes) {
        $imagePath = Join-Path $managedDirectory "$($volume.Name).vhdx"
        # Fresh fixed-disk VHDX volumes provide deterministic recycle-bin, cross-volume, and ADS capability boundaries.
        Invoke-ManagedDiskPart -Commands @(
            "create vdisk file=`"$imagePath`" maximum=512 type=expandable",
            "select vdisk file=`"$imagePath`"",
            'attach vdisk',
            'create partition primary',
            "format fs=$($volume.FileSystem) quick label=FMUI_$($volume.Name)",
            "assign letter=$($volume.Letter)"
        )

        $mounted = Get-Volume -DriveLetter $volume.Letter -ErrorAction Stop
        if ($mounted.FileSystem -ine $volume.FileSystem) {
            throw "Drive $($volume.Letter): mounted as $($mounted.FileSystem), expected $($volume.FileSystem)."
        }
        $testDataRoot = "$($volume.Letter):\filemanager-testdata"
        New-Item -ItemType Directory -Force -Path $testDataRoot | Out-Null
        # The marker grants only this newly created root to the reparse-safe NUnit cleanup routine.
        [IO.File]::WriteAllText((Join-Path $testDataRoot $ownershipMarkerName), $ownershipMarkerContents, [Text.Encoding]::ASCII)
    }

    # GITHUB_ENV is the supported boundary for passing dynamic drive assignments to later Actions steps.
    @(
        "FILEMANAGER_UI_TESTDATA_ROOT=$($volumes[0].Letter):\filemanager-testdata",
        "FILEMANAGER_UI_CROSS_VOLUME_ROOT=$($volumes[1].Letter):\filemanager-testdata",
        "FILEMANAGER_UI_ADS_UNSUPPORTED_TARGET_ROOT=$($volumes[2].Letter):\filemanager-testdata"
    ) | Out-File -LiteralPath $EnvironmentFile -Encoding utf8 -Append
}
catch {
    Remove-ManagedUiTestVolumes
    throw
}
