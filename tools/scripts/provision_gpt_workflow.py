#!/usr/bin/env python3
"""Workflow helpers for GPT image provisioning."""

from __future__ import annotations

import argparse
import os
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

from provision_bootmedia import (
    SECTOR,
    build_boot_config,
    fat32_write_volume,
    write_boot_partition_raw,
)
from provision_gpt_core import (
    VHD_FOOTER_SIZE,
    create_image,
    detect_vhd,
    parse_gpt,
    partition_gpt,
    scrub_data_partition_for_first_boot,
    set_file_size,
    parse_size_to_bytes,
)


def run(cmd: list[str]) -> None:
    print(f"[cmd] {' '.join(cmd)}")
    subprocess.check_call(cmd)


def maybe_generate_manifest(args: argparse.Namespace) -> Path | None:
    if not args.auto_manifest:
        return None
    if not args.kernel:
        print("[err] --auto-manifest requires --kernel")
        sys.exit(1)
    # Use mkstemp instead of mktemp to avoid the TOCTOU race window that
    # py/insecure-temporary-file flags: mktemp only returns a candidate
    # name and races with concurrent processes, while mkstemp opens the
    # file atomically and returns an fd we close immediately because the
    # gen_manifest.py invocation below truncates+writes the same path.
    tmp_fd, tmp_path = tempfile.mkstemp(prefix="manifest_", suffix=".bin")
    os.close(tmp_fd)
    tmp_manifest = Path(tmp_path)
    run(
        [
            "python3",
            "tools/scripts/gen_manifest.py",
            "--out",
            str(tmp_manifest),
            "--kernel",
            str(args.kernel),
            "--kernel-lba",
            "1",
        ]
    )
    args.manifest = tmp_manifest
    return tmp_manifest


def validate_kernel_elf(kernel: bytes) -> None:
    if len(kernel) < 64 or kernel[:4] != b"\x7fELF" or kernel[4] != 2 or kernel[5] != 1 or int.from_bytes(kernel[18:20], "little") != 62:
        raise SystemExit("[err] Kernel failed ELF64 preflight.")
    phoff = struct.unpack_from("<Q", kernel, 32)[0]
    phentsize = struct.unpack_from("<H", kernel, 54)[0]
    phnum = struct.unpack_from("<H", kernel, 56)[0]
    entry = struct.unpack_from("<Q", kernel, 24)[0]
    if phnum == 0 or phnum > 32 or phentsize != 56 or phoff > len(kernel) or phnum > (len(kernel) - phoff) // phentsize:
        raise SystemExit("[err] Kernel program-header table is invalid.")
    ranges: list[tuple[int, int]] = []
    for index in range(phnum):
        base = phoff + index * phentsize
        p_type, _flags, p_offset, _vaddr, paddr, filesz, memsz, _align = struct.unpack_from("<IIQQQQQQ", kernel, base)
        if p_type != 1 or memsz == 0:
            continue
        if filesz > memsz or p_offset > len(kernel) or filesz > len(kernel) - p_offset or paddr + memsz < paddr:
            raise SystemExit("[err] Kernel PT_LOAD segment is invalid.")
        ranges.append((paddr, paddr + memsz))
    if not ranges or not any(start <= entry < end for start, end in ranges):
        raise SystemExit("[err] Kernel entry is outside PT_LOAD segments.")


