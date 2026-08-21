[CmdletBinding()]
param(
    [string]$ManifestPath = (Join-Path $PSScriptRoot '..\tests\FileManager.UiTests\quarantined-ui-tests.json')
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# The release suite filters only entries proven here, preventing a broad category from silently hiding an untracked test.
$resolvedManifestPath = [System.IO.Path]::GetFullPath($ManifestPath)
if (-not (Test-Path -LiteralPath $resolvedManifestPath -PathType Leaf)) {
    throw "The UI-test quarantine manifest was not found: $resolvedManifestPath"
}

$manifest = Get-Content -LiteralPath $resolvedManifestPath -Raw | ConvertFrom-Json
if ($manifest.schemaVersion -ne 1 -or $null -eq $manifest.tests) {
    throw 'The UI-test quarantine manifest must use schemaVersion 1 and contain a tests array.'
}

$repositoryRoot = Split-Path -Parent $PSScriptRoot
$today = [DateTime]::UtcNow.Date
$identities = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
foreach ($entry in @($manifest.tests)) {
    foreach ($property in @('fullyQualifiedName', 'sourceFile', 'testName', 'reason', 'owner', 'trackingReference', 'expiresOn')) {
        if ([string]::IsNullOrWhiteSpace([string]$entry.$property)) {
            throw "Quarantine entry is missing required property '$property'."
        }
    }

    if (-not $identities.Add($entry.fullyQualifiedName)) {
        throw "Quarantine entry is duplicated: $($entry.fullyQualifiedName)"
    }
    if ($entry.trackingReference -notmatch '^https://github\.com/taskscape/FileManager/(actions/runs|issues)/\d+$') {
        throw "Quarantine entry has an invalid tracking reference: $($entry.trackingReference)"
    }

    try {
        $expiry = [DateTime]::ParseExact($entry.expiresOn, 'yyyy-MM-dd', [Globalization.CultureInfo]::InvariantCulture, [Globalization.DateTimeStyles]::None).Date
    }
    catch {
        throw "Quarantine entry has an invalid expiresOn date: $($entry.expiresOn)"
    }
    if ($expiry -le $today) {
        throw "Quarantine entry expired on $($entry.expiresOn): $($entry.fullyQualifiedName)"
    }

    $sourcePath = Join-Path $repositoryRoot $entry.sourceFile
    if (-not (Test-Path -LiteralPath $sourcePath -PathType Leaf)) {
        throw "Quarantine entry references a missing source file: $($entry.sourceFile)"
    }
    $source = Get-Content -LiteralPath $sourcePath -Raw
    $methodPattern = '(?s)\[Test\](?<attributes>.*?)public\s+void\s+' + [regex]::Escape($entry.testName) + '\s*\('
    $matches = [regex]::Matches($source, $methodPattern)
    if ($matches.Count -ne 1) {
        throw "Quarantine entry must identify exactly one [Test] method: $($entry.fullyQualifiedName)"
    }
    $attributes = $matches[0].Groups['attributes'].Value
    # Quarantine is filterable coverage, not an NUnit Ignore, so the release runner can keep all unexpected skips fatal.
    if ($attributes -notmatch '\[Category\("Quarantined"\)\]') {
        throw ('Quarantine entry is missing [Category("Quarantined")]: {0}' -f $entry.fullyQualifiedName)
    }
    if ($attributes -match '\[Ignore(?:\(|\])') {
        throw "Quarantine entry must not use [Ignore]: $($entry.fullyQualifiedName)"
    }
    if (-not $entry.fullyQualifiedName.EndsWith(".$($entry.testName)", [StringComparison]::Ordinal)) {
        throw "Quarantine entry testName does not match its fullyQualifiedName: $($entry.fullyQualifiedName)"
    }

    $qualifiedSegments = $entry.fullyQualifiedName.Split('.')
    $declaringType = $qualifiedSegments[$qualifiedSegments.Length - 2]
    $declaringNamespace = ($qualifiedSegments[0..($qualifiedSegments.Length - 3)] -join '.')
    if ($source -notmatch ('namespace\s+' + [regex]::Escape($declaringNamespace) + '(?:;|\s)') -or
        $source -notmatch ('class\s+' + [regex]::Escape($declaringType) + '\b')) {
        throw "Quarantine entry fullyQualifiedName does not match its source declaration: $($entry.fullyQualifiedName)"
    }
}

# Every category excluded by the release filter must appear in the manifest; otherwise a new annotation could silently remove coverage.
$manifestLocations = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
foreach ($entry in @($manifest.tests)) {
    [void]$manifestLocations.Add((($entry.sourceFile -replace '\\', '/') + '::' + $entry.testName))
}
$categorizedLocations = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
Get-ChildItem -LiteralPath (Join-Path $repositoryRoot 'tests\FileManager.UiTests') -Filter '*.cs' -Recurse | ForEach-Object {
    $relativePath = [System.IO.Path]::GetRelativePath($repositoryRoot, $_.FullName) -replace '\\', '/'
    $source = Get-Content -LiteralPath $_.FullName -Raw
    foreach ($match in [regex]::Matches($source, '(?s)(?<attributes>(?:\s*\[[^\]]+\]\s*)+)public\s+void\s+(?<name>[A-Za-z_][A-Za-z0-9_]*)\s*\(')) {
        $attributes = $match.Groups['attributes'].Value
        if ($attributes -match '\[Test\]' -and $attributes -match '\[Category\("Quarantined"\)\]') {
            [void]$categorizedLocations.Add($relativePath + '::' + $match.Groups['name'].Value)
        }
    }
}
foreach ($location in $categorizedLocations) {
    if (-not $manifestLocations.Contains($location)) {
        throw ('[Category("Quarantined")] is not declared in quarantined-ui-tests.json: {0}' -f $location)
    }
}
foreach ($location in $manifestLocations) {
    if (-not $categorizedLocations.Contains($location)) {
        throw ('Quarantine manifest entry is not represented by [Category("Quarantined")]: {0}' -f $location)
    }
}

Write-Host "Validated $($identities.Count) UI-test quarantine entries; every entry has a source category, owner, tracking reference, and unexpired review date." -ForegroundColor Green
