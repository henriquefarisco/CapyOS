#!/usr/bin/env python3
"""
Create an EFI El Torito boot image containing CAPYOS BOOTX64.EFI + kernel.

Hyper-V Gen2 expects the UEFI El Torito entry to point to a FAT filesystem image,
not directly to BOOTX64.EFI.

This script generates a small **FAT16** image (no sudo, no external mkfs needed).

It also embeds a small marker file (`CAPYOS.INI`) so BOOTX64.EFI can detect
that it's running from the installer ISO and enter "installer mode".
"""
import argparse
from pathlib import Path
import struct

SECTOR = 512


def ceil_div(a: int, b: int) -> int:
    return (a + b - 1) // b


def dirent16(name, ext, attr, cluster, size):
    e = bytearray(32)
    name8 = name.upper().ljust(8)[:8]
    ext3 = ext.upper().ljust(3)[:3]
    e[0:8] = name8.encode("ascii")
    e[8:11] = ext3.encode("ascii")
    e[11] = attr
    struct.pack_into("<H", e, 20, 0)  # FAT16: high cluster word = 0
    struct.pack_into("<H", e, 26, cluster & 0xFFFF)
    struct.pack_into("<I", e, 28, size)
    return e


def fat16_build_boot_sector(total_sectors, spc, reserved, num_fats, fat_size, root_entries, label):
    bs = bytearray(SECTOR)
    bs[0:3] = b"\xEB\x3C\x90"
    bs[3:11] = b"MSWIN4.1"
    struct.pack_into("<H", bs, 11, SECTOR)
    bs[13] = spc
    struct.pack_into("<H", bs, 14, reserved)
    bs[16] = num_fats
    struct.pack_into("<H", bs, 17, root_entries)
    # totsec16/totsec32
    if total_sectors <= 0xFFFF:
        struct.pack_into("<H", bs, 19, total_sectors)
        struct.pack_into("<I", bs, 32, 0)
    else:
        struct.pack_into("<H", bs, 19, 0)
        struct.pack_into("<I", bs, 32, total_sectors)
    bs[21] = 0xF8
    struct.pack_into("<H", bs, 22, fat_size)
    struct.pack_into("<H", bs, 24, 63)
    struct.pack_into("<H", bs, 26, 255)
    struct.pack_into("<I", bs, 28, 0)  # hidden
    bs[36] = 0x80  # drive num
    bs[38] = 0x29  # boot sig
    struct.pack_into("<I", bs, 39, 0x12345678)
    lab = (label.upper()[:11]).ljust(11)
    bs[43:54] = lab.encode("ascii")
    bs[54:62] = b"FAT16   "
    bs[510] = 0x55
    bs[511] = 0xAA
    return bs


