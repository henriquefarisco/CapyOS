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

### Release-host CPU baseline correction

The first PR CI artifact exposed an existing release-environment regression:
the floating `ubuntu-latest` runner fetched GNU-EFI 4.0.0-1 from Ubuntu's
`amd64v3` archive. Its `_IPrint` contains SSE4.1 `pinsrq`, so QEMU's baseline
x86-64 CPU raised #UD before the installer started. The same fault was present
in the scheduled module-install run `43890142036` before this PR.
The exact failed PR ISO SHA-256 is
`9b8d0cd4ff450cdc43138d99254d2d94c06c077556db7d406525ab9efe349152`.
Local reproduction normalized RIP to PE offset `0x23c7d`; disassembly of that
exact loader identifies `_IPrint+0x4d`, `pinsrq`, not an ACPI parser fault.
Evidence is retained under `build/release-0.11.4/ci-premerge/`.

Image-producing CI/release jobs now pin Ubuntu 24.04, whose baseline GNU-EFI
does not impose the host's optional v3 ISA on the guest. Kernel, userland and
loader compilation explicitly select `-march=x86-64 -mtune=generic`.
Changing compiler flags alone cannot repair a precompiled v3 static library.
The QEMU CPU and acceptance conditions remain unchanged. Final verification
must use the rebuilt CI artifact, not the earlier local ISO above.
The complete local `release-check` passed again after the explicit ISA flags
(472 seconds; `release-0.11.4-baseline-check.json`).

For local builds, use baseline amd64 GNU-EFI libraries (not amd64v3 or
`-march=native` libraries). The kernel's minimum ISA is independent of the
development host. See [Ubuntu architecture variants](https://ubuntu.com/project/docs/how-ubuntu-is-made/concepts/supported-architectures/).

Hyper-V Gen2 and physical hardware require their own device/runtime evidence.
This release does not certify them. Wi-Fi, battery/thermal providers, tray
integration and S3 resume remain open in Etapa 11. The follow-up branch will
prioritize hardware and Hyper-V compatibility as requested, retaining VMware
regression gates and explicit hardware targets.
