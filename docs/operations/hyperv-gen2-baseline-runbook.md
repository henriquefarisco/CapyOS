# Hyper-V Gen2 Baseline Runbook

This runbook defines the first lab gate for resuming the Hyper-V compatibility track without promoting Hyper-V to the official release platform.

## Scope

- Platform: `Hyper-V Generation 2` with UEFI boot.
- Status: laboratory compatibility track.
- Official release platform remains `VMware + UEFI + E1000`.
- Goal: collect enough boot, VMBus, input, storage and network evidence to decide the next focused Hyper-V slice.

## Host and VM preflight

1. Use a Generation 2 VM.
2. Disable Secure Boot unless the loader is signed for the selected template.
3. Use a fixed VHD attached through SCSI.
4. Use the synthetic `Network Adapter`; do not use a legacy NIC for Gen2.
5. Configure serial/COM capture when available.
6. Keep dynamic memory disabled for the first baseline pass.

On a Windows Hyper-V host, collect host-side evidence before boot when the PowerShell helper is available:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\scripts\hyperv_host_preflight.ps1 -Name CapyOSGen2 -IncludeEvents
```

## Build and provision

From the CapyOS workspace:

```bash
make all64 iso-uefi
make provision-vhd IMG='<fixed-vhd-path>'
make inspect-disk IMG='<fixed-vhd-path>'
```

`provision-vhd` must point at the same fixed VHD attached to the VM.

## Boot evidence to capture

Capture the full serial log from firmware handoff through login or fallback shell. The log must include, when present:

- `[vmbus]` contact/version negotiation lines;
- `[vmbus] OFFER` lines for `keyboard`, `netvsc` and `storvsc`;
- `[storvsc]` and `[storvsc-plan]` lines;
- `[netvsc]` runtime lines;
- boot warnings or fallback-mode messages.

## In-guest commands

After login or maintenance shell, run:

```text
runtime-native show
net-status
net-dump-runtime
recovery-storage
info
```

If the shell remains responsive, also run:

```text
runtime-native step
net-refresh
net-status
net-dump-runtime
```

## Baseline pass criteria

The first Hyper-V boot baseline passes when all of these are true:

- the VM reaches loader handoff and kernel boot without panic;
- the shell, login, or maintenance fallback is reachable;
- `runtime-native show` prints Hyper-V runtime state;
- `net-status` and `net-dump-runtime` report `vmbus=` and `stage=` fields;
- storage fallback is explicit: persistent volume, firmware storage, or ramdisk/recovery fallback;
- the VM does not reset spontaneously during command collection.

The baseline does not require NetVSC DHCP, StorVSC persistence, or VMBus keyboard promotion to be fully ready yet.

## Failure classification

| Symptom | Likely focus |
| --- | --- |
| No kernel handoff | UEFI loader, ESP layout, Secure Boot, VHD provisioning |
| `vmbus=off` | Hyper-V CPUID/MSR detection or VMBus prepare path |
| `vmbus=hypercall` or `synic` only | hypercall/SynIC setup and version contact |
| `vmbus=contact` without offers | `REQUESTOFFERS`, message wait budget, VMBus transport |
| offers visible but `stage=offers` | channel open and GPADL path |
| `stage=channel` or `control` | NetVSP/RNDIS or StorVSP control handshake |
| `stage=failed` | inspect `last_action`, `last_result`, `relid`, `connection_id` |
| shell input unavailable | EFI ConIn fallback, VMBus keyboard promotion gate, COM1 fallback |
| no persistence | VHD provisioning, firmware storage path, StorVSC gate |

## Required artifacts

Save these artifacts with the validation note:

- full serial boot log;
- host preflight output when available;
- `runtime-native show` output;
- `net-status` output;
- `net-dump-runtime` output;
- `recovery-storage` output;
- `make inspect-disk IMG=...` output.

## Host-side evidence check

After collecting the serial log and command outputs into one text file, run:

```bash
make hyperv-baseline-evidence HYPERV_BASELINE_LOG=build/hyperv-baseline.log
```

To persist a machine-readable summary for follow-up triage:

```bash
make hyperv-baseline-evidence HYPERV_BASELINE_LOG=build/hyperv-baseline.log HYPERV_BASELINE_SUMMARY=build/hyperv-baseline-summary.json
```

When serial, guest-command, disk-inspection and host-preflight outputs are
kept as separate files, place them in one directory and run:

```bash
make hyperv-baseline-evidence HYPERV_BASELINE_BUNDLE_DIR=build/hyperv-baseline-evidence
```

To make the captured `make inspect-disk IMG=...` output mandatory in that
same evidence bundle:

```bash
make hyperv-baseline-evidence HYPERV_BASELINE_LOG=build/hyperv-baseline.log HYPERV_BASELINE_REQUIRE_INSPECT_DISK=1
```

To make the captured `tools/scripts/hyperv_host_preflight.ps1` output
mandatory and verify the Gen2/no-secure-boot/no-dynamic-memory hints:

```bash
make hyperv-baseline-evidence HYPERV_BASELINE_BUNDLE_DIR=build/hyperv-baseline-evidence HYPERV_BASELINE_REQUIRE_HOST_PREFLIGHT=1
```

To require a minimum observed VMBus/runtime stage for a focused follow-up:

```bash
make hyperv-baseline-evidence HYPERV_BASELINE_LOG=build/hyperv-baseline.log HYPERV_BASELINE_MIN_VMBUS_STAGE=offers HYPERV_BASELINE_MIN_RUNTIME_STAGE=channel
```

This check is host-only: it does not build a new kernel, create an ISO, boot a VM or provision a VHD. It only verifies that the collected evidence contains the minimum baseline signals (`vmbus=`, `stage=`, runtime/native command output, storage/fallback evidence and no panic markers).

When `HYPERV_BASELINE_SUMMARY=...` is set, the JSON summary is the
audit record for CI and follow-up triage. For post-install kernel loader
failures, inspect these fields first:

- `summary_schema_version`: must be `2` or newer for the CI/kernel-loader
  triage fields below;
- `summary_schema_features`: stable feature names advertised by the summary,
  including `kernel-loader-triage`, `ci-acceptance-primary-reason` and
  `summary-schema-features-digest`;
- `summary_schema_features_sha256`: stable digest of `summary_schema_features`,
  suitable for detecting supported-feature contract changes in CI;
- `kernel_load_failure_markers`: stable marker codes such as
  `kernel-load-failed` and `kernel-manifest-entry-missing`;
- `kernel_load_failure_details`: source-aware entries with the combined
  text `line`, original `source`, `source_name`, `source_line` and
  `snippet`;
- `kernel_load_failure_refs`: portable entries with only `marker`,
  `source_name`, `source_line` and `snippet`;
- `kernel_load_failure_primary_ref`: the first portable kernel loader
  failure reference, so CI does not need to index
  `kernel_load_failure_refs[0]`;
- `kernel_load_failure_refs_sha256`: portable digest of
  `kernel_load_failure_refs`, suitable for comparing CI runs without
  depending on temporary paths;
- `kernel_load_failure_summary`: aggregate marker/source counts and the
  first source-local failure location;
- `kernel_load_failure_summary_sha256`: stable digest of
  `kernel_load_failure_summary`, suitable for comparing aggregate kernel
  loader failure shape across CI runs;
- `kernel_load_failure_triage`: host-only next-action hints, recommended
  evidence classes, evidence completeness and `blocking_reasons`;
- `kernel_load_failure_triage_sha256`: stable digest of
  `kernel_load_failure_triage`, suitable for comparing kernel-loader triage
  actions and blockers across CI runs;
- `ci_acceptance_reasons`: stable rejection reasons, including
  `kernel-loader:*` and `kernel-loader-triage:*`;
- `ci_acceptance_reasons_sha256`: stable digest of `ci_acceptance_reasons`,
  suitable for comparing CI rejection cause lists across runs;
- `ci_acceptance_primary_reason`: the first actionable CI reason, skipping
  the generic `validator-not-accepted` reason when a more specific failure is
  available;
- `ci_acceptance_primary_reason_sha256`: stable digest of
  `ci_acceptance_primary_reason`, suitable for comparing the first CI blocker;
- `ci_acceptance_primary_action`: stable host-only action mapped from the
  primary reason, such as `review-serial-kernel-loader-lines`;
- `ci_acceptance_primary_action_sha256`: stable digest of
  `ci_acceptance_primary_action`, suitable for comparing the first host-only
  triage action;
- `ci_acceptance_actions`: host-only actions deduplicated by action name and
  derived from all `ci_acceptance_reasons`, preserving the first source
  `reason` and `category`;
- `ci_acceptance_actions_sha256`: stable digest of `ci_acceptance_actions`,
  suitable for comparing CI triage output across runs;
- `ci_acceptance_action_names_sha256`: stable digest of only the action names,
  suitable for comparing triage flow changes while ignoring reason metadata;
- `ci_acceptance_action_summary`: aggregate counts, active categories and
  action names for `ci_acceptance_actions`;
- `ci_acceptance_action_summary_sha256`: stable digest of
  `ci_acceptance_action_summary`, suitable for comparing aggregate triage
  shape across CI runs;
- `ci_acceptance_reason_summary`: category counts for CI triage without
  parsing every reason string;
- `ci_acceptance_reason_summary_sha256`: stable digest of
  `ci_acceptance_reason_summary`, suitable for comparing aggregate rejection
  categories across CI runs.

Accepted evidence prints the observed VMBus/runtime stages and a `next=...`
recommendation plus `focus=baseline accepted`. Rejected evidence prints a
`focus=...` hint that names the first failed evidence class and, when the
summary contains one, an `[action] ...` line with the primary host-only CI
action, `[action-reason] ...` with the stable reason that produced it and
`[action-category] ...` with the stable reason category. Use `next=...` to
pick the next focused Hyper-V slice unless the raw serial log shows a clearer
earlier failure.

## Next slice decision

Use the first failing mandatory signal to choose the next implementation slice:

1. boot/VMBus contact;
2. VMBus offers/channel;
3. input promotion;
4. StorVSC storage;
5. NetVSC network.
