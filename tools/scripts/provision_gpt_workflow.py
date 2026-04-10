#!/usr/bin/env python3
"""Workflow helpers for GPT image provisioning."""

from __future__ import annotations

import argparse
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
    tmp_manifest = Path(tempfile.mktemp(prefix="manifest_", suffix=".bin"))
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


def provision_gpt_image(args: argparse.Namespace) -> None:
    tmp_manifest: Path | None = maybe_generate_manifest(args)

    vhd_footer = detect_vhd(args.img)
    orig_size = args.img.stat().st_size if args.img.exists() else None
    if vhd_footer:
        # Temporarily remove footer so GPT backup is not overwritten.
        set_file_size(args.img, orig_size - VHD_FOOTER_SIZE)

    try:
        create_image(args.img, args.size, args.allow_existing)
        partition_gpt(args.img, args.esp_size, args.boot_size)
        (p1_first, p1_last), (p2_first, p2_last), (p3_first, p3_last) = parse_gpt(args.img)

        esp_lba = p1_first
        esp_secs = p1_last - p1_first + 1
        boot_lba = p2_first
        boot_secs = p2_last - p2_first + 1
        data_lba = p3_first
        data_secs = p3_last - p3_first + 1

        if not args.bootx64 or not args.kernel:
            raise SystemExit("[err] --bootx64 and --kernel are required to populate ESP.")
        if not args.manifest or not args.manifest.exists():
            raise SystemExit("[err] manifest.bin missing. Use --manifest or --auto-manifest.")

        boot_cfg = build_boot_config(
            args.keyboard_layout, args.language, args.volume_key
        )
        files = {
            "BOOTX64.EFI": args.bootx64.read_bytes(),
            "CAPYOS64.BIN": args.kernel.read_bytes(),
            "MANIFEST.BIN": args.manifest.read_bytes(),
            "CAPYCFG.BIN": boot_cfg,
        }

        if not args.skip_data_scrub:
            scrub_data_partition_for_first_boot(args.img, data_lba, data_secs)
            print("[ok] DATA scrub (head/tail/mid) complete.")

        with args.img.open("r+b") as f:
            fat32_write_volume(f, esp_lba * SECTOR, esp_secs, files)
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
