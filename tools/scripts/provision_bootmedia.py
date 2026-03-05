#!/usr/bin/env python3
"""Boot config and FAT32 helpers for NoirOS disk provisioning."""

from __future__ import annotations

import struct
from pathlib import Path

SECTOR = 512

BOOT_CONFIG_MAGIC = 0xB001CF61
BOOT_CONFIG_VERSION = 2
BOOT_CONFIG_FLAG_HAS_VOLUME_KEY = 0x0001
BOOT_CONFIG_LAYOUT_LEN = 16
BOOT_CONFIG_KEY_LEN = 64
BOOT_CONFIG_RESERVED_LEN = 424


def normalize_keyboard_layout(raw: str | None) -> str:
    layout = (raw or "us").strip().lower()
    if not layout:
        layout = "us"
    if len(layout) >= BOOT_CONFIG_LAYOUT_LEN:
        raise SystemExit(
            f"[err] keyboard layout too long: {layout!r} (max {BOOT_CONFIG_LAYOUT_LEN - 1})"
        )
    for ch in layout:
        if not (ch.isalnum() or ch in "-_"):
            raise SystemExit(f"[err] invalid keyboard layout: {layout!r}")
    return layout


def normalize_volume_key(raw: str | None) -> str:
    if raw is None:
        return ""
    out: list[str] = []
    for ch in raw:
        if ch in "- \t\r\n":
            continue
        if not ch.isalnum():
            raise SystemExit(
                "[err] volume key must contain only letters/numbers (hyphens optional)."
            )
        out.append(ch.upper())

    key = "".join(out)
    if not key:
        return ""
    if len(key) < 8:
        raise SystemExit("[err] volume key too short after normalization (min 8).")
    if len(key) >= BOOT_CONFIG_KEY_LEN:
        raise SystemExit(
            f"[err] volume key too long (max {BOOT_CONFIG_KEY_LEN - 1} chars)."
        )
    return key


def build_boot_config(layout: str | None, volume_key: str | None) -> bytes:
    layout_norm = normalize_keyboard_layout(layout)
    key_norm = normalize_volume_key(volume_key)
    flags = BOOT_CONFIG_FLAG_HAS_VOLUME_KEY if key_norm else 0

    payload = struct.pack(
        "<IHH16s64s424s",
        BOOT_CONFIG_MAGIC,
        BOOT_CONFIG_VERSION,
        flags,
        layout_norm.encode("ascii").ljust(BOOT_CONFIG_LAYOUT_LEN, b"\x00"),
        key_norm.encode("ascii").ljust(BOOT_CONFIG_KEY_LEN, b"\x00"),
        b"\x00" * BOOT_CONFIG_RESERVED_LEN,
    )
    if len(payload) != SECTOR:
        raise SystemExit(
            f"[err] CAPYCFG.BIN invalid size: {len(payload)} (expected {SECTOR})"
        )
    return payload


def fat32_build_boot_sector(
    total_sectors: int,
    spc: int,
    reserved: int,
    num_fats: int,
    fat_size: int,
    root_cluster: int,
    label: str,
) -> bytes:
    bs = bytearray(SECTOR)
    bs[0:3] = b"\xEB\x58\x90"
    bs[3:11] = b"MSWIN4.1"
    struct.pack_into("<H", bs, 11, SECTOR)
    bs[13] = spc
    struct.pack_into("<H", bs, 14, reserved)
    bs[16] = num_fats
    struct.pack_into("<H", bs, 17, 0)  # root entries
    struct.pack_into("<H", bs, 19, 0)  # totsec16
    bs[21] = 0xF8
    struct.pack_into("<H", bs, 22, 0)  # fatsz16
    struct.pack_into("<H", bs, 24, 63)
    struct.pack_into("<H", bs, 26, 255)
    struct.pack_into("<I", bs, 28, 0)  # hidden
    struct.pack_into("<I", bs, 32, total_sectors)
    struct.pack_into("<I", bs, 36, fat_size)
    struct.pack_into("<H", bs, 40, 0)
    struct.pack_into("<H", bs, 42, 0)
    struct.pack_into("<I", bs, 44, root_cluster)
    struct.pack_into("<H", bs, 48, 1)  # fsinfo
    struct.pack_into("<H", bs, 50, 6)  # backup boot
    bs[64] = 0x80
    bs[66] = 0x29
    struct.pack_into("<I", bs, 67, 0x12345678)
    lab = (label.upper()[:11]).ljust(11)
    bs[71:82] = lab.encode("ascii")
    bs[82:90] = b"FAT32   "
    bs[510] = 0x55
    bs[511] = 0xAA
    return bytes(bs)


