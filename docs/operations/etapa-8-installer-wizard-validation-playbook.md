# Etapa 8 — installer wizard VMware validation

## Scope

This gate validates the official `VMware + UEFI + E1000` installer lifecycle.
It never reuses or edits an operator VM. A unique scratch VM, target VMDK and
guard VMDK are created under `build/ci/vmware-installer/<run-id>`.

The target is 2 GiB. The guard is a larger 3 GiB fixed VMDK with deterministic
sentinels. Both must be eligible. The harness selects the target by its unique
capacity, confirms the same `PathId`, sends the exact `ERASE` token, and hashes
the guest-visible flat extents while the VM is powered off.

## Prerequisites

- Windows host with VMware Workstation, `vmrun.exe`, `vmware-vdiskmanager.exe`
  and Python 3 available as `py.exe`.
- WSL build toolchain and the current CapyOS sibling workspace.
- No pre-existing files at the scratch paths. The operator's VM is irrelevant
  and must remain powered/owned independently.

## Command

Run through the workspace remote execution helper:

```bash
Automation/remote-exec.sh --bg -t 1800 \
  "wsl -e sh -lc \"cd '/mnt/c/Users/conta/Desktop/Devin Workspace/CapyOS' && make smoke-x64-vmware-installer-wizard TOOLCHAIN64=host\""
```

Optional arguments are passed through `SMOKE_X64_VMWARE_INSTALLER_ARGS`.
`--keep-vm` preserves a successful scratch VM for inspection. Failures always
preserve the scratch VM and disks.

## Required evidence

The gate only passes after:

1. a fresh ISO boot on EFI with an E1000 NIC;
2. exactly two eligible disks;
3. explicit target selection and matching `PathId` confirmation;
4. exact `ERASE` confirmation;
5. target flat-extent SHA-256 changes;
6. guard flat-extent SHA-256 remains identical after install and final boot;
7. interactive first boot, non-default login and persistent CAPYFS;
8. marker write, sync, reboot, second login and marker readback;
9. ordinary-user power denial from the shared persistence flow;
10. sanitized logs contain no recovery-key-shaped token.

Successful output includes:

```text
build/ci/installer-wizard-evidence.manifest
```

The manifest uses
`capyos-installer-wizard-evidence-manifest-v1`, includes only public artifact
names and SHA-256 values, and is rejected if target/guard invariants, required
booleans, field order or recovery-key declarations differ.

## Failure handling

On failure, the harness powers off only its scratch VM and preserves its unique
run directory and sanitized public logs. It never deletes or truncates an
existing VMDK/VMX. Recovery keys are held only in process memory and replaced
before logs are written publicly.

Do not upload a scratch directory, raw pipe transcript, `.vmem`, `.nvram`, VMDK
or unvalidated log. Public workflows must allowlist the evidence manifest and
sanitized logs rather than uploading all of `build/ci`.

## Acceptance boundary

A passing local Workstation run closes only the installer lifecycle evidence on
the official platform. It does not by itself prove a provisioned CI runner, a
signed update apply, public promotion evidence or A/B rollback. Those Etapa 8
criteria remain separate and fail closed.
