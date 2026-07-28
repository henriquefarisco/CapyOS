# Compatibility audit — 2026-07-20 — alpha.317

## Snapshot

| Repository | Release | Contract consumed by CapyOS |
|---|---|---|
| CapyOS | `0.8.0-alpha.317+20260720` | `capyos-base` v3, `capyos-package-apply` v1 |
| CapyUI | `2.24.1` | `capy-ui-widget` 2.22, desktop-session v1, display-list schema 7 |
| CapyAI | `0.2.1` | `capy-ai-core` artifact v0, TaskPlan v1 host contract |
| CapyBrowser | `0.6.7` | `capy-browser-core` v1 |
| CapyCodecs | `0.0.12` | `capy-codec-image` v2 |
| CapyAgent | `0.0.10` | component-index v1, Ed25519 signer/verifier |
| CapyLang | `0.1.12` | host-only partial v0 |
| CapyBenchmark | `0.0.11` | host-only planned report v1 |

## Contract result

No external ABI changed. `capyos-base` remains v3, package apply remains v1,
all sibling pins remain unchanged and the canonical `capy-ai-core` artifact
remains v0.

The internal UEFI-to-kernel handoff advances from v8 to v9 append-only. The v8
prefix remains exactly 440 bytes; the v9 structure is 560 bytes and is guarded
by compile-time offsets/sizes. A v9 loader fully zeroes the handoff and appends a
versioned GPT disk identity containing ESP, BOOT and DATA ranges plus disk and
partition GUIDs.

## Reliability and migration findings

- The primary GPT is parsed canonically and the backup header/array must match
  its geometry, CRC and every array byte; a colliding-CRC divergent array is
  rejected. Types/names, attributes, bounds and overlap remain strict.
- New installs generate nonzero pairwise-unique disk/partition GUIDs and fail
  installation if generation cannot produce a valid set.
- UEFI binds raw, boot ESP and logical DATA to the same physical Device Path
  prefix, so a cloned GUID/LBA on another disk cannot replace the origin.
  Destructive Block I/O honors `IoAlign`; every `FlushBlocks` failure aborts
  before success/reboot. Manifest binding avoids type-punning and selection
  requires one unambiguous result.
- Native NVMe/AHCI/ATA enumeration also requires exactly one matching identity
  and revalidates the complete fingerprint before constructing a DATA slice.
- Existing pre-alpha.317 disks with duplicate GUIDs use an explicit validated
  legacy mode for persistent mount only. That mode cannot create a writable
  update provider and remains pending migration before A/B apply.
- Disks beyond the current 32-bit sector ABI are rejected by installer policy.

## A/B foundation status

The redundant boot-control record, fixed A/B BOOT geometry, inactive payload
store and strict raw block provider are host-tested. Payload staging accepts
only an inactive `EMPTY`/`FAILED` slot and uses payload-first, flush, full
SHA-256 readback and header-last ordering; restaging a `VALID` slot is refused
before I/O until durable invalidation exists. Handoff flags are a closed set and
the UEFI disk discriminator validates the BOOT manifest structure. Runtime
retains a strictly verified raw device and DATA binding after persistent mount.

Production capability remains fail-closed because native durable flush is not
implemented. The update agent continues returning `-60` for prepare/stage/arm,
apply, health confirmation and rollback. No Etapa 8 update criterion is closed.

## Required gates

- `make release-check TOOLCHAIN64=elf`: passed, including host suite, layout and
  version audits, x86_64 build, UEFI ISO and five release checksums.
- QEMU installer regression with fresh alpha.317 GPT identity: passed on the ELF
  ISO with target changed, guard intact and persistence read after reboot.
- Official VMware UEFI/E1000 multi-disk installer regression: passed on the same
  ELF ISO (`98ef6215...e053`) with target changed, guard byte-identical and
  persistence read after reboot.
- Aggregate modules index: nine entries including CapyAI.
- The networked module-download gate remains blocked: the alpha.317 asset URL
  returns 404 until publication and must be rerun afterward.
- Future A/B power-cut, health and rollback matrices remain open.

## Open Etapa 8 work

- Durable native flush/barrier capability.
- Production runtime registration of the raw BOOT provider.
- UEFI slot-control consumption and boot attempt token in handoff.
- Generation-bound staging authorization plus signed inactive-slot apply,
  post-reboot health and rollback evidence.
- Transactional migration for legacy duplicate-GUID installations before they
  become update-capable.
- Production publisher public-key pin and external Ed25519 known-answer test.
