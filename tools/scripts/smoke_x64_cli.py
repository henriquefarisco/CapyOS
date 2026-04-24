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
import sys
from pathlib import Path

from smoke_x64_common import (
    boot_with_session,
    cleanup_file,
    create_runtime_ovmf_vars,
    print_log_tail,
    provision_disk,
    resolve_ovmf_or_raise,
    resolve_qemu_binary,
    run_build_if_requested,
    validate_installed_disk_artifacts,
)
from smoke_x64_flow import (
    ensure_shell_after_login,
    login,
    maybe_run_first_boot_setup,
    run_cmd,
    smoke_first_boot,
    smoke_second_boot,
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
        "--storage-bus",
        choices=("sata", "nvme"),
        default="sata",
        help="Storage bus used by QEMU for the provisioned disk image",
    )
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
        "--require-shell",
        action="store_true",
        help="Exit autostarted desktop and require the CLI prompt before running shell checks",
    )
    parser.add_argument(
        "--boot-perf-only",
        action="store_true",
        help="Run only first boot/login plus perf-boot collection, then stop",
    )
    parser.add_argument(
        "--keyboard-layout",
        default="us",
        help="Keyboard layout persisted in CAPYCFG.BIN",
    )
    parser.add_argument(
        "--language",
        default="en",
        help="System default language persisted in CAPYCFG.BIN",
    )
    parser.add_argument(
        "--volume-key",
        default="CAPYOS-SMOKE-KEY-2026-0001",
        help="Lab-only volume key persisted in CAPYCFG.BIN for smoke coverage",
    )
    parser.add_argument("--verbose", action="store_true", help="Print live serial output")
    return parser.parse_args()




def run_boot1(
    qemu_bin: str,
    ovmf_code: str,
    ovmf_vars_runtime: Path,
    disk_path: Path,
    boot1_log: Path,
    boot1_debugcon_log: Path,
    parsed: argparse.Namespace,
    marker: str,
) -> None:
    print("[info] boot #1: first-boot setup + login + write marker")
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
        )
        mode = login(
            session=session,
            timeout=parsed.step_timeout,
            user=parsed.user,
            password=parsed.password,
            allow_desktop=True,
        )
        if parsed.require_shell or parsed.boot_perf_only:
            mode = ensure_shell_after_login(
                session=session,
                timeout=parsed.step_timeout,
                mode=mode,
            )
        if parsed.boot_perf_only:
            run_cmd(
                session=session,
                cmd="perf-boot",
                timeout=parsed.step_timeout,
                expect="total_boot_to_login",
            )
            return
        if mode == "shell":
            smoke_first_boot(
                session=session,
                timeout=parsed.step_timeout,
                user=parsed.user,
                password=parsed.password,
                marker=marker,
            )
        else:
            print("[info] boot #1 entrou direto no desktop; smoke CLI pulando validacoes de shell.")
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
    print("[info] boot #2: login + persistence validation")
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
        if parsed.require_shell:
            mode = ensure_shell_after_login(
                session=session,
                timeout=parsed.step_timeout,
                mode=mode,
            )
        if mode == "shell":
            smoke_second_boot(
                session=session,
                timeout=parsed.step_timeout,
                user=parsed.user,
                password=parsed.password,
                marker=marker,
            )
        else:
            print("[info] boot #2 entrou direto no desktop; smoke CLI aceitando sessao grafica.")
    finally:
        session.stop()


def main() -> int:
    parsed = parse_args()

    repo_root = Path(__file__).resolve().parents[2]
    log_base = (repo_root / parsed.log).resolve()
    disk_path = (repo_root / parsed.disk).resolve()
    boot1_log = log_base.with_name(log_base.stem + ".boot1" + log_base.suffix)
    boot2_log = log_base.with_name(log_base.stem + ".boot2" + log_base.suffix)
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
        bootx64, kernel, manifest = validate_installed_disk_artifacts(repo_root)
        provision_disk(
            repo_root=repo_root,
            disk_path=disk_path,
            disk_size=parsed.disk_size,
            bootx64=bootx64,
            kernel=kernel,
            manifest=manifest,
            keyboard_layout=parsed.keyboard_layout,
            language=parsed.language,
            volume_key=parsed.volume_key,
        )
        ovmf_vars_runtime = create_runtime_ovmf_vars(log_base, ovmf_vars_template)

        run_boot1(
            qemu_bin=qemu_bin,
            ovmf_code=ovmf_code,
            ovmf_vars_runtime=ovmf_vars_runtime,
            disk_path=disk_path,
            boot1_log=boot1_log,
            boot1_debugcon_log=boot1_debugcon_log,
            parsed=parsed,
            marker=marker,
        )
        if not parsed.boot_perf_only:
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
        else:
            print("[ok] smoke x64 boot performance passed.")
    except Exception as exc:
        print(f"[err] smoke failed: {exc}", file=sys.stderr)
        print_log_tail(boot1_log)
        print_log_tail(boot2_log)
        return 1
    finally:
        cleanup_file(ovmf_vars_runtime)
        if not parsed.keep_disk:
            cleanup_file(disk_path)

    if not parsed.boot_perf_only:
        print("[ok] smoke x64 CLI + persistence passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
