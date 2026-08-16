# Changes the Windows administrator consent policy while preserving UAC itself;
# this keeps UAC virtualization and secure desktop protections available.
[CmdletBinding(SupportsShouldProcess)]
param(
    [switch] $Restore
)

$policyPath = 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Policies\System'
$valueName = 'ConsentPromptBehaviorAdmin'
$backupPath = Join-Path $PSScriptRoot 'Set-AdminElevationNoPrompt.backup.json'

if (-not ([Security.Principal.WindowsPrincipal] [Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole(
        [Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Run this script from an elevated PowerShell window. The policy is machine-wide and requires administrator rights.'
}

if ($Restore) {
    if (-not (Test-Path -LiteralPath $backupPath)) {
        throw "No backup was found at '$backupPath'."
    }

    $backup = Get-Content -LiteralPath $backupPath -Raw | ConvertFrom-Json
    if ($PSCmdlet.ShouldProcess($policyPath, "Restore $valueName to $($backup.Value)")) {
        Set-ItemProperty -LiteralPath $policyPath -Name $valueName -Value ([int]$backup.Value) -Type DWord
        Remove-Item -LiteralPath $backupPath
        Write-Host 'Previous administrator elevation behavior restored. Sign out and back in for the policy to take effect.'
    }
    exit
}

$current = Get-ItemProperty -LiteralPath $policyPath -Name $valueName -ErrorAction SilentlyContinue
if ($null -eq $current) {
    throw "The expected policy value '$valueName' was not found at '$policyPath'."
}

if (-not (Test-Path -LiteralPath $backupPath)) {
    [pscustomobject]@{
        Path  = $policyPath
        Name  = $valueName
        Value = [int]$current.$valueName
    } | ConvertTo-Json | Set-Content -LiteralPath $backupPath -Encoding UTF8
}

if ($PSCmdlet.ShouldProcess($policyPath, "Set $valueName to 0 (elevate administrators without a consent prompt)")) {
    Set-ItemProperty -LiteralPath $policyPath -Name $valueName -Value 0 -Type DWord
    Write-Host 'Administrator elevation prompts are disabled. Sign out and back in for the policy to take effect.'
    Write-Host "To restore the previous setting, run: $($MyInvocation.MyCommand.Name) -Restore"
}