def provision_gpt_image(args: argparse.Namespace) -> None:
    if not args.bootx64 or not args.kernel or not args.bootx64.exists() or not args.kernel.exists():
        raise SystemExit("[err] Existing --bootx64 and --kernel are required before target writes.")
    bootx64 = args.bootx64.read_bytes()
    kernel = args.kernel.read_bytes()
    if len(bootx64) < 0x40 or bootx64[:2] != b"MZ":
        raise SystemExit("[err] BOOTX64.EFI failed PE preflight.")
    pe_offset = int.from_bytes(bootx64[0x3C:0x40], "little")
    if pe_offset + 26 > len(bootx64) or bootx64[pe_offset:pe_offset + 4] != b"PE\0\0" or bootx64[pe_offset + 4:pe_offset + 6] != b"d\x86" or bootx64[pe_offset + 24:pe_offset + 26] != b"\x0b\x02":
        raise SystemExit("[err] BOOTX64.EFI is not an x86-64 EFI PE image.")
    validate_kernel_elf(kernel)
    boot_bytes = parse_size_to_bytes(args.boot_size)
    if not kernel or (len(kernel) + SECTOR - 1) // SECTOR > ((boot_bytes // SECTOR - 2) // 2 - 1):
        raise SystemExit("[err] Kernel does not fit slot A in requested BOOT size.")
    tmp_manifest: Path | None = maybe_generate_manifest(args)
    if not args.manifest or not args.manifest.exists():
        raise SystemExit("[err] manifest.bin missing. Use --manifest or --auto-manifest.")
    manifest = args.manifest.read_bytes()
    if not manifest or len(manifest) > SECTOR:
        raise SystemExit("[err] manifest.bin must be 1..512 bytes.")
    boot_cfg = build_boot_config(args.keyboard_layout, args.language,
                                 args.volume_key)
    files = {
        "BOOTX64.EFI": bootx64,
        "CAPYOS64.BIN": kernel,
        "MANIFEST.BIN": manifest,
        "CAPYCFG.BIN": boot_cfg,
    }
    esp_sectors_plan = parse_size_to_bytes(args.esp_size) // SECTOR
    fat_size = 1
    while True:
        clusters = (esp_sectors_plan - 32 - 2 * fat_size) // 8
        next_fat = ((clusters + 2) * 4 + SECTOR - 1) // SECTOR
        if next_fat == fat_size:
            break
        fat_size = next_fat
    needed_clusters = 4 + sum(max(1, (len(data) + 4095) // 4096)
                              for data in files.values())
    if clusters < 65525 or needed_clusters + 2 > clusters + 2:
        raise SystemExit("[err] ESP FAT32 capacity preflight failed.")

    vhd_footer = detect_vhd(args.img)
    orig_size = args.img.stat().st_size if args.img.exists() else None
    if vhd_footer:
        # Temporarily remove footer so GPT backup is not overwritten.
        set_file_size(args.img, orig_size - VHD_FOOTER_SIZE)

    try:
        create_image(args.img, args.size, args.allow_existing)
        if args.allow_existing:
            zero_chunk = b"\x00" * (1024 * 1024)
            with args.img.open("r+b") as target:
                remaining = args.img.stat().st_size
                target.seek(0)
                while remaining:
                    chunk = zero_chunk if remaining >= len(zero_chunk) else zero_chunk[:remaining]
                    target.write(chunk)
                    remaining -= len(chunk)
                target.flush()
                os.fsync(target.fileno())
        partition_gpt(args.img, args.esp_size, args.boot_size)
        (p1_first, p1_last), (p2_first, p2_last), (p3_first, p3_last) = parse_gpt(args.img)

        esp_lba = p1_first
        esp_secs = p1_last - p1_first + 1
        boot_lba = p2_first
        boot_secs = p2_last - p2_first + 1
        data_lba = p3_first
        data_secs = p3_last - p3_first + 1

        if not args.skip_data_scrub:
            scrub_data_partition_for_first_boot(args.img, data_lba, data_secs)
            print("[ok] DATA scrub (head/tail/mid) complete.")

        with args.img.open("r+b") as f:
            fat32_write_volume(f, esp_lba * SECTOR, esp_secs, files)
            f.flush()
            os.fsync(f.fileno())
        print("[ok] ESP FAT32 created and files copied.")

        if args.volume_key:
            print("[ok] CAPYCFG.BIN includes provisioned volume key.")
        else:
            print("[warn] CAPYCFG.BIN has no volume key (manual first-boot unlock path).")

        write_boot_partition_raw(
            args.img, boot_lba, boot_secs, files["MANIFEST.BIN"], files["CAPYOS64.BIN"]
        )
    finally:
        if vhd_footer and orig_size is not None:
            with args.img.open("ab") as f:
                f.write(vhd_footer)
            set_file_size(args.img, orig_size)
        if tmp_manifest and tmp_manifest.exists():
            tmp_manifest.unlink()

    print(f"[done] Image ready: {args.img}")
