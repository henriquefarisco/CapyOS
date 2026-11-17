# Etapa 11A — ACPI power discovery and diagnostics

Date: 2026-11-17. Owner: CapyOS only. Base: `80aca93` on `main`, published
version `0.11.3+20261004`. This is a local engineering slice, not a release.

## Scope and acceptance

The first power-management slice provides an actual `power-status` shell
command and a real ACPI PM-timer read through validated firmware resources.
Acceptance requires focused malformed-input tests, the repository host suite,
a complete kernel/UEFI artifact, QEMU boot/login/command/persistence and VMware
cold-boot evidence. It does **not** close any of the three end-user acceptance
checkboxes for the full Etapa 11.

Implementation:

- UEFI selects ACPI 2.0 before ACPI 1.0 independently of table order and of
  GNU-EFI's `CompareGuid` return convention. Invalid advertised roots are not
  replaced by a legacy scan. RSDP copying respects its validated length
  (20 bytes for v1, 36–4096 for v2+) instead of always reading 64 bytes.
- The kernel validates root/FADT/DSDT signatures, lengths and checksums. An
  advertised XSDT or X_DSDT is authoritative. Duplicate FADTs, partial entries,
  tables above 1 MiB and roots above 256 entries are rejected.
- Firmware views require retained EFI RAM and mapped kernel identity pages.
  The retained `EfiLoaderData` RSDP copy is accepted; conventional RAM, MMIO,
  overflowing/noncanonical spans and unmapped pages are rejected.
- Only representable SystemIO registers with supported width/offset/access
  are exposed. Extended unsupported registers do not fall back to legacy
  addresses. Hardware-reduced ACPI suppresses the legacy fixed-register path.
- `power-status` reports table discovery, PM1, 24/32-bit PM timer and ACPI reset
  availability. Boot emits `[smoke] acpi-power ready` only after validated
  discovery and an observed change in the real PM timer. No sleep transition,
  firmware ownership change or synthetic battery percentage is performed.

