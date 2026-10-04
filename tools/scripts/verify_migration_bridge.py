#!/usr/bin/env python3
"""Fail-closed verification of the one-time, production-signed OTA bridge."""
from __future__ import annotations

import argparse
from pathlib import Path
import re
import sys

from update_manifest_common import (
    PINNED_PUBLIC_KEY_HEX, ManifestError, ensure_regular_file, parse_manifest,
    payload_metadata, verify_signature,
)
from verify_update_payload_budget import LEGACY_CACHE_MAX_BYTES

SOURCE = "github:henriquefarisco/CapyOS"
ROOT_URL = "https://github.com/henriquefarisco/CapyOS/releases/download/"


def stable_key(version: str) -> tuple[int, int, int]:
    match = re.fullmatch(r"(\d+)\.(\d+)\.(\d+)\+\d{8}", version)
    if not match:
        raise ManifestError("migration requires a dated stable runtime version")
    return tuple(int(part) for part in match.groups())


def verify_bridge(bundle: Path, *, final_version: str, bridge_version: str,
                  published_at: str, openssl: str = "openssl") -> None:
    if stable_key(bridge_version) >= stable_key(final_version):
        raise ManifestError("bridge version must be strictly older than the full release")
    for manifest_name, payload_name, version, limit in (
        ("bridge.ini", "capyos-bridge64.bin", bridge_version, LEGACY_CACHE_MAX_BYTES),
        ("latest.ini", "capyos64.bin", final_version, 8 * 1024 * 1024),
    ):
        manifest, payload = bundle / manifest_name, bundle / payload_name
        ensure_regular_file(manifest, "migration manifest")
        ensure_regular_file(payload, "migration payload")
        fields, signed = parse_manifest(manifest.read_bytes())
        # No test-key or HTTP override exists on this public validator.
        verify_signature(openssl, bytes.fromhex(PINNED_PUBLIC_KEY_HEX), signed,
                         bytes.fromhex(fields["signature_ed25519"]))
        expected = {
            "available_version": version, "channel": "stable", "branch": "main",
            "source": SOURCE, "published_at": published_at,
            "payload_url": f"{ROOT_URL}v{final_version}/{payload_name}",
        }
        for key, value in expected.items():
            if fields.get(key) != value:
                raise ManifestError(f"{manifest_name}: {key} mismatch")
        size, digest = payload_metadata(payload)
        if size > limit:
            raise ManifestError(f"{payload_name}: exceeds cache limit {limit}")
        if fields.get("payload_size") != str(size) or fields["payload_sha256"] != digest:
            raise ManifestError(f"{manifest_name}: payload size/hash mismatch")
        with payload.open("rb") as stream:
            if stream.read(4) != b"\x7fELF":
                raise ManifestError(f"{payload_name}: kernel is not ELF")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bundle-dir", required=True, type=Path)
    parser.add_argument("--final-version", required=True)
    parser.add_argument("--bridge-version", required=True)
    parser.add_argument("--published-at", required=True)
    parser.add_argument("--openssl", default="openssl")
    args = parser.parse_args()
    try:
        verify_bridge(args.bundle_dir, final_version=args.final_version,
                      bridge_version=args.bridge_version,
                      published_at=args.published_at, openssl=args.openssl)
    except (OSError, ManifestError) as exc:
        print(f"[err] migration bridge: {exc}", file=sys.stderr)
        return 1
    print("[ok] production signatures, immutable URLs, versions and both payloads verified")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
