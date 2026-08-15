[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)] [string] $V143Results,
    [Parameter(Mandatory = $true)] [string] $V145Results
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Get-TestOutcomes {
    param([string] $Path)

    [xml]$trx = Get-Content -LiteralPath $Path -Raw
    $outcomes = @{}
    foreach ($result in @($trx.SelectNodes("//*[local-name()='UnitTestResult']"))) {
        if ([string]::IsNullOrWhiteSpace($result.testName) -or [string]::IsNullOrWhiteSpace($result.outcome)) {
            throw "TRX result has no test name or outcome: $Path"
        }
        if ($outcomes.ContainsKey($result.testName)) { throw "TRX contains duplicate result '$($result.testName)': $Path" }
        $outcomes[$result.testName] = $result.outcome
    }
    if ($outcomes.Count -eq 0) { throw "TRX contains no test results: $Path" }
    return $outcomes
}

$v143 = Get-TestOutcomes -Path $V143Results
$v145 = Get-TestOutcomes -Path $V145Results
$differences = @()
foreach ($name in @($v143.Keys + $v145.Keys | Sort-Object -Unique)) {
    if (-not $v143.ContainsKey($name) -or -not $v145.ContainsKey($name)) {
        $differences += "${name}: missing from one toolset"
    }
    elseif ($v143[$name] -ne $v145[$name]) {
        $differences += "${name}: v143=$($v143[$name]), v145=$($v145[$name])"
    }
}
if ($differences.Count -ne 0) {
    $differences | ForEach-Object { Write-Error $_ }
    throw 'v143/v145 test-result parity failed.'
}
Write-Host "v143/v145 test-result parity passed for $($v143.Count) tests."
