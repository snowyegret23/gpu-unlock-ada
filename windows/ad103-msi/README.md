# Experimental AD103 Windows UEFI port

[한국어](README.ko.md) | [Repository overview](../../README.md)

A Windows preboot port for a reviewed **MSI RTX 4070 Ti SUPER (AD103)** profile,
with read-only capture, guarded GPU initialization, automatic entry, ROM capture
preservation and reversible boot scheduling. It derives from the
[Latence.app AD104 contribution](../ad104-msi/README.md).
GPU model or PCI ID alone is insufficient: the captured firmware, ownership,
USB topology, PCI resources and Windows boot image must also match.

## Observed result

One v0.3.0 execution completed driver release, the GPU transaction, one PCIe
Function Level Reset (FLR), DMA cleanup and return to Windows. Matched cuBLAS
4096×4096 GEMM medians from three independent processes per phase were:

| Mode | Stock TFLOP/s | After TFLOP/s | Change |
|---|---:|---:|---:|
| FP32 | 30.34 | 30.37 | +0.1% |
| FP16 input, FP32 accumulation | 87.03 | 145.67 | +67.4% |
| BF16 input, FP32 accumulation | 86.63 | 143.97 | +66.2% |

Numerical checks passed and all 18 complete output hashes matched. The same GPU,
driver, VBIOS, libraries, script and power limit were used in both phases.
Background activity and thermal state were not perfectly controlled.
These measurements describe this GEMM workload; image generation, local language
models, mixed workloads, games and long-duration stability were not measured.
See the [measurement summary](../../docs/evidence/ad103-windows/summary.json).

Every-boot scheduling was subsequently configured and its firmware variables
read back. No additional boot was performed for that change. Repeated cold/warm
execution, the single-child hardware path and physical recovery remain unverified.

## Guarded behavior

The admission profile checks GPU/subsystem identity, ROM and firmware hashes,
cold registers, exact driver code and ownership relationships. GPU child paths
are inventoried and cross-checked; unknown, duplicate, orphaned or changing
paths are rejected. A logging failure stops PCI decode restoration.

After a **10-second countdown**, the EFI attempts once without a key. **S/Esc**
skips to the verified Windows image; **U** starts immediately. Before mutation,
a failure returns to Windows only if its image was verified and the log closed;
otherwise control returns to firmware. After mutation, a failure halts. There
is no automatic reset or retry within that boot. Uncertain DMA mappings remain
retained until power-off. Success still requires final Windows handoff checks.

This changes volatile GPU state, not the VBIOS EEPROM. It does not change the
configured voltage, clock or power limits. These facts do not establish physical
safety or guarantee recovery after a failure.

## Build and validate

Run commands below from `windows/ad103-msi`. Requirements: Windows x64, a real
Python 3 interpreter, GCC/MinGW-w64 with `ar` and `nm`, and MSVC x64 linker tools.
The existing scripts accept explicit compiler/interpreter paths.

```powershell
$Gcc = (Get-Command gcc.exe).Source
$Python = Get-Command python.exe -All | Where-Object Source -NotMatch '\\WindowsApps\\' |
    Select-Object -First 1 -ExpandProperty Source
.\uefi\ad103\Build-Experimental.ps1 -Gcc $Gcc -Python $Python
```

The build requires the separately reviewed `local/profile.json`, its private
ESP header and the exact Windows boot image. It reads that image and creates
local build outputs; it does not stage an EFI, change boot variables or run the
GPU transaction. New configuration must be captured and reviewed, not copied
from another machine or admitted by removing checks.

The [read-only diagnostic](uefi/diagnostic/README.md) uses
`tools/Configure.ps1`, `uefi/diagnostic/Build.ps1` and `tools/Probe.ps1`.
`Configure.ps1` writes a local profile and refuses an existing one. The probe
manager's `stage`, `arm`, `collect` and `cancel` operations remain separate
from the write-capable unlock manager.

