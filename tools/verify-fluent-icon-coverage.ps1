param()

$ErrorActionPreference = 'Stop'

# Guard the modern icon migration against silent bitmap/shell fallbacks and accidental app-icon edits.
$repoRoot = Split-Path -Parent $PSScriptRoot
$toolbarDefinitionsPath = Join-Path $repoRoot 'src\toolbar_button_defs.cpp'
$toolbarDirectory = Join-Path $repoRoot 'src\res\toolbars'
$errors = [System.Collections.Generic.List[string]]::new()

function Get-Sha256Hex {
    param([Parameter(Mandatory = $true)][string]$Path)

    # Use the BCL directly because minimal Windows PowerShell hosts can omit the Get-FileHash cmdlet.
    $stream = [System.IO.File]::OpenRead($Path)
    try {
        $bytes = [System.Security.Cryptography.SHA256]::Create().ComputeHash($stream)
        return (($bytes | ForEach-Object { $_.ToString('X2') }) -join '')
    }
    finally {
        $stream.Dispose()
    }
}

$definitionText = [System.IO.File]::ReadAllText($toolbarDefinitionsPath)
$mappedNames = [regex]::Matches(
    $definitionText,
    '(?m)^\s*(?:/\*.*?\*/\s*)?\{[^\r\n]*IDX_TB_[^\r\n]*"([A-Za-z0-9]+)"\s*\},?\s*$') |
    ForEach-Object { $_.Groups[1].Value }
$mappedNames += 'Focus', 'Stop'
# Drive-bar mappings include built-in locations and stable bundled plug-in module names.
$driveDefinitions = [System.IO.File]::ReadAllText((Join-Path $repoRoot 'src\drivelst.cpp'))
$driveMappings = [regex]::Match($driveDefinitions, '(?s)static const char\* GetDriveBarSVGName\(.*?\n}\r?\n').Value
if (-not $driveMappings) { $errors.Add('Drive-bar SVG mapping was not found.') }
$mappedNames += [regex]::Matches($driveMappings, '"([A-Za-z0-9]+)"') |
    ForEach-Object { $_.Groups[1].Value }
# Both plug-in surfaces share this table; guard every bundled identity against missing assets.
$svgSource = [System.IO.File]::ReadAllText((Join-Path $repoRoot 'src\svg.cpp'))
$pluginMappings = [regex]::Match($svgSource, '(?s)const char\* GetPluginSVGName\(.*?\n}\r?\n').Value
if (-not $pluginMappings) { $errors.Add('Shared plug-in SVG mapping was not found.') }
$mappedNames += [regex]::Matches($pluginMappings, '"([A-Za-z0-9]+)"') |
    ForEach-Object { $_.Groups[1].Value }
$mappedNames = $mappedNames | Sort-Object -Unique

foreach ($name in $mappedNames)
{
    $path = Join-Path $toolbarDirectory ($name + '.svg')
    if (-not (Test-Path -LiteralPath $path))
    {
        $errors.Add("Mapped toolbar icon is missing: $name.svg")
    }
}

$shellBackedRows = [regex]::Matches(
    $definitionText,
    '(?m)^\s*(?:/\*.*?\*/\s*)?\{(?:NIB1\(IDX_TB_[^)]+\)|IDX_TB_[^,]+),\s*([0-9]+),') |
    Where-Object { $_.Groups[1].Value -ne '0' }
foreach ($row in $shellBackedRows)
{
    $errors.Add("Core toolbar row still depends on a non-zero shell resource: $($row.Value.Trim())")
}

$allowedColors = @(
    '#0F6CBD', '#115EA3', '#DDEBF7',
    '#424242', '#616161', '#F3F2F1',
    '#D89B00', '#FFD666', '#E1C699', '#D2B48C', '#C19A6B', '#8E562E',
    '#107C10', '#C50F1F', '#F7630C', '#FFFFFF'
)

# Every optical directory must contain the full family; partial deployment would silently stretch small artwork.
$masterNames = @(Get-ChildItem -LiteralPath $toolbarDirectory -Filter '*.svg' | ForEach-Object Name | Sort-Object)
foreach ($size in @(24, 32))
{
    $variantNames = @(Get-ChildItem -LiteralPath (Join-Path $toolbarDirectory $size) -Filter '*.svg' | ForEach-Object Name | Sort-Object)
    if (Compare-Object $masterNames $variantNames) { $errors.Add("Incomplete $size px optical family.") }
}
foreach ($file in Get-ChildItem -LiteralPath $toolbarDirectory -Filter '*.svg' -Recurse)
{
    $content = [System.IO.File]::ReadAllText($file.FullName)
    if (-not $content.StartsWith('<!-- Fluent System Icons:'))
    {
        $errors.Add("Missing design-intent comment: $($file.Name)")
    }
    $size = if ($file.Directory.Name -in @('24', '32')) { [int]$file.Directory.Name } else { 16 }
    if ($content -notmatch "<svg\s+width=`"$size`"\s+height=`"$size`"\s+viewBox=`"0 0 $size $size`"")
    {
        $errors.Add("Toolbar icon does not match its $size px optical grid: $($file.FullName)")
    }

    # Fluent regular icons use one solid foreground; external images, text, gradients, and currentColor are unsupported here.
    [xml]$parsed = $content
    if ($content -match '<(image|text|filter|mask|clipPath|linearGradient|radialGradient)\b|currentColor')
        { $errors.Add("Unsupported SVG feature: $($file.FullName)") }

    $colors = [regex]::Matches($content, '#[0-9A-Fa-f]{6}') | ForEach-Object Value | Sort-Object -Unique
    if (@($colors).Count -ne 1) { $errors.Add("Expected one solid foreground color: $($file.FullName)") }
    foreach ($color in $colors)
    {
        if ($color.ToUpperInvariant() -notin ($allowedColors | ForEach-Object { $_.ToUpperInvariant() }))
        {
            $errors.Add("Unexpected palette color $color in $($file.Name)")
        }
    }
}

# The imported paths retain their upstream MIT notice when shipped beside the executable.
if (-not (Test-Path (Join-Path $toolbarDirectory 'LICENSE-fluent.txt'))) { $errors.Add('Missing Fluent MIT license.') }

$protectedAppIcons = @{
    'salamand.ico' = 'BBBC4E66BC304E4FD539D122D5515326B2CDCE6FD089B74CC4C679E4FB1543C1'
    'sal_r.ico' = '9DE6AF5D00BBAEA5729CFFC17517C0CFE42142C5871657CA189AD8BA5CA28B2E'
    'sal_g.ico' = '15073BE8CEEFFAA1D13D7E0DC3D5FF093E955F39F562F3AEB75DA01FC2A4BF9F'
    'sal_b.ico' = 'E30044D9DC474F59B4EDB79F4528372869A9D95B8F576CD10C2B571AE21C22E1'
}
foreach ($entry in $protectedAppIcons.GetEnumerator())
{
    $path = Join-Path $repoRoot ('src\res\' + $entry.Key)
    $actual = Get-Sha256Hex $path
    if ($actual -ne $entry.Value)
    {
        $errors.Add("Protected main application icon changed: $($entry.Key)")
    }
}

if ($errors.Count -gt 0)
{
    $errors | ForEach-Object { Write-Error $_ }
    exit 1
}

Write-Host "Fluent icon coverage verified: $($mappedNames.Count) mapped command assets and $((Get-ChildItem -LiteralPath $toolbarDirectory -Filter '*.svg').Count) SVG files."
