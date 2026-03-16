#!/usr/bin/env python3
"""
Smoke test for the ISO cancel path:

- boots the official ISO and installs to a blank disk
- boots the ISO again with the installed disk still attached
- cancels the installer prompt
- validates that firmware continues to the installed disk instead of booting the
  ISO kernel path
"""

from __future__ import annotations

import argparse
import shutil
import sys
from pathlib import Path

from smoke_x64_flow import (
    cancel_iso_install,
    complete_iso_install,
    login,
    maybe_run_first_boot_setup,
)
from smoke_x64_session import (
    SmokeSession,
    choose_free_port,
    detect_ovmf,
    make_qemu_cmd,
    run_command,
)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="CAPYOS x64 smoke for ISO cancel -> next boot device"
    )
    parser.add_argument(
        "--iso",
        default="build/CapyOS-Installer-UEFI.iso",
        help="Official installer ISO path",
    )
    parser.add_argument("--qemu", default="qemu-system-x86_64", help="QEMU binary")
    parser.add_argument("--ovmf", default=None, help="Path to OVMF_CODE.fd")
    parser.add_argument("--memory", type=int, default=1024, help="Guest memory in MB")
    parser.add_argument(
        "--storage-bus",
        choices=("sata", "nvme"),
        default="sata",
        help="Storage bus used by the install target disk",
    )
    parser.add_argument(
        "--step-timeout",
        type=float,
        default=60.0,
        help="Timeout per interaction step in seconds",
    )
    parser.add_argument(
        "--build",
        action="store_true",
        help="Run make all64 && make iso-uefi && make manifest64 first",
    )
    parser.add_argument(
        "--log",
        default="build/ci/smoke_x64_iso_cancel.log",
        help="Base log file path",
    )
    parser.add_argument(
        "--disk",
        default="build/ci/smoke_x64_iso_cancel.img",
        help="Blank install target disk path",
    )
    parser.add_argument("--disk-size", default="2G", help="Install target disk size")
    parser.add_argument("--keep-disk", action="store_true", help="Do not delete the target disk image")
    parser.add_argument("--user", default="admin", help="Admin username for first-boot + login")
    parser.add_argument("--password", default="admin", help="Admin password for first-boot + login")
    parser.add_argument(
        "--keyboard-layout",
        default="us",
        choices=("us", "br-abnt2"),
        help="Keyboard layout selected in the installer and first boot",
    )
    parser.add_argument("--verbose", action="store_true", help="Print live serial output")
    return parser.parse_args()


def parse_size(size: str) -> int:
    raw = size.strip().upper()
    mul = 1
    if raw.endswith("K"):
        mul = 1024
        raw = raw[:-1]
    elif raw.endswith("M"):
        mul = 1024 * 1024
        raw = raw[:-1]
    elif raw.endswith("G"):
        mul = 1024 * 1024 * 1024
        raw = raw[:-1]
    return int(raw, 10) * mul


def run_build_if_requested(repo_root: Path, parsed: argparse.Namespace) -> None:
    if not parsed.build:
        return
    run_command(["make", "all64"], cwd=repo_root)
    run_command(["make", "iso-uefi"], cwd=repo_root)
    run_command(["make", "manifest64"], cwd=repo_root)


def validate_artifacts(repo_root: Path, parsed: argparse.Namespace) -> Path:
    iso_path = (repo_root / parsed.iso).resolve()
    if not iso_path.exists():
        raise FileNotFoundError(f"required ISO missing: {iso_path}")
    return iso_path


def prepare_target_disk(disk_path: Path, disk_size: str) -> None:
    disk_path.parent.mkdir(parents=True, exist_ok=True)
    with disk_path.open("wb") as fp:
        fp.truncate(parse_size(disk_size))


def boot_with_session(
    *,
    qemu_bin: str,
    ovmf_code: str,
    ovmf_vars_runtime: Path,
    disk_path: Path,
    log_path: Path,
    debugcon_log: Path,
    parsed: argparse.Namespace,
    iso_path: Path | None = None,
    boot_from: str = "disk",
) -> SmokeSession:
    port = choose_free_port()
    cmd = make_qemu_cmd(
        qemu_bin=qemu_bin,
        ovmf_code=ovmf_code,
        ovmf_vars_runtime=ovmf_vars_runtime,
        disk_path=disk_path,
        serial_port=port,
        memory_mb=parsed.memory,
        storage_bus=parsed.storage_bus,
        debugcon_log=debugcon_log,
        iso_path=iso_path,
        boot_from=boot_from,
    )
    session = SmokeSession(
        cmd=cmd,
        serial_port=port,
        log_path=log_path,
        verbose=parsed.verbose,
    )
    session.start()
    return session


