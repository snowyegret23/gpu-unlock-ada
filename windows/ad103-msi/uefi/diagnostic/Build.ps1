param(
    [string]$VcVars,
    [string]$Python
)
$ErrorActionPreference = 'Stop'
if (-not $VcVars) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path -LiteralPath $vswhere)) { throw 'Visual Studio Installer was not found.' }
    $installation = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $installation) { throw 'Install the MSVC x64 build tools.' }
    $VcVars = Join-Path $installation 'VC\Auxiliary\Build\vcvars64.bat'
}
if (-not $Python) {
    $candidate = Get-Command python.exe -All | Where-Object { $_.Source -notmatch '\\WindowsApps\\' } | Select-Object -First 1 -ExpandProperty Source
    if (-not $candidate) { throw 'A real Python interpreter is required.' }
    $Python = & $candidate -X utf8 -c 'import sys; print(sys.executable)'
}
$env:PYTHONDONTWRITEBYTECODE = '1'
$env:PYTHONUTF8 = '1'
if (-not (Test-Path -LiteralPath $VcVars)) { throw "Missing MSVC setup: $VcVars" }
if ($VcVars -match '["&|<>%^!\r\n]' -or $PSScriptRoot -match '["&|<>%^!\r\n]') { throw 'Unsupported command-shell characters in build paths' }
$buildDir = Join-Path $PSScriptRoot 'build'
New-Item -ItemType Directory -Path $buildDir -Force | Out-Null
$profilePath = Join-Path $PSScriptRoot '..\..\local\profile.json'
$profile = Get-Content -LiteralPath $profilePath -Raw -Encoding utf8 | ConvertFrom-Json
if ($profile.schema -ne 1 -or $profile.device -ne '10de:2705' -or $profile.subsystem -ne '1462:513d' -or
    $profile.board -ne 'MAG B650M MORTAR WIFI (MS-7D76)' -or $profile.pci -ne '0000:01:00.0') { throw 'Unexpected local profile.' }
$espGuid = [guid]$profile.esp_guid
if ($espGuid -eq [guid]::Empty -or $profile.esp_partition -lt 1 -or $profile.esp_start_lba -lt 1 -or $profile.esp_size_lba -lt 1 -or
    $profile.bar0 -notmatch '^0x[0-9a-f]{8}$' -or ([Convert]::ToUInt32($profile.bar0.Substring(2),16) -band 0xffffff) -ne 0) { throw 'Invalid local partition or BAR0 profile.' }
