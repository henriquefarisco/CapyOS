#!/usr/bin/env python3
"""Build the exact latest.ini byte stream consumed by the CapyOS runtime."""

from __future__ import annotations

import argparse
import os
import sys
from pathlib import Path

from update_manifest_common import (
    MANIFEST_MAX_BYTES,
    PINNED_PUBLIC_KEY_HEX,
    ManifestError,
    atomic_write,
    canonical_body,
    ensure_regular_file,
    normalize_public_key_hex,
    payload_metadata,
    raw_public_from_private,
    require_private_key_mode,
    sign_bytes,
    verify_signature,
)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Create a deterministic CapyOS update manifest. Signing is offline; "
            "--unsigned is only for CI handoff material."
        )
    )
    parser.add_argument("--version", required=True)
    parser.add_argument("--channel", choices=("stable", "develop"), default="stable")
    parser.add_argument("--branch")
    parser.add_argument("--source", default="github:henriquefarisco/CapyOS")
    parser.add_argument("--published-at", required=True)
    parser.add_argument("--payload", required=True, type=Path)
    parser.add_argument("--payload-url", required=True)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument(
        "--private-key", type=Path, default=os.environ.get("CAPYOS_UPDATE_PRIVATE_KEY")
    )
    parser.add_argument(
        "--expected-public-key-hex",
        default=PINNED_PUBLIC_KEY_HEX,
        help="raw Ed25519 public key pinned by the runtime (hex64)",
    )
    parser.add_argument(
        "--unsigned",
        action="store_true",
        help="emit only canonical signed bytes for offline handoff; never deploy this file",
    )
    parser.add_argument("--force", action="store_true")
    parser.add_argument("--allow-insecure-key", action="store_true")
    parser.add_argument("--openssl", default=os.environ.get("OPENSSL", "openssl"))
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    try:
        expected_public = bytes.fromhex(
            normalize_public_key_hex(args.expected_public_key_hex)
        )
        size, digest = payload_metadata(args.payload)
        fields = {
            "available_version": args.version,
            "channel": args.channel,
            "branch": args.branch or ("develop" if args.channel == "develop" else "main"),
            "source": args.source,
            "published_at": args.published_at,
            "payload_url": args.payload_url,
            "payload_size": str(size),
            "payload_sha256": digest,
        }
        body = canonical_body(fields)
        if args.unsigned:
            if args.private_key:
                raise ManifestError("--unsigned must not receive or read a private key")
            if args.output.name == "latest.ini":
                raise ManifestError("unsigned handoff output must not be named latest.ini")
            atomic_write(args.output, body, args.force)
            print(f"[ok] unsigned canonical handoff written to {args.output}")
            print("[note] this file has no signature and must never be deployed as latest.ini")
            return 0

        if not args.private_key:
            raise ManifestError(
                "provide --private-key or CAPYOS_UPDATE_PRIVATE_KEY for a signed manifest"
            )
        private_key = Path(args.private_key)
        ensure_regular_file(private_key, "private key")
        require_private_key_mode(private_key, args.allow_insecure_key)
        actual_public = raw_public_from_private(args.openssl, private_key)
        if actual_public != expected_public:
            raise ManifestError("private key does not match the public key pinned by CapyOS")
        signature = sign_bytes(args.openssl, private_key, body)
        verify_signature(args.openssl, actual_public, body, signature)
        manifest = body + f"signature_ed25519={signature.hex()}\n".encode("ascii")
        if len(manifest) > MANIFEST_MAX_BYTES:
            raise ManifestError(
                f"signed manifest is {len(manifest)} bytes; runtime limit is {MANIFEST_MAX_BYTES}"
            )
        atomic_write(args.output, manifest, args.force)
        print(f"[ok] signed canonical update manifest written to {args.output}")
        print(f"[ok] payload sha256: {fields['payload_sha256']}")
        return 0
    except ManifestError as exc:
        print(f"[err] {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
