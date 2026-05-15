#!/usr/bin/env python3
from __future__ import annotations

import argparse
import hashlib
import os
import subprocess
import sys
import tempfile
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


def require_file(path: Path, label: str) -> int:
    if not path.exists() or not path.is_file():
        return fail(f"{label} ausente: {path}")
    if path.stat().st_size == 0:
        return fail(f"{label} vazio: {path}")
    return 0


def public_der(openssl: str, public_key: Path) -> bytes | None:
    proc = run_openssl(openssl, ["pkey", "-pubin", "-in", str(public_key), "-pubout", "-outform", "DER"])
    if proc.returncode != 0:
        return None
    return proc.stdout


def normalize_sha256_hex(value: str | None) -> str | None:
    if not value:
        return None
    normalized = value.strip().lower().replace(":", "")
    if len(normalized) != 64 or any(c not in "0123456789abcdef" for c in normalized):
        return ""
    return normalized


def require_ed25519_public_der(der: bytes | None) -> int:
    if not der or not der.startswith(ED25519_SPKI_PREFIX) or len(der) != len(ED25519_SPKI_PREFIX) + 32:
        return fail("chave publica nao e Ed25519 raw/SPKI valida")
    return 0


def manifest_value(value: str) -> str:
    return value.replace("\\", "/").replace("\r", "_").replace("\n", "_")


def build_manifest(public_key: Path, actual_sha256: str, expected_sha256: str) -> str:
    lines = [
        "format=capyos-release-public-key-manifest-v1",
        "algorithm=Ed25519",
        "public_key_encoding=PEM/SPKI",
        f"public_key_file={manifest_value(public_key.name)}",
        f"public_key_sha256={actual_sha256}",
        f"expected_public_key_sha256={expected_sha256}",
        "private_key_included=no",
    ]
    return "\n".join(lines) + "\n"


def atomic_write_text(path: Path, text: str, force: bool) -> int:
    if path.exists() and not force:
        return fail(f"saida ja existe: {path} (use --force para sobrescrever)")
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp_name = ""
    try:
        with tempfile.NamedTemporaryFile(
            "w",
            dir=str(path.parent),
            prefix=f".{path.name}.",
            delete=False,
            encoding="utf-8",
        ) as tmp:
            tmp.write(text)
            tmp.flush()
            os.fsync(tmp.fileno())
            tmp_name = tmp.name
        os.replace(tmp_name, path)
        tmp_name = ""
    finally:
        if tmp_name:
            try:
                Path(tmp_name).unlink()
            except FileNotFoundError:
                pass
    return 0


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Gera manifesto publico da chave Ed25519 de release CapyOS.")
    parser.add_argument("--public-key", type=Path)
    parser.add_argument("--expected-public-key-sha256", default=os.environ.get("RELEASE_PUBLIC_KEY_SHA256") or os.environ.get("CAPYOS_RELEASE_PUBLIC_KEY_SHA256"))
    parser.add_argument("--output", type=Path, default=Path("build/release-public-key.manifest"))
    parser.add_argument("--force", action="store_true")
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
    expected = normalize_sha256_hex(args.expected_public_key_sha256)
    if expected is None:
        return fail("informe --expected-public-key-sha256, RELEASE_PUBLIC_KEY_SHA256 ou CAPYOS_RELEASE_PUBLIC_KEY_SHA256")
    if expected == "":
        return fail("fingerprint SHA-256 invalido; use hex64 ou pares separados por ':'")
    der = public_der(args.openssl, public_key)
    rc = require_ed25519_public_der(der)
    if rc != 0:
        return rc
    assert der is not None
    actual = hashlib.sha256(der).hexdigest()
    if actual != expected:
        return fail(f"fingerprint SHA-256 da chave publica inesperado: {actual}")
    manifest = build_manifest(public_key, actual, expected)
    rc = atomic_write_text(args.output, manifest, args.force)
    if rc != 0:
        return rc
    print(f"[ok] manifesto da chave publica gravado em {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
