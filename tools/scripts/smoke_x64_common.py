#!/usr/bin/env python3
"""Shared runtime helpers for CapyOS x64 smoke scripts."""

from __future__ import annotations

import shutil
import sys
from pathlib import Path

from smoke_x64_session import (
    SmokeSession,
    choose_free_port,
    detect_ovmf,
    make_qemu_cmd,
    run_command,
)


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


def run_build_if_requested(repo_root: Path, build_requested: bool) -> None:
    if not build_requested:
        return
    run_command(["make", "all64"], cwd=repo_root)
    run_command(["make", "iso-uefi"], cwd=repo_root)
    run_command(["make", "manifest64"], cwd=repo_root)


def resolve_qemu_binary(qemu_name: str) -> str:
    qemu_bin = shutil.which(qemu_name)
    if not qemu_bin:
        raise FileNotFoundError(f"qemu not found in PATH: {qemu_name}")
    return qemu_bin


def resolve_ovmf_or_raise(ovmf_path: str | None) -> tuple[str, str]:
    return detect_ovmf(ovmf_path)


def create_runtime_ovmf_vars(log_base: Path, ovmf_vars_template: str) -> Path:
    ovmf_vars_runtime = log_base.with_name(log_base.stem + ".OVMF_VARS.runtime.fd")
    ovmf_vars_runtime.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(ovmf_vars_template, ovmf_vars_runtime)
    return ovmf_vars_runtime


def cleanup_file(path: Path | None) -> None:
    if path is None:
        return
    try:
        path.unlink(missing_ok=True)
    except Exception:
        pass


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


def validate_installed_disk_artifacts(repo_root: Path) -> tuple[Path, Path, Path]:
    bootx64 = (repo_root / "build/boot/uefi_loader.efi").resolve()
    kernel = (repo_root / "build/capyos64.bin").resolve()
    manifest = (repo_root / "build/manifest.bin").resolve()
    for path in (bootx64, kernel, manifest):
        if not path.exists():
            raise FileNotFoundError(f"required artifact missing: {path}")
    return bootx64, kernel, manifest


def validate_iso_artifact(repo_root: Path, iso_relpath: str) -> Path:
    iso_path = (repo_root / iso_relpath).resolve()
    if not iso_path.exists():
        raise FileNotFoundError(f"required ISO missing: {iso_path}")
    return iso_path


def prepare_target_disk(disk_path: Path, disk_size: str) -> None:
    disk_path.parent.mkdir(parents=True, exist_ok=True)
    with disk_path.open("wb") as fp:
        fp.truncate(parse_size(disk_size))


def provision_disk(
    *,
    repo_root: Path,
    disk_path: Path,
    disk_size: str,
    bootx64: Path,
    kernel: Path,
    manifest: Path,
    keyboard_layout: str,
    language: str,
    volume_key: str,
) -> None:
    disk_path.parent.mkdir(parents=True, exist_ok=True)
    run_command(
        [
            "python3",
            "tools/scripts/provision_gpt.py",
            "--img",
            str(disk_path),
            "--size",
            disk_size,
            "--bootx64",
            str(bootx64),
            "--kernel",
            str(kernel),
            "--manifest",
            str(manifest),
            "--keyboard-layout",
            keyboard_layout,
            "--language",
            language,
            "--volume-key",
            volume_key,
            "--allow-plain-volume-key",
            "--allow-existing",
            "--confirm",
        ],
        cwd=repo_root,
    )


def boot_with_session(
    *,
    qemu_bin: str,
    ovmf_code: str,
    ovmf_vars_runtime: Path,
    disk_path: Path,
    log_path: Path,
    debugcon_log: Path,
    memory_mb: int,
    storage_bus: str,
    verbose: bool,
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
        memory_mb=memory_mb,
        storage_bus=storage_bus,
        debugcon_log=debugcon_log,
        iso_path=iso_path,
        boot_from=boot_from,
    )
    session = SmokeSession(
        cmd=cmd,
        serial_port=port,
        log_path=log_path,
        verbose=verbose,
    )
    session.start()
    return session
