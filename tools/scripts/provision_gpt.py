#!/usr/bin/env python3
"""
Provision a disk image with GPT for NoirOS (ESP + BOOT + DATA), without sudo.

This script writes:
  - GPT with 3 partitions (ESP FAT32, BOOT raw, DATA raw)
  - ESP formatted as FAT32 (implemented here) with:
      \\EFI\\BOOT\\BOOTX64.EFI
      \\boot\\noiros64.bin
      \\boot\\manifest.bin (optional)
  - BOOT partition (raw):
      LBA+0: manifest.bin (512 bytes)
      LBA+1: kernel ELF (raw bytes)

It can operate on:
  - Raw .img files
  - Fixed VHD files (Hyper-V) by preserving the 512-byte footer at the end.
    Dynamic VHD is not supported.

Usage:
  python3 tools/scripts/provision_gpt.py --img build/disk-gpt.img --size 2G \
    --bootx64 build/boot/uefi_loader.efi --kernel build/noiros64.bin \
    --auto-manifest --confirm
"""
import argparse
import os
import subprocess
import sys
import tempfile
import struct
from pathlib import Path

SECTOR = 512
VHD_FOOTER_SIZE = 512
VHD_SIG = b"conectix"

# NoirOS BOOT partition type GUID (canonical form):
# on-disk bytes used in loader: 76 0b 98 04 42 10 4c 9b 86 1f 11 e0 29 ea c1 01
NOIROS_BOOT_GUID = "04980b76-1042-9b4c-861f-11e029eac101"

def run(cmd):
    print(f"[cmd] {' '.join(cmd)}")
    subprocess.check_call(cmd)


def create_image(img: Path, size: str, allow_existing: bool):
    if img.exists() and not allow_existing:
        raise SystemExit(f"[err] Image already exists: {img} (use --allow-existing)")
    if not img.exists():
        run(["truncate", "-s", size, str(img)])


def partition_gpt(img: Path, esp_size: str, boot_size: str):
    # Clear old partitions and create GPT with 3 partitions: ESP/BOOT/DATA
    run(["sgdisk", "--zap-all", str(img)])
    # ESP
    run(["sgdisk", "-n", f"1:0:+{esp_size}", "-t", "1:ef00", "-c", "1:ESP", str(img)])
    # BOOT
    run(["sgdisk", "-n", f"2:0:+{boot_size}", "-t", f"2:{NOIROS_BOOT_GUID}", "-c", "2:BOOT", str(img)])
    # DATA (rest)
    run(["sgdisk", "-n", "3:0:0", "-t", "3:8300", "-c", "3:DATA", str(img)])


def detect_vhd(img: Path):
    if not img.exists():
        return None
    if img.stat().st_size < VHD_FOOTER_SIZE:
        return None
    with img.open("rb") as f:
        head = f.read(8)
        if head == VHD_SIG:
            raise SystemExit("[err] Dynamic VHD not supported (header at start). Use fixed VHD or raw .img.")
        f.seek(-VHD_FOOTER_SIZE, os.SEEK_END)
        footer = f.read(VHD_FOOTER_SIZE)
        if footer[:8] == VHD_SIG:
            return footer
    return None


def parse_gpt(img: Path):
    with img.open("rb") as f:
        f.seek(SECTOR)  # LBA1
        hdr = f.read(SECTOR)
        if hdr[:8] != b"EFI PART":
            raise SystemExit("[err] GPT header not found after partitioning.")
        part_entry_lba = struct.unpack_from("<Q", hdr, 72)[0]
        num_entries = struct.unpack_from("<I", hdr, 80)[0]
        entry_size = struct.unpack_from("<I", hdr, 84)[0]
        f.seek(part_entry_lba * SECTOR)
        entries_bytes = f.read(min(num_entries * entry_size, SECTOR * 8))
        def entry_at(idx):
            off = idx * entry_size
            e = entries_bytes[off:off+entry_size]
            ptype = e[0:16]
            first = struct.unpack_from("<Q", e, 32)[0]
            last = struct.unpack_from("<Q", e, 40)[0]
            return ptype, first, last
        p1 = entry_at(0)
        p2 = entry_at(1)
        if p1[1] == 0 or p2[1] == 0:
            raise SystemExit("[err] GPT entries missing (expected partitions 1 and 2).")
        return p1, p2


def fat32_build_boot_sector(total_sectors, spc, reserved, num_fats, fat_size, root_cluster, label):
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
    return bs


