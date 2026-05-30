# Hyper-V compatibility stack plan

This document tracks the phased delivery of a complete Hyper-V
compatibility stack in CapyOS without impacting the official
`VMware + UEFI + E1000` validation track.

The Hyper-V track is a **laboratory compatibility track**. It does not
change the official release platform; release acceptance still requires
VMware + UEFI + E1000.

## Authority and scope

- Authority order: `docs/plans/active/capyos-master-plan.md`,
  `docs/plans/STATUS.md`, `docs/architecture/system-overview.md`.
- Driver source map: see the `capyos-drivers` skill.
- Storage and CAPYFS map: see the `capyos-storage-capyfs` skill.
- Baseline runbook for collecting Hyper-V evidence:
  `docs/operations/hyperv-gen2-baseline-runbook.md`.

## Components owned by this plan

| Component | Slice | Status |
|---|---|---|
| UEFI loader kernel base @ `0x10000000` for Hyper-V firmware | Slice 0 | delivered (`alpha.256+`) |
| Boot policy: maintenance only for real storage failure | Slice 0 | delivered (`alpha.256+`) |
| **ATA-PIO native probe (Hyper-V Gen1 / legacy IDE)** | **Slice 1** | **delivered (this change)** |
| **Wizard-in-RAM with loud persistence warning** | **Slice 2** | **delivered (this change)** |
| StorVSC SCSI I/O over VMBus (Hyper-V Gen2) | Slice 3 | first pure SCSI CDB builder delivered; data-plane still planned |
| NetVSC promotion to validated network runtime | Slice 4 | planned |
| VMBus keyboard promotion to runtime priority 1 | Slice 5 | planned |
| Hyper-V hot-plug device manager hooks | Slice 6 | planned |

## Slice 1 — ATA-PIO native probe (delivered)

### Goal

Hyper-V Generation 1 VMs attach the system disk through the synthetic
**IDE bus** (not SCSI/VMBus), exposed to the guest as legacy IDE on
ports `0x1F0`/`0x170`. CapyOS already implements an ATA-PIO driver at
`src/drivers/storage/ata_pio.c`, but until now it was unused by the
x86_64 native storage runtime.

This slice wires the driver as the third native candidate after NVMe
and AHCI, **strictly additive**: VMware NVMe/AHCI paths are unchanged.

### Files

- `include/drivers/storage/ata_pio.h` — public API.
- `src/drivers/storage/ata_pio.c` — unchanged driver implementation;
  `vga_write` already shimmed to `fbcon_print` via `arch/x86_64/stubs.c`.
- `include/arch/x86_64/storage_runtime.h` — `X64_STORAGE_BACKEND_ATA_PIO`
  appended to the enum.
- `src/arch/x86_64/storage_runtime.c` — `"ata-pio"` reported for both
  active backend and native candidate name.
- `src/arch/x86_64/storage_runtime_native.c` — probes ATA-PIO after
  AHCI fails or finds no device.
- `Makefile` — `$(BUILD)/x86_64/drivers/storage/ata_pio.o` added.

### Probe order

1. NVMe (`nvme_init` + `nvme_get_block_device(0)`)
2. AHCI (`ahci_init` + `ahci_get_block_device(0)`)
3. ATA-PIO (`ata_init` + `ata_primary_device`)

ATA-PIO is reached only when NVMe and AHCI both fail to produce a
DATA-bearing block device, so the official VMware track is never
affected.

### Validation

- Host self-test: `tools/scripts/test_hyperv_compat_storage_stack.py`
  (Makefile rule wires it through `hyperv-baseline-evidence-selftest`).
- External runtime gate: `make all64` to confirm the kernel still links
  on x86_64 with the new object.
- External smoke: `make smoke-x64-hyperv-boot IMG=...` followed by the
  in-guest commands listed in
  `docs/operations/hyperv-gen2-baseline-runbook.md`. On Gen1 the
  expected outcome is `runtime-native show` reporting
  `backend=ata-pio` and the wizard completing into persistent storage.

## Slice 2 — Wizard-in-RAM with loud persistence warning (delivered)

### Problem this fixes

Before this change, the boot policy forced **maintenance mode** when
`g_shell_persistent_storage` was 0. On Hyper-V Gen2 (where StorVSC I/O
is not yet wired) this trapped users in maintenance with no way to
reach the first-boot wizard.

### New policy

- `prepare_shell_runtime` always returns a usable filesystem (real or
  RAM).
- When `x64_storage_runtime_has_device()` is false (no native backend
  promoted), the kernel keeps `validated_storage_ready = 1` but emits a
  loud boot warning:
  `"Persistent storage unavailable; configuration will NOT survive reboot"`.
