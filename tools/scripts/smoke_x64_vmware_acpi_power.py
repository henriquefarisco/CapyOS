#!/usr/bin/env python3
"""Read-only ACPI diagnostics on two cold boots of a disposable VMware disk.

Windows-native runner. The prepared VM must use a serial named pipe, UEFI,
E1000 and the freshly built kernel/loader. Initial setup must be completed.
No installer, hardware passthrough or personal VM is used by this gate.
"""
from __future__ import annotations

import argparse
import json
import os
import shutil
from pathlib import Path

from smoke_x64_auth import login
from smoke_x64_cli import check_acpi_power
from smoke_x64_helpers import ensure_shell_after_login
from smoke_x64_vmware_installer import VmwareConsole


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--vmx", type=Path, required=True)
    parser.add_argument("--pipe-name", required=True)
    parser.add_argument("--vmrun", type=Path)
    parser.add_argument("--log-root", type=Path,
                        default=Path("build/ci/vmware-acpi-power"))
    parser.add_argument("--timeout", type=float, default=120)
    parser.add_argument("--user", default="admin")
    parser.add_argument("--password", default="admin", help="Disposable lab account only")
    args = parser.parse_args()
    if os.name != "nt":
        parser.error("Run with Windows Python for VMware named-pipe access")
    repo = Path(__file__).resolve().parents[2]
    vmx = args.vmx.resolve()
    # Fail closed on accidental selection of the user's normal VM directory.
    if not vmx.is_relative_to((repo / "build").resolve()) or not vmx.is_file():
        parser.error("--vmx must be an existing disposable VM under this checkout's build/")
    vmrun = args.vmrun or shutil.which("vmrun") or shutil.which("vmrun.exe")
    if not vmrun:
        parser.error("Supply --vmrun or add VMware Workstation to PATH")
    args.log_root.mkdir(parents=True, exist_ok=True)
    evidence = {"vmx": str(vmx), "boots": []}
    for boot in (1, 2):
        log = args.log_root / f"boot{boot}.serial.log"
        console = VmwareConsole(Path(vmrun), vmx, args.pipe_name, log,
                                secrets=(args.password,))
        try:
            console.start()
            mode = login(session=console, timeout=args.timeout, user=args.user,
                         password=args.password, allow_desktop=True)
            ensure_shell_after_login(session=console, timeout=args.timeout, mode=mode)
            check_acpi_power(console, args.timeout)
        finally:
            console.stop()
        # stop() drains/persists the serial log and verifies VM termination.
        captured = console.text()
        if any(marker in captured for marker in ("KERNEL PANIC", "#PF", "#GP")):
            raise RuntimeError("fault in final VMware capture")
        evidence["boots"].append({"boot": boot, "result": "pass", "serial": str(log)})
        (args.log_root / "result.json").write_text(json.dumps(evidence, indent=2), encoding="utf-8")
    print("[ok] VMware ACPI power-status passed on two cold boots; VM stopped")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
