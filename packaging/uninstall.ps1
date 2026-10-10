param([switch]$TestMode)
$ErrorActionPreference = 'Stop'
$installDir = [IO.Path]::GetFullPath($PSScriptRoot)
$manifest = Get-Content -LiteralPath (Join-Path $installDir 'installation.json') -Raw | ConvertFrom-Json
$expected = Join-Path $env:LOCALAPPDATA 'Programs\HomePodTray'
if ($manifest.TestMode) {
    if (-not $TestMode) { throw 'Test uninstallation requires -TestMode.' }
    $prefix = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\') + '\'
    if (-not $installDir.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) { throw 'Test path is outside temporary directory.' }
} elseif ($installDir -ne [IO.Path]::GetFullPath($expected)) { throw 'Unexpected installation path.' }
if ($installDir -ne [IO.Path]::GetFullPath($manifest.InstallDir)) { throw 'Installation manifest path mismatch.' }
$exe = Join-Path $installDir 'HomePodTray.exe'
if (-not $manifest.TestMode) {
    Add-Type -AssemblyName System.Windows.Forms
    if ([Windows.Forms.MessageBox]::Show('卸载 HomePodTray？将保留用户配置、日志和 VB-Cable 驱动。','HomePodTray 卸载','YesNo','Question') -ne 'Yes') { exit 0 }
}
Get-CimInstance Win32_Process -Filter "Name='HomePodTray.exe'" | Where-Object ExecutablePath -eq $exe | ForEach-Object { Stop-Process -Id $_.ProcessId -Force }
$shell = New-Object -ComObject WScript.Shell
foreach ($path in $manifest.Links) {
    if (Test-Path -LiteralPath $path) {
        if ($shell.CreateShortcut($path).TargetPath -eq $exe) { Remove-Item -LiteralPath $path -Force }
    }
}
if (-not $manifest.TestMode) {
    try {
        if (Get-NetFirewallRule -Name 'HomePodTray-Installed-UDP' -ErrorAction SilentlyContinue) {
            $argsText = '-NoProfile -ExecutionPolicy Bypass -File "' + (Join-Path $installDir 'allow-firewall.ps1') + '" -Remove'
            $p = Start-Process powershell.exe -WindowStyle Hidden -Verb RunAs -ArgumentList $argsText -Wait -PassThru
            if ($p.ExitCode -ne 0) { throw 'Firewall removal failed.' }
        }
    } catch { [Windows.Forms.MessageBox]::Show('防火墙规则未能移除，请在 Windows 防火墙中删除 HomePodTray - HomePod UDP in。','HomePodTray') | Out-Null }
    $runKey = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run'
    $run = (Get-ItemProperty -LiteralPath $runKey -Name HomePodTrayRebuild -ErrorAction SilentlyContinue).HomePodTrayRebuild
    if ($run -and $run.Contains($exe)) { Remove-ItemProperty -LiteralPath $runKey -Name HomePodTrayRebuild }
}
$expectedKey = if ($manifest.TestMode) {'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\HomePodTray-PackageTest'} else {'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\HomePodTray'}
if ($manifest.UninstallKey -ne $expectedKey) { throw 'Unexpected uninstall registry key.' }
if (Test-Path -LiteralPath $expectedKey) { Remove-Item -LiteralPath $expectedKey -Recurse -Force }
# Validated installation directory only, using one PowerShell process end-to-end.
Remove-Item -LiteralPath $installDir -Recurse -Force
Write-Output 'HomePodTray uninstalled; user configuration and VB-Cable retained.'
