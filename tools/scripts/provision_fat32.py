#!/usr/bin/env python3
from __future__ import annotations

import struct

from provision_boot_config import SECTOR


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
    struct.pack_into("<H", bs, 17, 0)
    struct.pack_into("<H", bs, 19, 0)
    bs[21] = 0xF8
    struct.pack_into("<H", bs, 22, 0)
    struct.pack_into("<H", bs, 24, 63)
    struct.pack_into("<H", bs, 26, 255)
    struct.pack_into("<I", bs, 28, 0)
    struct.pack_into("<I", bs, 32, total_sectors)
    struct.pack_into("<I", bs, 36, fat_size)
    struct.pack_into("<H", bs, 40, 0)
    struct.pack_into("<H", bs, 42, 0)
    struct.pack_into("<I", bs, 44, root_cluster)
    struct.pack_into("<H", bs, 48, 1)
    struct.pack_into("<H", bs, 50, 6)
    bs[64] = 0x80
    bs[66] = 0x29
    struct.pack_into("<I", bs, 67, 0x12345678)
    label_text = (label.upper()[:11]).ljust(11)
    bs[71:82] = label_text.encode("ascii")
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
    entry = bytearray(32)
    name8 = name.upper().ljust(8)[:8]
    ext3 = ext.upper().ljust(3)[:3]
    entry[0:8] = name8.encode("ascii")
    entry[8:11] = ext3.encode("ascii")
    entry[11] = attr
    struct.pack_into("<H", entry, 20, (cluster >> 16) & 0xFFFF)
    struct.pack_into("<H", entry, 26, cluster & 0xFFFF)
    struct.pack_into("<I", entry, 28, size)
    return bytes(entry)


def fat32_write_volume(
    handle,
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
        for idx, cluster in enumerate(chain):
            fat_entries[cluster] = (
                chain[idx + 1] if idx + 1 < len(chain) else 0x0FFFFFFF
            )
        return start, chain

    efi_cl, _ = alloc(cluster_bytes)
    efi_boot_cl, _ = alloc(cluster_bytes)
    bootdir_cl, _ = alloc(cluster_bytes)
    bootx64_start, bootx64_chain = alloc(len(files["BOOTX64.EFI"]))
    kernel_start, kernel_chain = alloc(len(files["CAPYOS64.BIN"]))

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
        "BOOTX64", "EFI", 0x20, bootx64_start, len(files["BOOTX64.EFI"])
    )

    bootdir = bytearray(cluster_bytes)
    bootdir[0:32] = dirent(".", "", 0x10, bootdir_cl, 0)
    bootdir[32:64] = dirent("..", "", 0x10, root_cluster, 0)
    bootdir[64:96] = dirent(
        "CAPYOS64", "BIN", 0x20, kernel_start, len(files["CAPYOS64.BIN"])
    )

    off = 96
    if manifest_data is not None:
        bootdir[off:off + 32] = dirent(
            "MANIFEST", "BIN", 0x20, manifest_start, len(manifest_data)
        )
        off += 32
    if bootcfg_data is not None:
        bootdir[off:off + 32] = dirent(
            "CAPYCFG", "BIN", 0x20, bootcfg_start, len(bootcfg_data)
        )

    bs = fat32_build_boot_sector(
        total_sectors, spc, reserved, num_fats, fat_size, root_cluster, label
    )
    fsinfo = fat32_build_fsinfo()
    handle.seek(base_off)
    handle.write(bs)
    handle.seek(base_off + SECTOR)
    handle.write(fsinfo)
    handle.seek(base_off + SECTOR * 6)
    handle.write(bs)
    handle.seek(base_off + SECTOR * 7)
    handle.write(fsinfo)

    fat_bytes = bytearray(fat_size * SECTOR)
    for idx, value in enumerate(fat_entries):
        struct.pack_into("<I", fat_bytes, idx * 4, value)

    fat1_off = base_off + reserved * SECTOR
    fat2_off = fat1_off + fat_size * SECTOR
    handle.seek(fat1_off)
    handle.write(fat_bytes)
    handle.seek(fat2_off)
    handle.write(fat_bytes)

    def write_cluster(cluster: int, data: bytes) -> None:
        sector = data_start + (cluster - 2) * spc
        off_local = base_off + sector * SECTOR
        handle.seek(off_local)
        handle.write(data.ljust(cluster_bytes, b"\x00"))

    def write_chain(chain: list[int], data: bytes) -> None:
        for idx, cluster in enumerate(chain):
            chunk = data[idx * cluster_bytes:(idx + 1) * cluster_bytes]
            write_cluster(cluster, chunk)

    write_cluster(root_cluster, root)
    write_cluster(efi_cl, efi)
    write_cluster(efi_boot_cl, efi_boot)
    write_cluster(bootdir_cl, bootdir)
    write_chain(bootx64_chain, files["BOOTX64.EFI"])
    write_chain(kernel_chain, files["CAPYOS64.BIN"])
    if manifest_data is not None:
        write_chain(manifest_chain, manifest_data)
    if bootcfg_data is not None:
        write_chain(bootcfg_chain, bootcfg_data)
