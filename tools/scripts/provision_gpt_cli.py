#!/usr/bin/env python3
"""CLI helpers for GPT image provisioning."""

from __future__ import annotations

import argparse
import sys
from pathlib import Path


def parse_args() -> argparse.Namespace:
    ap = argparse.ArgumentParser(
        description="Provision GPT disk image for CAPYOS (ESP/BOOT/DATA)."
    )
    ap.add_argument(
        "--img",
        default="build/disk-gpt.img",
        type=Path,
        help="Path to disk image to create",
    )
    ap.add_argument("--size", default="2G", help="Total disk size (e.g., 2G)")
    ap.add_argument("--esp-size", default="512M", help="ESP size (default 512M)")
    ap.add_argument("--boot-size", default="256M", help="BOOT size (default 256M)")
    ap.add_argument("--bootx64", type=Path, help="Path to BOOTX64.EFI to copy into ESP")
    ap.add_argument(
        "--kernel",
        type=Path,
        help="Path to kernel (capyos64.bin) to copy into ESP/BOOT",
    )
    ap.add_argument(
        "--manifest",
        type=Path,
        help="Path to manifest.bin to place at BOOT partition start",
    )
    ap.add_argument(
        "--auto-manifest",
        action="store_true",
        help="Generate manifest from kernel (LBA=1 in BOOT partition)",
    )
    ap.add_argument(
        "--keyboard-layout",
        default="us",
        help="Keyboard layout persisted in BOOT/CAPYCFG.BIN (default: us)",
    )
    ap.add_argument(
        "--language",
        default="en",
        help="Installer/system default language persisted in BOOT/CAPYCFG.BIN (default: en)",
    )
    ap.add_argument(
        "--volume-key",
        default=None,
        help="Lab-only volume key persisted in BOOT/CAPYCFG.BIN (letters/numbers, hyphens optional)",
    )
    ap.add_argument(
        "--allow-plain-volume-key",
        action="store_true",
        help="Explicitly allow storing a clear-text volume key in CAPYCFG.BIN for labs/smoke tests",
    )
    ap.add_argument(
        "--skip-data-scrub",
        action="store_true",
        help="Skip DATA head/tail/mid scrub after GPT (debug only)",
    )
    ap.add_argument(
        "--allow-existing",
        action="store_true",
        help="Allow reprovision of existing image (WARNING: rewrites GPT/ESP/BOOT)",
    )
    ap.add_argument("--confirm", action="store_true", help="Actually perform actions")
    return ap.parse_args()


def validate_args(args: argparse.Namespace) -> None:
    if not args.confirm:
        print("[err] Add --confirm to proceed (safety).")
        sys.exit(1)

    if args.img.exists() and not args.allow_existing:
        print(f"[err] Image already exists: {args.img} (use --allow-existing)")
        sys.exit(1)
    if args.volume_key and not args.allow_plain_volume_key:
        print(
            "[err] Refusing to persist a clear-text volume key in CAPYCFG.BIN "
            "without --allow-plain-volume-key (lab/smoke only)."
        )
        sys.exit(1)
