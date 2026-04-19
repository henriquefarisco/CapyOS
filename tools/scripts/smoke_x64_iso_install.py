#!/usr/bin/env python3
"""
CAPYOS x64 smoke test for the official ISO install flow:

- boots the release ISO in UEFI mode
- completes the installer wizard on a blank disk
- reboots from the installed disk
- runs first-boot setup when required
- validates login + core CLI
- writes a marker file, reboots, validates persistence
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

from smoke_x64_common import (
    boot_with_session,
    cleanup_file,
    create_runtime_ovmf_vars,
    prepare_target_disk,
    print_log_tail,
    resolve_ovmf_or_raise,
    resolve_qemu_binary,
    run_build_if_requested,
    validate_iso_artifact,
)
from smoke_x64_flow import (
    complete_iso_install,
    login,
    maybe_run_first_boot_setup,
    smoke_first_boot,
    smoke_second_boot,
)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="CAPYOS x64 ISO install smoke test (QEMU/UEFI)"
    )
    parser.add_argument("--iso", default="build/CapyOS-Installer-UEFI.iso", help="Official installer ISO path")
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
    parser.add_argument("--log", default="build/ci/smoke_x64_iso_install.log", help="Base log file path")
    parser.add_argument(
        "--disk",
        default="build/ci/smoke_x64_iso_install.img",
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


def run_installer_boot(
    qemu_bin: str,
    ovmf_code: str,
    ovmf_vars_runtime: Path,
    iso_path: Path,
    disk_path: Path,
    installer_log: Path,
    installer_debugcon_log: Path,
    parsed: argparse.Namespace,
) -> None:
    print("[info] boot #0: official ISO installer")
    session = boot_with_session(
        qemu_bin=qemu_bin,
        ovmf_code=ovmf_code,
        ovmf_vars_runtime=ovmf_vars_runtime,
        disk_path=disk_path,
        log_path=installer_log,
        debugcon_log=installer_debugcon_log,
        memory_mb=parsed.memory,
        storage_bus=parsed.storage_bus,
        verbose=parsed.verbose,
        iso_path=iso_path,
        boot_from="cdrom",
    )
    try:
        complete_iso_install(
            session=session,
            timeout=parsed.step_timeout,
            keyboard_layout=parsed.keyboard_layout,
            user=parsed.user,
            password=parsed.password,
        )
    finally:
        session.stop()


def extract_volume_key(installer_log: Path) -> str | None:
    try:
        text = installer_log.read_text(encoding="utf-8", errors="ignore")
    except OSError:
        return None
    match = re.search(r"\b([A-Z0-9]{4}(?:-[A-Z0-9]{4}){5})\b", text)
    if not match:
        return None
    return match.group(1)


def run_boot1(
    qemu_bin: str,
    ovmf_code: str,
    ovmf_vars_runtime: Path,
    disk_path: Path,
    boot1_log: Path,
    boot1_debugcon_log: Path,
    parsed: argparse.Namespace,
    marker: str,
    volume_key: str | None,
) -> None:
    print("[info] boot #1: first boot from installed disk")
    session = boot_with_session(
        qemu_bin=qemu_bin,
        ovmf_code=ovmf_code,
        ovmf_vars_runtime=ovmf_vars_runtime,
        disk_path=disk_path,
        log_path=boot1_log,
        debugcon_log=boot1_debugcon_log,
        memory_mb=parsed.memory,
        storage_bus=parsed.storage_bus,
        verbose=parsed.verbose,
    )
    try:
        maybe_run_first_boot_setup(
            session=session,
            timeout=parsed.step_timeout,
            user=parsed.user,
            password=parsed.password,
            keyboard_layout=parsed.keyboard_layout,
            volume_key=volume_key,
        )
        mode = login(
            session=session,
            timeout=parsed.step_timeout,
            user=parsed.user,
            password=parsed.password,
            allow_desktop=True,
        )
        if mode == "shell":
            smoke_first_boot(
                session=session,
                timeout=parsed.step_timeout,
                user=parsed.user,
                password=parsed.password,
                marker=marker,
            )
    finally:
        session.stop()


def run_boot2(
    qemu_bin: str,
    ovmf_code: str,
    ovmf_vars_runtime: Path,
    disk_path: Path,
    boot2_log: Path,
    boot2_debugcon_log: Path,
    parsed: argparse.Namespace,
    marker: str,
) -> None:
    print("[info] boot #2: persistence validation")
    session = boot_with_session(
        qemu_bin=qemu_bin,
        ovmf_code=ovmf_code,
        ovmf_vars_runtime=ovmf_vars_runtime,
        disk_path=disk_path,
        log_path=boot2_log,
        debugcon_log=boot2_debugcon_log,
        memory_mb=parsed.memory,
        storage_bus=parsed.storage_bus,
        verbose=parsed.verbose,
    )
    try:
        mk = session.marker()
        session.wait_for_any(
            ["Usuario:", "User:"],
            timeout=parsed.step_timeout * 4,
            start_at=mk,
        )
        mode = login(
            session=session,
            timeout=parsed.step_timeout,
            user=parsed.user,
            password=parsed.password,
            allow_desktop=True,
        )
        if mode == "shell":
            smoke_second_boot(
                session=session,
                timeout=parsed.step_timeout,
                user=parsed.user,
                password=parsed.password,
                marker=marker,
            )
    finally:
        session.stop()


def main() -> int:
    parsed = parse_args()

    repo_root = Path(__file__).resolve().parents[2]
    log_base = (repo_root / parsed.log).resolve()
    disk_path = (repo_root / parsed.disk).resolve()
    installer_log = log_base.with_name(log_base.stem + ".installer" + log_base.suffix)
    boot1_log = log_base.with_name(log_base.stem + ".boot1" + log_base.suffix)
    boot2_log = log_base.with_name(log_base.stem + ".boot2" + log_base.suffix)
    installer_debugcon_log = log_base.with_name(log_base.stem + ".installer.debugcon.log")
    boot1_debugcon_log = log_base.with_name(log_base.stem + ".boot1.debugcon.log")
    boot2_debugcon_log = log_base.with_name(log_base.stem + ".boot2.debugcon.log")
    marker = "persist-ok"

    try:
        qemu_bin = resolve_qemu_binary(parsed.qemu)
    except FileNotFoundError as exc:
        print(f"[err] {exc}", file=sys.stderr)
        return 2

    try:
        ovmf_code, ovmf_vars_template = resolve_ovmf_or_raise(parsed.ovmf)
    except FileNotFoundError as exc:
        print(f"[err] {exc}", file=sys.stderr)
        return 2

    ovmf_vars_runtime: Path | None = None
    try:
        run_build_if_requested(repo_root, parsed.build)
        iso_path = validate_iso_artifact(repo_root, parsed.iso)
        prepare_target_disk(disk_path, parsed.disk_size)
        ovmf_vars_runtime = create_runtime_ovmf_vars(log_base, ovmf_vars_template)

        run_installer_boot(
            qemu_bin=qemu_bin,
            ovmf_code=ovmf_code,
            ovmf_vars_runtime=ovmf_vars_runtime,
            iso_path=iso_path,
            disk_path=disk_path,
            installer_log=installer_log,
            installer_debugcon_log=installer_debugcon_log,
            parsed=parsed,
        )
        volume_key = extract_volume_key(installer_log)
        run_boot1(
            qemu_bin=qemu_bin,
            ovmf_code=ovmf_code,
            ovmf_vars_runtime=ovmf_vars_runtime,
            disk_path=disk_path,
            boot1_log=boot1_log,
            boot1_debugcon_log=boot1_debugcon_log,
            parsed=parsed,
            marker=marker,
            volume_key=volume_key,
        )
        run_boot2(
            qemu_bin=qemu_bin,
            ovmf_code=ovmf_code,
            ovmf_vars_runtime=ovmf_vars_runtime,
            disk_path=disk_path,
            boot2_log=boot2_log,
            boot2_debugcon_log=boot2_debugcon_log,
            parsed=parsed,
            marker=marker,
        )
    except Exception as exc:
        print(f"[err] ISO smoke failed: {exc}", file=sys.stderr)
        print_log_tail(installer_log)
        print_log_tail(boot1_log)
        print_log_tail(boot2_log)
        return 1
    finally:
        cleanup_file(ovmf_vars_runtime)
        if not parsed.keep_disk:
            cleanup_file(disk_path)

    print("[ok] smoke x64 ISO install + persistence passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
