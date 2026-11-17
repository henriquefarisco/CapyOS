# CapyOS 0.11.4+20261117

Candidate `0.11.4+20261117` publishes the first ACPI power-management slice.
Publication, signatures and runtime acceptance of the final release bytes are
pending. The currently published Latest remains 0.11.3 until signed promotion.

## Changes

- Validated RSDP/RSDT/XSDT/FADT/DSDT discovery, bounded firmware mappings and
  fail-closed register decoding. A real PM-timer probe confirms advancement.
- New read-only `power-status` reports ACPI, fixed registers, timer and reset
  availability; unavailable battery, thermal and suspend capabilities remain
  explicit.
- UEFI prefers ACPI 2.0 independently of configuration-table order, rejects
  malformed advertised roots and copies only the validated RSDP length.
- Maintained QEMU and VMware diagnostic gates cover authenticated commands and
  invalid arguments on two boots. Installer evidence exports the canonical
  `core` profile for the UI's `basic` selection without relaxing its schema.
- Includes the signed offline legacy-recovery tooling merged after 0.11.3;
  see [recovery procedure](../operations/signed-offline-recovery.md).

No package ABI, handoff layout, dependency pin, trust anchor or disk-format
change. The immutable modules index remains `capyos-base-v3-r2`. No new OTA
bridge is required by this tag: the historical bridge belongs to 0.11.3.

## Validation

The pre-version-bump engineering slice passed 277 focused checks, ASan/UBSan,
the full host suite, layout/build and QEMU/VMware runtime gates. All three
loader installer regressions passed on the same development ISO. These results
are recorded in [11A evidence](../operations/etapa11-acpi-power-20261117.md)
and do not substitute for the final versioned artifact or public A/B gate.

The versioned local candidate passed `make release-check TOOLCHAIN64=elf`
(199 seconds), QEMU ACPI/CLI/persistence (138 seconds), fresh VMware Full
installation with modules/desktop/music/persistence (141 seconds, run
`b20751e50cc4`) and two subsequent VMware ACPI diagnostic boots (14 seconds).
QEMU without UART passed with the target disk unchanged (40 seconds).
VMware without UART also passed (67 seconds), preserving target and guard
disks; `build/ci/release-0.11.4-no-uart.manifest`. All three installer gates
used the same ISO hash below.
Logs/results: `build/etapa11-power/release-0.11.4-*.{json,log}`;
VMware installation: `build/ci/release-0.11.4-local-full.manifest`.
The retained local ISO is 24,788,992 bytes, SHA-256
`7e9367bf4d4f4fef8c28af7265d23718e0ca1a631491beae820cc1a83260b3b9`.
The CI will rebuild from the merged tag; public bytes need separate checks.

Release acceptance requires the final release-check, QEMU and VMware gates,
protected PR checks, authenticated draft, offline signatures, immutable signed
promotion, public-artifact verification, fresh VMware installation and the
production A/B cycle from the public 0.11.3 predecessor.

## Compatibility limits

Hyper-V Gen2 and physical hardware require their own device/runtime evidence.
This release does not certify them. Wi-Fi, battery/thermal providers, tray
integration and S3 resume remain open in Etapa 11. The follow-up branch will
prioritize hardware and Hyper-V compatibility as requested, retaining VMware
regression gates and explicit hardware targets.
