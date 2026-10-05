# Branch cleanup and Dependabot review - 2026-10-04

## Recoverable cleanup

Deleted seven retired feature/release branches locally and remotely. Before each
deletion, verified the current remote SHA equals the merged PR's head and its
merge commit is reachable from main. Remote deletion used an exact-SHA lease.
No unmerged work, open PR branch, main, develop, active recovery branch, tag or
immutable release was removed.

| Repository | Branches | Merged PRs | Verified backup SHA-256 |
| --- | --- | --- | --- |
| CapyOS | feature/etapa-10-audio-multimedia; fix/audio-release-payload-budget; fix/qemu-wav-capture-clock; release/v0.9.1; release/v0.9.1-trust-recovery | 75, 76, 77, 49, 50 | b422d835f2229e8c5758d350c699e0dd2f2f12d8101cbde194d3130ad6a912b7 |
| CapyUI | feature/etapa-10-audio-multimedia | 29 | 22a9c38e4424691d0ddc84dc177c0455eab9404b31cf5923bf23ff73f6682788 |
| CapyCodecs | feature/etapa-10-audio-multimedia | 4 | ec4fa8ea2ddd0101c94ee837e34e5017957eb852a4111890364121f2ec42d02a |

Backups are full-history Git bundles in CapyOS/build:
branch-cleanup-20261004.bundle, branch-cleanup-CapyUI-20261004.bundle and
branch-cleanup-CapyCodecs-20261004.bundle. Each passed git bundle verify.
To recover, fetch the desired refs/heads/BRANCH from its bundle into a new local
branch; remote publication is a separate operation.

## CapyOS Dependabot PRs: not merge-ready

Reviewed every changed file of PRs
[59](https://github.com/henriquefarisco/CapyOS/pull/59),
[60](https://github.com/henriquefarisco/CapyOS/pull/60),
[72](https://github.com/henriquefarisco/CapyOS/pull/72),
[73](https://github.com/henriquefarisco/CapyOS/pull/73) and
[74](https://github.com/henriquefarisco/CapyOS/pull/74).
Each changes one workflow action SHA only, not kernel code or workflow permissions.
Verified proposed SHAs against the official upstream GitHub tag objects (including
dereferencing annotated tags):

| PR | Action | Verified upstream commit | Result |
| --- | --- | --- | --- |
| 59 | softprops/action-gh-release v3.0.3 | efb35369e0ad2afab669f228072c1b0d510eae64 | Release gates fail; CodeQL/lint pass |
| 60 | step-security/harden-runner v2.21.1 | e14015d583714f6e62063499dc959a02595150a1 | Release gates fail; CodeQL/lint pass |
| 72 | github/codeql-action/init v4.38.2 | 2892aa5e19bbd11bc0cff5427e3b750a04d9e3c2 | Mixed CodeQL versions fail; release gates fail |
| 73 | github/codeql-action/analyze v4.38.2 | 2892aa5e19bbd11bc0cff5427e3b750a04d9e3c2 | Mixed CodeQL versions fail; release gates fail |
| 74 | github/codeql-action/upload-sarif v4.38.2 | 2892aa5e19bbd11bc0cff5427e3b750a04d9e3c2 | CodeQL/lint pass; release gates fail |

These are functional/pin-provenance checks, not an exhaustive security audit of
the actions' transitive dependencies or a successful release rehearsal.

Confirmed blockers:

1. Release sibling checkout clones the private CapyAI repository after
   CapyBenchmark. Dependabot-triggered jobs have no CAPYAI_DEPLOY_KEY available:
   gh secret list --app dependabot returned no configured names. Normal Actions
   secrets are not sufficient for this trigger. Failed logs from runs
   33892324207, 33892329763 and 37235372300 show HTTPS clone authentication
   failure, exit 128. This failure predates the latest CodeQL update.
2. Runs 37235371362 and 37235384505 explicitly reject a configuration created
   by CodeQL 4.38.2 when analyze runs 4.37.3, or the reverse. Init/analyze must
   move together; preferably group github/codeql-action/* updates.

No PR was merged, closed, rebased or commented on. Keep all five bot branches.
Before integration: authorize a dedicated read-only CapyAI credential for the
Dependabot context, coordinate CodeQL pins, then require fresh complete CI and
QEMU gates. Do not use pull_request_target with untrusted PR code to obtain
secrets; do not give the bot release signing/private storage keys.

CapyUI also has three open Dependabot PRs:
[22](https://github.com/henriquefarisco/CapyUI/pull/22),
[27](https://github.com/henriquefarisco/CapyUI/pull/27) and
[28](https://github.com/henriquefarisco/CapyUI/pull/28). All three diffs change
only the same upstream action SHAs verified above. PR 22's published checks
(validation, public ABI guard, CodeQL and hardened compile) pass, but predate
the latest main; this is not fresh integration evidence. PRs 27/28 fail with
the same split init/analyze version mismatch (run 36374931746 confirms it).
CapyCodecs has no open PRs. All CapyUI PRs/branches were preserved without merge.
