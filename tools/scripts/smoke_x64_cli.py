#!/usr/bin/env python3
"""
CAPYOS x64 smoke test for installed-disk flow:

- provisions GPT disk (ESP/BOOT/DATA) with CAPYCFG volume key
- boots via QEMU/UEFI
- runs first-boot setup when required
- validates login + core CLI
- writes a marker file, reboots (fresh VM session), validates persistence
"""

from __future__ import annotations

import argparse
import shutil
import sys
import time
from pathlib import Path

from smoke_x64_flow import (
    login,
    maybe_run_first_boot_setup,
    smoke_first_boot,
    smoke_second_boot,
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
        description="CAPYOS x64 installed-disk smoke test (QEMU/UEFI)"
    )
    parser.add_argument(
        "--iso",
        default="build/CapyOS-Installer-UEFI.iso",
        help="Deprecated compatibility flag (unused).",
    )
    parser.add_argument("--qemu", default="qemu-system-x86_64", help="QEMU binary")
    parser.add_argument("--ovmf", default=None, help="Path to OVMF_CODE.fd")
    parser.add_argument("--memory", type=int, default=1024, help="Guest memory in MB")
    parser.add_argument(
        "--step-timeout", type=float, default=60.0, help="Timeout per interaction step in seconds"
    )
    parser.add_argument(
        "--build",
        action="store_true",
        help="Run make all64 && make iso-uefi && make manifest64 first",
    )
    parser.add_argument("--log", default="build/ci/smoke_x64_cli.log", help="Base log file path")
    parser.add_argument(
        "--disk",
        default="build/ci/smoke_x64_cli.img",
        help="Provisioned GPT disk image path",
    )
    parser.add_argument("--disk-size", default="2G", help="Disk size for GPT provisioning")
    parser.add_argument("--keep-disk", action="store_true", help="Do not delete provisioned disk image")
    parser.add_argument("--user", default="admin", help="Admin username for first-boot + login")
    parser.add_argument("--password", default="admin", help="Admin password for first-boot + login")
    parser.add_argument(
        "--keyboard-layout",
        default="us",
        help="Keyboard layout persisted in CAPYCFG.BIN",
    )
    parser.add_argument(
        "--volume-key",
        default="CAPYOS-SMOKE-KEY-2026-0001",
        help="Volume key persisted in CAPYCFG.BIN",
    )
    parser.add_argument("--verbose", action="store_true", help="Print live serial output")
    return parser.parse_args()


def run_build_if_requested(repo_root: Path, parsed: argparse.Namespace) -> None:
    if not parsed.build:
        return
    run_command(["make", "all64"], cwd=repo_root)
    run_command(["make", "iso-uefi"], cwd=repo_root)
    run_command(["make", "manifest64"], cwd=repo_root)


def validate_artifacts(repo_root: Path) -> tuple[Path, Path, Path]:
    bootx64 = (repo_root / "build/boot/uefi_loader.efi").resolve()
    kernel = (repo_root / "build/capyos64.bin").resolve()
    manifest = (repo_root / "build/manifest.bin").resolve()
    for p in (bootx64, kernel, manifest):
        if not p.exists():
            raise FileNotFoundError(f"required artifact missing: {p}")
    return bootx64, kernel, manifest


def provision_disk(
    repo_root: Path,
    disk_path: Path,
    parsed: argparse.Namespace,
    bootx64: Path,
    kernel: Path,
    manifest: Path,
) -> None:
    disk_path.parent.mkdir(parents=True, exist_ok=True)
    run_command(
        [
            "python3",
            "tools/scripts/provision_gpt.py",
            "--img",
            str(disk_path),
            "--size",
            parsed.disk_size,
            "--bootx64",
            str(bootx64),
            "--kernel",
            str(kernel),
            "--manifest",
            str(manifest),
            "--keyboard-layout",
            parsed.keyboard_layout,
            "--volume-key",
            parsed.volume_key,
            "--allow-existing",
            "--confirm",
        ],
        cwd=repo_root,
    )


def run_boot1(
    qemu_bin: str,
    ovmf_code: str,
    ovmf_vars_runtime: Path,
    disk_path: Path,
    boot1_log: Path,
    parsed: argparse.Namespace,
    marker: str,
) -> None:
    print("[info] boot #1: first-boot setup + login + write marker")
    port = choose_free_port()
    cmd = make_qemu_cmd(
        qemu_bin=qemu_bin,
        ovmf_code=ovmf_code,
        ovmf_vars_runtime=ovmf_vars_runtime,
        disk_path=disk_path,
        serial_port=port,
        memory_mb=parsed.memory,
    )
    session = SmokeSession(
        cmd=cmd,
        serial_port=port,
        log_path=boot1_log,
        verbose=parsed.verbose,
    )
    session.start()
    try:
        maybe_run_first_boot_setup(
            session=session,
            timeout=parsed.step_timeout,
            user=parsed.user,
            password=parsed.password,
            keyboard_layout=parsed.keyboard_layout,
        )
        login(session=session, timeout=parsed.step_timeout, user=parsed.user, password=parsed.password)
        smoke_first_boot(
            session=session,
            timeout=parsed.step_timeout,
            user=parsed.user,
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
    parsed: argparse.Namespace,
    marker: str,
) -> None:
    print("[info] boot #2: login + persistence validation")
    port = choose_free_port()
    cmd = make_qemu_cmd(
        qemu_bin=qemu_bin,
        ovmf_code=ovmf_code,
        ovmf_vars_runtime=ovmf_vars_runtime,
        disk_path=disk_path,
        serial_port=port,
        memory_mb=parsed.memory,
    )
    session = SmokeSession(
        cmd=cmd,
        serial_port=port,
        log_path=boot2_log,
        verbose=parsed.verbose,
    )
    session.start()
    try:
        mk = session.marker()
        session.wait_for("Usuario:", timeout=parsed.step_timeout * 4, start_at=mk)
        smoke_second_boot(
            session=session,
            timeout=parsed.step_timeout,
            user=parsed.user,
            password=parsed.password,
            marker=marker,
        )
    finally:
        session.stop()


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
    boot1_log = log_base.with_name(log_base.stem + ".boot1" + log_base.suffix)
    boot2_log = log_base.with_name(log_base.stem + ".boot2" + log_base.suffix)
    marker = f"persist-{int(time.time())}"

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
        bootx64, kernel, manifest = validate_artifacts(repo_root)
        provision_disk(repo_root, disk_path, parsed, bootx64, kernel, manifest)

        ovmf_vars_runtime = log_base.parent / "OVMF_VARS.runtime.fd"
        ovmf_vars_runtime.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(ovmf_vars_template, ovmf_vars_runtime)

        run_boot1(
            qemu_bin=qemu_bin,
            ovmf_code=ovmf_code,
            ovmf_vars_runtime=ovmf_vars_runtime,
            disk_path=disk_path,
            boot1_log=boot1_log,
            parsed=parsed,
            marker=marker,
        )
        run_boot2(
            qemu_bin=qemu_bin,
            ovmf_code=ovmf_code,
            ovmf_vars_runtime=ovmf_vars_runtime,
            disk_path=disk_path,
            boot2_log=boot2_log,
            parsed=parsed,
            marker=marker,
        )
    except Exception as exc:
        print(f"[err] smoke failed: {exc}", file=sys.stderr)
        print_log_tail(boot1_log)
        print_log_tail(boot2_log)
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

    print("[ok] smoke x64 CLI + persistence passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
