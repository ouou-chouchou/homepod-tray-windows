param([string]$BuildDir, [string]$OutputRoot)
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
if (-not $BuildDir) { $BuildDir = Join-Path $repo 'build' }
if (-not $OutputRoot) { $OutputRoot = $repo }
$BuildDir = [IO.Path]::GetFullPath($BuildDir)
$releaseName = 'HomePodTray-0.1.0-Windows-x64'
$release = Join-Path $OutputRoot $releaseName
$zip = Join-Path $OutputRoot ($releaseName + '.zip')
if ((Test-Path -LiteralPath $release) -or (Test-Path -LiteralPath $zip)) { throw 'Release output already exists. Move it aside before rebuilding.' }
$sourceExe = Join-Path $BuildDir 'Release\HomePodTray.exe'
$mbedLicense = Join-Path $BuildDir '_deps\mbedtls-src\LICENSE'
if (-not (Test-Path -LiteralPath $sourceExe)) { throw 'Build HomePodTray Release first.' }
if (-not (Test-Path -LiteralPath $mbedLicense)) { throw 'Mbed TLS license is missing from build dependencies.' }
$work = Join-Path ([IO.Path]::GetTempPath()) ('HPT-' + [Guid]::NewGuid().ToString('N').Substring(0,8))
$payload = Join-Path $work 'payload'
New-Item -ItemType Directory -Path $payload,$release -Force | Out-Null
Copy-Item -LiteralPath $sourceExe -Destination (Join-Path $payload 'HomePodTray.exe')
$utf8Bom = New-Object Text.UTF8Encoding($true)
foreach ($name in @('install.ps1','uninstall.ps1','allow-firewall.ps1')) {
    # Windows PowerShell 5.1 needs BOM for Chinese text.
    $text = [IO.File]::ReadAllText((Join-Path $PSScriptRoot $name), [Text.Encoding]::UTF8)
    [IO.File]::WriteAllText((Join-Path $payload $name), $text, $utf8Bom)
}
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'install.cmd') -Destination $payload
Copy-Item -LiteralPath (Join-Path $repo 'LICENSE') -Destination (Join-Path $payload 'LICENSE.txt')
Copy-Item -LiteralPath (Join-Path $repo 'THIRD_PARTY.md') -Destination (Join-Path $payload 'THIRD_PARTY.txt')
$notice = [IO.File]::ReadAllText((Join-Path $repo 'NOTICE')) + [Environment]::NewLine + [IO.File]::ReadAllText((Join-Path $repo 'vendor\airplay2-sender-cpp\NOTICE'))
[IO.File]::WriteAllText((Join-Path $payload 'NOTICE.txt'), $notice, $utf8Bom)
$licenses = @{
    'mbedtls.license.txt'=$mbedLicense
    'airplay-sender.license.txt'=(Join-Path $repo 'vendor\airplay2-sender-cpp\LICENSE')
    'upstream-notices.license.txt'=(Join-Path $repo 'vendor\airplay2-sender-cpp\licenses\THIRD-PARTY-NOTICES.txt')
    'ed25519.license.txt'=(Join-Path $repo 'vendor\airplay2-sender-cpp\third_party\ed25519\LICENSE.txt')
}
foreach ($name in $licenses.Keys) { Copy-Item -LiteralPath $licenses[$name] -Destination (Join-Path $payload $name) }
Copy-Item -LiteralPath (Join-Path $PSScriptRoot '快速开始.md') -Destination $release
Copy-Item -LiteralPath (Join-Path $PSScriptRoot '02-请从官方渠道下载 VB-Cable.url') -Destination $release
foreach ($name in @('LICENSE.txt','THIRD_PARTY.txt','NOTICE.txt')) { Copy-Item -LiteralPath (Join-Path $payload $name) -Destination $release }
$licenseDir = Join-Path $release 'licenses'
New-Item -ItemType Directory -Path $licenseDir -Force | Out-Null
foreach ($name in $licenses.Keys) { Copy-Item -LiteralPath (Join-Path $payload $name) -Destination $licenseDir }
$target = Join-Path $work 'HomePodTray-Setup.exe'
$targetSed = $target.Replace('\','/')
$payloadSed = $payload.Replace('\','/')
$files = @(Get-ChildItem -LiteralPath $payload -File | Sort-Object Name)
$options = @"
[Version]
Class=IEXPRESS
SEDVersion=3
[Options]
PackagePurpose=InstallApp
ShowInstallProgramWindow=1
HideExtractAnimation=1
UseLongFileName=1
InsideCompressed=1
CAB_FixedSize=0
CAB_ResvCodeSigning=0
RebootMode=N
InstallPrompt=
DisplayLicense=
FinishMessage=HomePodTray installation is complete.
TargetName=$targetSed
FriendlyName=HomePodTray 0.1.0
AppLaunched=install.cmd
PostInstallCmd=<None>
AdminQuietInstCmd=
UserQuietInstCmd=
SourceFiles=SourceFiles
[SourceFiles]
SourceFiles0=$payloadSed
[SourceFiles0]
"@
for ($i=0; $i -lt $files.Count; $i++) { $options += [Environment]::NewLine + '%FILE' + $i + '%=' }
$options += [Environment]::NewLine + '[Strings]'
for ($i=0; $i -lt $files.Count; $i++) { $options += [Environment]::NewLine + 'FILE' + $i + '=' + $files[$i].Name }
$sed = Join-Path $work 'package.sed'
[IO.File]::WriteAllText($sed, (($options -replace '\r?\n', "`r`n") + "`r`n"), [Text.Encoding]::Default)
$p = Start-Process -FilePath (Join-Path $env:SystemRoot 'System32\iexpress.exe') -WorkingDirectory $work -WindowStyle Hidden -ArgumentList ('/N /Q ' + $sed) -Wait -PassThru
if ($p.ExitCode -ne 0 -or -not (Test-Path -LiteralPath $target)) { throw ('IExpress build failed: ' + $p.ExitCode) }
Copy-Item -LiteralPath $target -Destination (Join-Path $release '01-HomePodTray-Setup.exe')
$hash = (Get-FileHash -LiteralPath (Join-Path $release '01-HomePodTray-Setup.exe') -Algorithm SHA256).Hash
[IO.File]::WriteAllText((Join-Path $release 'SHA256SUMS.txt'), "$hash  01-HomePodTray-Setup.exe" + [Environment]::NewLine, [Text.Encoding]::ASCII)
Compress-Archive -LiteralPath $release -DestinationPath $zip -CompressionLevel Optimal
Write-Output $release
Write-Output $zip