- When mount/unlock of the discovered backend failed and the recovery
  RAM fallback took over, the kernel emits:
  `"Persistent volume not mounted; running in RAM (no persistence)"`.
- When `shell_runtime_rc != 0` (true storage runtime failure), the boot
  policy still falls to maintenance.

This trades the previous fail-closed behavior for a fail-degraded UX
that lets the user complete the wizard and operate the system while a
persistent backend is being prepared.

The official VMware + UEFI + E1000 track always promotes a real
NVMe/AHCI block device, so neither degraded branch is exercised there.

### Files

- `src/arch/x86_64/kernel_main.c` — boot policy block at the start of
  stage 8/8.
- `src/arch/x86_64/kernel_shell_runtime.c` — diagnostic message clarifies
  that persistent storage is needed for setup completion.

### Validation

- Host self-test: `tools/scripts/test_hyperv_persistent_boot_policy.py`
  (now asserts both the new policy and the absence of the legacy
  fail-closed override).

## Slice 3 — StorVSC SCSI I/O over VMBus (planned)

### Goal

Make Hyper-V Generation 2 VMs reach **real persistent storage** by
implementing the SCSI I/O path on top of the existing StorVSC
control-plane runtime (`src/drivers/storage/storvsc_*.c`).

### Current state

The existing StorVSC implementation reaches `STORVSC_BACKEND_READY`
after:

1. `STORVSC_BACKEND_PROBE` — VMBus offer query.
2. `STORVSC_BACKEND_CHANNEL` — channel open + GPADL handshake.
3. `STORVSC_BACKEND_CONTROL` — STORVSP version + properties negotiation.

The runtime then exposes only **status getters** (offer relid,
connection id, phase name). It does **not** implement the SCSI I/O
data plane and never produces a `struct block_device`. The native
storage runtime today consumes only NVMe/AHCI/ATA-PIO block devices.

The first Slice 3 preparatory cuts now provide
`include/drivers/storage/storvsc_scsi.h` +
`src/drivers/storage/storvsc_scsi.c`, a pure SCSI CDB builder/parser for
TEST UNIT READY, INQUIRY, READ CAPACITY(16), READ/WRITE(10) and
READ/WRITE(16), covered by `tests/drivers/test_storvsc_scsi.c`. It does
not submit I/O yet; it locks the command encoding required by the
future VMBus packet layer. `block_io_classify_scsi()` extends the
unified storage retry vocabulary to SCSI/SRB completions without adding
runtime MMIO.

`include/drivers/storage/storvsc_io.h` +
`src/drivers/storage/storvsc_io.c` then lock the first host-testable
STORVSP `EXECUTE_SRB` request/completion layout used by the future
VMBus packet layer. This TU still performs no ring writes and owns no
DMA buffers; it only encodes the 64-byte STORVSP packet and parses
completion status into the unified block error vocabulary. It also
constructs/parses the first single-range VMBus GPA-direct packet
envelope around that STORVSP packet so the future runtime sender can
wire the bytes into `vmbus_ring.c` without changing command layout.
`vmbus_write_prebuilt_packet_runtime()` and
`vmbus_channel_runtime_send_prebuilt()` provide the reusable ring-write
bridge for those prebuilt GPA-direct packets while preserving the
existing ring trailer and signaling behavior.

### Target architecture

| Layer | New TU | Responsibility |
|---|---|---|
| SCSI CDB builder | `src/drivers/storage/storvsc_scsi.c` | TEST UNIT READY, INQUIRY, READ CAPACITY (16), READ (10/16), WRITE (10/16). |
| VMBus packet construction | `src/drivers/storage/storvsc_io.c` | Build STORVSP I/O payloads, parse completion correlation/status, and construct single-range GPA-direct packet envelopes for 4 KiB physically-contiguous buffers. |
| VMBus ring send bridge | `src/drivers/hyperv/vmbus_ring.c` | Write prebuilt GPA-direct packets to the channel ring with the existing trailer and signaling discipline. |
| Block device facade | `src/drivers/storage/storvsc_block.c` | `struct block_device_ops` implementation; sync read/write wrappers that drive the async VMBus path through `storvsc_io_step`. |
| Native runtime adapter | extension to `src/arch/x86_64/storage_runtime_native.c` | After `STORVSC_BACKEND_READY`, ask `storvsc_block_promote()` to produce a `block_device` and feed it into `probe_native_storage_backend_from_raw` with `X64_STORAGE_BACKEND_STORVSC`. |
| Public header | `include/drivers/storage/storvsc_block.h` | `storvsc_block_init`, `storvsc_block_promote`, `storvsc_block_status`. |

