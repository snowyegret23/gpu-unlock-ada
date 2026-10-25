# AD103 Windows port changes

Based on `windows/ad104-msi` from JustAResearcher/gpu-unlock-ada
`4e7fefcc4923f9e083d5324dc75a7d2f906af77f`. Upstream licence notices and source
hashes are retained. The Linux admission rules and existing AD104 port are unchanged.

- Adapt the captured GPU identity, ROM/FWSEC, memory geometry, driver code,
  ownership and platform paths to the guarded AD103 profile.
- Bind internal ESP access and Windows handoff to the reviewed private partition
  identity and boot image; preserve Windows files.
- Stop PCI decode restoration immediately if its intent log fails.
- Record named preflight checks, actual verified driver counts and bounded
  ownership diagnostics without relaxing admission.
- Default to one attempt after ten seconds; S/Esc skips and U starts immediately.
  Before-mutation Windows fallback requires a verified image and closed log.
  Post-mutation failures halt and retain uncertain DMA mappings.
- Inventory known GPU child paths, cross-check ownership and freeze the selected
  handles through release. Refuse unknown, duplicate, orphaned, changed or leftover
  paths. Both one-child and two-child paths have mock coverage; only the latter
  was observed in the successful hardware run.
- Preserve and verify the reviewed ROM capture offline. No flash/restore API.
- Add one-time and every-boot scheduling with exact state/readback checks.
  Restore the original boot order before removing an owned persistent option;
  refuse external drift, preserve unrelated entries and allow disable after a
  Windows loader update when the same Windows/ESP identity matches.
- Keep package-integrity verification separate from physical recovery claims.

The original EFI suite covers 334 mocked scenarios; the manager covers 65.
The hardware observation consists of one successful transaction/FLR/Windows
return and a matched short GEMM comparison. Repeated boot execution, application
speedups, endurance and physical restoration are not qualified. See the bilingual
port README and the repository's measurement/validation summaries.
