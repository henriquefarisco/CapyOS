#!/usr/bin/env python3
"""Core GPT and disk provisioning helpers for CAPYOS images."""

from __future__ import annotations

import os
import struct
import uuid
import zlib
from pathlib import Path

SECTOR = 512
VHD_FOOTER_SIZE = 512
VHD_SIG = b"conectix"

# CAPYOS BOOT partition type GUID (canonical form):
# on-disk bytes used in loader: 76 0b 98 04 42 10 4c 9b 86 1f 11 e0 29 ea c1 01
CAPYOS_BOOT_GUID = "04980b76-1042-9b4c-861f-11e029eac101"


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


def align_up(value: int, align: int) -> int:
    if align <= 1:
        return value
    rem = value % align
    if rem == 0:
        return value
    return value + (align - rem)


def guid_to_bytes_le(guid_text: str) -> bytes:
    return uuid.UUID(guid_text).bytes_le


def gpt_part_name_bytes(name: str) -> bytes:
    raw = (name or "")[:36].encode("utf-16le")
    return raw.ljust(72, b"\x00")


def build_gpt_header(
    current_lba: int,
    backup_lba: int,
    first_usable_lba: int,
    last_usable_lba: int,
    disk_guid_le: bytes,
    part_entry_lba: int,
    num_part_entries: int,
    part_entry_size: int,
    part_array_crc32: int,
) -> bytes:
    header = bytearray(SECTOR)
    struct.pack_into(
        "<8sIIIIQQQQ16sQIII",
        header,
        0,
        b"EFI PART",
        0x00010000,
        92,
        0,
        0,
        current_lba,
        backup_lba,
        first_usable_lba,
        last_usable_lba,
        disk_guid_le,
        part_entry_lba,
        num_part_entries,
        part_entry_size,
        part_array_crc32,
    )
    crc = zlib.crc32(header[:92]) & 0xFFFFFFFF
    struct.pack_into("<I", header, 16, crc)
    return bytes(header)


def write_protective_mbr(f, total_lba: int) -> None:
    mbr = bytearray(SECTOR)
    # Single 0xEE partition protecting the whole disk.
    entry_off = 446
    mbr[entry_off + 0] = 0x00
    mbr[entry_off + 1 : entry_off + 4] = b"\x00\x02\x00"
    mbr[entry_off + 4] = 0xEE
    mbr[entry_off + 5 : entry_off + 8] = b"\xFF\xFF\xFF"
    struct.pack_into("<I", mbr, entry_off + 8, 1)
    size_lba = min(total_lba - 1, 0xFFFFFFFF)
    struct.pack_into("<I", mbr, entry_off + 12, size_lba)
    mbr[510] = 0x55
    mbr[511] = 0xAA
    f.seek(0)
    f.write(mbr)


def partition_gpt(img: Path, esp_size: str, boot_size: str) -> None:
    esp_bytes = parse_size_to_bytes(esp_size)
    boot_bytes = parse_size_to_bytes(boot_size)
    esp_sectors = max(1, (esp_bytes + SECTOR - 1) // SECTOR)
    boot_sectors = max(1, (boot_bytes + SECTOR - 1) // SECTOR)

    total_bytes = img.stat().st_size
    if total_bytes < SECTOR * 4096:
        raise SystemExit("[err] disk image too small for GPT layout.")
    if (total_bytes % SECTOR) != 0:
        raise SystemExit("[err] disk image size is not sector-aligned.")

    total_lba = total_bytes // SECTOR
    last_lba = total_lba - 1

    part_entry_size = 128
    num_part_entries = 128
    part_entries_bytes = num_part_entries * part_entry_size
    part_entries_sectors = (part_entries_bytes + SECTOR - 1) // SECTOR

    first_usable_lba = 2 + part_entries_sectors
    backup_part_lba = last_lba - part_entries_sectors
    last_usable_lba = backup_part_lba - 1
    if first_usable_lba >= last_usable_lba:
        raise SystemExit("[err] disk image too small after GPT metadata reservation.")

    align_lba = 2048

    p1_first = align_up(first_usable_lba, align_lba)
    p1_last = p1_first + esp_sectors - 1

    p2_first = align_up(p1_last + 1, align_lba)
    p2_last = p2_first + boot_sectors - 1

    p3_first = align_up(p2_last + 1, align_lba)
    p3_last = last_usable_lba

    if p1_last >= p2_first or p2_last >= p3_first or p3_first > p3_last:
        raise SystemExit("[err] partition layout does not fit on disk.")

    entries = bytearray(part_entries_bytes)

    def write_entry(
        index: int,
        type_guid: str,
        first_lba: int,
        last_lba: int,
        name: str,
    ) -> None:
        if index < 0 or index >= num_part_entries:
            return
        off = index * part_entry_size
        unique_guid = uuid.uuid4().bytes_le
        struct.pack_into(
            "<16s16sQQQ72s",
            entries,
            off,
            guid_to_bytes_le(type_guid),
            unique_guid,
            first_lba,
            last_lba,
            0,
            gpt_part_name_bytes(name),
        )

    write_entry(0, "c12a7328-f81f-11d2-ba4b-00a0c93ec93b", p1_first, p1_last, "ESP")
    write_entry(1, CAPYOS_BOOT_GUID, p2_first, p2_last, "BOOT")
    write_entry(2, "0fc63daf-8483-4772-8e79-3d69d8477de4", p3_first, p3_last, "DATA")

    entries_crc = zlib.crc32(entries) & 0xFFFFFFFF
    disk_guid_le = uuid.uuid4().bytes_le

    primary_header = build_gpt_header(
        current_lba=1,
        backup_lba=last_lba,
        first_usable_lba=first_usable_lba,
        last_usable_lba=last_usable_lba,
        disk_guid_le=disk_guid_le,
        part_entry_lba=2,
        num_part_entries=num_part_entries,
        part_entry_size=part_entry_size,
        part_array_crc32=entries_crc,
    )
    backup_header = build_gpt_header(
        current_lba=last_lba,
        backup_lba=1,
        first_usable_lba=first_usable_lba,
        last_usable_lba=last_usable_lba,
        disk_guid_le=disk_guid_le,
        part_entry_lba=backup_part_lba,
        num_part_entries=num_part_entries,
        part_entry_size=part_entry_size,
        part_array_crc32=entries_crc,
    )

    with img.open("r+b") as f:
        write_protective_mbr(f, total_lba)
        f.seek(2 * SECTOR)
        f.write(entries)
        f.seek(backup_part_lba * SECTOR)
        f.write(entries)
        f.seek(SECTOR)
        f.write(primary_header)
        f.seek(last_lba * SECTOR)
        f.write(backup_header)


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


def parse_gpt(img: Path) -> tuple[tuple[int, int], tuple[int, int], tuple[int, int]]:
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

        def entry_at(idx: int) -> tuple[int, int]:
            off = idx * entry_size
            e = entries_bytes[off : off + entry_size]
            first = struct.unpack_from("<Q", e, 32)[0]
            last = struct.unpack_from("<Q", e, 40)[0]
            return first, last

        p1 = entry_at(0)
        p2 = entry_at(1)
        p3 = entry_at(2)
        if p1[0] == 0 or p2[0] == 0 or p3[0] == 0:
            raise SystemExit(
                "[err] GPT entries missing (expected partitions 1, 2 and 3)."
            )
        return p1, p2, p3


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