### Data-plane invariants

- DMA buffers come from `pmm_alloc_pages` / `kmalloc_aligned(4096)`;
  never use stack buffers for VMBus payloads.
- GPA descriptor lists use the physical address of the kernel-managed
  buffer; the kernel runs identity-paged for the lower 4 GiB so virtual
  == physical for the kernel image range (loader contract preserved).
- The first GPA-direct helper supports one range and up to 16 PFNs;
  the initial runtime path should feed it a 4 KiB page-aligned buffer
  so each I/O uses one PFN.
- Prebuilt GPA-direct packets are already fully descriptor-prefixed;
  the ring bridge must append only the VMBus trailer and must not wrap
  them in a second inband descriptor.
- One in-flight I/O per LUN initially; upgrade to ring-buffered
  in-flight tracking once a focused smoke validates the single-LUN
  path.
- Current VMBus channels use 48 KiB send + 48 KiB receive rings so the
  total 24 pages fit the guest's current single-message GPADL header
  implementation without runtime shrink/fallback. Larger rings require
  GPADL_BODY multi-message support first.
- Synchronous wrapper polls the VMBus ring with a bounded wait; on
  timeout, the wrapper returns `BLOCK_IO_ERR_TIMEOUT` and the upper
  layer retries through `block_device_read`/`_write`.
- Driver failure must never `panic()`; on persistent I/O failure the
  block device disappears and the system falls to RAM with the loud
  warning from Slice 2.

### Backend enum

Append `X64_STORAGE_BACKEND_STORVSC` to `enum x64_storage_backend`
(append-only contract; existing members keep their values).

### Validation plan

- Host: `tools/scripts/test_hyperv_compat_storage_stack.py` covers
  the new TUs; focused unit tests under `tests/drivers/test_storvsc_scsi.c`
  and `tests/drivers/test_storvsc_io.c` exercise SCSI CDB encoding,
  STORVSP payload construction/completion parsing, GPA-direct envelope
  layout, prebuilt VMBus ring writes and timeout classification behavior.
- External runtime gate: `make all64` after the new TUs are added.
- External smoke: a new `make smoke-x64-hyperv-gen2-storage IMG=...`
  gate that boots a Hyper-V Gen2 VM with a fixed VHD attached via
  SCSI and verifies that `runtime-native show` reports
  `backend=storvsc` and that `recovery-storage` reports a healthy
  CAPYFS mount over the synthetic disk.

### Out of scope for Slice 3

- Async multi-queue I/O.
- Hot-plug attach/detach of additional SCSI LUNs.
- Pass-through SCSI for CD/DVD.
- Trim / UNMAP / VSS quiesce hooks.

These are tracked for later slices and must not block Slice 3
acceptance.

## Slice 4-6 placeholders

- **Slice 4**: NetVSC promotion. Today the `netvsc*` files exist but
  the synthetic NIC does not reach `NET_STATUS_READY`. Wiring it to
  `net_status.runtime_supported = 1` requires the same kind of
  data-plane implementation as Slice 3 (RNDIS over VMBus). VMware
  E1000 path stays authoritative.

- **Slice 5**: VMBus keyboard. Already in the input runtime priority
  chain at priority 3 (`capyos-boot-uefi` skill describes the order).
  Promoting it to priority 1 on Hyper-V hosts requires a closed
  external gate similar to the USB HID slice 3E.

- **Slice 6**: Device manager hooks for Hyper-V hot-plug. The planned
  unified device manager (`capyos-drivers` skill) must own attach /
  detach lifecycle for VMBus offers. Until then, offered channels are
  enumerated only at boot.

## Cross-repo impact

This plan touches only CapyOS-owned code. No sister repo contract
changes are required:

- `CapyUI`, `CapyAgent`, `CapyBrowser`, `CapyCodecs`, `CapyLang`,
  `CapyBenchmark` are untouched.
- `services/capypkg` adapter is untouched.
- The activation gate `kernel/module_gate.c` is untouched.
- The first-boot wizard schema is untouched.

If a future slice changes a sister-repo-visible ABI, follow the
`cross-repo-contract-sync` workflow.

## References

- Hyper-V baseline runbook: `docs/operations/hyperv-gen2-baseline-runbook.md`.
- Source layout: `docs/architecture/source-layout.md`.
- Compatibility matrix: `docs/reference/integration/compatibility-matrix.md`.
- Etapa 3 driver foundation plan: `docs/architecture/etapa-3-driver-foundation-plan.md`
  (closed in `alpha.253`, frames the broader driver discipline this
  Hyper-V track follows).