def fat16_write_volume(f, total_sectors, files, *, spc=2, label="EFIBOOT"):
    reserved = 1
    num_fats = 2
    root_entries = 512

    root_dir_sectors = ceil_div(root_entries * 32, SECTOR)

    # compute fat size iteratively
    fat_size = 1
    while True:
        data_sectors = total_sectors - reserved - num_fats * fat_size - root_dir_sectors
        clusters = data_sectors // spc
        need = (clusters + 2) * 2  # FAT16: 2 bytes per entry
        new_fat = ceil_div(need, SECTOR)
        if new_fat == fat_size:
            cluster_count = clusters
            break
        fat_size = new_fat

    if cluster_count < 4085 or cluster_count >= 65525:
        raise SystemExit(
            f"[err] FAT16 invÃƒÂ¡lido: clusters={cluster_count}. "
            f"Ajuste --size/--spc (ex.: --size 8M --spc 2)."
        )

    cluster_bytes = spc * SECTOR
    fat1_lba = reserved
    fat2_lba = fat1_lba + fat_size
    root_lba = fat2_lba + fat_size
    data_lba = root_lba + root_dir_sectors

    # FAT entries
    fat_entries = [0] * (cluster_count + 2)
    fat_entries[0] = 0xFFF8
    fat_entries[1] = 0xFFFF
    next_free = 2

    def alloc(bytes_needed):
        nonlocal next_free
        need_clusters = max(1, ceil_div(bytes_needed, cluster_bytes))
        start = next_free
        chain = list(range(start, start + need_clusters))
        next_free += need_clusters
        for i, c in enumerate(chain):
            fat_entries[c] = chain[i + 1] if i + 1 < len(chain) else 0xFFFF
        return start, chain

    # allocate dirs
    efi_cl, _ = alloc(cluster_bytes)
    efi_boot_cl, _ = alloc(cluster_bytes)
    bootdir_cl, _ = alloc(cluster_bytes)

    # allocate files
    bootx64_start, bootx64_chain = alloc(len(files["BOOTX64.EFI"]))
    kernel_start, kernel_chain = alloc(len(files["CAPYOS64.BIN"]))
    manifest_data = files.get("MANIFEST.BIN")
    manifest_start = 0
    manifest_chain = []
    if manifest_data is not None:
        manifest_start, manifest_chain = alloc(len(manifest_data))

    marker_data = files.get("CAPYOS.INI")
    marker_start = 0
    marker_chain = []
    if marker_data is not None:
        marker_start, marker_chain = alloc(len(marker_data))

    def cluster_lba(cluster):
        return data_lba + (cluster - 2) * spc

    def write_cluster(cluster, data):
        off = cluster_lba(cluster) * SECTOR
        f.seek(off)
        f.write(data.ljust(cluster_bytes, b"\x00"))

    def write_chain(chain, data):
        for idx, cl in enumerate(chain):
            chunk = data[idx * cluster_bytes:(idx + 1) * cluster_bytes]
            write_cluster(cl, chunk)

    # --- Root directory (fixed area)
    root = bytearray(root_dir_sectors * SECTOR)
    root[0:32] = dirent16("EFI", "", 0x10, efi_cl, 0)
    root[32:64] = dirent16("BOOT", "", 0x10, bootdir_cl, 0)
    if marker_data is not None:
        root[64:96] = dirent16("CAPYOS", "INI", 0x20, marker_start, len(marker_data))

    # --- EFI directory
    efi = bytearray(cluster_bytes)
    efi[0:32] = dirent16(".", "", 0x10, efi_cl, 0)
    efi[32:64] = dirent16("..", "", 0x10, 0, 0)  # parent = root
    efi[64:96] = dirent16("BOOT", "", 0x10, efi_boot_cl, 0)

    # --- EFI\\BOOT directory
    efi_boot = bytearray(cluster_bytes)
    efi_boot[0:32] = dirent16(".", "", 0x10, efi_boot_cl, 0)
    efi_boot[32:64] = dirent16("..", "", 0x10, efi_cl, 0)
    efi_boot[64:96] = dirent16("BOOTX64", "EFI", 0x20, bootx64_start, len(files["BOOTX64.EFI"]))

    # --- \\BOOT directory
    bootdir = bytearray(cluster_bytes)
    bootdir[0:32] = dirent16(".", "", 0x10, bootdir_cl, 0)
    bootdir[32:64] = dirent16("..", "", 0x10, 0, 0)
    bootdir[64:96] = dirent16("CAPYOS64", "BIN", 0x20, kernel_start, len(files["CAPYOS64.BIN"]))
    if manifest_data is not None:
        bootdir[96:128] = dirent16("MANIFEST", "BIN", 0x20, manifest_start, len(manifest_data))

    # --- Write boot sector
    bs = fat16_build_boot_sector(total_sectors, spc, reserved, num_fats, fat_size, root_entries, label)
    f.seek(0)
    f.write(bs)

    # --- Write FATs
    fat_bytes = bytearray(fat_size * SECTOR)
    for i, v in enumerate(fat_entries):
        struct.pack_into("<H", fat_bytes, i * 2, v)
    f.seek(fat1_lba * SECTOR)
    f.write(fat_bytes)
    f.seek(fat2_lba * SECTOR)
    f.write(fat_bytes)

    # --- Write root directory
    f.seek(root_lba * SECTOR)
    f.write(root)

    # --- Write directory clusters
    write_cluster(efi_cl, efi)
    write_cluster(efi_boot_cl, efi_boot)
    write_cluster(bootdir_cl, bootdir)

    # --- Write file chains
    write_chain(bootx64_chain, files["BOOTX64.EFI"])
    write_chain(kernel_chain, files["CAPYOS64.BIN"])
    if manifest_data is not None:
        write_chain(manifest_chain, manifest_data)
    if marker_data is not None:
        write_chain(marker_chain, marker_data)


def parse_size(s: str) -> int:
    s = s.strip().upper()
    mul = 1
    if s.endswith("K"):
        mul = 1024
        s = s[:-1]
    elif s.endswith("M"):
        mul = 1024 * 1024
        s = s[:-1]
    elif s.endswith("G"):
        mul = 1024 * 1024 * 1024
        s = s[:-1]
    try:
        n = int(s, 10)
    except ValueError as e:
        raise SystemExit(f"[err] invalid --size: {s!r}") from e
    return n * mul


def main() -> None:
    ap = argparse.ArgumentParser(description="Create an EFI FAT boot image (efiboot.img).")
    ap.add_argument("--out", required=True, type=Path, help="Output path (e.g. build/iso-uefi-root/EFI/BOOT/efiboot.img)")
    ap.add_argument("--size", default="8M", help="Image size (e.g. 8M, 16M). Default: %(default)s")
    ap.add_argument("--spc", type=int, default=2, help="Sectors per cluster (FAT16). Default: %(default)s")
    ap.add_argument("--label", default="EFIBOOT", help="Volume label (max 11 chars). Default: %(default)s")
    ap.add_argument("--bootx64", required=True, type=Path, help="Path to BOOTX64.EFI")
    ap.add_argument("--kernel", required=True, type=Path, help="Path to kernel (capyos64.bin)")
    ap.add_argument("--manifest", type=Path, help="Optional manifest.bin to place in /boot")
    args = ap.parse_args()

    size_bytes = parse_size(args.size)
    if size_bytes % SECTOR != 0:
        raise SystemExit(f"[err] --size must be a multiple of {SECTOR} bytes")
    total_sectors = size_bytes // SECTOR

    args.out.parent.mkdir(parents=True, exist_ok=True)

    files = {
        "BOOTX64.EFI": args.bootx64.read_bytes(),
        "CAPYOS64.BIN": args.kernel.read_bytes(),
        "CAPYOS.INI": b"INSTALLER=1\n",
    }
    if args.manifest:
        files["MANIFEST.BIN"] = args.manifest.read_bytes()

    # Create/overwrite the image file
    with args.out.open("wb") as f:
        f.truncate(size_bytes)

    with args.out.open("r+b") as f:
        fat16_write_volume(f, total_sectors, files, spc=args.spc, label=args.label)

    print(f"[ok] EFI boot image ready: {args.out} ({size_bytes} bytes)")


if __name__ == "__main__":
    main()
