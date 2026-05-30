#!/usr/bin/env python3

"""Host-only self-test for the Hyper-V broad-hardware storage stack.

Slice 1 (ATA-PIO probe) invariants:

- ``ata_pio.o`` must be wired into the x86_64 kernel build so legacy IDE
  emulation (Hyper-V Generation 1, older QEMU/Bochs/VirtualBox legacy IDE
  machines, bare-metal ATA fallback) probes alongside NVMe/AHCI.
- ``include/drivers/storage/ata_pio.h`` must declare the public API used
  by the native storage runtime.
- ``X64_STORAGE_BACKEND_ATA_PIO`` must be appended to the backend enum
  (append-only contract).
- ``storage_runtime.c`` must report ``"ata-pio"`` for both the active
  backend name and the native candidate name.
- ``storage_runtime_native.c`` must include the ATA-PIO header AND probe
  ATA-PIO after AHCI fails or finds no device, never replacing the AHCI /
  NVMe path used by the VMware + UEFI + E1000 official track.

Slice 1 must keep VMware unaffected:

- AHCI and NVMe probe ordering must remain NVMe-first, AHCI-second.
- The ATA-PIO branch must run only if both NVMe and AHCI did not promote
  a device.
- StorVSC Slice 3 prep must wire the pure SCSI CDB, STORVSP I/O
  payload and GPA-direct envelope builders into both kernel and host tests.
- NetVSC/StorVSC VMBus rings must fit the current single-message GPADL
  limit without requiring runtime shrink/fallback.

This self-test is host-only and does not build the kernel or boot a VM.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
MAKEFILE = REPO_ROOT / "Makefile"
TEST_RUNNER = REPO_ROOT / "tests/test_runner.c"
ATA_HEADER = REPO_ROOT / "include/drivers/storage/ata_pio.h"
RUNTIME_HEADER = REPO_ROOT / "include/arch/x86_64/storage_runtime.h"
RUNTIME_IMPL = REPO_ROOT / "src/arch/x86_64/storage_runtime.c"
RUNTIME_NATIVE = REPO_ROOT / "src/arch/x86_64/storage_runtime_native.c"
STORVSC_SCSI_HEADER = REPO_ROOT / "include/drivers/storage/storvsc_scsi.h"
STORVSC_IO_HEADER = REPO_ROOT / "include/drivers/storage/storvsc_io.h"
STORVSC_BACKEND_HEADER = REPO_ROOT / "include/drivers/storage/storvsc_backend.h"
NETVSC_BACKEND_HEADER = REPO_ROOT / "include/drivers/net/netvsc_backend.h"
BLOCK_ERROR_HEADER = REPO_ROOT / "include/drivers/storage/block_error.h"
HYPERV_HEADER = REPO_ROOT / "include/drivers/hyperv/hyperv.h"
VMBUS_RING_HEADER = REPO_ROOT / "src/drivers/hyperv/internal/vmbus_ring.h"


def fail(message: str) -> int:
    print(f"[err] {message}", file=sys.stderr)
    return 1


def check_makefile_wiring(makefile: str) -> int:
    if (
        "$(BUILD)/x86_64/drivers/storage/ata_pio.o"
        not in makefile
    ):
        return fail("Makefile must build src/drivers/storage/ata_pio.c for x86_64")
    # Ensure existing VMware-track objects remain wired and StorVSC prep is built.
    for required in (
        "$(BUILD)/x86_64/drivers/storage/ahci.o",
        "$(BUILD)/x86_64/drivers/storage/efi_block.o",
        "$(BUILD)/x86_64/drivers/storage/storvsc_scsi.o",
        "$(BUILD)/x86_64/drivers/storage/storvsc_io.o",
    ):
        if required not in makefile:
            return fail(f"Makefile lost required storage object: {required}")
    if (
        "tests/drivers/test_storvsc_scsi.c "
        "src/drivers/storage/storvsc_scsi.c"
        not in makefile
    ):
        return fail("Makefile must run the host StorVSC SCSI CDB tests")
    if (
        "tests/drivers/test_storvsc_io.c "
        "src/drivers/storage/storvsc_io.c"
        not in makefile
    ):
        return fail("Makefile must run the host StorVSC I/O payload tests")
    if (
        "tests/drivers/test_vmbus_ring.c "
        "src/drivers/hyperv/vmbus_ring.c"
        not in makefile
    ):
        return fail("Makefile must run the host VMBus ring packet tests")
    return 0


def check_test_runner_wiring(test_runner: str) -> int:
    for symbol in (
        "run_vmbus_ring_tests",
        "run_storvsc_scsi_tests",
        "run_storvsc_io_tests",
    ):
        if f"int {symbol}(void);" not in test_runner:
            return fail(f"test_runner.c must declare {symbol}")
        if f"failures += {symbol}();" not in test_runner:
            return fail(f"test_runner.c must invoke {symbol}")
    return 0


def check_header_present() -> int:
    if not ATA_HEADER.is_file():
        return fail("include/drivers/storage/ata_pio.h must exist")
    text = ATA_HEADER.read_text(encoding="utf-8")
    for symbol in (
        "void ata_init(void);",
        "int ata_devices_count(void);",
        "struct block_device *ata_primary_device(void);",
    ):
        if symbol not in text:
            return fail(f"ata_pio.h must declare: {symbol}")
    return 0


def check_backend_enum(runtime_header: str) -> int:
    if "X64_STORAGE_BACKEND_ATA_PIO" not in runtime_header:
        return fail(
            "include/arch/x86_64/storage_runtime.h must define "
            "X64_STORAGE_BACKEND_ATA_PIO"
        )
    # Append-only: the existing four members must remain in order.
    expected_order = [
        "X64_STORAGE_BACKEND_NONE",
        "X64_STORAGE_BACKEND_EFI_BLOCK_IO",
        "X64_STORAGE_BACKEND_AHCI",
        "X64_STORAGE_BACKEND_NVME",
        "X64_STORAGE_BACKEND_ATA_PIO",
    ]
    positions = []
    for name in expected_order:
        idx = runtime_header.find(name)
        if idx < 0:
            return fail(f"Backend enum missing: {name}")
        positions.append(idx)
    if positions != sorted(positions):
        return fail(
            "Backend enum order must stay append-only "
            "(NONE, EFI_BLOCK_IO, AHCI, NVME, ATA_PIO)"
        )
    return 0


def check_runtime_impl(runtime_impl: str) -> int:
    if 'case X64_STORAGE_BACKEND_ATA_PIO:' not in runtime_impl:
        return fail(
            "storage_runtime.c must label X64_STORAGE_BACKEND_ATA_PIO in both "
            "backend_name and native_candidate_name switches"
        )
    if runtime_impl.count('return "ata-pio";') < 2:
        return fail(
            'storage_runtime.c must report "ata-pio" for backend_name AND '
            "native_candidate_name"
        )
    return 0


def check_native_probe(runtime_native: str) -> int:
    if '#include "drivers/storage/ata_pio.h"' not in runtime_native:
        return fail("storage_runtime_native.c must include drivers/storage/ata_pio.h")
    if "ata_devices_count()" not in runtime_native or "ata_primary_device()" not in runtime_native:
        return fail(
            "storage_runtime_native.c must call ata_devices_count() and "
            "ata_primary_device() during native probing"
        )
    if "X64_STORAGE_BACKEND_ATA_PIO" not in runtime_native:
        return fail(
            "storage_runtime_native.c must promote the ATA-PIO backend via "
            "X64_STORAGE_BACKEND_ATA_PIO"
        )

    nvme_idx = runtime_native.find("nvme_device_count")
    ahci_idx = runtime_native.find("ahci_device_count")
    ata_idx = runtime_native.find("ata_devices_count")
    if nvme_idx < 0 or ahci_idx < 0 or ata_idx < 0:
        return fail(
            "storage_runtime_native.c must call NVMe, AHCI and ATA-PIO probes"
        )
    if not (nvme_idx < ahci_idx < ata_idx):
        return fail(
            "Native probe order must remain NVMe -> AHCI -> ATA-PIO so the "
            "VMware + UEFI + E1000 track is unaffected"
        )
    return 0


def check_storvsc_scsi_builder() -> int:
    if not STORVSC_SCSI_HEADER.is_file():
        return fail("include/drivers/storage/storvsc_scsi.h must exist")
    text = STORVSC_SCSI_HEADER.read_text(encoding="utf-8")
    for symbol in (
        "storvsc_scsi_build_test_unit_ready",
        "storvsc_scsi_build_inquiry",
        "storvsc_scsi_build_read_capacity_16",
        "storvsc_scsi_build_rw10",
        "storvsc_scsi_build_rw16",
        "storvsc_scsi_parse_read_capacity_16",
    ):
        if symbol not in text:
            return fail(f"storvsc_scsi.h must declare {symbol}")
    return 0


def check_storvsc_io_builder() -> int:
    if not STORVSC_IO_HEADER.is_file():
        return fail("include/drivers/storage/storvsc_io.h must exist")
    text = STORVSC_IO_HEADER.read_text(encoding="utf-8")
    for symbol in (
        "storvsc_io_build_execute_srb",
        "storvsc_io_parse_completion",
        "storvsc_io_build_gpa_direct_packet",
        "storvsc_io_parse_gpa_direct_packet",
        "STORVSC_IO_GPA_DIRECT_MAX_PFNS",
        "struct storvsc_io_request",
        "struct storvsc_io_completion",
        "struct storvsc_io_gpa_direct_request",
        "struct storvsc_io_gpa_direct_info",
    ):
        if symbol not in text:
            return fail(f"storvsc_io.h must declare {symbol}")
    return 0


def check_scsi_error_classifier() -> int:
    text = BLOCK_ERROR_HEADER.read_text(encoding="utf-8")
    if "block_io_classify_scsi" not in text:
        return fail("block_error.h must expose the SCSI/SRB classifier")
    return 0


def check_vmbus_prebuilt_writer() -> int:
    hyperv = HYPERV_HEADER.read_text(encoding="utf-8")
    ring = VMBUS_RING_HEADER.read_text(encoding="utf-8")
    if "vmbus_channel_runtime_send_prebuilt" not in hyperv:
        return fail("hyperv.h must expose vmbus_channel_runtime_send_prebuilt")
    if "vmbus_write_prebuilt_packet_runtime" not in ring:
        return fail("vmbus_ring.h must expose vmbus_write_prebuilt_packet_runtime")
    return 0


def check_ring_single_gpadl(header: Path, macro_name: str) -> int:
    text = header.read_text(encoding="utf-8")
    match = re.search(
        rf"#define\s+{macro_name}\s+\((\d+)u\s*\*\s*1024u\)", text
    )
    if not match:
        return fail(f"{header.name} must define {macro_name} as KiB expression")
    per_ring_kib = int(match.group(1))
    total_pages = (per_ring_kib * 1024 * 2) // 4096
    if total_pages > 24:
        return fail(
            f"{macro_name} total send+recv pages must fit single GPADL header "
            f"(got {total_pages}, max 24)"
        )
    return 0


def main() -> int:
    makefile = MAKEFILE.read_text(encoding="utf-8")
    test_runner = TEST_RUNNER.read_text(encoding="utf-8")
    runtime_header = RUNTIME_HEADER.read_text(encoding="utf-8")
    runtime_impl = RUNTIME_IMPL.read_text(encoding="utf-8")
    runtime_native = RUNTIME_NATIVE.read_text(encoding="utf-8")

    for rc in (
        check_makefile_wiring(makefile),
        check_test_runner_wiring(test_runner),
        check_header_present(),
        check_backend_enum(runtime_header),
        check_runtime_impl(runtime_impl),
        check_native_probe(runtime_native),
        check_storvsc_scsi_builder(),
        check_storvsc_io_builder(),
        check_scsi_error_classifier(),
        check_vmbus_prebuilt_writer(),
        check_ring_single_gpadl(
            STORVSC_BACKEND_HEADER, "STORVSC_BACKEND_RING_BYTES"
        ),
        check_ring_single_gpadl(
            NETVSC_BACKEND_HEADER, "NETVSC_BACKEND_RING_BYTES"
        ),
    ):
        if rc != 0:
            return rc

    print("[ok] Hyper-V compatibility storage stack self-test passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
