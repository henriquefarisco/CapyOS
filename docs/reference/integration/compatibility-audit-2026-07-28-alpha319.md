# Compatibility audit — 2026-07-28 — alpha.319

## Snapshot

| Repository | Release | Contract consumed by CapyOS |
|---|---|---|
| CapyOS | `0.8.0-alpha.319+20260728` | `capyos-base` v3, `capyos-package-apply` v1 |
| CapyUI | `2.24.1` | `capy-ui-widget` 2.22, desktop-session v1, display-list schema 7 |
| CapyAI | `0.2.1` | `capy-ai-core` artifact v0, TaskPlan v1 host contract |
| CapyBrowser | `0.6.7` | `capy-browser-core` v1 |
| CapyCodecs | `0.0.12` | `capy-codec-image` v2 |
| CapyAgent | `0.0.10` | component-index v1, Ed25519 signer/verifier |
| CapyLang | `0.1.12` | host-only partial v0 |
| CapyBenchmark | `0.0.11` | host-only planned report v1 |

## Contract result

No external ABI changed and no sibling pin moved. `capyos-base` remains v3,
`capyos-package-apply` remains v1 and the canonical `capy-ai-core` artifact
remains v0. The internal UEFI-to-kernel handoff stays at v10 with no new field:
the v8 prefix is still exactly 440 bytes, the v9 prefix 560, the attempt token 16
and the total 576, all enforced by compile-time assertions.

Additive internal surfaces only:

- `x64_storage_runtime_current_boot_attempt` — copies the already-validated
  attempt token so the boot log can state which slot is running.
- `x64_storage_boot_provider_reason_label` — stable ASCII label for the nine
  provider denial reasons.
- `struct system_update_status.rollback_applied` — distinguishes an observed
  loader rollback from "nothing pending" while keeping rc at 0.
- `http_parse_ipv4_literal` — module-internal dotted-quad fast path.

## Trust anchor posture

This is the security-relevant change of the release and it does not move the
production anchor.

- The production Ed25519 public key pinned in
  `src/services/update_agent_parse.c` is byte-identical to `alpha.318`
  (`be230bdd…ae6d`) and remains the only anchor any publishable artifact
  accepts.
- A build may override it with `CAPYOS_UPDATE_LAB_TRUST_KEY_HEX`, which exists so
  the automated Etapa 8 gate can sign a manifest without the offline private key.
  The same macro pins the catalog URL (`CAPYOS_UPDATE_LAB_MANIFEST_URL`) and
  relaxes `payload_url` to plain `http://`, because the kernel TLS stack always
  verifies the peer and a hermetic host endpoint cannot present a publicly
  trusted certificate. One switch controls all three relaxations, so there is no
  intermediate configuration.
- Fail-closed guarantees: malformed hex verifies nothing and never falls back to
  the release key; the kernel prints
  `[lab] update trust anchor overridden; kernel not for production` on every
  boot; `iso-uefi` refuses an ELF carrying that banner outside the smoke target;
  `release-check` refuses a release kernel carrying it; and the build-variant
  fingerprint invalidates kernel and userland objects when returning to the
  official configuration.
- The Python tooling mirrors the runtime exactly: `payload_url_prefixes(False)`
  is `("https://", "/system/update/")`, and `http://` requires the explicit
  `--allow-lab-http-payload-url` opt-in on both `build_update_manifest.py` and
  `verify_update_manifest.py`.

This anchor is unrelated to the capypkg publisher trust chain (CapyAgent signer /
CapyOS verifier), which stays fail-closed pending a production publisher key, and
unrelated to the release-checksum signing key used by `sign-release-checksums`.

## Etapa 8 status — criterion still OPEN

The last open acceptance criterion (signed HTTPS update applied to the inactive
slot with generational authorization and durable flush, UEFI attempt-token
consumption, post-reboot health confirmation and rollback) now has an automated
gate instead of a manual runbook, but **the gate has not produced evidence yet**,
so the criterion and the Etapa remain open.

The first real run of the gate exposed a kernel boot regression that precedes any
update logic: the loader completes `ExitBootServices` and transfers control (last
debugcon marker `J` in `efi_main.c`), and the kernel dies with
`#UD - Invalid Opcode` **before the first `dbgcon_putc('H')` in
`kernel_main64`**, with `RIP` inside the freshly copied `.rodata` segment
(`.rodata + 0x1B7`; observed segments: `.text` `0x10000000+0x175B42`, `.rodata`
`0x10176000+0x103A10`, `.data/.bss` `0x1027A000`). `_start` is transferring
control into data instead of calling `kernel_main64`.