def fat32_build_fsinfo() -> bytes:
    fs = bytearray(SECTOR)
    struct.pack_into("<I", fs, 0, 0x41615252)
    struct.pack_into("<I", fs, 484, 0x61417272)
    struct.pack_into("<I", fs, 488, 0xFFFFFFFF)
    struct.pack_into("<I", fs, 492, 0xFFFFFFFF)
    struct.pack_into("<I", fs, 508, 0xAA550000)
    return bytes(fs)


def dirent(name: str, ext: str, attr: int, cluster: int, size: int) -> bytes:
    e = bytearray(32)
    name8 = name.upper().ljust(8)[:8]
    ext3 = ext.upper().ljust(3)[:3]
    e[0:8] = name8.encode("ascii")
    e[8:11] = ext3.encode("ascii")
    e[11] = attr
    struct.pack_into("<H", e, 20, (cluster >> 16) & 0xFFFF)
    struct.pack_into("<H", e, 26, cluster & 0xFFFF)
    struct.pack_into("<I", e, 28, size)
    return bytes(e)


def fat32_write_volume(
    f,
    base_off: int,
    total_sectors: int,
    files: dict[str, bytes],
    *,
    spc: int = 8,
    label: str = "ESP",
) -> None:
    reserved = 32
    num_fats = 2
    root_cluster = 2

    fat_size = 1
    while True:
        data_sectors = total_sectors - reserved - num_fats * fat_size
        clusters = data_sectors // spc
        need = (clusters + 2) * 4
        new_fat = (need + SECTOR - 1) // SECTOR
        if new_fat == fat_size:
            cluster_count = clusters
            break
        fat_size = new_fat

    cluster_bytes = spc * SECTOR
    data_start = reserved + num_fats * fat_size

    if cluster_count < 65525:
        raise SystemExit(
            f"[err] FAT32 volume too small: clusters={cluster_count}. "
            f"Increase size (total_sectors={total_sectors}) or reduce --spc."
        )

    fat_entries = [0] * (cluster_count + 2)
    fat_entries[0] = 0x0FFFFFF8
    fat_entries[1] = 0x0FFFFFFF
    fat_entries[root_cluster] = 0x0FFFFFFF
    next_free = 3

    def alloc(bytes_needed: int) -> tuple[int, list[int]]:
        nonlocal next_free
        need_clusters = max(1, (bytes_needed + cluster_bytes - 1) // cluster_bytes)
        start = next_free
        chain = list(range(start, start + need_clusters))
        next_free += need_clusters
        for i, c in enumerate(chain):
            fat_entries[c] = chain[i + 1] if i + 1 < len(chain) else 0x0FFFFFFF
        return start, chain

    # allocate dirs
    efi_cl, _ = alloc(cluster_bytes)
    efi_boot_cl, _ = alloc(cluster_bytes)
    bootdir_cl, _ = alloc(cluster_bytes)

    # allocate files
    bootx64_start, bootx64_chain = alloc(len(files["BOOTX64.EFI"]))
    kernel_start, kernel_chain = alloc(len(files["NOIROS64.BIN"]))

    manifest_data = files.get("MANIFEST.BIN")
    manifest_start = 0
    manifest_chain: list[int] = []
    if manifest_data is not None:
        manifest_start, manifest_chain = alloc(len(manifest_data))

    bootcfg_data = files.get("CAPYCFG.BIN")
    bootcfg_start = 0
    bootcfg_chain: list[int] = []
    if bootcfg_data is not None:
        bootcfg_start, bootcfg_chain = alloc(len(bootcfg_data))

    # build directories
    root = bytearray(cluster_bytes)
    root[0:32] = dirent("EFI", "", 0x10, efi_cl, 0)
    root[32:64] = dirent("BOOT", "", 0x10, bootdir_cl, 0)

    efi = bytearray(cluster_bytes)
    efi[0:32] = dirent(".", "", 0x10, efi_cl, 0)
    efi[32:64] = dirent("..", "", 0x10, root_cluster, 0)
    efi[64:96] = dirent("BOOT", "", 0x10, efi_boot_cl, 0)

    efi_boot = bytearray(cluster_bytes)
    efi_boot[0:32] = dirent(".", "", 0x10, efi_boot_cl, 0)
    efi_boot[32:64] = dirent("..", "", 0x10, efi_cl, 0)
    efi_boot[64:96] = dirent(
        "BOOTX64",
        "EFI",
        0x20,
        bootx64_start,
        len(files["BOOTX64.EFI"]),
    )

    bootdir = bytearray(cluster_bytes)
    bootdir[0:32] = dirent(".", "", 0x10, bootdir_cl, 0)
    bootdir[32:64] = dirent("..", "", 0x10, root_cluster, 0)
    bootdir[64:96] = dirent("NOIROS64", "BIN", 0x20, kernel_start, len(files["NOIROS64.BIN"]))

    off = 96
    if manifest_data is not None:
        bootdir[off : off + 32] = dirent(
            "MANIFEST",
            "BIN",
            0x20,
            manifest_start,
            len(manifest_data),
        )
        off += 32
    if bootcfg_data is not None:
        bootdir[off : off + 32] = dirent(
            "CAPYCFG",
            "BIN",
            0x20,
            bootcfg_start,
            len(bootcfg_data),
        )

    # write boot sector + fsinfo + backups
    bs = fat32_build_boot_sector(
        total_sectors,
        spc,
        reserved,
        num_fats,
        fat_size,
        root_cluster,
        label,
    )
    fsinfo = fat32_build_fsinfo()
    f.seek(base_off)
    f.write(bs)
    f.seek(base_off + SECTOR)
    f.write(fsinfo)
    f.seek(base_off + SECTOR * 6)
    f.write(bs)
    f.seek(base_off + SECTOR * 7)
    f.write(fsinfo)

    fat_bytes = bytearray(fat_size * SECTOR)
    for i, v in enumerate(fat_entries):
        struct.pack_into("<I", fat_bytes, i * 4, v)

    fat1_off = base_off + reserved * SECTOR
    fat2_off = fat1_off + fat_size * SECTOR
    f.seek(fat1_off)
    f.write(fat_bytes)
    f.seek(fat2_off)
    f.write(fat_bytes)

    def write_cluster(cluster: int, data: bytes) -> None:
        sec = data_start + (cluster - 2) * spc
        off_local = base_off + sec * SECTOR
        f.seek(off_local)
        f.write(data.ljust(cluster_bytes, b"\x00"))

    def write_chain(chain: list[int], data: bytes) -> None:
        for idx, cl in enumerate(chain):
            chunk = data[idx * cluster_bytes : (idx + 1) * cluster_bytes]
            write_cluster(cl, chunk)

    write_cluster(root_cluster, root)
    write_cluster(efi_cl, efi)
    write_cluster(efi_boot_cl, efi_boot)
    write_cluster(bootdir_cl, bootdir)

    write_chain(bootx64_chain, files["BOOTX64.EFI"])
    write_chain(kernel_chain, files["NOIROS64.BIN"])
    if manifest_data is not None:
        write_chain(manifest_chain, manifest_data)
    if bootcfg_data is not None:
        write_chain(bootcfg_chain, bootcfg_data)


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
    with img.open("r+b") as f:
        f.seek(boot_lba * SECTOR)
        f.write(manifest.ljust(SECTOR, b"\x00"))
        f.write(kernel)
    print("[ok] BOOT partition: manifest@0, kernel@+1 sector")
