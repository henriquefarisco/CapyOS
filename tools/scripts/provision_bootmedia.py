#!/usr/bin/env python3
"""Boot config and FAT32 helpers for CAPYOS disk provisioning."""

from __future__ import annotations

import hashlib
import os
import re
import struct
import zlib
from pathlib import Path

from provision_boot_config import SECTOR, build_boot_config
from provision_fat32 import fat32_write_volume


def _version() -> str:
    header = Path(__file__).resolve().parents[2] / "include/core/version.h"
    match = re.search(r'^#define CAPYOS_VERSION_FULL\s+"([^"]+)"', header.read_text(), re.MULTILINE)
    if not match:
        raise SystemExit("[err] CAPYOS_VERSION_FULL not found.")
    return match.group(1)


def _layout(boot_sectors: int) -> tuple[tuple[int, int, int], tuple[int, int, int], tuple[int, int]]:
    if boot_sectors < 6:
        raise SystemExit("[err] BOOT partition is too small for A/B.")
    usable = boot_sectors - 2
    first = usable // 2
    second = usable - first
    if first < 2 or second < 2:
        raise SystemExit("[err] BOOT partition cannot fit A/B regions.")
    return (0, 1, first - 1), (first, first + 1, second - 1), (boot_sectors - 2, boot_sectors - 1)


def _put_u32(buf: bytearray, offset: int, value: int) -> None:
    struct.pack_into("<I", buf, offset, value)


def _put_u64(buf: bytearray, offset: int, value: int) -> None:
    struct.pack_into("<Q", buf, offset, value)


def _crc(buf: bytearray) -> None:
    _put_u32(buf, 508, zlib.crc32(buf[:508]) & 0xFFFFFFFF)


def _version_bytes(version: str) -> bytes:
    encoded = version.encode("ascii")
    if not encoded or len(encoded) >= 40 or any(ch < 0x20 or ch > 0x7E for ch in encoded):
        raise SystemExit("[err] Invalid A/B version string.")
    return encoded + b"\x00" * (40 - len(encoded))


def _slot_header(boot_sectors: int, slot: int, region: tuple[int, int, int],
                 kernel: bytes, version: str) -> bytes:
    header_lba, payload_lba, capacity = region
    header = bytearray(SECTOR)
    header[:8] = b"CAPYSLT0"
    _put_u32(header, 8, 0)
    _put_u32(header, 12, SECTOR)
    _put_u32(header, 16, slot)
    _put_u32(header, 20, boot_sectors)
    _put_u32(header, 24, header_lba)
    _put_u32(header, 28, payload_lba)
    _put_u32(header, 32, capacity)
    _put_u32(header, 36, len(kernel))
    header[40:80] = _version_bytes(version)
    header[80:112] = hashlib.sha256(kernel).digest()
    _crc(header)
    return bytes(header)


def _encode_slot(record: bytearray, offset: int, state: int,
                 region: tuple[int, int, int], kernel: bytes,
                 version: str, active: bool) -> None:
    header_lba, payload_lba, capacity = region
    _put_u32(record, offset, state)
    _put_u32(record, offset + 4, 1 if active else 0)
    _put_u32(record, offset + 8, 1 if active else 0)
    _put_u32(record, offset + 16, header_lba)
    _put_u32(record, offset + 20, payload_lba)
    _put_u32(record, offset + 24, capacity)
    if active:
        _put_u32(record, offset + 28, len(kernel))
        _put_u32(record, offset + 36, 1)
        record[offset + 40:offset + 80] = _version_bytes(version)
        record[offset + 80:offset + 112] = hashlib.sha256(kernel).digest()


def _control(boot_sectors: int, generation: int,
             a: tuple[int, int, int], b: tuple[int, int, int],
             controls: tuple[int, int], kernel: bytes, version: str) -> bytes:
    record = bytearray(SECTOR)
    record[:8] = b"CAPYAB00"
    _put_u32(record, 8, 0)
    _put_u32(record, 12, SECTOR)
    _put_u64(record, 16, generation)
    _put_u32(record, 24, 0)
    _put_u32(record, 28, 0)
    _put_u32(record, 32, 0)
    _put_u32(record, 36, 0)
    _put_u32(record, 40, 0xFFFFFFFF)
    _put_u32(record, 44, 0)
    _put_u32(record, 48, boot_sectors)
    _put_u32(record, 52, controls[0])
    _put_u32(record, 56, controls[1])
    _encode_slot(record, 64, 2, a, kernel, version, True)
    _encode_slot(record, 192, 0, b, b"", version, False)
    _crc(record)
    return bytes(record)


def write_boot_partition_raw(
    img: Path,
    boot_lba: int,
    boot_sectors: int,
    manifest: bytes,
    kernel: bytes,
) -> None:
    del manifest
    a, b, controls = _layout(boot_sectors)
    sectors = (len(kernel) + SECTOR - 1) // SECTOR
    if not kernel or sectors > a[2]:
        raise SystemExit("[err] Kernel does not fit in slot A.")
    version = _version()
    padded = kernel.ljust(sectors * SECTOR, b"\x00")
    header = _slot_header(boot_sectors, 0, a, kernel, version)
    control1 = _control(boot_sectors, 1, a, b, controls, kernel, version)
    control2 = _control(boot_sectors, 2, a, b, controls, kernel, version)
    with img.open("r+b") as handle:
        handle.seek(boot_lba * SECTOR)
        zero_chunk = b"\x00" * (1024 * 1024)
        remaining = boot_sectors * SECTOR
        while remaining:
            chunk = zero_chunk if remaining >= len(zero_chunk) else zero_chunk[:remaining]
            handle.write(chunk)
            remaining -= len(chunk)
        handle.flush()
        os.fsync(handle.fileno())
        handle.seek((boot_lba + a[1]) * SECTOR)
        handle.write(padded)
        handle.flush()
        os.fsync(handle.fileno())
        handle.seek((boot_lba + a[0]) * SECTOR)
        handle.write(header)
        handle.flush()
        os.fsync(handle.fileno())
        handle.seek((boot_lba + controls[0]) * SECTOR)
        handle.write(control1)
        handle.flush()
        os.fsync(handle.fileno())
        handle.seek((boot_lba + controls[1]) * SECTOR)
        handle.write(control2)
        handle.flush()
        os.fsync(handle.fileno())
        handle.seek((boot_lba + a[0]) * SECTOR)
        if handle.read(SECTOR) != header:
            raise SystemExit("[err] Slot A header readback mismatch.")
        handle.seek((boot_lba + a[1]) * SECTOR)
        payload = handle.read(len(padded))
        if payload != padded or hashlib.sha256(payload[:len(kernel)]).digest() != hashlib.sha256(kernel).digest():
            raise SystemExit("[err] Slot A payload readback mismatch.")
        handle.seek((boot_lba + controls[0]) * SECTOR)
        if handle.read(SECTOR) != control1:
            raise SystemExit("[err] Control copy 0 readback mismatch.")
        handle.seek((boot_lba + controls[1]) * SECTOR)
        if handle.read(SECTOR) != control2:
            raise SystemExit("[err] Control copy 1 readback mismatch.")
    print("[ok] BOOT partition: CAPYSLT0 A + CAPYAB00 generations 1/2")
