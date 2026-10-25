param([ValidateSet('status','stage','arm','cancel','collect')][string]$Action = 'status')
$ErrorActionPreference = 'Stop'
$candidate = Get-Command python.exe -All | Where-Object { $_.Source -notmatch '\\WindowsApps\\' } | Select-Object -First 1 -ExpandProperty Source
if (-not $candidate) { throw 'A real Python interpreter is required.' }
$interpreter = & $candidate -X utf8 -c 'import sys; print(sys.executable)'
if ($LASTEXITCODE -ne 0) { throw 'Python resolution failed.' }
$env:PYTHONDONTWRITEBYTECODE = '1'
& $interpreter -X utf8 (Join-Path $PSScriptRoot 'probe_boot.py') $Action
if ($LASTEXITCODE -ne 0) { throw "Diagnostic operation failed: $Action" }
