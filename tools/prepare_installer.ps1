[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$BuildDir,
    [Parameter(Mandatory = $true)]
    [string]$StagingDir,
    [Parameter(Mandatory = $true)]
    [string]$BuildNumber
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repositoryRoot = Split-Path -Parent $PSScriptRoot
$buildRoot = [IO.Path]::GetFullPath($BuildDir)
$releaseRoot = Join-Path $buildRoot 'salamander\Release_x64'
$stagingRoot = [IO.Path]::GetFullPath($StagingDir)

if (-not (Test-Path -LiteralPath $releaseRoot -PathType Container)) {
    throw "The Release x64 artifact root is missing: $releaseRoot"
}

function Copy-ReleaseArtifact {
    param(
        [Parameter(Mandatory = $true)]
        [string]$RelativePath,
        [Parameter(Mandatory = $true)]
        [string]$DestinationDirectory,
        [switch]$Optional
    )

    $source = Join-Path $releaseRoot $RelativePath
    if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
        if ($Optional) {
            return $false
        }

        throw "The current Release x64 build did not produce required installer artifact: $source"
    }

    Copy-Item -LiteralPath $source -Destination $DestinationDirectory -Force
    Write-Host "Staged $RelativePath from the current Release build." -ForegroundColor Green
    return $true
}

function Copy-StandaloneReleaseArtifact {
    param(
        [Parameter(Mandatory = $true)]
        [string]$RelativePath,
        [Parameter(Mandatory = $true)]
        [string]$DestinationDirectory,
        [switch]$Optional
    )

    $source = Join-Path $buildRoot $RelativePath
    if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
        if ($Optional) {
            return $false
        }

        throw "The current Release build did not produce required installer artifact: $source"
    }

    Copy-Item -LiteralPath $source -Destination $DestinationDirectory -Force
    Write-Host "Staged $RelativePath from the current Release build." -ForegroundColor Green
    return $true
}

Write-Host '=== Open Salamander Installer Staging Script ===' -ForegroundColor Cyan
Write-Host "Build Number: $BuildNumber" -ForegroundColor Cyan

if (Test-Path -LiteralPath $stagingRoot) {
    # A stale shared staging tree can contain a running executable, so callers use a per-run directory and this removes only that owned tree.
    Remove-Item -LiteralPath $stagingRoot -Recurse -Force -ErrorAction Stop
}

New-Item -ItemType Directory -Path $stagingRoot -Force | Out-Null
$pluginsDirectory = Join-Path $stagingRoot 'plugins'
$languageDirectory = Join-Path $stagingRoot 'lang'
$convertDirectory = Join-Path $stagingRoot 'convert'
$toolbarsDirectory = Join-Path $stagingRoot 'toolbars'
$utilsDirectory = Join-Path $stagingRoot 'utils'
New-Item -ItemType Directory -Path $pluginsDirectory, $languageDirectory, $convertDirectory, $toolbarsDirectory, $utilsDirectory -Force | Out-Null

# The build metadata identifies the exact artifact tree that was copied, rather than a stale executable found under src.
$buildDateUtc = (Get-Date).ToUniversalTime().ToString('yyyy-MM-dd HH:mm:ss UTC')
@"
Build Number: $BuildNumber
Build Date: $buildDateUtc
"@ | Out-File -FilePath (Join-Path $stagingRoot 'build_info.txt') -Encoding utf8

Copy-Item -LiteralPath (Join-Path $repositoryRoot 'Installer\LICENSE') -Destination $stagingRoot -Force

Copy-ReleaseArtifact -RelativePath 'salamand.exe' -DestinationDirectory $stagingRoot
Copy-ReleaseArtifact -RelativePath 'utils\salmon.exe' -DestinationDirectory $stagingRoot
Copy-ReleaseArtifact -RelativePath 'salbroker.exe' -DestinationDirectory $stagingRoot
Copy-ReleaseArtifact -RelativePath 'utils\salextx64.dll' -DestinationDirectory $utilsDirectory
Copy-ReleaseArtifact -RelativePath 'utils\salextx86.dll' -DestinationDirectory $utilsDirectory

# Optional utilities vary by configuration, but they must come from this build whenever they are present.
Copy-ReleaseArtifact -RelativePath 'utils\salopen.exe' -DestinationDirectory $stagingRoot -Optional | Out-Null
Copy-ReleaseArtifact -RelativePath 'utils\salspawn.exe' -DestinationDirectory $stagingRoot -Optional | Out-Null
Copy-StandaloneReleaseArtifact -RelativePath 'tserver\Release\tserver.exe' -DestinationDirectory $stagingRoot -Optional | Out-Null
Copy-StandaloneReleaseArtifact -RelativePath 'sfx7zip\Release\sfx7zip.exe' -DestinationDirectory $stagingRoot -Optional | Out-Null
Copy-ReleaseArtifact -RelativePath 'plugins\zip\zip2sfx\zip2sfx.exe' -DestinationDirectory $stagingRoot -Optional | Out-Null
Copy-StandaloneReleaseArtifact -RelativePath 'translator\Release\translator.exe' -DestinationDirectory $stagingRoot -Optional | Out-Null
Copy-ReleaseArtifact -RelativePath 'plugins\filecomp\fcremote.exe' -DestinationDirectory $stagingRoot -Optional | Out-Null

