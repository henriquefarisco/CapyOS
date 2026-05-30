#!/usr/bin/env python3

"""Host-only self-test for the Hyper-V boot policy invariants.

The UEFI installer-mode invariant (Slice 0):
- The CAPYOS.INI marker must be the single trigger for installer mode.
- readonly / cdrom flags alone must NOT trigger installer mode.

- The boot policy invariant for Hyper-V:
- Hyper-V must not allow the authoritative first-boot wizard to complete on
  RAM fallback. If no persistent DATA backend is mounted, boot policy must
  fail closed into maintenance instead of returning to the wizard every reboot.
- Non-Hyper-V recovery fallback can still remain reachable with a loud warning.
- When shell_runtime_rc != 0 the kernel must still log the original
  unavailability warning (real storage failure path).

This complements ``test_uefi_kernel_load_contract.py`` (load address) and
``test_hyperv_baseline_evidence_*.py`` (Hyper-V evidence triage).
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
EFI_MAIN = REPO_ROOT / "src/boot/uefi_loader/efi_main.c"
KERNEL_MAIN = REPO_ROOT / "src/arch/x86_64/kernel_main.c"
# The Stage 8 boot-policy block was extracted from kernel_main64() into the
# kernel_main TU-group sibling kernel_boot_stages.c (2026-05-29 split). The
# boot-policy invariant is enforced across the combined TU-group source so it
# follows the code wherever it lives within the group.
KERNEL_BOOT_STAGES = REPO_ROOT / "src/arch/x86_64/kernel_boot_stages.c"


def fail(message: str) -> int:
    print(f"[err] {message}", file=sys.stderr)
    return 1


def check_uefi_installer_invariant(efi_main: str) -> int:
    installer_conditions = re.findall(
        r"if \((?:!already_installed|already_installed) && ([^)]+)\)", efi_main
    )
    for condition in installer_conditions:
        if "install_ro" in condition or "install_cdrom" in condition:
            return fail(
                "UEFI installer mode must not be selected by readonly/cdrom alone"
            )
    if "if (!already_installed && install_marker)" not in efi_main:
        return fail("UEFI installer mode must require the CAPYOS.INI marker")
    return 0


def check_boot_policy_invariant(kernel_main: str) -> int:
    if "!x64_storage_runtime_has_device()" not in kernel_main:
        return fail(
            "kernel_main must distinguish RAM fallback from persistent backend"
        )

    hyperv_no_device_pattern = re.compile(
        r"!x64_storage_runtime_has_device\(\)\)\s*\{[^}]*?"
        r"x64_storage_runtime_hyperv_present\(\)\)\s*\{[^}]*?"
        r"validated_storage_ready\s*=\s*0;[^}]*?"
        r"setup blocked until DATA is persistent",
        re.DOTALL,
    )
    if not hyperv_no_device_pattern.search(kernel_main):
        return fail(
            "Hyper-V RAM fallback must force validated_storage_ready=0 and "
            "warn that setup is blocked until DATA is persistent"
        )

    hyperv_non_persistent_pattern = re.compile(
        r"!g_shell_persistent_storage\)\s*\{[^}]*?"
        r"x64_storage_runtime_hyperv_present\(\)\)\s*\{[^}]*?"
        r"validated_storage_ready\s*=\s*0;[^}]*?"
        r"maintenance prevents non-persistent setup",
        re.DOTALL,
    )
    if not hyperv_non_persistent_pattern.search(kernel_main):
        return fail(
            "Hyper-V mount/unlock fallback must force maintenance instead of "
            "allowing first-boot setup on RAM"
        )

    non_hyperv_warning_pattern = re.compile(
        r"Persistent storage unavailable; configuration will NOT survive reboot",
        re.DOTALL,
    )
    if not non_hyperv_warning_pattern.search(kernel_main):
        return fail(
            "Non-Hyper-V RAM fallback must retain its explicit persistence warning"
        )

    return 0


def main() -> int:
    efi_main = EFI_MAIN.read_text(encoding="utf-8")
    kernel_main = KERNEL_MAIN.read_text(encoding="utf-8")
    boot_stages = (
        KERNEL_BOOT_STAGES.read_text(encoding="utf-8")
        if KERNEL_BOOT_STAGES.exists()
        else ""
    )

    rc = check_uefi_installer_invariant(efi_main)
    if rc != 0:
        return rc

    # Scan the whole kernel_main TU group: the boot-policy block may live in
    # kernel_main.c or in the extracted kernel_boot_stages.c sibling.
    rc = check_boot_policy_invariant(kernel_main + "\n" + boot_stages)
    if rc != 0:
        return rc

    print("[ok] Hyper-V persistent boot policy self-test passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