$guidBytes = ($espGuid.ToByteArray() | ForEach-Object { '0x{0:x2}' -f $_ }) -join ','
$profileHeader = @"
#define PROBE_ESP_GUID_BYTES {$guidBytes}
#define PROBE_ESP_PARTITION $($profile.esp_partition)u
#define PROBE_ESP_START_LBA $($profile.esp_start_lba)ULL
#define PROBE_ESP_SIZE_LBA $($profile.esp_size_lba)ULL
#define PROBE_BAR0_BASE $($profile.bar0)u
"@
Set-Content -LiteralPath (Join-Path $buildDir 'profile.h') -Value $profileHeader -Encoding ascii
$profileHash = (Get-FileHash -LiteralPath $profilePath -Algorithm SHA256).Hash.ToLowerInvariant()
$sourceHashes = [ordered]@{}
Get-ChildItem -LiteralPath (Join-Path $PSScriptRoot 'src') -File | Sort-Object Name | ForEach-Object { $sourceHashes[$_.Name] = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant() }
$identityText = (($sourceHashes.GetEnumerator() | ForEach-Object { $_.Name + '=' + $_.Value }) -join "`n")
$identityText += "`nheaders=" + (Get-FileHash -LiteralPath (Join-Path $PSScriptRoot '..\vendor\edk2\headers.json') -Algorithm SHA256).Hash
$identityText += "`nbuild=" + (Get-FileHash -LiteralPath $PSCommandPath -Algorithm SHA256).Hash
$identityText += "`nprofile=" + $profileHash
$hasher = [Security.Cryptography.SHA256]::Create()
try { $buildId = [BitConverter]::ToString($hasher.ComputeHash([Text.Encoding]::UTF8.GetBytes($identityText))).Replace('-','').ToLowerInvariant() } finally { $hasher.Dispose() }
Set-Content -LiteralPath (Join-Path $buildDir 'build_id.h') -Value ('#define PROBE_BUILD_ID "' + $buildId + '"') -Encoding ascii
$script = @'
@echo off
call "@VCVARS@" >nul
if errorlevel 1 exit /b 1
cd /d "@ROOT@"
cl /nologo /W4 /WX /O2 /Oi /GS- /Gs65536 /Zl /c /TC /Ibuild /I..\vendor\edk2\Include /I..\vendor\edk2\Include\X64 /Fobuild\core.obj src\core.c
if errorlevel 1 exit /b 1
cl /nologo /W4 /WX /O2 /Oi /GS- /Gs65536 /Zl /c /TC /FIbuild_id.h /Ibuild /Ibuild /I..\vendor\edk2\Include /I..\vendor\edk2\Include\X64 /Fobuild\probe.obj src\probe.c
if errorlevel 1 exit /b 1
cl /nologo /W4 /WX /O2 /Oi /GS- /Gs65536 /Zl /c /TC /Ibuild /I..\vendor\edk2\Include /I..\vendor\edk2\Include\X64 /Fobuild\sample.obj src\sample.c
if errorlevel 1 exit /b 1
cl /nologo /W4 /WX /O2 /Oi /GS- /Gs65536 /Zl /c /TC /Ibuild /I..\vendor\edk2\Include /I..\vendor\edk2\Include\X64 /Fobuild\platform.obj src\platform.c
if errorlevel 1 exit /b 1
cl /nologo /W4 /WX /Od /Oi- /GS- /Zl /c /TC /Ibuild /I..\vendor\edk2\Include /I..\vendor\edk2\Include\X64 /Fobuild\runtime.obj src\runtime.c
if errorlevel 1 exit /b 1
link /nologo /subsystem:efi_application /entry:UefiMain /machine:x64 /nodefaultlib /incremental:no /fixed:no /dynamicbase:no /nxcompat:no /out:build\AD103-PROBE.EFI build\core.obj build\probe.obj build\sample.obj build\platform.obj build\runtime.obj
if errorlevel 1 exit /b 1
cl /nologo /W4 /WX /Od /D_CRT_SECURE_NO_WARNINGS /FIbuild_id.h /Ibuild /Ibuild /I..\vendor\edk2\Include /I..\vendor\edk2\Include\X64 tests\host_tests.c src\core.c src\sample.c src\platform.c /Febuild\host-tests.exe /Fobuild\
if errorlevel 1 exit /b 1
build\host-tests.exe
if errorlevel 1 exit /b 1
cl /nologo /W4 /WX /Od /D_CRT_SECURE_NO_WARNINGS /Ibuild /I..\vendor\edk2\Include /I..\vendor\edk2\Include\X64 tests\platform_tests.c src\core.c src\platform.c /Febuild\platform-tests.exe /Fobuild\
if errorlevel 1 exit /b 1
build\platform-tests.exe
if errorlevel 1 exit /b 1
'@
$script = $script.Replace('@VCVARS@', $VcVars).Replace('@ROOT@', $PSScriptRoot)
$cmdFile = Join-Path $buildDir 'compile.cmd'
Set-Content -LiteralPath $cmdFile -Value $script -Encoding ascii
& $env:ComSpec /d /c $cmdFile
if ($LASTEXITCODE -ne 0) { throw 'Compiler or host test failure' }
& $Python (Join-Path $PSScriptRoot 'tools\verify.py')
if ($LASTEXITCODE -ne 0) { throw 'PE/source verification failure' }
& $Python -m unittest discover -s (Join-Path $PSScriptRoot 'tests') -p 'test_*.py' -v
if ($LASTEXITCODE -ne 0) { throw 'Python verification test failure' }
$binary = Join-Path $buildDir 'AD103-PROBE.EFI'
$manifest = [ordered]@{ version='0.5.2'; profile_sha256=$profileHash; build_id=$buildId; binary='AD103-PROBE.EFI'; sha256=(Get-FileHash -LiteralPath $binary -Algorithm SHA256).Hash.ToLowerInvariant(); source_sha256=$sourceHashes; hardware_tested=$false; gpu_write_capability=$false; mmio_read_capability=$true; validation='MSVC host mocks and PE checks'; utc=[DateTime]::UtcNow.ToString('o') }
$manifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $buildDir 'manifest.json') -Encoding utf8
Write-Output $binary