$languageFiles = @(Get-ChildItem -LiteralPath (Join-Path $releaseRoot 'lang') -Filter '*.slg' -File)
if ($languageFiles.Count -eq 0) {
    throw "The current Release x64 build did not produce main-language files below $releaseRoot."
}
$languageFiles | ForEach-Object { Copy-Item -LiteralPath $_.FullName -Destination $languageDirectory -Force }

Copy-Item -Path (Join-Path $repositoryRoot 'convert\*') -Destination $convertDirectory -Recurse -Force
Copy-Item -Path (Join-Path $releaseRoot 'toolbars\*') -Destination $toolbarsDirectory -Recurse -Force

$pluginSourceDirectory = Join-Path $releaseRoot 'plugins'
# DemoPlug is an SDK sample kept in the solution for local builds; never stage it for official distribution.
$pluginPayloads = @(Get-ChildItem -LiteralPath $pluginSourceDirectory -Recurse -File |
    Where-Object {
        $_.FullName -notmatch '\\Intermediate\\' -and
        $_.FullName -notmatch '\\demoplug\\' -and
        $_.Extension -in @('.dll', '.exe', '.slg', '.spl')
    })
if (@($pluginPayloads | Where-Object { $_.Extension -eq '.spl' }).Count -eq 0) {
    throw "The current Release x64 build did not produce plug-ins below $pluginSourceDirectory."
}

foreach ($pluginPayload in $pluginPayloads) {
    $relativePath = $pluginPayload.FullName.Substring($pluginSourceDirectory.Length).TrimStart('\')
    $destination = Join-Path $pluginsDirectory $relativePath
    $destinationDirectory = Split-Path -Parent $destination
    New-Item -ItemType Directory -Path $destinationDirectory -Force | Out-Null
    Copy-Item -LiteralPath $pluginPayload.FullName -Destination $destination -Force
}

# Each installer gets a monotonic manifest so an existing 6.0 profile notices the plug-ins bundled by this build.
$pluginManifestVersion = [DateTimeOffset]::UtcNow.ToUnixTimeSeconds()
if ($pluginManifestVersion -gt [int]::MaxValue) {
    throw "The generated plug-in manifest version exceeds the legacy plug-in loader range: $pluginManifestVersion"
}
$pluginManifestLines = @("${pluginManifestVersion}:")
$pluginManifestLines += $pluginPayloads |
    Where-Object { $_.Extension -eq '.spl' } |
    ForEach-Object {
        $relativePath = $_.FullName.Substring($pluginSourceDirectory.Length).TrimStart('\')
        "${pluginManifestVersion}:$relativePath"
    } |
    Sort-Object
[IO.File]::WriteAllLines((Join-Path $pluginsDirectory 'plugins.ver'), [string[]]$pluginManifestLines,
    [Text.UTF8Encoding]::new($false))

# These plug-ins load sibling binaries at runtime, so verify the generic payload copy did not regress to staging only .spl files.
$requiredPluginPayloads = @(
    '7zip\7za.dll',
    '7zip\7zwrapper.dll',
    'pictview\exif.dll',
    'unchm\chmlib.dll'
)
$missingPluginPayloads = @($requiredPluginPayloads |
    Where-Object { -not (Test-Path -LiteralPath (Join-Path $pluginsDirectory $_) -PathType Leaf) })
if ($missingPluginPayloads.Count -ne 0) {
    throw "The current Release plug-in staging tree is incomplete: $($missingPluginPayloads -join ', ')"
}

# Verify the input hand-off before Inno runs so a packaging failure names the missing current-build artifact.
$requiredStagedFiles = @('salamand.exe', 'salmon.exe', 'salbroker.exe', 'LICENSE', 'plugins\plugins.ver') |
    ForEach-Object { Join-Path $stagingRoot $_ }
$missingStagedFiles = @($requiredStagedFiles | Where-Object { -not (Test-Path -LiteralPath $_ -PathType Leaf) })
if ($missingStagedFiles.Count -ne 0) {
    throw "The installer staging tree is incomplete: $($missingStagedFiles -join ', ')"
}

Write-Host "Staged $($pluginPayloads.Count) current-build plug-in payload files." -ForegroundColor Green
Write-Host "Files staged in: $stagingRoot" -ForegroundColor Cyan
