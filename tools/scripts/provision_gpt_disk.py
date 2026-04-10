#!/usr/bin/env python3
"""Disk image and scrub helpers for GPT provisioning."""

from __future__ import annotations

import os
from pathlib import Path

SECTOR = 512
VHD_FOOTER_SIZE = 512
VHD_SIG = b"conectix"


def parse_size_to_bytes(raw: str) -> int:
    text = str(raw).strip()
    if not text:
        raise SystemExit("[err] invalid size: empty")
    mult = 1
    units = {
        "k": 1024,
        "kb": 1024,
        "m": 1024**2,
        "mb": 1024**2,
        "g": 1024**3,
        "gb": 1024**3,
        "t": 1024**4,
        "tb": 1024**4,
    }
    i = 0
    while i < len(text) and (text[i].isdigit() or text[i] == "."):
        i += 1
    num = text[:i]
    suf = text[i:].strip().lower()
    if suf.endswith("ib"):
        suf = suf[:-2]
    elif suf.endswith("b"):
        suf = suf[:-1]
    if suf:
        if suf not in units:
            raise SystemExit(f"[err] invalid size unit: {raw!r}")
        mult = units[suf]
    if not num:
        raise SystemExit(f"[err] invalid size value: {raw!r}")
    try:
        value = float(num)
    except ValueError as exc:
        raise SystemExit(f"[err] invalid size value: {raw!r}") from exc
    out = int(value * mult)
    if out <= 0:
        raise SystemExit(f"[err] invalid non-positive size: {raw!r}")
    return out


def set_file_size(path: Path, new_size: int) -> None:
    with path.open("r+b") as f:
        f.truncate(new_size)


def create_image(img: Path, size: str, allow_existing: bool) -> None:
    if img.exists() and not allow_existing:
        raise SystemExit(f"[err] Image already exists: {img} (use --allow-existing)")
    if not img.exists():
        size_bytes = parse_size_to_bytes(size)
        with img.open("wb") as f:
            f.truncate(size_bytes)


def detect_vhd(img: Path) -> bytes | None:
    if not img.exists():
        return None
    if img.stat().st_size < VHD_FOOTER_SIZE:
        return None
    with img.open("rb") as f:
        head = f.read(8)
        if head == VHD_SIG:
            raise SystemExit(
                "[err] Dynamic VHD not supported (header at start). Use fixed VHD or raw .img."
            )
        f.seek(-VHD_FOOTER_SIZE, os.SEEK_END)
        footer = f.read(VHD_FOOTER_SIZE)
        if footer[:8] == VHD_SIG:
            return footer
    return None


def wipe_blocks(f, start_lba: int, sectors: int, chunk_sectors: int = 2048) -> None:
    if sectors <= 0:
        return
    chunk = b"\x00" * (chunk_sectors * SECTOR)
    done = 0
    while done < sectors:
        nsec = min(chunk_sectors, sectors - done)
        f.seek((start_lba + done) * SECTOR)
        f.write(chunk[: nsec * SECTOR])
        done += nsec


def scrub_data_partition_for_first_boot(
    img: Path, data_lba: int, data_sectors: int
) -> None:
    if data_sectors <= 0:
        return
    edge_span = 4096  # 2 MiB with 512-byte sectors
    mid_span = 16  # 8 KiB around midpoint

    with img.open("r+b") as f:
        head_secs = min(data_sectors, edge_span)
        wipe_blocks(f, data_lba, head_secs)

        if data_sectors > head_secs:
            tail_start = data_lba + data_sectors - head_secs
            wipe_blocks(f, tail_start, head_secs)

        if data_sectors > (mid_span + 2):
            mid_rel = data_sectors // 2
            if mid_rel + mid_span >= data_sectors:
                mid_rel = data_sectors - mid_span
            wipe_blocks(f, data_lba + mid_rel, mid_span)
