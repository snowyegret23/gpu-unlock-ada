# AD103 experimental EFI 0.3.0

See the port's [English README](../../README.md) or [한국어 README](../../README.ko.md)
for compatibility limits, build prerequisites, measurements, automatic boot
scheduling and recovery. The EFI requires the reviewed machine profile;
a GPU model match alone is insufficient.

Build from the port directory with `./uefi/ad103/Build-Experimental.ps1` and
explicit GCC/Python paths. `Build.ps1` produces only the transaction archive.
Build outputs are not staged or scheduled. Public sources preserve the exact
executable code used in the observed run; private configuration is required.

Original notices, licences and upstream source hashes are retained in this
directory. [CHANGES.md](CHANGES.md) records the port's changes.