def fat32_build_fsinfo():
    fs = bytearray(SECTOR)
    struct.pack_into("<I", fs, 0, 0x41615252)
    struct.pack_into("<I", fs, 484, 0x61417272)
    struct.pack_into("<I", fs, 488, 0xFFFFFFFF)
    struct.pack_into("<I", fs, 492, 0xFFFFFFFF)
    struct.pack_into("<I", fs, 508, 0xAA550000)
    return fs


def dirent(name, ext, attr, cluster, size):
    e = bytearray(32)
    name8 = name.upper().ljust(8)[:8]
    ext3 = ext.upper().ljust(3)[:3]
    e[0:8] = name8.encode("ascii")
    e[8:11] = ext3.encode("ascii")
    e[11] = attr
    struct.pack_into("<H", e, 20, (cluster >> 16) & 0xFFFF)
    struct.pack_into("<H", e, 26, cluster & 0xFFFF)
    struct.pack_into("<I", e, 28, size)
    return e


def fat32_write_volume(f, base_off, total_sectors, files, *, spc=8, label="ESP"):
    # FAT32 parameters (simple)
    reserved = 32
    num_fats = 2
    root_cluster = 2

    # compute fat size
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

    # FAT32 requires cluster count >= 65525; otherwise the volume is FAT12/16.
    if cluster_count < 65525:
        raise SystemExit(
            f"[err] FAT32 volume muito pequena: clusters={cluster_count}. "
            f"Aumente o tamanho (total_sectors={total_sectors}) ou reduza --spc."
        )

    fat_entries = [0] * (cluster_count + 2)
    fat_entries[0] = 0x0FFFFFF8
    fat_entries[1] = 0x0FFFFFFF
    fat_entries[root_cluster] = 0x0FFFFFFF
    next_free = 3

    def alloc(bytes_needed):
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
    manifest_chain = []
    if manifest_data is not None:
        manifest_start, manifest_chain = alloc(len(manifest_data))

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
    efi_boot[64:96] = dirent("BOOTX64", "EFI", 0x20, bootx64_start, len(files["BOOTX64.EFI"]))

    bootdir = bytearray(cluster_bytes)
    bootdir[0:32] = dirent(".", "", 0x10, bootdir_cl, 0)
    bootdir[32:64] = dirent("..", "", 0x10, root_cluster, 0)
    bootdir[64:96] = dirent("NOIROS64", "BIN", 0x20, kernel_start, len(files["NOIROS64.BIN"]))
    if manifest_data is not None:
        bootdir[96:128] = dirent("MANIFEST", "BIN", 0x20, manifest_start, len(manifest_data))

    # write boot sector + fsinfo + backups
    bs = fat32_build_boot_sector(total_sectors, spc, reserved, num_fats, fat_size, root_cluster, label)
    fsinfo = fat32_build_fsinfo()
    f.seek(base_off + 0)
    f.write(bs)
    f.seek(base_off + SECTOR * 1)
    f.write(fsinfo)
    f.seek(base_off + SECTOR * 6)
    f.write(bs)
    f.seek(base_off + SECTOR * 7)
    f.write(fsinfo)

    # build FAT bytes
    fat_bytes = bytearray(fat_size * SECTOR)
    for i, v in enumerate(fat_entries):
        struct.pack_into("<I", fat_bytes, i * 4, v)
    # write FAT1/FAT2
    fat1_off = base_off + reserved * SECTOR
    fat2_off = fat1_off + fat_size * SECTOR
    f.seek(fat1_off)
    f.write(fat_bytes)
    f.seek(fat2_off)
    f.write(fat_bytes)

    def write_cluster(cluster, data):
        sec = data_start + (cluster - 2) * spc
        off = base_off + sec * SECTOR
        f.seek(off)
        f.write(data.ljust(cluster_bytes, b"\x00"))

    # write directories
    write_cluster(root_cluster, root)
    write_cluster(efi_cl, efi)
    write_cluster(efi_boot_cl, efi_boot)
    write_cluster(bootdir_cl, bootdir)

    # write file chains
    def write_chain(chain, data):
        for idx, cl in enumerate(chain):
            chunk = data[idx * cluster_bytes:(idx + 1) * cluster_bytes]
            write_cluster(cl, chunk)

    write_chain(bootx64_chain, files["BOOTX64.EFI"])
    write_chain(kernel_chain, files["NOIROS64.BIN"])
    if manifest_data is not None:
        write_chain(manifest_chain, manifest_data)


