# Compatibility audit — 2026-07-15

## Snapshot

| Repository | Release | Contract consumed by CapyOS |
|---|---|---|
| CapyOS | `0.8.0-alpha.315+20260715` | `capyos-base` v3, `capyos-package-apply` v1 |
| CapyUI | `2.24.1` | `capy-ui-widget` 2.22, desktop-session v1, display-list schema 7 |
| CapyAI | `0.2.0` | `capy-ai-core` artifact v0, TaskPlan v1 host contract |
| CapyBrowser | `0.6.7` | `capy-browser-core` v1 |
| CapyCodecs | `0.0.12` | `capy-codec-image` v2 |
| CapyAgent | `0.0.10` | component-index v1, Ed25519 signer/verifier |
| CapyLang | `0.1.12` | host-only partial v0 |
| CapyBenchmark | `0.0.11` | host-only planned report v1 |

## Contract result

No breaking ABI change was introduced. CapyUI is a package patch: widget 2.22,
desktop-session v1 and schema 7 are unchanged. CapyAI advances to 0.2.0 because
it adds governed orchestration, but its freestanding model artifact remains
major 0 and source-compatible with the CapyOS parser.

CapyOS alpha.315 consumes the immutable tags `v2.24.1` and `v0.2.0` through
`tools/scripts/release_siblings.env`. File, desktop-app and power operations are
bound to typed adapters. Network, package, service, update, user and security
capabilities remain fail-closed.

## Security findings closed

- Persistent service workers start with no inherited authenticated principal.
- Legacy and typed background dispatch reject a missing session and bind the
  caller-owned sanitized snapshot for the operation only.
- First boot requires marker + user + config and cannot be silently completed by
  stale users/config alone.
- Installer writes require explicit target selection, exact `ERASE` and repeated
  preflight before wipe/GPT.
- Production ISO refuses preseeded admin setup and a leaked CapyAI smoke marker.
- Updater apply and health confirmation both return unsupported until persistent
  A/B boot control exists.
- GitHub Actions are pinned by SHA; CodeQL init/analyze/upload share 4.37.0.

## Gates

Required before tags:

- CapyAI: `make release-check`, task acceptance and benchmark.
- CapyUI: `make validate`, clean package, desktop syntax gate.
- CapyOS: host tests, focused CapyAI/capypkg tests, layout/version audits, full
  and core-only builds, ISO, installer smoke and CapyAI GUI smoke.
- GitHub: CI, CodeQL, Security/Scorecard, ABI Guard and release workflows.

## Open external gates

- VMware multi-disk installer proof with guard disk byte-identical.
- Persistent A/B apply, post-reboot health confirmation and rollback.
- Production publisher public-key pin and external Ed25519 known-answer test.
- Extended networked modules/VMware matrix for the governed CapyAI adapters.

Therefore Etapa 8 remains open and the release channel remains experimental.