def print_log_tail(log_path: Path) -> None:
    try:
        if not log_path.exists():
            return
        tail = log_path.read_text(encoding="latin-1", errors="replace")[-2500:]
        print(f"----- {log_path.name} tail -----", file=sys.stderr)
        print(tail, file=sys.stderr)
        print("---------------------------", file=sys.stderr)
    except Exception:
        pass


def main() -> int:
    parsed = parse_args()
    repo_root = Path(__file__).resolve().parents[2]
    log_base = (repo_root / parsed.log).resolve()
    disk_path = (repo_root / parsed.disk).resolve()
    installer_log = log_base.with_name(log_base.stem + ".installer" + log_base.suffix)
    cancel_log = log_base.with_name(log_base.stem + ".cancel" + log_base.suffix)
    installer_debugcon_log = log_base.with_name(log_base.stem + ".installer.debugcon.log")
    cancel_debugcon_log = log_base.with_name(log_base.stem + ".cancel.debugcon.log")

    qemu_bin = shutil.which(parsed.qemu)
    if not qemu_bin:
        print(f"[err] qemu not found in PATH: {parsed.qemu}", file=sys.stderr)
        return 2

    try:
        ovmf_code, ovmf_vars_template = detect_ovmf(parsed.ovmf)
    except FileNotFoundError as exc:
        print(f"[err] {exc}", file=sys.stderr)
        return 2

    ovmf_vars_runtime: Path | None = None
    try:
        run_build_if_requested(repo_root, parsed)
        iso_path = validate_artifacts(repo_root, parsed)
        prepare_target_disk(disk_path, parsed.disk_size)

        ovmf_vars_runtime = log_base.with_name(log_base.stem + ".OVMF_VARS.runtime.fd")
        ovmf_vars_runtime.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(ovmf_vars_template, ovmf_vars_runtime)

        print("[info] boot #0: install from ISO")
        install_session = boot_with_session(
            qemu_bin=qemu_bin,
            ovmf_code=ovmf_code,
            ovmf_vars_runtime=ovmf_vars_runtime,
            disk_path=disk_path,
            log_path=installer_log,
            debugcon_log=installer_debugcon_log,
            parsed=parsed,
            iso_path=iso_path,
            boot_from="cdrom",
        )
        try:
            complete_iso_install(
                session=install_session,
                timeout=parsed.step_timeout,
                keyboard_layout=parsed.keyboard_layout,
            )
        finally:
            install_session.stop()

        print("[info] boot #1: cancel ISO and expect firmware fallback to disk")
        cancel_session = boot_with_session(
            qemu_bin=qemu_bin,
            ovmf_code=ovmf_code,
            ovmf_vars_runtime=ovmf_vars_runtime,
            disk_path=disk_path,
            log_path=cancel_log,
            debugcon_log=cancel_debugcon_log,
            parsed=parsed,
            iso_path=iso_path,
            boot_from="cdrom",
        )
        try:
            cancel_iso_install(cancel_session, timeout=parsed.step_timeout)
            maybe_run_first_boot_setup(
                session=cancel_session,
                timeout=parsed.step_timeout,
                user=parsed.user,
                password=parsed.password,
                keyboard_layout=parsed.keyboard_layout,
            )
            login(
                session=cancel_session,
                timeout=parsed.step_timeout,
                user=parsed.user,
                password=parsed.password,
            )
        finally:
            cancel_session.stop()
    except Exception as exc:
        print(f"[err] ISO cancel smoke failed: {exc}", file=sys.stderr)
        print_log_tail(installer_log)
        print_log_tail(cancel_log)
        return 1
    finally:
        if ovmf_vars_runtime is not None:
            try:
                ovmf_vars_runtime.unlink(missing_ok=True)
            except Exception:
                pass
        if not parsed.keep_disk:
            try:
                disk_path.unlink(missing_ok=True)
            except Exception:
                pass

    print("[ok] smoke x64 ISO cancel -> disk fallback passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
