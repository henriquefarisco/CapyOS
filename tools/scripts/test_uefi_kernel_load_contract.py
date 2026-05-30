#!/usr/bin/env python3

from __future__ import annotations

import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
HEADER = REPO_ROOT / "src/boot/uefi_loader/internal/uefi_loader_internal.h"
LINKER = REPO_ROOT / "src/arch/x86_64/linker64.ld"
MIN_HYPERV_SAFE_BASE = 0x10000000
MAX_LOW_IDENTITY_LIMIT = 0x40000000


def fail(message: str) -> int:
    print(f"[err] {message}", file=sys.stderr)
    return 1


def parse_int_token(token: str) -> int:
    value = token.strip().rstrip(";,")
    suffix = value[-1:].upper()
    if suffix in {"K", "M", "G"}:
        number = int(value[:-1], 0)
        scale = {"K": 1024, "M": 1024 * 1024, "G": 1024 * 1024 * 1024}[suffix]
        return number * scale
    value = re.sub(r"(?:ULL|UL|LL|U|L)$", "", value)
    return int(value, 0)


def parse_header_define(name: str) -> int:
    text = HEADER.read_text(encoding="utf-8")
    match = re.search(rf"^#define\s+{re.escape(name)}\s+(.+)$", text, re.MULTILINE)
    if not match:
        raise ValueError(f"missing {name}")
    value = match.group(1).strip()
    if "*" in value:
        product = 1
        for part in value.replace("(", "").replace(")", "").split("*"):
            product *= parse_int_token(part)
        return product
    return parse_int_token(value)


def parse_linker_base() -> int:
    text = LINKER.read_text(encoding="utf-8")
    match = re.search(r"^\s*\.\s*=\s*([^;]+);", text, re.MULTILINE)
    if not match:
        raise ValueError("missing linker base assignment")
    return parse_int_token(match.group(1))


def main() -> int:
    try:
        fixed_base = parse_header_define("KERNEL_FIXED_RESERVE_BASE")
        reserve_bytes = parse_header_define("KERNEL_FIXED_RESERVE_BYTES")
        linker_base = parse_linker_base()
    except (OSError, ValueError) as exc:
        return fail(str(exc))

    if fixed_base != linker_base:
        return fail(
            f"loader fixed base 0x{fixed_base:x} does not match linker base 0x{linker_base:x}"
        )
    if fixed_base < MIN_HYPERV_SAFE_BASE:
        return fail(f"kernel fixed base 0x{fixed_base:x} regressed below Hyper-V-safe floor")
    if fixed_base % 0x1000 != 0:
        return fail(f"kernel fixed base 0x{fixed_base:x} is not page aligned")
    if reserve_bytes < 32 * 1024 * 1024:
        return fail(f"kernel reserve window too small: {reserve_bytes} bytes")
    if fixed_base + reserve_bytes > MAX_LOW_IDENTITY_LIMIT:
        return fail(
            f"kernel reserve window exceeds low identity window: 0x{fixed_base + reserve_bytes:x}"
        )

    print("[ok] UEFI kernel load contract self-test passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