def write_boot_partition_raw(img: Path, boot_lba: int, boot_sectors: int, manifest: bytes, kernel: bytes):
    total = boot_sectors * SECTOR
    needed = SECTOR + len(kernel)
    if needed > total:
        raise SystemExit("[err] Kernel+manifest do not fit in BOOT partition.")
    with img.open("r+b") as f:
        f.seek(boot_lba * SECTOR)
        f.write(manifest.ljust(SECTOR, b"\x00"))
        f.write(kernel)
    print("[ok] BOOT partition: manifest@0, kernel@+1 sector")


def main():
    ap = argparse.ArgumentParser(description="Provision GPT disk image for NoirOS (ESP/BOOT/DATA).")
    ap.add_argument("--img", default="build/disk-gpt.img", type=Path, help="Path to disk image to create")
    ap.add_argument("--size", default="2G", help="Total disk size (e.g., 2G)")
    ap.add_argument("--esp-size", default="512M", help="ESP size (default 512M)")
    ap.add_argument("--boot-size", default="256M", help="BOOT size (default 256M)")
    ap.add_argument("--bootx64", type=Path, help="Path to BOOTX64.EFI to copy into ESP")
    ap.add_argument("--kernel", type=Path, help="Path to kernel (noiros64.bin) to copy into ESP/boot")
    ap.add_argument("--manifest", type=Path, help="Path to manifest.bin to place at BOOT partition start")
    ap.add_argument("--auto-manifest", action="store_true",
                    help="Gera manifest automaticamente a partir do kernel (LBA=1 na partição BOOT)")
    ap.add_argument("--allow-existing", action="store_true",
                    help="Permite reprovisionar uma imagem existente (CUIDADO: reescreve GPT/ESP/BOOT)")
    ap.add_argument("--confirm", action="store_true", help="Actually perform actions (otherwise dry-run aborts)")
    args = ap.parse_args()

    if not args.confirm:
        print("[err] Add --confirm to proceed (safety).")
        sys.exit(1)

    if args.img.exists() and not args.allow_existing:
        print(f"[err] Image already exists: {args.img} (use --allow-existing)")
        sys.exit(1)

    tmp_manifest = None
    if args.auto_manifest:
        if not args.kernel:
            print("[err] --auto-manifest requer --kernel")
            sys.exit(1)
        tmp_manifest = Path(tempfile.mktemp(prefix="manifest_", suffix=".bin"))
        run(["python3", "tools/scripts/gen_manifest.py",
             "--out", str(tmp_manifest),
             "--kernel", str(args.kernel),
             "--kernel-lba", "1"])
        args.manifest = tmp_manifest

    vhd_footer = detect_vhd(args.img)
    orig_size = args.img.stat().st_size if args.img.exists() else None
    if vhd_footer:
        # Temporariamente remove footer para não destruir GPT backup.
        run(["truncate", "-s", str(orig_size - VHD_FOOTER_SIZE), str(args.img)])

    try:
        create_image(args.img, args.size, args.allow_existing)
        partition_gpt(args.img, args.esp_size, args.boot_size)
        (p1_type, p1_first, p1_last), (p2_type, p2_first, p2_last) = parse_gpt(args.img)
        esp_lba = p1_first
        esp_secs = p1_last - p1_first + 1
        boot_lba = p2_first
        boot_secs = p2_last - p2_first + 1

        if not args.bootx64 or not args.kernel:
            raise SystemExit("[err] --bootx64 e --kernel são obrigatórios para popular a ESP.")
        if not args.manifest or not args.manifest.exists():
            raise SystemExit("[err] manifest.bin ausente. Use --manifest ou --auto-manifest.")

        files = {
            "BOOTX64.EFI": args.bootx64.read_bytes(),
            "NOIROS64.BIN": args.kernel.read_bytes(),
            "MANIFEST.BIN": args.manifest.read_bytes(),
        }

        with args.img.open("r+b") as f:
            fat32_write_volume(f, esp_lba * SECTOR, esp_secs, files)
        print("[ok] ESP FAT32 criado e arquivos copiados.")

        write_boot_partition_raw(args.img, boot_lba, boot_secs, files["MANIFEST.BIN"], files["NOIROS64.BIN"])
    finally:
        if vhd_footer and orig_size is not None:
            with args.img.open("ab") as f:
                f.write(vhd_footer)
            run(["truncate", "-s", str(orig_size), str(args.img)])
        if tmp_manifest and tmp_manifest.exists():
            tmp_manifest.unlink()
    print(f"[done] Image ready: {args.img}")


if __name__ == "__main__":
    main()
