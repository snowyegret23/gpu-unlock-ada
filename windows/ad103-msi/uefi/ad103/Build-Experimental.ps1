param([string]$Gcc, [string]$Python, [string]$VcVars64)
$ErrorActionPreference = 'Stop'
Push-Location -LiteralPath $PSScriptRoot
try {
    if (!$Gcc) { $Gcc = (Resolve-Path -LiteralPath '../../_external/gcc-15.2.0/mingw64/bin/gcc.exe').Path }
    if (!$Python) {
        $Python = Get-Command python.exe -All | Where-Object Source -NotMatch '\\WindowsApps\\' |
            Select-Object -First 1 -ExpandProperty Source
    }
    if (!$Python) { throw 'A real Python interpreter is required.' }
    $Python = & $Python -X utf8 -c 'import sys; print(sys.executable)'
    if ($LASTEXITCODE -ne 0) { throw 'Python resolution failed.' }
    $profile = Get-Content -LiteralPath '../../local/profile.json' -Raw -Encoding utf8 | ConvertFrom-Json
    if ($profile.device -ne '10de:2705' -or $profile.subsystem -ne '1462:513d' -or
        $profile.bar0 -ne '0xf5000000' -or $profile.esp_partition -ne 1 -or
        $profile.esp_start_lba -ne 2048 -or $profile.esp_size_lba -ne 409600) { throw 'Unexpected local profile.' }
    $boot = $profile.volume_path + 'EFI\Microsoft\Boot\bootmgfw.efi'
    $bootHash = (Get-FileHash -LiteralPath $boot).Hash.ToLowerInvariant()
    if ((Get-Item -LiteralPath $boot).Length -ne 3086680 -or
        $bootHash -ne '2d60a2203865122dad22772136b3d905167d9dcf926d602bb5f48b3b6668abec' -or
        $bootHash -ne $profile.windows_boot_manager_sha256) { throw 'Windows loader does not match the reviewed image.' }
    $guid = ([guid]$profile.esp_guid).ToString().Split('-')
    $tail = $guid[3] + $guid[4]
    $bytes = for ($i = 0; $i -lt 16; $i += 2) { '0x' + $tail.Substring($i,2) }
    $header = '/* Local partition identity; generated from local/profile.json. */' + "`r`n" +
        '#define GU_WINDOWS_PARTITION_INITIALIZER {0x' + $guid[0] + ',0x' + $guid[1] + ',0x' + $guid[2] + ',{' + ($bytes -join ',') + '}}' + "`r`n"
    $privateHeader = '../../local/unlock-boot-profile.h'
    if (Test-Path -LiteralPath $privateHeader) {
        if ([IO.File]::ReadAllText((Resolve-Path -LiteralPath $privateHeader)) -cne $header) { throw 'Existing private header differs; inspect it before replacing.' }
    } else { [IO.File]::WriteAllText((Join-Path $PSScriptRoot $privateHeader), $header, [Text.UTF8Encoding]::new($false)) }
    & ./Build.ps1 -Gcc $Gcc -Python $Python
    if (!$?) { throw 'Core build failed.' }
    $flags = @('-std=c11','-O2','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-Wno-misleading-indentation',
        '-D__KERNEL__','-DGU_UEFI_PORT','-DGU_AD103_TRANSACTION','-DGU_ENTRY_LATCHED_SS1_FULL',
        '-Icompat','-I../vendor/edk2/Include','-I../vendor/edk2/Include/X64',
        '-ffreestanding','-fno-builtin','-fno-stack-protector','-mno-stack-arg-probe','-mno-red-zone','-fshort-wchar','-fstack-usage')
    foreach ($source in @('app','boot_io','msi_release')) {
        & $Gcc @flags -c ($source + '.c') -o ('build/' + $source + '.o')
        if ($LASTEXITCODE -ne 0) { throw "Compile failed: $source" }
    }
    $vcvars = $VcVars64
    if (!$vcvars) {
        $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
        $vcvars = @(& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -find 'VC\Auxiliary\Build\vcvars64.bat') | Select-Object -First 1
    }
    if (!$vcvars -or !(Test-Path -LiteralPath $vcvars)) { throw 'MSVC x64 tools not found.' }
    if ($vcvars -match '["&|<>%^!\r\n]' -or $PSScriptRoot -match '["&|<>%^!\r\n]') { throw 'Unsupported command-shell path characters.' }
    $objects = @('app','boot_io','msi_release','port','compat','checks','profile','sha256','runtime','format',
        'vendor_unlock_610','vendor_prep_610','vendor_layout_610','vendor_vbios_610','vendor_gfw_eval')
    $lines = @('@echo off',('call "' + $vcvars + '" >nul'),'if errorlevel 1 exit /b 1',('cd /d "' + $PSScriptRoot + '"'),
        ('link /nologo /subsystem:efi_application /entry:UefiMain /machine:x64 /nodefaultlib /incremental:no /fixed:no /dynamicbase:no /nxcompat:no /brepro /out:build\AD103-EXPERIMENTAL.EFI ' +
        (($objects | ForEach-Object { 'build\' + $_ + '.o' }) -join ' ')), 'exit /b %errorlevel%')
    Set-Content -LiteralPath build/link-msvc.cmd -Value $lines -Encoding ascii
    & $env:ComSpec /d /c ('"' + (Join-Path $PSScriptRoot 'build\link-msvc.cmd') + '"')
    if ($LASTEXITCODE -ne 0) { throw 'EFI link failed.' }
    & $Python -B -X utf8 tools/verify_experimental.py
    if ($LASTEXITCODE -ne 0) { throw 'EFI verification failed.' }
    $hashes = [ordered]@{}
    Get-ChildItem -LiteralPath . -Recurse -File | Where-Object {
        $_.Extension -in @('.c','.h','.ps1','.py') -and $_.FullName -notlike '*\build\*'
    } | Sort-Object FullName | ForEach-Object {
        $hashes[$_.FullName.Substring($PSScriptRoot.Length + 1)] = (Get-FileHash -LiteralPath $_.FullName).Hash.ToLowerInvariant()
    }
    [ordered]@{
        version = '0.3.0'; mode = 'automatic-experimental-one-physical-attempt'; binary = 'AD103-EXPERIMENTAL.EFI'
        sha256 = (Get-FileHash -LiteralPath build/AD103-EXPERIMENTAL.EFI).Hash.ToLowerInvariant()
        gpu_write_capability = $true; hardware_tested = $false; automatic_unlock = $true
        requires_key = $null; automatic_delay_seconds = 10; default = 'one attempt after 10s; S/Esc cancels'; source_hashes = $hashes
        profile_sha256 = (Get-FileHash -LiteralPath '../../local/profile.json').Hash.ToLowerInvariant()
        private_header_sha256 = (Get-FileHash -LiteralPath $privateHeader).Hash.ToLowerInvariant()
        utc = [DateTime]::UtcNow.ToString('o')
    } | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath build/experimental-manifest.json -Encoding utf8
    Write-Output 'Built experimental EFI locally. It is not staged or scheduled.'
} finally { Pop-Location }
