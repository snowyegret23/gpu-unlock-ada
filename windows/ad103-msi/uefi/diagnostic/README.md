# MSI AD103 UEFI diagnostic

Read-only adaptation for one MSI RTX 4070 Ti SUPER (`10de:2705`, subsystem `1462:513d`, revision A1) with a separately reviewed platform profile. Upstream source and licences are recorded in `../PROVENANCE.json`, `LICENSE.txt` and `THIRD_PARTY.md`.

Run `../../tools/Configure.ps1`, then `./Build.ps1` from PowerShell. MSVC and a real Python interpreter are discovered locally. Output: `build/AD103-PROBE.EFI` and `build/manifest.json`.

The probe reads PCI configuration, bounded GPU registers and PROM, cached EFI ROM, and UEFI display-driver ownership. It checks the configured GPU, PCI address, BAR0 and exact GPT partition identity before diagnostic access. It writes new captures only under `EFI/gpu-unlock-probe/captures` on that partition. It does not change GPU registers, bus mastering, display ownership, firmware boot variables, voltage, clocks or power limits. It returns to the firmware boot manager after eight seconds.

The Windows helper stages the binary and optionally schedules one boot using a separate `Boot8Fxx` variable and `BootNext`. It does not modify `BootOrder` or Windows Boot Manager. See the [port README](../../README.md) for commands.

Host tests and PE checks validate the software paths. They do not establish physical safety, hardware execution, or unlock compatibility. The BAR0 binding is observed in Windows; a different preboot assignment is logged and rejected. A firmware capture is needed before implementing this board's unlock transaction and Windows handoff.

Raw captures include proprietary ROM/driver bytes and machine-specific data. Keep them local. The analyzer validates the captured hashes and structure but never authorizes GPU writes or generates an unlock profile.
