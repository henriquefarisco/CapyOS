#!/usr/bin/env python3
from __future__ import annotations

import mmap
import struct

LOG_LINES: list[str] = []
SECTOR_SIZE = 512

PLACE_KS = b"\xFE\x0F\xDC\xBA"  # 0xBADC0FFE
PLACE_KL = b"\xCE\xFA\xED\xFE"  # 0xFEEDFACE
PLACE_S2L = b"\xEF\xBE\xAD\xDE"  # 0xDEADBEEF
PLACE_S2S = b"\xBE\xBA\xFE\xCA"  # 0xCAFEBABE

ESP_GUID = bytes(
    [0x28, 0x73, 0x2A, 0xC1, 0x1F, 0xF8, 0xD2, 0x11,
     0xBA, 0x4B, 0x00, 0xA0, 0xC9, 0x3E, 0xC9, 0x3B]
)
BOOT_GUID = bytes(
    [0x76, 0x0B, 0x98, 0x04, 0x42, 0x10, 0x4C, 0x9B,
     0x86, 0x1F, 0x11, 0xE0, 0x29, 0xEA, 0xC1, 0x01]
)


def log(msg: str) -> None:
    print(msg)
    LOG_LINES.append(msg)


def u32(data: bytes, off: int) -> int:
    return struct.unpack_from("<I", data, off)[0]


def count_pattern(buf: bytes, pat: bytes) -> int:
    count = 0
    plen = len(pat)
    for idx in range(0, len(buf) - plen + 1):
        if buf[idx:idx + plen] == pat:
            count += 1
    return count


def read_lba(img: mmap.mmap, lba: int, sectors: int = 1) -> bytes:
    start = lba * SECTOR_SIZE
    end = start + sectors * SECTOR_SIZE
    return img[start:end]
