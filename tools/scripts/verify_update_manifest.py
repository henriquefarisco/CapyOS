#!/usr/bin/env python3
"""Verify latest.ini exactly as CapyOS will capture and authenticate it."""

from __future__ import annotations

import argparse
import os
import sys
import tempfile
from pathlib import Path

from update_manifest_common import (
    PINNED_PUBLIC_KEY_HEX,
    ManifestError,
    canonical_body,
    ensure_regular_file,
    normalize_public_key_hex,
    parse_manifest,
    payload_sha256,
    raw_public_from_public,
    raw_public_from_private,
    run_openssl,
    sign_bytes,
    verify_signature,
)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Verify a canonical signed CapyOS latest.ini and its payload."
    )
    parser.add_argument("--manifest", type=Path)
    parser.add_argument("--payload", type=Path)
    parser.add_argument("--public-key", type=Path)
    parser.add_argument(
        "--expected-public-key-hex",
        default=PINNED_PUBLIC_KEY_HEX,
        help="raw Ed25519 public key pinned by the runtime (hex64)",
    )
    parser.add_argument("--expected-version")
    parser.add_argument("--expected-channel")
    parser.add_argument("--expected-branch")
    parser.add_argument("--expected-source")
    parser.add_argument("--expected-payload-url")
    parser.add_argument("--openssl", default=os.environ.get("OPENSSL", "openssl"))
    parser.add_argument("--self-test", action="store_true")
    return parser.parse_args()


def require_expected(fields: dict[str, str], key: str, expected: str | None) -> None:
    if expected is not None and fields[key] != expected:
        raise ManifestError(
            f"{key} mismatch: expected {expected!r}, found {fields[key]!r}"
        )


def run_self_test(openssl: str) -> int:
    try:
        with tempfile.TemporaryDirectory(prefix="capyos-update-manifest-test-") as tmp:
            root = Path(tmp)
            private_key = root / "ephemeral-ed25519.pem"
            proc = run_openssl(
                openssl,
                ["genpkey", "-algorithm", "Ed25519", "-out", str(private_key)],
            )
            if proc.returncode != 0:
                raise ManifestError("self-test could not generate an ephemeral key")
            raw_public = raw_public_from_private(openssl, private_key)
            fields = {
                "available_version": "0.8.0-alpha.999+20991231",
                "channel": "stable",
                "branch": "main",
                "source": "github:example/CapyOS",
                "published_at": "2099-12-31",
                "payload_url": (
                    "https://github.com/example/CapyOS/releases/download/"
                    "v0.8.0-alpha.999+20991231/capyos64.bin"
                ),
                "payload_sha256": "00" * 32,
            }
            body = canonical_body(fields)
            signature = sign_bytes(openssl, private_key, body)
            manifest = body + (
                f"signature_ed25519={signature.hex()}\n".encode("ascii")
            )
            parsed, captured = parse_manifest(manifest)
            verify_signature(
                openssl,
                raw_public,
                captured,
                bytes.fromhex(parsed["signature_ed25519"]),
            )
            tampered = bytearray(signature)
            tampered[0] ^= 1
            try:
                verify_signature(openssl, raw_public, body, bytes(tampered))
            except ManifestError:
                pass
            else:
                raise ManifestError("self-test accepted a tampered signature")
            try:
                parse_manifest(manifest.replace(b"\n", b"\r\n"))
            except ManifestError:
                pass
            else:
                raise ManifestError("self-test accepted non-canonical CRLF output")
        print("[ok] update manifest self-test: canonical signature accepted; tampering rejected")
        return 0
    except ManifestError as exc:
        print(f"[err] {exc}", file=sys.stderr)
        return 1


def main() -> int:
    args = parse_args()
    if args.self_test:
        return run_self_test(args.openssl)
    if not args.manifest:
        print("[err] provide --manifest or --self-test", file=sys.stderr)
        return 1
    try:
        ensure_regular_file(args.manifest, "manifest")
        expected_public = bytes.fromhex(
            normalize_public_key_hex(args.expected_public_key_hex)
        )
        raw = args.manifest.read_bytes()
        fields, signed = parse_manifest(raw)
        actual_public = expected_public
        if args.public_key:
            ensure_regular_file(args.public_key, "public key")
            actual_public = raw_public_from_public(args.openssl, args.public_key)
            if actual_public != expected_public:
                raise ManifestError(
                    "public key file does not match the key pinned by CapyOS"
                )
        signature = bytes.fromhex(fields["signature_ed25519"])
        verify_signature(args.openssl, actual_public, signed, signature)
        require_expected(fields, "available_version", args.expected_version)
        require_expected(fields, "channel", args.expected_channel)
        require_expected(fields, "branch", args.expected_branch)
        require_expected(fields, "source", args.expected_source)
        require_expected(fields, "payload_url", args.expected_payload_url)
        if args.payload:
            actual_sha256 = payload_sha256(args.payload)
            if actual_sha256 != fields["payload_sha256"].lower():
                raise ManifestError(
                    "payload SHA-256 does not match payload_sha256 in latest.ini"
                )
        print(f"[ok] canonical Ed25519 update manifest verified: {args.manifest}")
        if args.payload:
            print(f"[ok] payload SHA-256 verified: {args.payload}")
        return 0
    except (ManifestError, OSError, ValueError) as exc:
        print(f"[err] {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
