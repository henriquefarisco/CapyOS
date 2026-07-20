# Compatibility audit — 2026-07-20

## Snapshot

| Repository | Release | Contract consumed by CapyOS |
|---|---|---|
| CapyOS | `0.8.0-alpha.316+20260720` | `capyos-base` v3, `capyos-package-apply` v1 |
| CapyUI | `2.24.1` | `capy-ui-widget` 2.22, desktop-session v1, display-list schema 7 |
| CapyAI | `0.2.1` | `capy-ai-core` artifact v0, TaskPlan v1 host contract |
| CapyBrowser | `0.6.7` | `capy-browser-core` v1 |
| CapyCodecs | `0.0.12` | `capy-codec-image` v2 |
| CapyAgent | `0.0.10` | component-index v1, Ed25519 signer/verifier |
| CapyLang | `0.1.12` | host-only partial v0 |
| CapyBenchmark | `0.0.11` | host-only planned report v1 |

## Contract result

No breaking ABI change was introduced. CapyAI advances from 0.2.0 to 0.2.1,
while the freestanding `capy-ai-core` artifact remains v0 and source-compatible
with the CapyOS adapter. CapyUI, CapyBrowser, CapyCodecs, CapyAgent, CapyLang and
CapyBenchmark remain at their previously validated releases.

CapyOS alpha.316 consumes immutable sibling tags through
`tools/scripts/release_siblings.env`. The generated aggregate module index has
nine unique entries and pins the CapyAI assistant package at 0.2.1. The index,
assistant payload and `modules.sha256` pass their integrity checks before
publication.

## Security and reliability findings closed

- CapyAI 0.2.1 uses leakage-free dataset groups and a strict mass-evaluation
  release gate with zero risk underclassification.
- Installer automation selects an exact target by capacity and `PathId`, then
  revalidates the identity before the destructive operation.
- A larger guard disk must remain byte-identical across installation and
  persistence boots.
- Serial and debug-console streams are evaluated independently, avoiding
  byte-interleaved false negatives during VM automation.
- Repeated identical target counts emitted by mirrored channels are accepted;
  conflicting counts remain fail-closed.
- Public evidence logs redact the recovery key and failed scratch disks stay
  confined to `build/ci` for diagnosis.

## Release gates

- CapyAI: complete release check, isolated package install, deterministic
  artifacts and the strict model campaign.
- CapyOS: host tests, layout/version audits, x86_64 build, UEFI ISO, release
  checksums, module-index integrity and QEMU multi-disk install/persistence.
- VMware Workstation: official UEFI/E1000 multi-disk evidence gate passed with
  target changed, guard byte-identical and persistence read after reboot.
- GitHub: CI, CodeQL, Security Hardening and Release Artifacts are monitored
  after publication; the release asset index is downloaded and revalidated.

## Open Etapa 8 work

- Persistent A/B apply, post-reboot health confirmation and rollback.
- Production publisher public-key pin and external Ed25519 known-answer test.
- Remaining external update and recovery matrices outside this installer slice.

Therefore this release closes the alpha.316 installer-evidence slice without
claiming the whole Etapa 8 complete.
