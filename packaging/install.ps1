param([string]$TestRoot)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Windows.Forms
$testMode = -not [string]::IsNullOrWhiteSpace($TestRoot)
if ($testMode) {
    $root = [IO.Path]::GetFullPath($TestRoot)
    $tempPrefix = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\') + '\'
    if (-not $root.StartsWith($tempPrefix, [StringComparison]::OrdinalIgnoreCase)) { throw 'TestRoot must be inside the temporary directory.' }
    $installDir = Join-Path $root 'Programs\HomePodTray'
    $desktop = Join-Path $root 'Desktop'
    $startMenu = Join-Path $root 'StartMenu'
    $uninstallKey = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\HomePodTray-PackageTest'
} else {
    $installDir = Join-Path $env:LOCALAPPDATA 'Programs\HomePodTray'
    $desktop = [Environment]::GetFolderPath('Desktop')
    $startMenu = Join-Path $env:APPDATA 'Microsoft\Windows\Start Menu\Programs'
    $uninstallKey = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\HomePodTray'
}
$exe = Join-Path $installDir 'HomePodTray.exe'
if (Get-CimInstance Win32_Process -Filter "Name='HomePodTray.exe'" | Where-Object ExecutablePath -eq $exe) { throw 'Close the installed HomePodTray before upgrading.' }
New-Item -ItemType Directory -Path $installDir,$desktop,$startMenu -Force | Out-Null
foreach ($name in @('HomePodTray.exe','uninstall.ps1','allow-firewall.ps1','LICENSE.txt','NOTICE.txt','THIRD_PARTY.txt')) {
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot $name) -Destination $installDir -Force
}
New-Item -ItemType Directory -Path (Join-Path $installDir 'licenses') -Force | Out-Null
Get-ChildItem -LiteralPath $PSScriptRoot -Filter '*.license.txt' -File | Copy-Item -Destination (Join-Path $installDir 'licenses') -Force
$links = @((Join-Path $desktop 'HomePodTray.lnk'), (Join-Path $startMenu 'HomePodTray.lnk'))
$shell = New-Object -ComObject WScript.Shell
foreach ($path in $links) {
    $link = $shell.CreateShortcut($path)
    $link.TargetPath = $exe
    $link.WorkingDirectory = $installDir
    $link.IconLocation = "$exe,0"
    $link.Description = 'HomePodTray'
    $link.Save()
}
@{InstallDir=$installDir; Links=$links; UninstallKey=$uninstallKey; TestMode=$testMode} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $installDir 'installation.json') -Encoding UTF8
New-Item -Path $uninstallKey -Force | Out-Null
$properties = @{
    DisplayName='HomePodTray'; DisplayVersion='0.1.0'; Publisher='HomePodTray contributors'
    InstallLocation=$installDir; DisplayIcon=$exe
    UninstallString=('powershell.exe -NoProfile -ExecutionPolicy Bypass -File "' + (Join-Path $installDir 'uninstall.ps1') + '"')
}
foreach ($name in $properties.Keys) { New-ItemProperty -Path $uninstallKey -Name $name -Value $properties[$name] -PropertyType String -Force | Out-Null }
New-ItemProperty -Path $uninstallKey -Name NoModify -Value 1 -PropertyType DWord -Force | Out-Null
New-ItemProperty -Path $uninstallKey -Name NoRepair -Value 1 -PropertyType DWord -Force | Out-Null
if (-not $testMode) {
    $answer = [Windows.Forms.MessageBox]::Show('是否添加仅限本地子网、仅针对 HomePodTray 的 UDP 入站防火墙规则？需要管理员权限；拒绝仍可完成安装。','HomePodTray 安装','YesNo','Question')
    if ($answer -eq 'Yes') {
        try {
            $argsText = '-NoProfile -ExecutionPolicy Bypass -File "' + (Join-Path $installDir 'allow-firewall.ps1') + '"'
            $p = Start-Process powershell.exe -WindowStyle Hidden -Verb RunAs -ArgumentList $argsText -Wait -PassThru
            if ($p.ExitCode -ne 0) { throw 'Firewall helper failed.' }
        } catch { [Windows.Forms.MessageBox]::Show('未添加防火墙规则。软件已安装；首次连接失败时请手动允许 HomePodTray 入站 UDP。','HomePodTray') | Out-Null }
    }
    [Windows.Forms.MessageBox]::Show('安装完成。请从 VB-Audio 官网安装 VB-Cable，重启后按快速开始操作。','HomePodTray') | Out-Null
}
# First launch is manual, so setup never takes over the user's current session.
Write-Output $installDir