This is not an artefact of the gate or of the lab anchor: `make smoke-x64-cli`
with the official build (no lab flags at all) fails identically, never emitting a
kernel marker. It is consistent with the `alpha.318` record, which explicitly
states that the QEMU/VMware installer gates were not re-run: that release made the
loader authoritatively A/B (handoff v10, attempt token, `boot_slot_store_arm`) and
never had a kernel boot validated at runtime. `alpha.317` was the last release with
green QEMU/VMware gates.

Blocking sequence: bisect the boot between `alpha.317` and the in-tree
`alpha.318` work using `make smoke-x64-cli`, resolve the faulting address with
`objdump`/`nm` against the exact failing build, fix it, and only then run the
signed A/B gate.

Once the boot is restored, `smoke-x64-vmware-update-ab` (official, VMware + UEFI +
E1000) and `smoke-x64-qemu-update-ab` (pre-flight) install the official ISO on a
blank disk and drive two complete cycles over four power cycles:

1. boot 1 — provider ready; signed manifest fetched over HTTP; payload verified
   by declared size and SHA-256; inactive slot written; one attempt armed.
2. boot 2 — loader spent the attempt; `update-rollback-check` reports the pending
   attempt; `update-confirm-health` commits durably; cycle two is armed.
3. boot 3 — the attempt is spent and deliberately left unconfirmed.
4. boot 4 — the loader restored the confirmed slot and published a ROLLBACK
   token; the updater reports the rollback and disarms the staged update.

The residual publish-side proof (an asset signed by the production offline key)
is key custody, not engineering, and is documented as an operator step in
`docs/operations/etapa-8-signed-update-playbook.md` §4.

## Corrections carried by this release

- `update-rollback-check` selected its `[ok]` line from `rc > 0`, which
  `update_agent_check_rollback` never returns, so the operator was told
  "no boot rollback pending" immediately after a real loader rollback. The
  durable outcome now drives both the line and the history event name.
- `http_get` routed every host through the DNS resolver, so a URL naming a bare
  IPv4 address failed with `HTTP_ERR_DNS` even though no resolution was needed.
- The help text of `update-prepare`, `update-stage` and `update-apply` still
  claimed the commands were unavailable pending persistent boot-slot writes,
  stale since `alpha.318` enabled them.
- The `alpha.317` audit and the master plan still stated that a `VALID` inactive
  slot is refused before any I/O. `alpha.318` added the durable invalidation
  transition (both control mirrors are committed to `FAILED` before any payload
  byte), so restaging a `VALID` slot is legal. Both documents are corrected.

## Required gates

| Gate | Result |
|---|---|
| `make test` | exit 0 |
| `make layout-audit` | exit 0, zero warnings |
| `make version-audit` | exit 0 |
| `make update-ab-selftest` | exit 0 |
| `make all64` + `make iso-uefi` (host) | exit 0 |
| `make test-http-url` | exit 0 (`[PASS] http_url`) |
| `make smoke-x64-qemu-update-ab` | **FAIL** — blocked by the kernel `#UD` above; the gate reached the installed boot, so the installer, signing, HTTP endpoint and ISO paths are proven |
| `make smoke-x64-cli` | **FAIL** — same `#UD` on the official build, isolating the regression from the gate |
| `make smoke-x64-vmware-update-ab` | not run — pointless until the boot is restored |
| `make release-check` (ELF) | not run — must not certify a kernel that does not boot |

## Gates not run in this session

- `make modules-index`: no publisher output and no external inventory changed, so
  the `alpha.317` aggregate still stands.
- `make smoke-x64-vmware-installer-wizard`: the installer path was not touched;
  the `alpha.317` evidence manifest remains valid for that criterion.

## Open external gates

- `make smoke-x64-iso-modules-net` stays blocked: the pinned modules index asset
  is still unpublished (HTTP 404). The pin remains at `alpha.318` because
  `src/config/first_boot/modules.c` was not changed; `version-audit` enforces
  that they agree.
- The CapyAgent external signer KAT and a production publisher public key remain
  pending, so signed capypkg publishers stay fail-closed.
- Publishing an update payload signed by the production offline key, then
  repeating the cycle on an official install, remains an operator step.
