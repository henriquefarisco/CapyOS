#!/usr/bin/env python3

"""Host-only self-test for the Hyper-V hybrid firmware runtime invariants.

This guards the installed Hyper-V boot path where the native StorVSC/NetVSC
data paths are not ready yet:

- The UEFI loader may keep Boot Services active only on Microsoft Hyper-V and
  only when a persistent DATA partition was found.
- That hybrid handoff must preserve firmware BlockIO and mark firmware input,
  firmware block I/O, Boot Services active and hybrid boot.
- The EFI Simple Network Protocol backend must be wired before the PCI probe
  so first boot and maintenance can use Hyper-V firmware networking.
- Maintenance mode must start networkd so the stack is attempted there too.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
EFI_MAIN = REPO_ROOT / "src/boot/uefi_loader/efi_main.c"
KERNEL_MAIN_C = REPO_ROOT / "src/arch/x86_64/kernel_main.c"
SERVICE_MANAGER = REPO_ROOT / "src/services/service_manager.c"
MAKEFILE = REPO_ROOT / "Makefile"
NET_PROBE_H = REPO_ROOT / "include/drivers/net/net_probe.h"
NET_PROBE_C = REPO_ROOT / "src/drivers/net/net_probe.c"
STACK_DRIVER_C = REPO_ROOT / "src/net/core/stack_driver.c"
STORAGE_RUNTIME_C = REPO_ROOT / "src/arch/x86_64/storage_runtime.c"
EFI_SNP_H = REPO_ROOT / "include/drivers/net/efi_snp.h"
EFI_SNP_C = REPO_ROOT / "src/drivers/net/efi_snp.c"


def fail(message: str) -> int:
    print(f"[err] {message}", file=sys.stderr)
    return 1


def check_loader_hybrid_handoff(efi_main: str) -> int:
    if "static BOOLEAN uefi_loader_running_on_hyperv(void)" not in efi_main:
        return fail("UEFI loader must detect the Microsoft Hyper-V CPUID vendor")
    if "'M' && vendor[1] == 'i'" not in efi_main or "vendor[11] == 'v'" not in efi_main:
        return fail("UEFI Hyper-V detection must match the Microsoft Hv vendor")

    hybrid_gate = re.compile(
        r"hyperv_hybrid_requested\s*=\s*\(\s*"
        r"uefi_loader_running_on_hyperv\(\)\s*&&\s*runtime_disk\s*&&\s*"
        r"runtime_data_count\s*!=\s*0\s*\)",
        re.DOTALL,
    )
    if not hybrid_gate.search(efi_main):
        return fail(
            "Hybrid handoff must require Hyper-V, a runtime disk and a DATA partition"
        )

    start = efi_main.find("if (hyperv_hybrid_requested) {\n      UINTN got")
    end = efi_main.find("if (attempt == 0)", start)
    if start < 0 or end < 0:
        return fail("UEFI loader must have a dedicated hybrid GetMemoryMap path")
    hybrid_block = efi_main[start:end]
    for required in (
        "BootServices->GetMemoryMap",
        "boot_services_kept_active = TRUE;",
        "exited_boot_services = TRUE;",
    ):
        if required not in hybrid_block:
            return fail(f"Hybrid path must contain: {required}")
    if "BootServices->ExitBootServices" in hybrid_block:
        return fail("Hybrid path must not call ExitBootServices in the loader")

    flags_match = re.search(
        r"handoff->runtime_flags\s*=\s*boot_services_kept_active\s*\?"
        r"\s*\((.*?)\)\s*:\s*0;",
        efi_main,
        re.DOTALL,
    )
    if not flags_match:
        return fail("Handoff runtime flags must be conditional on boot_services_kept_active")
    flags = flags_match.group(1)
    for flag in (
        "BOOT_HANDOFF_RUNTIME_BOOT_SERVICES_ACTIVE",
        "BOOT_HANDOFF_RUNTIME_FIRMWARE_INPUT",
        "BOOT_HANDOFF_RUNTIME_FIRMWARE_BLOCK_IO",
        "BOOT_HANDOFF_RUNTIME_HYBRID_BOOT",
    ):
        if flag not in flags:
            return fail(f"Hybrid handoff must set {flag}")

    block_io_patterns = (
        r"handoff->efi_block_io\s*=\s*boot_services_kept_active\s*\?"
        r"\s*\(UINT64\)\(UINTN\)runtime_disk\s*:\s*0;",
        r"handoff->efi_block_io_raw\s*=\s*boot_services_kept_active\s*\?"
        r"\s*\(UINT64\)\(UINTN\)runtime_disk_raw\s*:\s*0;",
    )
    for pattern in block_io_patterns:
        if not re.search(pattern, efi_main, re.DOTALL):
            return fail(
                "Hybrid handoff must preserve EFI BlockIO pointers only while Boot Services are active"
            )

    return 0


def check_kernel_keeps_firmware_descriptors(kernel_main_c: str) -> int:
    if "x64_platform_tables_prepare_bridge()" in kernel_main_c:
        return fail(
            "Hybrid firmware-runtime boot must not install the kernel bridge IDT before EFI calls"
        )
    if "Hyper-V\n     firmware BlockIO/SNP" not in kernel_main_c:
        return fail(
            "kernel_main must document why firmware descriptors stay active during hybrid boot"
        )
    native_tables = re.compile(
        r"if\s*\(\s*!handoff_boot_services_active\(\)\s*\)\s*\{\s*"
        r"x64_platform_tables_init\(1\);",
        re.DOTALL,
    )
    if not native_tables.search(kernel_main_c):
        return fail("kernel_main must install native descriptor tables only after BootServices is inactive")
    return 0


def check_maintenance_network(service_manager: str) -> int:
    start = service_manager.find(
        "seed_target(SYSTEM_SERVICE_TARGET_MAINTENANCE"
    )
    end = service_manager.find("seed_target(SYSTEM_SERVICE_TARGET_FULL", start)
    if start < 0 or end < 0:
        return fail("service_manager must define the maintenance target")
    maintenance_block = service_manager[start:end]
    if "SYSTEM_SERVICE_NETWORKD" not in maintenance_block:
        return fail("Maintenance target must start networkd")
    return 0


def check_efi_snp_network(makefile: str, net_probe_h: str, net_probe_c: str,
                          stack_driver_c: str) -> int:
    if not EFI_SNP_H.is_file() or not EFI_SNP_C.is_file():
        return fail("EFI SNP network backend source and header must exist")
    if "$(BUILD)/x86_64/drivers/net/efi_snp.o" not in makefile:
        return fail("Makefile must link the EFI SNP network backend")
    if "NET_NIC_KIND_EFI_SNP" not in net_probe_h:
        return fail("net_probe.h must expose NET_NIC_KIND_EFI_SNP")
    if "case NET_NIC_KIND_EFI_SNP:" not in net_probe_c:
        return fail("net_probe.c must mark EFI SNP runtime-supported and name it")

    snp_idx = net_probe_c.find("probe_try_efi_snp(out)")
    pci_idx = net_probe_c.find("pci_init()")
    if snp_idx < 0 or pci_idx < 0 or snp_idx > pci_idx:
        return fail("EFI SNP probe must run before PCI scanning")

    for symbol in (
        "efi_snp_init",
        "efi_snp_ready",
        "efi_snp_send_frame",
        "efi_snp_poll_frame",
    ):
        if symbol not in stack_driver_c:
            return fail(f"stack_driver.c must wire {symbol}")
    return 0


def check_firmware_blockio_alignment(storage_runtime_c: str) -> int:
    start = storage_runtime_c.find("static int probe_blockio_lba0")
    end = storage_runtime_c.find("static void print_efi_blockio_status", start)
    if start < 0 or end < 0:
        return fail("storage_runtime.c must define probe_blockio_lba0 before status printing")
    probe = storage_runtime_c[start:end]
    if "block_device_read(&dev->dev, 0, probe_buf)" not in probe:
        return fail(
            "EFI BlockIO probe must use block_device_read so the EFI backend applies its aligned bounce buffer"
        )
    if "bio->read_blocks(" in probe:
        return fail(
            "EFI BlockIO probe must not call ReadBlocks directly with the shared probe buffer"
        )
    return 0


def main() -> int:
    efi_main = EFI_MAIN.read_text(encoding="utf-8")
    kernel_main_c = KERNEL_MAIN_C.read_text(encoding="utf-8")
    service_manager = SERVICE_MANAGER.read_text(encoding="utf-8")
    makefile = MAKEFILE.read_text(encoding="utf-8")
    net_probe_h = NET_PROBE_H.read_text(encoding="utf-8")
    net_probe_c = NET_PROBE_C.read_text(encoding="utf-8")
    stack_driver_c = STACK_DRIVER_C.read_text(encoding="utf-8")
    storage_runtime_c = STORAGE_RUNTIME_C.read_text(encoding="utf-8")

    for rc in (
        check_loader_hybrid_handoff(efi_main),
        check_kernel_keeps_firmware_descriptors(kernel_main_c),
        check_maintenance_network(service_manager),
        check_efi_snp_network(makefile, net_probe_h, net_probe_c, stack_driver_c),
        check_firmware_blockio_alignment(storage_runtime_c),
    ):
        if rc != 0:
            return rc

    print("[ok] Hyper-V hybrid firmware runtime self-test passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
