#!/usr/bin/env python3
"""Boot config and FAT32 helpers for CAPYOS disk provisioning."""

from __future__ import annotations

from pathlib import Path

from provision_boot_config import SECTOR, build_boot_config
from provision_fat32 import fat32_write_volume


def write_boot_partition_raw(
    img: Path,
    boot_lba: int,
    boot_sectors: int,
    manifest: bytes,
    kernel: bytes,
) -> None:
    total = boot_sectors * SECTOR
    needed = SECTOR + len(kernel)
    if needed > total:
        raise SystemExit("[err] Kernel+manifest do not fit in BOOT partition.")
    with img.open("r+b") as handle:
        handle.seek(boot_lba * SECTOR)
        handle.write(manifest.ljust(SECTOR, b"\x00"))
        handle.write(kernel)
    print("[ok] BOOT partition: manifest@0, kernel@+1 sector")
