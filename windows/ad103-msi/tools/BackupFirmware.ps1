param(
    [ValidateSet('create','verify')][string]$Action = 'create',
    [string]$Directory,
    [string]$Capture
)
$ErrorActionPreference = 'Stop'
$candidate = Get-Command python.exe -All | Where-Object Source -NotMatch '\\WindowsApps\\' |
    Select-Object -First 1 -ExpandProperty Source
if (!$candidate) { throw 'A real Python interpreter is required.' }
$interpreter = & $candidate -X utf8 -c 'import sys; print(sys.executable)'
if ($LASTEXITCODE -ne 0) { throw 'Python resolution failed.' }
$arguments = @('-B', '-X', 'utf8', (Join-Path $PSScriptRoot 'backup_firmware.py'), $Action)
if ($Directory) { $arguments += @('--directory', $Directory) }
if ($Capture) { $arguments += @('--capture', $Capture) }
& $interpreter @arguments
if ($LASTEXITCODE -ne 0) { throw "Firmware capture backup failed: $Action" }
