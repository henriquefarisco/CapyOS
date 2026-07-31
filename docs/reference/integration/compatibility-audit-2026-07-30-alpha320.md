# Compatibility audit — 2026-07-30 — alpha.320

## Snapshot

| Repository | Release | Contract consumed by CapyOS |
|---|---|---|
| CapyOS | `0.8.0-alpha.320+20260730` | `capyos-base` v3, `capyos-package-apply` v1 |
| CapyUI | `2.24.2` | `capy-ui-widget` 2.22, desktop-session v1, display-list schema 7 |
| CapyAI | `0.2.1` | `capy-ai-core` artifact v0, TaskPlan v1 host contract |
| CapyBrowser | `0.6.7` | `capy-browser-core` v1 |
| CapyCodecs | `0.0.12` | `capy-codec-image` v2 |
| CapyAgent | `0.0.10` | component-index v1, Ed25519 signer/verifier |
| CapyLang | `0.1.12` | host-only partial v0 |
| CapyBenchmark | `0.0.11` | host-only planned report v1 |

## Contract result

No external ABI changed. The only sibling pin move is CapyUI `2.24.1` to
`2.24.2`, a supply-chain-only patch whose workflow validates the exact tag,
`VERSION` and `PUBLISH_TAG`; its C payload contracts remain `capy-ui-widget`
2.22, desktop-session v1 and display-list schema 7. `capyos-base` remains v3,
`capyos-package-apply` remains v1 and the internal UEFI-to-kernel handoff remains
v10. This is a boot/install reliability release, not an ABI migration and not
the closure of Etapa 8.

The immutable modules-index contract for this release has exactly nine unique
HTTPS entries. Each entry carries an expected byte size and SHA-256 digest.
Publication acceptance must re-download the index from the release, compare it
byte for byte with the generated file and validate all nine payloads. That
post-publication proof is pending until the tag and release assets exist.

## Boot regression resolution

The `alpha.319` regression was caused by compiling the UEFI loader with the SysV
red zone enabled. Firmware and interrupt activity may overwrite the 128 bytes
below `RSP`; the loader used that area before kernel entry and corrupted its own
frame. The corrected loader is built without red zone, so every helper reserves
its own real stack frame. Inspection of the exact artifact with `objdump`
confirms the stack discipline.

The kernel load path now also validates ELF segments, copies each segment and
compares the destination byte for byte before transfer. The x86_64 entry path
reloads the complete GDT instead of relying on residual firmware state.

## Runtime evidence

| Gate or evidence | Result in 2026-07-30 |
|---|---|
| exact-artifact `objdump` stack inspection | **PASS** |
| ELF validation and copy/compare | **PASS** |
| complete GDT reload and kernel entry | **PASS** |
| focused KVM cycles | **PASS** — 2 cycles, 2 boots each, 4 boots total with persistence |
| official ISO KVM smoke | **PASS** — ISO install, installed-disk boot and persistence reboot; `marker:persist-ok` observed; runner reported `[ok] smoke x64 ISO install + persistence passed` |
| CLI TCG smoke (`make smoke-x64-cli`) | **PASS** — 86.2 s, 2 boots with persistence |
| official ISO TCG smoke | **PENDING** — not run yet |
| signed A/B preflight (`make smoke-x64-qemu-update-ab`) | **PENDING** — not run yet |
| official VMware signed A/B gate | **PENDING** |
| remote nine-module post-publish verification | **PENDING** — requires tag/assets |

KVM evidence and the independent CLI TCG smoke prove that the boot/install
regression is fixed across both accelerated and emulated CPU paths. They do not
substitute for the official ISO TCG smoke or the VMware + UEFI + E1000 A/B
acceptance gate. Etapa 8 therefore remains active and overall progress remains
7/16.

## Workflow and security posture

External actions are pinned to immutable SHAs:

| Action | Version | Commit |
|---|---|---|
| `actions/checkout` | 7.0.1 | `3d3c42e5aac5ba805825da76410c181273ba90b1` |
| `actions/setup-python` | 7.0.0 | `5fda3b95a4ea91299a34e894583c3862153e4b97` |
| `github/codeql-action` | 4.37.3 | `e4fba868fa4b1b91e1fdab776edc8cfbe6e9fb81` |
| `ossf/scorecard-action` | 2.4.4 | `2d1146689b8cda280b9bc96326124645441f03bc` |
| `softprops/action-gh-release` | 3.0.2 | `3d0d9888cb7fd7b750713d6e236d1fcb99157228` |

CodeQL alert #121 is actionable and has a local regression fixture; closure is
pending the remote scan. Scorecard findings #50 (`CodeReviewID`) and #49
(`CIIBestPracticesID`) concern external repository governance and are not code
defects closed by this release.

## Promotion decision

The compatibility snapshot is internally consistent and the original boot
blocker is resolved. Promotion remains conditional on all required release
gates completing, including the not-yet-run official ISO TCG smoke, the update
A/B and VMware acceptance gates, and the post-publication modules-index
verification. Until then, `alpha.320` is a release candidate and Etapa 8 stays
open.
