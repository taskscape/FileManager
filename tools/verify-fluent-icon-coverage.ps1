param()

$ErrorActionPreference = 'Stop'

# Guard the modern icon migration against silent bitmap/shell fallbacks and accidental app-icon edits.
$repoRoot = Split-Path -Parent $PSScriptRoot
$toolbarDefinitionsPath = Join-Path $repoRoot 'src\toolbar_button_defs.cpp'
$toolbarDirectory = Join-Path $repoRoot 'src\res\toolbars'
$errors = [System.Collections.Generic.List[string]]::new()

$definitionText = [System.IO.File]::ReadAllText($toolbarDefinitionsPath)
$mappedNames = [regex]::Matches(
    $definitionText,
    '(?m)^\s*(?:/\*.*?\*/\s*)?\{[^\r\n]*IDX_TB_[^\r\n]*"([A-Za-z0-9]+)"\s*\},?\s*$') |
    ForEach-Object { $_.Groups[1].Value }
$mappedNames += 'Focus', 'Stop'
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

foreach ($file in Get-ChildItem -LiteralPath $toolbarDirectory -Filter '*.svg')
{
    $content = [System.IO.File]::ReadAllText($file.FullName)
    if (-not $content.StartsWith('<!-- Fluent Color Essentials:'))
    {
        $errors.Add("Missing design-intent comment: $($file.Name)")
    }
    if ($content -notmatch '<svg\s+width="16"\s+height="16"\s+viewBox="0 0 16 16"')
    {
        $errors.Add("Toolbar icon is not authored on the 16 px master grid: $($file.Name)")
    }

    $colors = [regex]::Matches($content, '#[0-9A-Fa-f]{6}') | ForEach-Object Value | Sort-Object -Unique
    foreach ($color in $colors)
    {
        if ($color.ToUpperInvariant() -notin ($allowedColors | ForEach-Object { $_.ToUpperInvariant() }))
        {
            $errors.Add("Unexpected palette color $color in $($file.Name)")
        }
    }
}

$protectedAppIcons = @{
    'salamand.ico' = 'BBBC4E66BC304E4FD539D122D5515326B2CDCE6FD089B74CC4C679E4FB1543C1'
    'sal_r.ico' = '9DE6AF5D00BBAEA5729CFFC17517C0CFE42142C5871657CA189AD8BA5CA28B2E'
    'sal_g.ico' = '15073BE8CEEFFAA1D13D7E0DC3D5FF093E955F39F562F3AEB75DA01FC2A4BF9F'
    'sal_b.ico' = 'E30044D9DC474F59B4EDB79F4528372869A9D95B8F576CD10C2B571AE21C22E1'
}
foreach ($entry in $protectedAppIcons.GetEnumerator())
{
    $path = Join-Path $repoRoot ('src\res\' + $entry.Key)
    $actual = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash
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
