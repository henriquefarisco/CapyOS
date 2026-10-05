# Signed offline recovery

## Verified production run

2026-10-04, VMware run `8e6cdbede301`: public predecessor
`0.10.0+20260904` -> production-signed offline bridge `0.11.2+20261004` ->
public HTTPS Latest `0.11.3+20261004`. Five boots passed, including an
unconfirmed full-kernel rollback, verified-cache reapply, final durable health
confirmation and equal-version rejection. No lab trust override was present.

Evidence: `build/ci/release-0.11.3-offline-migration.manifest`,
`build/ci/smoke_x64_vmware_update_ab_8e6cdbede301.<phase>.log` and
`build/release-0.11.3-offline-migration-v3.log`.
Original image SHA-256:
`11b785a9c7ac3c53c58b242488a236b0be26b2c09967b1b64b054ce0b82c5994`.
DATA SHA-256 at offline cloning/staging:
`63848777bc0038dd354e153d11d2f93035b542593682f6d2e5fcf297a34dc918`.
The original was rehashed after the entire gate; all protected regions were
compared before booting the copy. Network/user settings persisted across boots.

Runs `f87fba65e646` and `a8c5f35fe335` are failed test-oracle runs, not acceptance:
the first expected a transient confirmation summary in update-status; the
second expected a nonexistent "Rollback pending: no" status line. Actual
receipts and source show confirmation succeeded. The corrected gate requires
the fresh durable commit receipt, active-slot health and absence of an armed
rollback in that command's complete response; 12 evidence regression tests pass.
Full host make test passed twice, including the final oracle. Eleven offline
image safety tests, seven bridge contract tests, strict layout/version audits
and installer disk selftests passed. The new host backend builds with warnings
as errors, without UNIT_TEST.

This accepts the offline bootstrap followed by production OTA, not a claim
that the old client's editor can perform an entirely online bridge bootstrap.

The 0.10.0 guest cannot reliably edit its root-owned update route. Its terminal
also truncates an Ed25519 signature line. Neither limitation authorizes weakening
the signature verifier or silently confirming a new kernel.

The Linux/WSL operator entry point tools/scripts/offline_recovery.py instead
authenticates both canonical published manifests with the fixed production
Ed25519 anchor and checks their immutable URLs, payload lengths and hashes.
It stages the bridge into a NEW disk copy using the same C BOOT slot/control
implementation as the loader. It arms exactly one pending boot attempt without
confirming health. DATA, ESP, GPT, the confirmed kernel and every byte outside
the inactive BOOT region and redundant control records must remain unchanged.

## Supported scope

- Powered-off local VMware VM with a single-extent FLAT VMDK source attached
  exactly once; no snapshots, sparse/split VMDK, VHD or physical disks.
- Canonical, non-overlapping GPT with matching primary/backup CRCs and unique
  disk/partition GUIDs. Legacy duplicate-GUID repair is not part of this tool.
- Healthy confirmed slot, no pending update/rollback, exact expected current
  version and verified confirmed payload SHA-256.
- Existing output directory with sufficient free space; output must not exist.
  Regular inputs only, no symlink or hard-linked source.
- Keep VMware stopped throughout recovery. Power-state/lock checks are guards,
  not a cross-process VMware startup inhibitor. All local VMware VMs must be off.

From the CapyOS repository in Linux/WSL:

```sh
make offline-recovery-host
python3 tools/scripts/offline_recovery.py \
  --source /path/to/target-flat.vmdk \
  --output /path/to/new-recovered-flat.vmdk \
  --vmx /path/to/source.vmx --vmrun /path/to/vmrun.exe \
  --bundle /path/to/public-release-assets \
  --helper "$PWD/build/offline-recovery-slot" \
  --current 0.10.0+20260904
```

The output is a raw flat extent, not an ISO or a standalone VMDK descriptor.
Original descriptor/VM configuration and original disk remain untouched. Retain
them as rollback material. Attach the copy with an appropriate new FLAT VMDK
descriptor only after checking the JSON evidence. Never delete the original
before boot, login, DATA persistence and update verification succeed.

The private offline-recovery-slot executable is not the authenticated operator
interface. It must not be invoked directly on the original image: it is the
bounded C backend for already-verified disposable copies.

## Acceptance gates

make test-offline-recovery checks the real canonical C backend with synthetic
images. Mock signatures in positive fixtures are test-only; the operator tool
has no test-key, signature bypass or automatic confirmation option. A negative
test executes the real verifier and rejects the unsigned fixture.

The Windows VMware production harness accepts --offline-recovery-wsl DISTRO
alongside all production bridge arguments. It installs the public predecessor,
configures its network, powers it off, runs recovery under the named WSL distro,
attaches only the new disposable copy, boots/confirms the bridge, then exercises
public HTTPS full-payload fetch, unconfirmed rollback, verified-cache reapply,
health confirmation and equal-version rejection. Its distinct offline evidence
format never claims the guest fetched or retired bridge.ini via Latest.

Compilation/host tests are not migration acceptance. Require the complete
production VM evidence manifest and serial logs before claiming support or
deploying this tool to a user's installed disk. No published immutable release,
trust anchor, on-disk protocol, runtime ABI or sibling pin is replaced.
