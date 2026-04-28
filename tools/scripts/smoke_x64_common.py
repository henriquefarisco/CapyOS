#!/usr/bin/env python3
"""Shared runtime helpers for CapyOS x64 smoke scripts."""

from __future__ import annotations

import shutil
import struct
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
    iso_path = _resolve_iso_artifact(repo_root, iso_relpath)
    if not iso_path.exists():
        raise FileNotFoundError(f"required ISO missing: {iso_path}")
    _validate_iso_tree(repo_root)
    _validate_efiboot_image(repo_root)
    return iso_path


def _resolve_iso_artifact(repo_root: Path, iso_relpath: str) -> Path:
    requested = (repo_root / iso_relpath).resolve()
    sidecar = (repo_root / "build/CapyOS-Installer-UEFI.last-built.txt").resolve()

    if sidecar.exists():
      try:
          recorded = sidecar.read_text(encoding="utf-8", errors="ignore").strip()
      except OSError:
          recorded = ""
      if recorded:
          recorded_path = Path(recorded)
          if not recorded_path.is_absolute():
              recorded_path = (repo_root / recorded).resolve()
          if recorded_path.exists():
              if requested == (repo_root / "build/CapyOS-Installer-UEFI.iso").resolve():
                  print(f"[info] using latest recorded ISO: {recorded_path}")
                  return recorded_path
              if requested == recorded_path:
                  return recorded_path

    if requested.exists():
        return requested

    matches = sorted(
        (repo_root / "build").glob("CapyOS-Installer-UEFI.iso.*.iso"),
        key=lambda path: path.stat().st_mtime,
        reverse=True,
    )
    if matches:
        print(f"[info] requested ISO not found; using newest fallback: {matches[0]}")
        return matches[0].resolve()
    return requested


def _read_fat16_dir(
    image: bytes,
    *,
    bytes_per_sector: int,
    sectors_per_cluster: int,
    root_lba: int | None = None,
    root_dir_sectors: int | None = None,
    data_lba: int,
    cluster: int | None = None,
) -> dict[str, tuple[int, int, int]]:
    if cluster is None:
        if root_lba is None or root_dir_sectors is None:
            raise ValueError("root directory geometry is required")
        offset = root_lba * bytes_per_sector
        length = root_dir_sectors * bytes_per_sector
    else:
        offset = (data_lba + (cluster - 2) * sectors_per_cluster) * bytes_per_sector
        length = sectors_per_cluster * bytes_per_sector

    entries: dict[str, tuple[int, int, int]] = {}
    directory = image[offset : offset + length]
    for i in range(0, len(directory), 32):
        entry = directory[i : i + 32]
        if len(entry) < 32 or entry[0] == 0x00:
            break
        if entry[0] == 0xE5 or entry[11] == 0x0F:
            continue
        name = entry[0:8].decode("ascii", errors="ignore").rstrip()
        ext = entry[8:11].decode("ascii", errors="ignore").rstrip()
        full_name = f"{name}.{ext}" if ext else name
        attr = entry[11]
        start_cluster = struct.unpack_from("<H", entry, 26)[0]
        size = struct.unpack_from("<I", entry, 28)[0]
        entries[full_name] = (attr, start_cluster, size)
    return entries


def _require_dir_entry(
    entries: dict[str, tuple[int, int, int]], name: str, *, directory: bool = False
) -> tuple[int, int, int]:
    entry = entries.get(name)
    if entry is None:
        raise FileNotFoundError(f"missing {name} in efiboot.img")
    if directory and (entry[0] & 0x10) == 0:
        raise FileNotFoundError(f"{name} is not a directory in efiboot.img")
    return entry


def _validate_iso_tree(repo_root: Path) -> None:
    iso_root = (repo_root / "build/iso-uefi-root").resolve()
    marker = iso_root / "CAPYOS.INI"
    if not marker.exists():
        raise FileNotFoundError(f"missing ISO installer marker: {marker}")

    iso_tree = iso_root / "boot"
    required = ("capyos64.bin", "manifest.bin", "capycfg.bin")
    for name in required:
        path = iso_tree / name
        if not path.exists():
            raise FileNotFoundError(f"missing ISO tree payload: {path}")


def _validate_efiboot_image(repo_root: Path) -> None:
    image_path = (repo_root / "build/iso-uefi-root/EFI/BOOT/efiboot.img").resolve()
    if not image_path.exists():
        raise FileNotFoundError(f"missing efiboot image: {image_path}")

    image = image_path.read_bytes()
    bytes_per_sector = struct.unpack_from("<H", image, 11)[0]
    sectors_per_cluster = image[13]
    reserved = struct.unpack_from("<H", image, 14)[0]
    num_fats = image[16]
    root_entries = struct.unpack_from("<H", image, 17)[0]
    fat_size = struct.unpack_from("<H", image, 22)[0]
    root_dir_sectors = (root_entries * 32 + bytes_per_sector - 1) // bytes_per_sector
    root_lba = reserved + num_fats * fat_size
    data_lba = root_lba + root_dir_sectors

    root = _read_fat16_dir(
        image,
        bytes_per_sector=bytes_per_sector,
        sectors_per_cluster=sectors_per_cluster,
        root_lba=root_lba,
        root_dir_sectors=root_dir_sectors,
        data_lba=data_lba,
    )
    efi = _require_dir_entry(root, "EFI", directory=True)
    boot = _require_dir_entry(root, "BOOT", directory=True)
    _require_dir_entry(root, "CAPYOS.INI")

    efi_dir = _read_fat16_dir(
        image,
        bytes_per_sector=bytes_per_sector,
        sectors_per_cluster=sectors_per_cluster,
        data_lba=data_lba,
        cluster=efi[1],
    )
    efi_boot = _require_dir_entry(efi_dir, "BOOT", directory=True)
    efi_boot_dir = _read_fat16_dir(
        image,
        bytes_per_sector=bytes_per_sector,
        sectors_per_cluster=sectors_per_cluster,
        data_lba=data_lba,
        cluster=efi_boot[1],
    )
    _require_dir_entry(efi_boot_dir, "BOOTX64.EFI")

    boot_dir = _read_fat16_dir(
        image,
        bytes_per_sector=bytes_per_sector,
        sectors_per_cluster=sectors_per_cluster,
        data_lba=data_lba,
        cluster=boot[1],
    )
    _require_dir_entry(boot_dir, "CAPYOS64.BIN")
    _require_dir_entry(boot_dir, "MANIFEST.BIN")
    _require_dir_entry(boot_dir, "CAPYCFG.BIN")


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
