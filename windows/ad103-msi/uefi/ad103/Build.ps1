param([string]$Gcc, [string]$Python)
$ErrorActionPreference = 'Stop'
$originalPath = $env:PATH
Push-Location -LiteralPath $PSScriptRoot
try {
    if (!$Gcc) { $Gcc = (Resolve-Path -LiteralPath '../../_external/gcc-15.2.0/mingw64/bin/gcc.exe').Path }
    if (!$Python) {
        $Python = Get-Command python.exe -All | Where-Object Source -NotMatch '\\WindowsApps\\' |
            Select-Object -First 1 -ExpandProperty Source
    }
    if (!$Python) { throw 'A real Python interpreter is required; the Windows Store alias is not supported.' }
    $Python = & $Python -X utf8 -c 'import sys; print(sys.executable)'
    if ($LASTEXITCODE -ne 0 -or !(Test-Path -LiteralPath $Python -PathType Leaf)) { throw 'Python resolution failed.' }
    $toolBin = Split-Path -Parent $Gcc
    $env:PATH = $toolBin + [IO.Path]::PathSeparator + $originalPath
    $ar = Join-Path $toolBin 'ar.exe'
    New-Item -ItemType Directory -Path build -Force | Out-Null
    $flags = @('-std=c11','-O2','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-Wno-misleading-indentation',
        '-D__KERNEL__','-DGU_UEFI_PORT','-DGU_AD103_TRANSACTION','-DGU_ENTRY_LATCHED_SS1_FULL',
        '-Icompat','-I../vendor/edk2/Include','-I../vendor/edk2/Include/X64',
        '-ffreestanding','-fno-builtin','-fno-stack-protector','-mno-stack-arg-probe','-mno-red-zone','-fshort-wchar','-fstack-usage')
    $sources = @('port','compat','checks','profile','sha256','runtime','format',
        'vendor/unlock_610','vendor/prep_610','vendor/layout_610','vendor/vbios_610','vendor/gfw_eval')
    $objects = @()
    foreach ($source in $sources) {
        $object = 'build/' + ($source -replace '/', '_') + '.o'
        & $Gcc @flags -c ($source + '.c') -o $object
        if ($LASTEXITCODE -ne 0) { throw "Compile failed: $source" }
        $objects += $object
    }
    $archive = 'build/AD103-TRANSACTION.a'
    if (Test-Path -LiteralPath $archive) { Remove-Item -LiteralPath $archive }
    & $ar rcs $archive @objects
    if ($LASTEXITCODE -ne 0) { throw 'Archive creation failed.' }
    & $Python -B -X utf8 tools/verify_archive.py
    if ($LASTEXITCODE -ne 0) { throw 'Archive verification failed.' }
    $hashes = [ordered]@{}
    Get-ChildItem -LiteralPath . -Recurse -File |
        Where-Object { $_.Extension -in @('.c','.h','.ps1','.py') -and $_.FullName -notlike '*\build\*' } |
        Sort-Object FullName | ForEach-Object {
            $hashes[$_.FullName.Substring($PSScriptRoot.Length + 1)] = (Get-FileHash -LiteralPath $_.FullName).Hash.ToLowerInvariant()
        }
    [ordered]@{
        artifact = 'AD103-TRANSACTION.a'
        sha256 = (Get-FileHash -LiteralPath $archive).Hash.ToLowerInvariant()
        source_hashes = $hashes
        bootable = $false
        hardware_unlock_validated = $false
        gpu_writes_performed = $false
        utc = [DateTime]::UtcNow.ToString('o')
    } | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath build/manifest.json -Encoding utf8
    Write-Output 'Built AD103 transaction library. No EFI entry point and no GPU execution.'
} finally {
    $env:PATH = $originalPath
    Pop-Location
}
