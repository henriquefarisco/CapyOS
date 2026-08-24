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
    payload_metadata,
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
    parser.add_argument("--expected-published-at")
    parser.add_argument("--expected-payload-url")
    parser.add_argument(
        "--allow-lab-http-payload-url",
        action="store_true",
        help=(
            "accept a plain-http payload_url; only a kernel built with "
            "CAPYOS_UPDATE_LAB_TRUST_KEY_HEX consumes it (Etapa 8 A/B gate)"
        ),
    )
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
                "payload_size": "1",
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
            legacy_fields = dict(fields)
            legacy_fields.pop("payload_size")
            legacy_body = canonical_body(legacy_fields)
            legacy_signature = sign_bytes(openssl, private_key, legacy_body)
            legacy_manifest = legacy_body + (
                f"signature_ed25519={legacy_signature.hex()}\n".encode("ascii")
            )
            legacy_parsed, legacy_captured = parse_manifest(legacy_manifest)
            verify_signature(
                openssl,
                raw_public,
                legacy_captured,
                bytes.fromhex(legacy_parsed["signature_ed25519"]),
            )
            tampered_size = manifest.replace(
                b"payload_size=1\n", b"payload_size=2\n"
            )
            tampered_fields, tampered_captured = parse_manifest(tampered_size)
            try:
                verify_signature(
                    openssl,
                    raw_public,
                    tampered_captured,
                    bytes.fromhex(tampered_fields["signature_ed25519"]),
                )
            except ManifestError:
                pass
            else:
                raise ManifestError("self-test accepted a tampered payload_size")
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
        print("[ok] update manifest self-test: sized + legacy signatures accepted; tampering rejected")
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
        fields, signed = parse_manifest(
            raw, allow_lab_http=args.allow_lab_http_payload_url
        )
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
        require_expected(fields, "published_at", args.expected_published_at)
        require_expected(fields, "payload_url", args.expected_payload_url)
        if args.payload:
            actual_size, actual_sha256 = payload_metadata(args.payload)
            if "payload_size" in fields and actual_size != int(
                fields["payload_size"]
            ):
                raise ManifestError(
                    "payload size does not match payload_size in latest.ini"
                )
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
