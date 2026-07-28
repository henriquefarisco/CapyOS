# Compatibility audit — 2026-07-27 — alpha.318

## Snapshot

| Repository | Release | Contract consumed by CapyOS |
|---|---|---|
| CapyOS | `0.8.0-alpha.318+20260727` | `capyos-base` v3, `capyos-package-apply` v1 |
| CapyUI | `2.24.1` | `capy-ui-widget` 2.22, desktop-session v1, display-list schema 7 |
| CapyAI | `0.2.1` | `capy-ai-core` artifact v0, TaskPlan v1 host contract |
| CapyBrowser | `0.6.7` | `capy-browser-core` v1 |
| CapyCodecs | `0.0.12` | `capy-codec-image` v2 |
| CapyAgent | `0.0.10` | component-index v1, Ed25519 signer/verifier |
| CapyLang | `0.1.12` | host-only partial v0 |
| CapyBenchmark | `0.0.11` | host-only planned report v1 |

## Contract result

No external ABI changed. `capyos-base` remains v3, package apply remains v1 and
every sibling pin is unchanged. The canonical `capy-ai-core` artifact remains v0.

The internal UEFI-to-kernel handoff stays at v10. The v8 prefix remains exactly
440 bytes, the v9 prefix exactly 560 bytes, the attempt token exactly 16 bytes
and the total exactly 576 bytes; all four are enforced by compile-time
assertions. This release adds no handoff field.

## Correction to the alpha.317 audit

The alpha.317 audit stated that native durable flush did not exist and that the
update agent returned `-60` for every persistent transition. Both statements
were already stale relative to the tree at publication time and are corrected
here:

- Durable flush is implemented by all three native backends. NVMe issues the
  `FLUSH` command; AHCI and ATA-PIO issue `FLUSH CACHE` (`0xE7`) or
  `FLUSH CACHE EXT` (`0xEA`), with the capability discovered from IDENTIFY word
  83. `block_device_supports_flush` gates the provider, and every wrapper
  (offset, chunk, AES-XTS) only exposes a flush-capable ops table when the lower
  device has one.
- The UEFI loader already consumes the boot-slot control, spends the attempt
  durably and publishes the attempt token in the handoff.

## A/B lifecycle status

The lifecycle is now complete in production code:

- Staging authorizes against a full manager snapshot bound to generation,
  authority epoch and lease epoch, durably invalidates a `VALID` inactive slot
  before any payload I/O, then writes payload-first, flushes, re-reads every
  sector, re-derives SHA-256, validates the final padding and writes the header
  last.
- `boot_slot_store_arm` promotes the staged slot to pending with exactly one
  attempt, requires a provider-backed binding plus the exact lease and the exact
  generation produced by staging, and refuses a second arm.
- The loader spends the single attempt with a durable commit, validates the slot
  header against the manager and re-hashes the payload before transferring
  control; when the pending slot fails to load it re-selects and applies the
  rollback.
- The runtime confirms health only for the exact slot and generation carried by
  the handoff token, and reports rollback state instead of mutating metadata a
  second time.

`-60` is no longer an unconditional capability refusal. It now means exactly one
thing: the persistent transition cannot be proven — no registered provider with
durable flush, a stale generation or lease, an unverified payload, or an
indeterminate commit. `update_agent_apply_boot_slot` (digest-less) stays refused
by design.

Pre-`alpha.317` disks with duplicate GUIDs still enter through the validated
legacy identity path for persistent mount only and never receive a writable
update provider; migration remains a prerequisite for A/B apply on those disks.

## Required gates

- `make test`: passed, including the end-to-end A/B lifecycle over the
  provider-backed harness (stage, single-attempt arm, attempt consumption,
  durable confirmation and rollback of an unconfirmed attempt), the pure stage
  planner and the updater success/refusal paths.
- `make layout-audit`: passed with zero warnings after splitting
  `src/boot/boot_slot.c` into `src/boot/boot_slot_lifecycle.c`.
- `make version-audit`: passed.
- `make all64` and `make iso-uefi` with the default host toolchain: passed with
  zero warnings.
- `make release-check` (forces `TOOLCHAIN64=elf`): passed, including the host
  suite, both audits, self-tests, the ELF kernel, the UEFI loader, the manifest,
  the ISO and all five verified release checksums. Final ISO
  `9ae5f60f...0e47`.

## Gates not run in this session

- `make modules-index`: not rerun. No publisher output or external inventory
  changed in this release, so the aggregate from `alpha.317` still applies.
- QEMU and VMware installer regressions: not rerun after this ISO rebuild. The
  installer path is untouched by this release, but the artifacts changed, so a
  rerun is recommended before promotion.

## Open external gates

- Signed-payload update cycle on VMware UEFI/E1000: publish the signed payload
  for the tag, run fetch → download → prepare → apply, reboot, confirm health,
  then repeat without confirming to observe the loader-applied rollback. Not
  executed in this session; the Etapa 8 update criterion stays open.
- Networked modules index: the `alpha.318` asset is not published, so
  `make smoke-x64-iso-modules-net` remains blocked by an external precondition.