Generate the pinned NVIDIA assets with this repository's `make assets` workflow
on Linux/WSL. Keep the exact required files and licence notice in the reviewed
local asset directory. The unlock manager requires matching source, profile,
EFI and validation-report hashes plus the verified ROM preservation copy.
A build alone does not create a valid `validation-manifest.json` or authorize
staging. The inherited 334 EFI mock scenarios remain tied to their exact source
and private inputs; the Windows manager has 65 mock checks. A relocated build
and manager validation are recorded in
[validation.json](../../docs/evidence/ad103-windows/validation.json).

## Boot scheduling on a validated installation

Use the original installation directory with its verified build, assets,
private profile and saved boot state. A fresh source checkout is not an
installation. Do not manage one deployment from two directories with different
state files. Run these commands in an elevated PowerShell on the target PC.

| Command | Effect |
|---|---|
| `.\tools\Unlock.ps1 status` | Read staging and boot state; `every_boot_enabled` reports persistent selection. |
| `.\tools\Unlock.ps1 stage` | Stage the verified EFI/assets in the bound ESP; no boot reservation. |
| `.\tools\Unlock.ps1 arm` | Reserve one boot using `BootNext`. |
| `.\tools\Unlock.ps1 enable-auto` | Put an owned `Boot8F80`–`Boot8FFF` entry before the saved Windows boot order. |
| `.\tools\Unlock.ps1 disable-auto` | Restore the saved order before removing the owned option. |
| `.\tools\Unlock.ps1 cancel` | Cancel either one-time or every-boot selection. |
| `.\tools\Unlock.ps1 collect` | Preserve run logs; refuse to overwrite different existing bytes. |

These commands do not restart Windows. Every-boot mode preserves the original
Windows option and files, and does not use `BootNext`. Disable/cancel preserves
unrelated entries and refuses externally changed ownership/order. Disabling is
still possible after a Windows loader update when the same Windows/ESP identity
matches. It does not reset the GPU state in the running Windows session.

To bypass one attempt, press S/Esc during the countdown or choose **Windows Boot
Manager** from the firmware boot menu. If Windows does not return within four
minutes, fully power off, wait ten seconds and choose Windows Boot Manager on
the next boot, then run `disable-auto`. **Every-boot mode will attempt again if
you simply power off and back on without bypassing it.** Reinstallation, ESP
changes, firmware resets or Secure Boot changes can invalidate the setup.
A changed Windows image is refused before GPU mutation. Do not restore an old
Windows image merely to satisfy the pinned hash. Logs are preserved; exhausted
log space or numbering causes refusal rather than automatic deletion.

## ROM preservation and recovery

```powershell
.\tools\BackupFirmware.ps1 create
.\tools\BackupFirmware.ps1 verify
```

These commands operate on the previously reviewed, complete read-only capture.
They preserve its original bytes, captured PROM and cached EFI ROM with hashes;
they do not reread the GPU, flash it or restore firmware. Full flash-chip
coverage and compatibility with a programmer/flashing tool are unverified.

Keep a machine-specific recovery package off the target PC, containing the
private profile, original captures, boot-state snapshots, logs and appropriate
Windows recovery material. `tools/recovery/Verify-Package.ps1` validates an
existing package manifest and file hashes without accessing GPU or firmware.
It does not create bootable recovery media or prove that physical recovery works.

## Credits and licensing

AD103 adaptation and automatic boot management are maintained in this fork.
The Windows UEFI base and ownership work come from **Latence.app** and
[impacifist/Windows-GPU-UNLOCK](https://github.com/impacifist/Windows-GPU-UNLOCK).
Ada integration derives from [JustAResearcher/gpu-unlock-ada](https://github.com/JustAResearcher/gpu-unlock-ada),
[Zagitta/gpu-unlock](https://github.com/Zagitta/gpu-unlock) and
[TOTORONG/AD103_win_unlocker](https://huggingface.co/TOTORONG/AD103_win_unlocker).

The unlock code is GPL-2.0-only; diagnostic code and bundled dependencies retain
their own licences. See [provenance](provenance.json),
[notices](uefi/ad103/THIRD_PARTY_NOTICES.md),
[diagnostic notices](uefi/diagnostic/THIRD_PARTY.md) and the included licence files.
