[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$verifier = Join-Path $PSScriptRoot 'verify-no-new-raw-thread-creation.ps1'
$pwsh = (Get-Command pwsh.exe -ErrorAction Stop).Source
$temporaryRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath())
$testRepository = Join-Path $temporaryRoot ("filemanager-thread-ratchet-{0}" -f [Guid]::NewGuid().ToString('N'))

function Invoke-TestGit {
    param([Parameter(ValueFromRemainingArguments = $true)][string[]]$Arguments)

    & git @Arguments | Out-Null
    if ($LASTEXITCODE -ne 0) {
        throw "Test repository git command failed: git $($Arguments -join ' ')"
    }
}

function Invoke-Verifier {
    param([Parameter(Mandatory = $true)][string]$BaseCommit)

    # Windows PowerShell promotes a child pwsh stderr record to NativeCommandError;
    # capture the verifier's intentional failing case without aborting this harness.
    $previousErrorActionPreference = $ErrorActionPreference
    try {
        $ErrorActionPreference = 'Continue'
        $output = & $pwsh -NoProfile -ExecutionPolicy Bypass -File $verifier -BaseCommit $BaseCommit 2>&1 | Out-String
        $exitCode = $LASTEXITCODE
    }
    finally {
        $ErrorActionPreference = $previousErrorActionPreference
    }
    return [pscustomobject]@{ ExitCode = $exitCode; Output = $output }
}

try {
    New-Item -ItemType Directory -Path $testRepository | Out-Null
    Push-Location $testRepository
    try {
        Invoke-TestGit init --initial-branch=main
        Invoke-TestGit config user.name 'FileManager verifier test'
        Invoke-TestGit config user.email 'verifier-test@invalid.example'
        Invoke-TestGit config core.autocrlf false

        New-Item -ItemType Directory -Path 'src' | Out-Null
        Set-Content -LiteralPath 'src/baseline.cpp' -Encoding utf8 -Value 'void Baseline() {}'
        Invoke-TestGit add src/baseline.cpp
        Invoke-TestGit commit -m 'Create verifier baseline'
        $baseline = (& git rev-parse HEAD).Trim()

        # Upstream 7-Zip owns these calls through its portability layer, so method
        # names, comments, and raw platform primitives in the pinned subtree are exempt.
        New-Item -ItemType Directory -Path 'src/plugins/7zip/7za/cpp' -Force | Out-Null
        Set-Content -LiteralPath 'src/plugins/7zip/7za/cpp/vendor.cpp' -Encoding utf8 -Value @'
/* CreateThread(NULL, 0, VendorThread, NULL, 0, NULL); */
HRESULT CDecoder::CreateThread() { return Thread.Create(VendorThread, this); }
void StartVendorThread() { _beginthreadex(NULL, 0, VendorThread, NULL, 0, NULL); }
'@
        Invoke-TestGit add src/plugins/7zip/7za/cpp/vendor.cpp
        Invoke-TestGit commit -m 'Add pinned vendor thread layer'
        $vendorResult = Invoke-Verifier -BaseCommit $baseline
        if ($vendorResult.ExitCode -ne 0) {
            throw "Vendored 7-Zip source was not exempted:`n$($vendorResult.Output)"
        }

        $vendorCommit = (& git rev-parse HEAD).Trim()
        # Full-file comment and literal scanning must ignore textual API names even
        # when the added line sits inside a block comment that began earlier.
        Set-Content -LiteralPath 'src/first_party.cpp' -Encoding utf8 -Value @'
/*
CreateThread(NULL, 0, CommentOnly, NULL, 0, NULL);
*/
const char* ThreadApiName = "_beginthreadex(NULL, 0, LiteralOnly, NULL, 0, NULL)";
'@
        Invoke-TestGit add src/first_party.cpp
        Invoke-TestGit commit -m 'Add comment and literal fixtures'
        $commentResult = Invoke-Verifier -BaseCommit $vendorCommit
        if ($commentResult.ExitCode -ne 0) {
            throw "Comments or literals were mistaken for raw calls:`n$($commentResult.Output)"
        }

        $commentCommit = (& git rev-parse HEAD).Trim()
        Add-Content -LiteralPath 'src/first_party.cpp' -Encoding utf8 -Value @'
void StartFirstPartyThread(HANDLE* thread)
{
    *thread = CreateThread(NULL, 0, FirstPartyThread, NULL, 0, NULL);
}
'@
        Invoke-TestGit add src/first_party.cpp
        Invoke-TestGit commit -m 'Add prohibited first-party thread start'
        $failureResult = Invoke-Verifier -BaseCommit $commentCommit
        if ($failureResult.ExitCode -eq 0 -or
            $failureResult.Output -notmatch 'src/first_party\.cpp:\d+:' -or
            $failureResult.Output -notmatch '\*thread = CreateThread') {
            throw "Pointer-based raw creation did not produce an actionable diagnostic:`n$($failureResult.Output)"
        }
    }
    finally {
        Pop-Location
    }
}
finally {
    # Restrict recursive cleanup to the uniquely named directory created beneath
    # the operating-system temporary root for this test invocation.
    $resolvedTestRepository = [IO.Path]::GetFullPath($testRepository)
    if ($resolvedTestRepository.StartsWith($temporaryRoot, [StringComparison]::OrdinalIgnoreCase) -and
        [IO.Path]::GetFileName($resolvedTestRepository).StartsWith('filemanager-thread-ratchet-', [StringComparison]::Ordinal)) {
        Remove-Item -LiteralPath $resolvedTestRepository -Recurse -Force -ErrorAction SilentlyContinue
    }
}

# The expected negative fixture leaves the native-command status at one; clear it
# so callers observe the harness result rather than its deliberately failing child.
$global:LASTEXITCODE = 0
Write-Host 'Raw thread-creation verifier regression cases passed.'
