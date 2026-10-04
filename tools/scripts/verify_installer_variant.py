#!/usr/bin/env python3
"""Keep diagnostic builds out of the canonical installer and its active pointer."""

from __future__ import annotations

import argparse
from pathlib import Path
import re
import sys


CANONICAL_ISO = Path("build/CapyOS-Installer-UEFI.iso")
CANONICAL_POINTER = Path("build/CapyOS-Installer-UEFI.last-built.txt")
BOOT_MARKERS = (
    b"[user_init] CAPYOS_BOOT_RUN_HELLO defined; spawning hello.",
    b"[user_init] CAPYOS_BOOT_RUN_TWO_BUSY defined; spawning two.",
    b"[smoke] audio-playback-roundtrip starting",
    b"[smoke] audio-multi starting",
    b"[smoke] media-player-playlist starting",
    b"[smoke] capyai-gui-async ready",
    b"[lab] update trust anchor overridden",
    b"[fp-corrupt]",
)


def verify_installer_variant(
    kernel: Path, variant: Path, iso: Path, last_built: Path, reuse: bool
) -> None:
    # resolve() also catches ./, .., absolute names and symlink aliases.
    protected = {CANONICAL_ISO.resolve(), CANONICAL_POINTER.resolve()}
    if iso.resolve() not in protected and last_built.resolve() not in protected:
        return
    if reuse:
        raise ValueError("canonical installer outputs cannot reuse diagnostic variants")
    flags = variant.read_text(encoding="utf-8")
    if not flags.strip():
        raise ValueError("canonical installer requires build-variant provenance")
    for macro in re.findall(r"(?:^|[\s=])-D\s*([A-Za-z_][A-Za-z_0-9]*)", flags):
        if (
            "SMOKE" in macro
            or macro.startswith(("CAPYOS_BOOT_RUN_", "CAPYOS_HELLO_"))
            or macro in ("CAPYOS_PREEMPTIVE_DEMO", "CAPYOS_UPDATE_LAB_TRUST_KEY_HEX")
        ):
            raise ValueError(f"canonical installer contains diagnostic flag {macro}")
    data = kernel.read_bytes()
    for marker in BOOT_MARKERS:
        if marker in data:
            raise ValueError("canonical installer kernel contains diagnostic boot hooks")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--kernel", type=Path, required=True)
    parser.add_argument("--variant", type=Path, required=True)
    parser.add_argument("--iso", type=Path, required=True)
    parser.add_argument("--last-built", type=Path, required=True)
    parser.add_argument("--reuse", choices=("0", "1"), required=True)
    args = parser.parse_args()
    try:
        verify_installer_variant(
            args.kernel, args.variant, args.iso, args.last_built, args.reuse == "1"
        )
    except (OSError, ValueError) as exc:
        print(f"[FAIL] installer variant: {exc}", file=sys.stderr)
        return 1
    print("[OK] installer/diagnostic output isolation")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
