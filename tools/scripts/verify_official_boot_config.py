#!/usr/bin/env python3

from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path

BOOT_CONFIG_MAGIC = 0xB001CF61
BOOT_CONFIG_VERSION = 4
BOOT_CONFIG_FLAG_HAS_SETUP_DATA = 0x0002
BOOT_CONFIG_SIZE = 512
ADMIN_PASSWORD_OFFSET = 184
ADMIN_PASSWORD_SIZE = 64


def verify_official_boot_config(data: bytes) -> None:
    if len(data) != BOOT_CONFIG_SIZE:
        raise ValueError("boot config must be exactly 512 bytes")
    magic, version, flags = struct.unpack_from("<IHH", data, 0)
    if magic != BOOT_CONFIG_MAGIC:
        raise ValueError("boot config magic mismatch")
    if version != BOOT_CONFIG_VERSION:
        raise ValueError("boot config version mismatch")
    if flags & BOOT_CONFIG_FLAG_HAS_SETUP_DATA:
        raise ValueError("official installer must not contain preseeded setup data")
    password = data[
        ADMIN_PASSWORD_OFFSET : ADMIN_PASSWORD_OFFSET + ADMIN_PASSWORD_SIZE
    ]
    if any(password):
        raise ValueError("official installer must not contain an admin password")


def self_test() -> int:
    valid = bytearray(BOOT_CONFIG_SIZE)
    struct.pack_into("<IHH", valid, 0, BOOT_CONFIG_MAGIC, BOOT_CONFIG_VERSION, 0)
    verify_official_boot_config(bytes(valid))

    setup = bytearray(valid)
    struct.pack_into("<H", setup, 6, BOOT_CONFIG_FLAG_HAS_SETUP_DATA)
    password = bytearray(valid)
    password[ADMIN_PASSWORD_OFFSET : ADMIN_PASSWORD_OFFSET + 5] = b"admin"
    malformed = [bytes(valid[:-1]), bytes(setup), bytes(password)]
    for payload in malformed:
        try:
            verify_official_boot_config(payload)
        except ValueError:
            continue
        print("[FAIL] official boot config self-test accepted invalid input")
        return 1
    print("[OK] official boot config self-test")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("boot_config", nargs="?", type=Path)
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        return self_test()
    if not args.boot_config:
        parser.error("boot_config is required unless --self-test is used")
    try:
        verify_official_boot_config(args.boot_config.read_bytes())
    except (OSError, ValueError) as exc:
        print(f"[FAIL] official boot config: {exc}", file=sys.stderr)
        return 1
    print("[OK] official boot config is interactive-only")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
