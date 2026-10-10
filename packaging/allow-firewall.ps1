param([switch]$Remove)
$ErrorActionPreference = 'Stop'
$ruleName = 'HomePodTray-Installed-UDP'
$exe = Join-Path $PSScriptRoot 'HomePodTray.exe'
if ($Remove) {
    Get-NetFirewallRule -Name $ruleName -ErrorAction SilentlyContinue | Remove-NetFirewallRule
} else {
    if (-not (Test-Path -LiteralPath $exe)) { throw 'HomePodTray.exe is missing.' }
    Get-NetFirewallRule -Name $ruleName -ErrorAction SilentlyContinue | Remove-NetFirewallRule
    New-NetFirewallRule -Name $ruleName -DisplayName 'HomePodTray - HomePod UDP in' -Direction Inbound -Action Allow -Program $exe -Protocol UDP -RemoteAddress LocalSubnet -Profile Any | Out-Null
}
