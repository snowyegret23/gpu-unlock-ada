$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$gpu = @(Get-CimInstance Win32_PnPEntity -Filter "Name = 'NVIDIA GeForce RTX 4070 Ti SUPER'")
if ($gpu.Count -ne 1 -or $gpu[0].PNPDeviceID -notlike 'PCI\VEN_10DE&DEV_2705&SUBSYS_513D1462&REV_A1\*') {
    throw 'This diagnostic is restricted to one MSI 10de:2705 / 1462:513d A1 GPU.'
}
$board = Get-CimInstance Win32_BaseBoard
if ($board.Product -ne 'MAG B650M MORTAR WIFI (MS-7D76)') { throw 'Unexpected motherboard.' }
$systemDisks = @(Get-Disk | Where-Object IsSystem)
if ($systemDisks.Count -ne 1) { throw 'Expected one system disk.' }
$esp = @(Get-Partition -DiskNumber $systemDisks[0].Number | Where-Object {
    $_.GptType -eq '{c12a7328-f81f-11d2-ba4b-00a0c93ec93b}'
})
if ($esp.Count -ne 1) { throw 'Expected one EFI system partition on the system disk.' }
$esp = $esp[0]
$sectorSize = $systemDisks[0].LogicalSectorSize
if ($sectorSize -ne 512 -or $esp.Offset % $sectorSize -or $esp.Size % $sectorSize) {
    throw 'Unexpected partition geometry.'
}
$resources = @(Get-CimAssociatedInstance -InputObject $gpu[0] -Association Win32_PNPAllocatedResource | Where-Object {
    $_.CimClass.CimClassName -eq 'Win32_DeviceMemoryAddress' -and
    ([uint64]$_.EndingAddress - [uint64]$_.StartingAddress + 1) -eq 0x1000000
})
if ($resources.Count -ne 1 -or [uint64]$resources[0].StartingAddress -gt [uint32]::MaxValue) {
    throw 'Expected a single 16 MiB, 32-bit BAR0 resource.'
}
$bootManager = $esp.AccessPaths[0] + 'EFI\Microsoft\Boot\bootmgfw.efi'
if (-not (Test-Path -LiteralPath $bootManager)) { throw 'Windows Boot Manager is not accessible.' }
$profile = [ordered]@{
    schema = 1
    device = '10de:2705'
    subsystem = '1462:513d'
    revision = 'a1'
    pci = '0000:01:00.0'
    board = $board.Product
    bios = (Get-CimInstance Win32_BIOS).SMBIOSBIOSVersion
    bar0 = ('0x{0:x8}' -f [uint64]$resources[0].StartingAddress)
    esp_guid = ([guid]$esp.Guid).ToString()
    esp_partition = $esp.PartitionNumber
    esp_start_lba = [uint64]($esp.Offset / $sectorSize)
    esp_size_lba = [uint64]($esp.Size / $sectorSize)
    volume_path = $esp.AccessPaths[0]
    windows_boot_manager_sha256 = (Get-FileHash -LiteralPath $bootManager -Algorithm SHA256).Hash.ToLowerInvariant()
    secure_boot = [bool](Confirm-SecureBootUEFI)
    capture_utc = [DateTime]::UtcNow.ToString('o')
    hardware_unlock_validated = $false
}
$localDir = Join-Path $root 'local'
New-Item -Path $localDir -ItemType Directory -Force | Out-Null
$profilePath = Join-Path $localDir 'profile.json'
if (Test-Path -LiteralPath $profilePath) { throw 'local/profile.json already exists; review it before reconfiguring.' }
$profile | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $profilePath -Encoding utf8
Write-Output 'Saved local/profile.json. No GPU or boot settings changed.'
