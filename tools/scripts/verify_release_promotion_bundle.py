#!/usr/bin/env python3
"""Validate the exact public asset inventory required before release promotion."""

from __future__ import annotations

import argparse
import hashlib
import re
import sys
from pathlib import Path


CHECKSUMS_FILE = "release-artifacts.sha256"
AI_PAYLOAD_RE = re.compile(
    r"^org\.capyos\.ai\.assistant-[0-9A-Za-z][0-9A-Za-z.+-]*\.bin$"
)
FIXED_PAYLOADS = {
    "CapyOS-Installer-UEFI.iso",
    "capyos64.bin",
    "manifest.bin",
    "modules-index.txt",
    "modules.sha256",
}
SIGNED_MATERIALS = {
    "latest.ini",
    "release-artifacts.sha256.sig",
    "release-ed25519.pub.pem",
    "release-public-key.manifest",
    "release-publication.manifest",
}


class PromotionBundleError(ValueError):
    pass


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def parse_checksums(path: Path) -> list[tuple[str, str]]:
    try:
        text = path.read_text(encoding="utf-8")
    except UnicodeDecodeError as exc:
        raise PromotionBundleError("release checksum file is not UTF-8") from exc

    entries: list[tuple[str, str]] = []
    seen: set[str] = set()
    for line_no, line in enumerate(text.splitlines(), 1):
        if len(line) < 67 or line[64:66] != "  ":
            raise PromotionBundleError(
                f"malformed release checksum line {line_no}"
            )
        digest = line[:64]
        name = line[66:]
        if digest != digest.lower() or any(
            char not in "0123456789abcdef" for char in digest
        ):
            raise PromotionBundleError(
                f"invalid SHA-256 on release checksum line {line_no}"
            )
        if (
            not name
            or "\\" in name
            or "\x00" in name
            or Path(name).name != name
            or name in (".", "..")
        ):
            raise PromotionBundleError(
                f"unsafe asset name on release checksum line {line_no}"
            )
        if name in seen:
            raise PromotionBundleError(f"duplicate release checksum entry: {name}")
        seen.add(name)
        entries.append((name, digest))
    if not entries:
        raise PromotionBundleError("release checksum file has no entries")
    return entries


def verify_bundle(bundle_dir: Path) -> list[str]:
    if not bundle_dir.exists() or not bundle_dir.is_dir() or bundle_dir.is_symlink():
        raise PromotionBundleError(f"invalid promotion bundle directory: {bundle_dir}")

    entries = list(bundle_dir.iterdir())
    for entry in entries:
        if entry.is_symlink() or not entry.is_file():
            raise PromotionBundleError(
                f"promotion bundle entry is not a regular file: {entry.name}"
            )
        if entry.stat().st_size == 0:
            raise PromotionBundleError(f"promotion bundle asset is empty: {entry.name}")

    names = {entry.name for entry in entries}
    ai_payloads = sorted(name for name in names if AI_PAYLOAD_RE.fullmatch(name))
    if len(ai_payloads) != 1:
        raise PromotionBundleError(
            f"expected exactly one CapyAI payload, found {len(ai_payloads)}"
        )

    payloads = FIXED_PAYLOADS | {ai_payloads[0]}
    expected = payloads | SIGNED_MATERIALS | {CHECKSUMS_FILE}
    missing = sorted(expected - names)
    unexpected = sorted(names - expected)
    if missing:
        raise PromotionBundleError(
            "promotion bundle is missing assets: " + ", ".join(missing)
        )
    if unexpected:
        raise PromotionBundleError(
            "promotion bundle has unexpected assets: " + ", ".join(unexpected)
        )

    checksum_entries = parse_checksums(bundle_dir / CHECKSUMS_FILE)
    checksum_names = [name for name, _ in checksum_entries]
    expected_checksum_names = sorted(payloads)
    if checksum_names != expected_checksum_names:
        raise PromotionBundleError(
            "release checksum inventory differs from the six payload assets"
        )
    for name, expected_digest in checksum_entries:
        actual_digest = sha256_file(bundle_dir / name)
        if actual_digest != expected_digest:
            raise PromotionBundleError(
                f"release checksum mismatch for {name}: {actual_digest}"
            )

    signature = bundle_dir / "release-artifacts.sha256.sig"
    if signature.stat().st_size != 64:
        raise PromotionBundleError(
            "release-artifacts.sha256.sig must be a 64-byte raw Ed25519 signature"
        )
    return sorted(names)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Validate the exact signed GitHub Release asset inventory before "
            "a CapyOS draft can be promoted."
        )
    )
    parser.add_argument("--bundle-dir", type=Path, required=True)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    try:
        assets = verify_bundle(args.bundle_dir.expanduser())
    except (OSError, PromotionBundleError) as exc:
        print(f"[err] {exc}", file=sys.stderr)
        return 1
    print(
        f"[ok] signed release promotion bundle inventory verified ({len(assets)} assets)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
