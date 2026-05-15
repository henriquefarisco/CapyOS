#!/usr/bin/env python3
from __future__ import annotations

import argparse
import hashlib
import os
import subprocess
import sys
from pathlib import Path

ED25519_SPKI_PREFIX = bytes.fromhex("302a300506032b6570032100")


def fail(message: str) -> int:
    print(f"[err] {message}", file=sys.stderr)
    return 1


def run_openssl(openssl: str, args: list[str]) -> subprocess.CompletedProcess[bytes]:
    try:
        return subprocess.run(
            [openssl, *args],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
        )
    except FileNotFoundError:
        return subprocess.CompletedProcess([openssl, *args], 127, b"", b"openssl not found")


def public_der(openssl: str, public_key: Path) -> bytes | None:
    proc = run_openssl(openssl, ["pkey", "-pubin", "-in", str(public_key), "-pubout", "-outform", "DER"])
    if proc.returncode != 0:
        return None
    return proc.stdout


def require_file(path: Path, label: str) -> int:
    if not path.exists() or not path.is_file():
        return fail(f"{label} ausente: {path}")
    if path.stat().st_size == 0:
        return fail(f"{label} vazio: {path}")
    return 0


def require_ed25519_public_der(der: bytes | None) -> int:
    if not der or not der.startswith(ED25519_SPKI_PREFIX) or len(der) != len(ED25519_SPKI_PREFIX) + 32:
        return fail("chave publica nao e Ed25519 raw/SPKI valida")
    return 0


def colon_fingerprint(value: str) -> str:
    return ":".join(value[i:i + 2] for i in range(0, len(value), 2))


def valid_env_name(value: str) -> bool:
    head = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz_"
    tail = head + "0123456789"
    if not value or value[0] not in head:
        return False
    return all(c in tail for c in value)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Emite o fingerprint SHA-256 da chave publica Ed25519 de release CapyOS.")
    parser.add_argument("--public-key", type=Path)
    parser.add_argument("--format", choices=("env", "hex", "colon"), default="env")
    parser.add_argument("--env-name", default="RELEASE_PUBLIC_KEY_SHA256")
    parser.add_argument("--openssl", default=os.environ.get("OPENSSL", "openssl"))
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    public_key = args.public_key
    if public_key is None:
        env_path = os.environ.get("RELEASE_PUBLIC_KEY") or os.environ.get("CAPYOS_RELEASE_PUBLIC_KEY")
        if env_path:
            public_key = Path(env_path)
    if public_key is None:
        return fail("informe --public-key, RELEASE_PUBLIC_KEY ou CAPYOS_RELEASE_PUBLIC_KEY")
    public_key = public_key.expanduser()
    rc = require_file(public_key, "chave publica")
    if rc != 0:
        return rc
    der = public_der(args.openssl, public_key)
    rc = require_ed25519_public_der(der)
    if rc != 0:
        return rc
    assert der is not None
    fingerprint = hashlib.sha256(der).hexdigest()
    if args.format == "hex":
        print(fingerprint)
        return 0
    if args.format == "colon":
        print(colon_fingerprint(fingerprint))
        return 0
    if not valid_env_name(args.env_name):
        return fail("nome de variavel invalido para --env-name")
    print(f"{args.env_name}={fingerprint}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
