#!/usr/bin/env python3
"""
Provision a disk image with GPT for CAPYOS (ESP + BOOT + DATA), without sudo.

This script writes:
  - GPT with 3 partitions (ESP FAT32, BOOT raw, DATA raw)
  - ESP formatted as FAT32 with:
      \\EFI\\BOOT\\BOOTX64.EFI
      \\BOOT\\CAPYOS64.BIN
      \\BOOT\\MANIFEST.BIN
      \\BOOT\\CAPYCFG.BIN (keyboard layout + optional lab-only volume key)
  - BOOT partition (raw):
      LBA+0: manifest.bin (512 bytes)
      LBA+1: kernel ELF (raw bytes)
  - DATA scrub (head/tail/mid) to avoid stale-volume false positives

It can operate on:
  - Raw .img files
  - Fixed VHD files (Hyper-V) by preserving the 512-byte footer at the end
    Dynamic VHD is not supported.

Usage:
  python3 tools/scripts/provision_gpt.py --img build/disk-gpt.img --size 2G \
    --bootx64 build/boot/uefi_loader.efi --kernel build/capyos64.bin \
    --auto-manifest --volume-key ABCD-1234-EFGH-5678 \
    --allow-plain-volume-key --confirm
"""

from __future__ import annotations

from provision_gpt_cli import parse_args, validate_args
from provision_gpt_workflow import provision_gpt_image


def main() -> None:
    args = parse_args()
    validate_args(args)
    provision_gpt_image(args)


if __name__ == "__main__":
    main()
