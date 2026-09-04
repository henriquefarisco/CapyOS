#!/usr/bin/env python3
"""Resolve and sign the CapyOS ABI-token module index with Ed25519."""

from __future__ import annotations

import argparse
import sys
import tempfile
from pathlib import Path

import build_modules_index as index_builder
from modules_index_catalog import CORE_ABI_TOKEN, DEFAULT_REPOS, INDEX_EPOCH
from update_manifest_common import (
    ManifestError as SigningError,
    ensure_regular_file,
    raw_public_from_private,
    require_private_key_mode,
    sign_bytes,
    verify_signature,
)

PINNED_CAPYPKG_PUBLIC_KEY_HEX = (
    "1c52cc62ac20b94bb0c64291b6cdc545"
    "3b9aee53392a25ed2799fc7e4a718ef2"
)


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Resolve, sign, and self-verify a CapyOS modules index"
    )
    parser.add_argument("--workspace", required=True)
    parser.add_argument("--private-key", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--release-tag", default=None)
    parser.add_argument("--abi-token", default=CORE_ABI_TOKEN)
    parser.add_argument("--epoch", type=int, default=INDEX_EPOCH)
    parser.add_argument("--openssl", default="openssl")
    parser.add_argument("--allow-insecure-key", action="store_true")
    args = parser.parse_args()

    workspace = Path(args.workspace).resolve()
    private_key = Path(args.private_key).resolve()
    output = Path(args.output).resolve()
    try:
        ensure_regular_file(private_key, "CapyPKG publisher private key")
        require_private_key_mode(private_key, args.allow_insecure_key)
        raw_public = raw_public_from_private(args.openssl, private_key)
        if raw_public.hex() != PINNED_CAPYPKG_PUBLIC_KEY_HEX:
            raise SigningError(
                "private key does not match the CapyOS-pinned CapyPKG publisher key"
            )
        with tempfile.TemporaryDirectory(prefix="capyos-modules-index-") as tmp:
            tmp_root = Path(tmp)
            unsigned = tmp_root / "modules-index.unsigned.txt"
            descriptor = tmp_root / "modules-index.descriptor"
            index_builder.build_index(
                workspace,
                list(DEFAULT_REPOS),
                unsigned,
                release_tag=args.release_tag,
                abi_token=args.abi_token,
                epoch=args.epoch,
                descriptor_output=descriptor,
            )
            signed_text = descriptor.read_bytes()
            signature = sign_bytes(args.openssl, private_key, signed_text)
            verify_signature(args.openssl, raw_public, signed_text, signature)
            index_builder.build_index(
                workspace,
                list(DEFAULT_REPOS),
                output,
                release_tag=args.release_tag,
                abi_token=args.abi_token,
                epoch=args.epoch,
                signature_hex=signature.hex(),
            )
        print(f"[ok] signed and self-verified {output}")
        print(f"[ok] publisher raw public key: {raw_public.hex()}")
        return 0
    except (OSError, UnicodeError, SigningError, index_builder.ManifestError) as exc:
        print(f"[error] {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
