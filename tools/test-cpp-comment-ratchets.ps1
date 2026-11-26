[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$toolsRoot = $PSScriptRoot
$powershell = (Get-Command powershell.exe -ErrorAction Stop).Source
$temporaryRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath())
$testRepository = Join-Path $temporaryRoot ('filemanager-comment-ratchet-' + [Guid]::NewGuid().ToString('N'))

function Invoke-TestGit {
    param([Parameter(ValueFromRemainingArguments = $true)][string[]]$Arguments)
    & git @Arguments | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "Test Git command failed: $($Arguments -join ' ')" }
}

function Assert-VerifierResult {
    param([string]$Verifier, [bool]$ShouldPass, [string]$Diagnostic = '')
    # Intentional negative fixtures must not terminate the Windows PowerShell harness.
    $previousPreference = $ErrorActionPreference
    try {
        $ErrorActionPreference = 'Continue'
        $output = & $powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $toolsRoot $Verifier) -BaseCommit $baseline 2>&1 | Out-String
        $exitCode = $LASTEXITCODE
    }
    finally { $ErrorActionPreference = $previousPreference }
    if (($exitCode -eq 0) -ne $ShouldPass -or ($Diagnostic -ne '' -and $output -notmatch $Diagnostic)) {
        throw "$Verifier returned $exitCode (expected pass=$ShouldPass):`n$output"
    }
}

function Set-Fixture {
    param([string[]]$Lines)
    Set-Content -LiteralPath 'src/legacy.cpp' -Encoding utf8 -Value $Lines
    Invoke-TestGit add src
    Invoke-TestGit commit --allow-empty -m 'Ratchet fixture'
}

try {
    New-Item -ItemType Directory -Path $testRepository | Out-Null
    Push-Location $testRepository
    try {
        Invoke-TestGit init --initial-branch=main
        Invoke-TestGit config user.name 'FileManager verifier test'
        Invoke-TestGit config user.email 'verifier-test@invalid.example'
        Invoke-TestGit config core.autocrlf false
        New-Item -ItemType Directory -Path src | Out-Null
        $tick = 'DWORD started = GetTickCount(); // original'
        # Literal comment markers, escaped quotes, and character literals must stay executable text.
        $path = 'const char* url = "https://example.test/\"quoted\""; char slash = ''/''; char path[MAX_PATH]; // original'
        Set-Fixture @($tick, $path)
        $baseline = (& git rev-parse HEAD).Trim()

        Set-Fixture @($tick.Replace('// original', '// translated'), $path.Replace('// original', '// translated'))
        Assert-VerifierResult 'verify-no-new-gettickcount.ps1' $true
        Assert-VerifierResult 'verify-no-new-max-path-buffers.ps1' $true

        # A name appearing only in a trailing comment is not an executable clock call.
        Set-Fixture @($tick, $path, 'DWORD idle = 0; // GetTickCount() documentation')
        Assert-VerifierResult 'verify-no-new-gettickcount.ps1' $true

        Set-Fixture @($tick.Replace('GetTickCount()', 'GetTickCount() + 1'), $path)
        Assert-VerifierResult 'verify-no-new-gettickcount.ps1' $false 'src/legacy\.cpp:.*GetTickCount'
        Set-Fixture @($tick, $path.Replace('[MAX_PATH]', '[MAX_PATH + 1]'))
        Assert-VerifierResult 'verify-no-new-max-path-buffers.ps1' $false 'src/legacy\.cpp:.*MAX_PATH'

        # Each deleted line can excuse only one replacement, even with different comment text.
        $translatedTick = $tick.Replace('// original', '// translated')
        $translatedPath = $path.Replace('// original', '// translated')
        Set-Fixture @($translatedTick, $translatedTick, $translatedPath, $translatedPath)
        Assert-VerifierResult 'verify-no-new-gettickcount.ps1' $false 'src/legacy\.cpp:'
        Assert-VerifierResult 'verify-no-new-max-path-buffers.ps1' $false 'src/legacy\.cpp:'

        # A literal change after https:// must not be mistaken for a comment translation.
        Set-Fixture @($tick, $path.Replace('example.test', 'different.test'))
        Assert-VerifierResult 'verify-no-new-max-path-buffers.ps1' $false 'src/legacy\.cpp:'

        # Raw strings and block comments require lexical state: compare these
        # lines conservatively rather than erasing executable text after //.
        . (Join-Path $toolsRoot 'cpp-ratchet-diff.ps1')
        foreach ($complexLine in @(
            'const char* text = R"(a " // text)"; char path[MAX_PATH];',
            '/* // text */ char path[MAX_PATH];'
        )) {
            if ((Remove-CppTrailingComment $complexLine) -cne $complexLine) {
                throw "Complex C++ syntax was truncated: $complexLine"
            }
        }

        # Removals in another file must not excuse newly added code here.
        Set-Content -LiteralPath 'src/other.cpp' -Encoding utf8 -Value @($translatedTick, $translatedPath)
        Set-Fixture @('void LegacyRemoved() {}')
        Assert-VerifierResult 'verify-no-new-gettickcount.ps1' $false 'src/other\.cpp:'
        Assert-VerifierResult 'verify-no-new-max-path-buffers.ps1' $false 'src/other\.cpp:'
    }
    finally { Pop-Location }
}
finally {
    # Only the unique fixture directory under the system temp root can be deleted.
    $resolvedTestRepository = [IO.Path]::GetFullPath($testRepository)
    if ($resolvedTestRepository.StartsWith($temporaryRoot, [StringComparison]::OrdinalIgnoreCase) -and
        [IO.Path]::GetFileName($resolvedTestRepository).StartsWith('filemanager-comment-ratchet-', [StringComparison]::Ordinal)) {
        Remove-Item -LiteralPath $resolvedTestRepository -Recurse -Force -ErrorAction SilentlyContinue
    }
}
$global:LASTEXITCODE = 0
Write-Host 'C++ comment ratchet regression cases passed.'
