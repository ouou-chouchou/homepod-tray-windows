param([Parameter(Mandatory=$true)][string]$InstallerPath)
$ErrorActionPreference = 'Stop'
function Assert($ok, $message) { if (-not $ok) { throw $message }; Write-Output ("PASS: " + $message) }
$testRoot = Join-Path ([IO.Path]::GetTempPath()) ('HomePodTray-test-' + [Guid]::NewGuid().ToString('N'))
$extract = Join-Path $testRoot 'extract'
$key = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\HomePodTray-PackageTest'
if (Test-Path -LiteralPath $key) { throw 'A previous package test registry entry exists.' }
New-Item -ItemType Directory -Path $extract -Force | Out-Null
$p = Start-Process -FilePath $InstallerPath -WindowStyle Hidden -ArgumentList ('/Q /T:"' + $extract + '" /C') -Wait -PassThru
Assert ($p.ExitCode -eq 0) 'IExpress extraction succeeded'
$names = @('HomePodTray.exe','install.cmd','install.ps1','uninstall.ps1','allow-firewall.ps1','LICENSE.txt','NOTICE.txt','THIRD_PARTY.txt','mbedtls.license.txt','airplay-sender.license.txt','upstream-notices.license.txt','ed25519.license.txt')
foreach ($name in $names) { Assert (Test-Path -LiteralPath (Join-Path $extract $name)) ("payload contains " + $name) }
Assert (@(Get-ChildItem -LiteralPath $extract -File).Count -eq $names.Count) 'payload contains only expected files'
foreach ($name in @('install.ps1','uninstall.ps1','allow-firewall.ps1')) {
    $tokens=$null; $errors=$null
    [Management.Automation.Language.Parser]::ParseFile((Join-Path $extract $name), [ref]$tokens, [ref]$errors) | Out-Null
    Assert ($errors.Count -eq 0) ("Windows PowerShell parser accepts " + $name)
}
& (Join-Path $extract 'install.ps1') -TestRoot $testRoot
$installed = Join-Path $testRoot 'Programs\HomePodTray'
$exe = Join-Path $installed 'HomePodTray.exe'
Assert ((Get-FileHash -LiteralPath $exe).Hash -eq (Get-FileHash -LiteralPath (Join-Path $extract 'HomePodTray.exe')).Hash) 'installed executable hash matches payload'
Assert ((Get-ItemProperty -LiteralPath $key).DisplayVersion -eq '0.1.0') 'test uninstall registry entry created'
$shell = New-Object -ComObject WScript.Shell
$links = @((Join-Path $testRoot 'Desktop\HomePodTray.lnk'), (Join-Path $testRoot 'StartMenu\HomePodTray.lnk'))
foreach ($link in $links) { Assert ($shell.CreateShortcut($link).TargetPath -eq $exe) 'shortcut target matches installation' }
Assert (@(Get-ChildItem -LiteralPath (Join-Path $installed 'licenses') -File).Count -eq 4) 'third-party license texts installed'
# Repeat install to cover replacement of an existing, non-running version.
& (Join-Path $extract 'install.ps1') -TestRoot $testRoot
Assert (Test-Path -LiteralPath $exe) 'reinstall succeeded'
$invalidInstallDir = [IO.Path]::GetFullPath($testRoot)
$didReject=$false
try { & (Join-Path $extract 'install.ps1') -TestRoot ([IO.Path]::GetPathRoot($invalidInstallDir)) } catch { $didReject=$true }
Assert $didReject 'installer rejects a test target outside TEMP'
& (Join-Path $installed 'uninstall.ps1') -TestMode
Assert (-not (Test-Path -LiteralPath $installed)) 'uninstall removes only test installation'
foreach ($link in $links) { Assert (-not (Test-Path -LiteralPath $link)) 'test shortcut removed' }
Assert (-not (Test-Path -LiteralPath $key)) 'test uninstall registry entry removed'
Write-Output ('Test extraction retained at ' + $testRoot)
