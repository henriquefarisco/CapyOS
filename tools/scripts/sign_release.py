#!/usr/bin/env python3
from __future__ import annotations

import argparse
import os
import stat
import subprocess
import sys
import tempfile
from pathlib import Path

ED25519_SPKI_PREFIX = bytes.fromhex("302a300506032b6570032100")


def fail(message: str) -> int:
    print(f"[err] {message}", file=sys.stderr)
    return 1


def require_file(path: Path, label: str) -> int:
    if not path.exists() or not path.is_file():
        return fail(f"{label} ausente: {path}")
    if path.stat().st_size == 0:
        return fail(f"{label} vazio: {path}")
    return 0


def require_private_key_mode(path: Path, allow_insecure: bool) -> int:
    if allow_insecure or os.name == "nt":
        return 0
    mode = stat.S_IMODE(path.stat().st_mode)
    if mode & 0o077:
        return fail(f"chave privada com permissoes inseguras ({mode:o}); use chmod 600 ou --allow-insecure-key")
    return 0


def run_openssl(openssl: str, args: list[str]) -> subprocess.CompletedProcess[bytes]:
    return subprocess.run(
        [openssl, *args],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )


def public_der_from_private(openssl: str, private_key: Path) -> bytes | None:
    proc = run_openssl(openssl, ["pkey", "-in", str(private_key), "-pubout", "-outform", "DER"])
    if proc.returncode != 0:
        return None
    return proc.stdout


def public_der_from_public(openssl: str, public_key: Path) -> bytes | None:
    proc = run_openssl(openssl, ["pkey", "-pubin", "-in", str(public_key), "-pubout", "-outform", "DER"])
    if proc.returncode != 0:
        return None
    return proc.stdout


def require_ed25519_public_der(der: bytes | None, label: str) -> int:
    if not der or not der.startswith(ED25519_SPKI_PREFIX) or len(der) != len(ED25519_SPKI_PREFIX) + 32:
        return fail(f"{label} nao e uma chave publica Ed25519 raw/SPKI valida")
    return 0


def atomic_replace_bytes(path: Path, data: bytes, force: bool) -> int:
    if path.exists() and not force:
        return fail(f"saida ja existe: {path} (use --force para sobrescrever)")
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp_name = ""
    try:
        with tempfile.NamedTemporaryFile(dir=str(path.parent), prefix=f".{path.name}.", delete=False) as tmp:
            tmp.write(data)
            tmp.flush()
            os.fsync(tmp.fileno())
            tmp_name = tmp.name
        os.replace(tmp_name, path)
    finally:
        if tmp_name:
            try:
                Path(tmp_name).unlink()
            except FileNotFoundError:
                pass
    return 0


def sign_bytes(openssl: str, input_path: Path, private_key: Path, signature_path: Path, force: bool) -> int:
    if signature_path.exists() and not force:
        return fail(f"assinatura ja existe: {signature_path} (use --force para sobrescrever)")
    signature_path.parent.mkdir(parents=True, exist_ok=True)
    tmp_name = ""
    try:
        with tempfile.NamedTemporaryFile(dir=str(signature_path.parent), prefix=f".{signature_path.name}.", delete=False) as tmp:
            tmp_name = tmp.name
        proc = run_openssl(
            openssl,
            [
                "pkeyutl",
                "-sign",
                "-rawin",
                "-inkey",
                str(private_key),
                "-in",
                str(input_path),
                "-out",
                tmp_name,
            ],
        )
        if proc.returncode != 0:
            return fail("openssl pkeyutl -sign falhou")
        os.replace(tmp_name, signature_path)
        tmp_name = ""
    finally:
        if tmp_name:
            try:
                Path(tmp_name).unlink()
            except FileNotFoundError:
                pass
    return 0


def verify_signature(openssl: str, input_path: Path, public_key: Path, signature_path: Path) -> int:
    proc = run_openssl(
        openssl,
        [
            "pkeyutl",
            "-verify",
            "-rawin",
            "-pubin",
            "-inkey",
            str(public_key),
            "-in",
            str(input_path),
            "-sigfile",
            str(signature_path),
        ],
    )
    if proc.returncode != 0:
        return fail("auto-verificacao Ed25519 falhou apos assinar")
    return 0


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Assina artefatos de release CapyOS com Ed25519 via OpenSSL.")
    parser.add_argument("--input", default="build/release-artifacts.sha256", type=Path)
    parser.add_argument("--private-key", type=Path, default=os.environ.get("CAPYOS_RELEASE_PRIVATE_KEY"))
    parser.add_argument("--signature", type=Path)
    parser.add_argument("--public-key-out", type=Path)
    parser.add_argument("--force", action="store_true")
    parser.add_argument("--allow-insecure-key", action="store_true")
    parser.add_argument("--openssl", default=os.environ.get("OPENSSL", "openssl"))
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if not args.private_key:
        return fail("informe --private-key ou CAPYOS_RELEASE_PRIVATE_KEY")

    input_path = Path(args.input)
    private_key = Path(args.private_key)
    signature_path = (
        Path(args.signature)
        if args.signature
        else input_path.with_name(f"{input_path.name}.sig")
    )

    for path, label in ((input_path, "arquivo de checksums"), (private_key, "chave privada")):
        rc = require_file(path, label)
        if rc != 0:
            return rc

    rc = require_private_key_mode(private_key, args.allow_insecure_key)
    if rc != 0:
        return rc

    public_der = public_der_from_private(args.openssl, private_key)
    rc = require_ed25519_public_der(public_der, "chave privada")
    if rc != 0:
        return rc

    if args.public_key_out:
        args.public_key_out = Path(args.public_key_out)
        public_pem = run_openssl(args.openssl, ["pkey", "-in", str(private_key), "-pubout"])
        if public_pem.returncode != 0:
            return fail("nao foi possivel extrair chave publica")
        rc = atomic_replace_bytes(args.public_key_out, public_pem.stdout, args.force)
        if rc != 0:
            return rc
        rc = require_ed25519_public_der(public_der_from_public(args.openssl, args.public_key_out), "chave publica exportada")
        if rc != 0:
            return rc

    rc = sign_bytes(args.openssl, input_path, private_key, signature_path, args.force)
    if rc != 0:
        return rc

    if args.public_key_out:
        rc = verify_signature(args.openssl, input_path, args.public_key_out, signature_path)
        if rc != 0:
            return rc

    print(f"[ok] assinatura Ed25519 gravada em {signature_path}")
    if args.public_key_out:
        print(f"[ok] chave publica Ed25519 gravada em {args.public_key_out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