The specification reference is [ACPI 6.6, software programming model](https://uefi.org/specs/ACPI/6.6/05_ACPI_Software_Programming_Model.html)
and [hardware specification](https://uefi.org/specs/ACPI/6.6/04_ACPI_Hardware_Specification.html).

## Validation

**Accepted locally.** All gates below exited zero on the final artifact.
Local logs and disposable VM artifacts are under `build/etapa11-power/`;
the installer harnesses also retain evidence under their required `build/ci/`
root. These generated artifacts are not committed.

| Gate | Observed evidence |
|---|---|
| Focused host + ASan/UBSan | 262 driver/table checks + 15 UEFI checks; `final-checks.log` |
| Repository suite | `make -j8 test TOOLCHAIN64=elf`, all tests passed; refreshed after the wizard export fix in `tests-after-wizard-fix.json` |
| Layout | `make layout-audit`, no warnings; refreshed in `layout-final.json` |
| Kernel/loader/ISO | `make all64`, `iso-uefi manifest64`, cross toolchain; `final-checks.json`, exit 0 |
| QEMU | `qemu-final-accepted.json`, exit 0; two boots, first setup, authenticated diagnostics, invalid argument rejection, existing CLI regression suite, persistence and actual VM poweroff |
| VMware disk preparation | `vmware-convert.json`, `vmware-disk-compare.json`: prepared QEMU disk converted to monolithicFlat VMDK, images identical before VMware boot |
| VMware | `vmware-accepted.json`, `vmware/logs/result.json`: two independent cold boots, authenticated diagnostics/invalid arguments and no panic/fault markers; both VM stops verified by `vmrun list` |
| Marker policy | `marker-policy.json`: policy, VMware ordering and release-evidence selftests passed |
| QEMU installer without UART | `qemu-no-uart-result.json`: idle prompt, ConIn cancellation, absence of UART confirmed by device tree, disposable disk hash unchanged |
| VMware installer without UART | `vmware-no-uart-result.json`, `build/ci/etapa11-vmware-no-uart.manifest`, run `837a1089719b`: keyboard cancellation and unchanged target/guard disks |
| Fresh VMware installation | `vmware-wizard-final.json`, `build/ci/etapa11-vmware-wizard.manifest`, run `33545a940196`: explicit target selection, install, first boot/login, persistence across reboot and unchanged guard disk; profile basic/core |

All three installer gates above used the exact retained ISO SHA-256
`59408b66229d2b875eb5dc2123e9488a27173b15247e5c8f1e2485c56e3a4636`.
The evidence manifests bind the source ISO and protected-disk hashes; no VM
was left running.

QEMU 10.2.1, q35/TCG, OVMF `OVMF_CODE_4M.fd`, 1 GiB RAM, default single
vCPU, SATA/AHCI and E1000 (restricted user network). VMware Workstation
26.0.0 build 25388281, hardware version 22, UEFI, Secure Boot disabled,
1 GiB RAM, 2 vCPUs, SATA/AHCI and E1000 NAT. The disposable VM has sound and
USB disabled; this is a CLI diagnostic gate, not a desktop/audio regression
acceptance. VMware cold boots end through the harness's hard stop; only the
QEMU CLI gate proves guest-requested shutdown here.

Both environments report XSDT, PM1 SystemIO and a 24-bit PM timer. The QEMU
loader now reports `RSDP src=cfg`, revision 2, instead of the previous fallback
scan selecting revision 0. Fault/missing capability paths fail the gate rather
than marking battery or suspend as implemented.

Build tools: x86_64-elf GCC 13.2.0, host GCC 15.2.0, NASM 3.01 and GNU
binutils 2.46 in Ubuntu WSL. Source consumers were clean: CapyUI `0507b6f`,
CapyAI `74b3c4b`, CapyBrowser `330828b`, CapyCodecs `394874d` (unchanged).

### Retained artifacts

Copies under `build/etapa11-power/delivery/`; manifest of hashes in
`build/etapa11-power/artifacts.json`:

| Artifact | Bytes | SHA-256 |
|---|---:|---|
| `capyos64.bin` | 7417072 | `4d307317ecfb336fc3708b4cc93853a2b73dae2b9eada53f3ada2fbd2575ea3f` |
| `uefi_loader.efi` | 210154 | `5bd5412e8c6407e4b1875f5860ba69404b338e63da3647a20f92e326091b86c1` |
| `manifest.bin` | 512 | `11ce002895e420e1a4e4442a3fd75dce38bed596697af3ab2cc554dd0c2aeaf4` |
| `CapyOS-11A-ACPI-development.iso` | 24788992 | `59408b66229d2b875eb5dc2123e9488a27173b15247e5c8f1e2485c56e3a4636` |

The ISO was generated and checked by the maintained build tooling. The ACPI
runtime gates booted disposable provisioned disks containing these kernel/loader
bytes. The additional UEFI checklist gates then validated the same ISO directly,
including fresh basic/core installation in VMware. Full-module installation,
desktop/audio acceptance and public release promotion were not part of 11A.

Maintained entry points:

```sh
make test-acpi-power ACPI_TEST_FLAGS=-fsanitize=address,undefined
make test TOOLCHAIN64=elf
make smoke-x64-qemu-acpi-power TOOLCHAIN64=elf
make smoke-x64-vmware-acpi-power SMOKE_X64_VMWARE_ACPI_ARGS="..."
```

The QEMU gate provisions its own disposable disk, completes first-boot setup,
checks `power-status` and invalid arguments after login on both boots, and
checks filesystem persistence using the existing CLI harness. The Windows-native
VMware runner requires a prepared disposable project VM/disk under `build/`
(initial setup must already be completed), UEFI, E1000 and a serial named pipe.
It performs login and the same command checks on two cold boots, preserves
separate serial logs, and verifies that the VM was stopped. A fresh empty
disk stops at initial setup before the ACPI probe; a marker-only timeout there
is not ACPI evidence. Neither gate is an installer acceptance or public release.
The separate installer gates in the validation table provide the required
loader-regression evidence.

Exact Windows invocation used (replace the discovered VMware installation path
when necessary):

```powershell
python tools/scripts/smoke_x64_vmware_acpi_power.py --vmx build/etapa11-power/vmware/acpi.vmx --pipe-name capyos-acpi-power-20261117 --vmrun '<VMware installation>/vmrun.exe' --log-root build/etapa11-power/vmware/logs --timeout 120
```

Earlier failed runs were preserved: marker-only runs stopped at initial setup;
the first interactive gate exposed rejection of the loader's retained RSDP
copy. The mapping policy was corrected with a regression test. A further
source review added noncanonical-span rejection, then all invalidated gates
were rerun on the exact final artifact above. No timeout or oracle was relaxed.

The first wizard run (`dbaff097a837`) completed its VM operations but failed
while exporting evidence: the runner supplied the UI profile `basic`, while
the existing v4 schema requires `core` or `full`. The producer now translates
`basic` to `core`; schema validation is unchanged and still rejects `basic`.
`wizard-contract-final.json` tests that mapping, full-profile preservation,
invalid profiles and the unchanged strict schema. The full repository suite
and entire wizard were rerun successfully. The failed run remains recorded as
a failure, not retroactively accepted.

## Limits and next slice

No AML interpreter, EC battery/thermal driver, tray provider, WiFi transport,
WPA supplicant or S3 resume path is delivered here. The pre-existing simplified
S5 lookup/fallback is not a general AML implementation or proof of suspend.
PM timer wrap arithmetic is host-tested; the runtime probe proves advancement,
not frequency calibration or a new scheduler clock source. Read-only firmware
pages unsupported by the current identity-map query remain unavailable.

Next: a bounded AML/ACPI device-evaluation provider and actual `_BIF`/`_BIX`,
`_BST` and thermal readout before integrating live battery state into CapyUI.
Missing hardware must be reported as unavailable, never inferred from a fixed
firmware table or filled with simulated values.

No package ABI, handoff layout, dependency pin, trust anchor or disk format
changes. The published version remains immutable; no commit, push, tag or
publication is part of this delivery.
