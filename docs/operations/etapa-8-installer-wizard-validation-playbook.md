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

- Windows host with VMware Workstation, `vmrun.exe`, `vmware-vdiskmanager.exe`,
  `vmcli.exe` and Python 3 available as `py.exe`.
- WSL build toolchain and the current CapyOS sibling workspace.
- No pre-existing files at the scratch paths. The operator's VM is irrelevant
  and must remain powered/owned independently.

## Command

To rebuild and test the current tree, run from WSL:

```bash
make smoke-x64-vmware-installer-no-uart TOOLCHAIN64=elf
make smoke-x64-vmware-installer-wizard TOOLCHAIN64=elf
```

Optional arguments are passed through `SMOKE_X64_VMWARE_INSTALLER_ARGS`.
`--keep-vm` preserves a successful scratch VM for inspection. Failures always
preserve the scratch VM and disks.

For a release candidate or an ISO downloaded from a draft, do not use a target
that depends on `iso-uefi`: that target is phony and rebuilds the image. Run the
two exact-artifact gates instead:

```bash
make smoke-x64-vmware-installer-no-uart-existing-iso \
  EXISTING_ISO=build/CapyOS-Installer-UEFI.iso
make smoke-x64-vmware-installer-wizard-existing-iso \
  EXISTING_ISO=build/CapyOS-Installer-UEFI.iso
```

Record the SHA-256 before both commands and after the second command. Promotion
evidence is valid only when QEMU, VMware without UART and the complete VMware
wizard name the same digest. The publish workflow builds its own ISO; therefore
the final promotion must repeat these commands against the ISO downloaded from
the draft, not infer equivalence from a local build.

## VMware without UART

`smoke-x64-vmware-installer-no-uart` is complementary to the complete wizard.
It creates a separate UEFI VM with Secure Boot, networking and every
`serialN.*` device disabled. A temporary unauthenticated RFB listener exists
only on `127.0.0.1` for the duration of the gate.

The no-UART gate requires an eight-second byte-identical idle prompt, sends
exactly `0` down/up, verifies that only the input field changed, waits another
two seconds without phantom input, sends Return down/up and accepts only the
installer's defined cancellation path back to VMware firmware. Target and guard
VMDKs must remain byte-identical. Exact RFB frames, sanitized VMX/logs, runtime
serial query, VMware version and ISO hashes are stored under `build/ci`.

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

The no-UART harness also queries `vmrun list` after teardown and fails if its
exact disposable VM remains active. It never stops another listed VM.

Do not upload a scratch directory, raw pipe transcript, `.vmem`, `.nvram`, VMDK
or unvalidated log. Public workflows must allowlist the evidence manifest and
sanitized logs rather than uploading all of `build/ci`.

## Acceptance boundary

A passing local Workstation run closes only the installer lifecycle evidence on
the official platform. It does not by itself prove a provisioned CI runner, a
signed update apply, public promotion evidence or A/B rollback. Those Etapa 8
criteria remain separate and fail closed.
