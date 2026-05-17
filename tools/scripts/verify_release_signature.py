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


def require_file(path: Path, label: str) -> int:
    if not path.exists() or not path.is_file():
        return fail(f"{label} ausente: {path}")
    if path.stat().st_size == 0:
        return fail(f"{label} vazio: {path}")
    return 0


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


def require_ed25519_public_der(der: bytes | None) -> int:
    if not der or not der.startswith(ED25519_SPKI_PREFIX) or len(der) != len(ED25519_SPKI_PREFIX) + 32:
        return fail("chave publica nao e Ed25519 raw/SPKI valida")
    return 0


def normalize_sha256_hex(value: str | None) -> str | None:
    if not value:
        return None
    normalized = value.strip().lower().replace(":", "")
    if len(normalized) != 64 or any(c not in "0123456789abcdef" for c in normalized):
        return ""
    return normalized


def public_key_sha256_hex(der: bytes) -> str:
    return hashlib.sha256(der).hexdigest()


def public_key_sha256_matches(der: bytes, expected: str | None) -> bool:
    normalized = normalize_sha256_hex(expected)
    if not normalized:
        return False
    return public_key_sha256_hex(der) == normalized


def require_expected_public_key_sha256(der: bytes, expected: str | None) -> int:
    normalized = normalize_sha256_hex(expected)
    if normalized is None:
        return 0
    if normalized == "":
        return fail("fingerprint SHA-256 da chave publica invalido; use hex64 ou pares separados por ':'")
    actual = public_key_sha256_hex(der)
    if actual != normalized:
        return fail(f"fingerprint SHA-256 da chave publica inesperado: {actual}")
    return 0


def signature_verify_ok(openssl: str, input_path: Path, public_key: Path, signature_path: Path) -> bool:
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
    return proc.returncode == 0


def verify_signature(openssl: str, input_path: Path, public_key: Path, signature_path: Path) -> int:
    if not signature_verify_ok(openssl, input_path, public_key, signature_path):
        return fail("assinatura Ed25519 invalida")
    return 0


def run_self_test(openssl: str) -> int:
    with tempfile.TemporaryDirectory(prefix="capyos-release-signature-") as tmp:
        root = Path(tmp)
        input_path = root / "release-artifacts.sha256"
        private_key = root / "release-ed25519.pem"
        public_key = root / "release-ed25519.pub.pem"
        signature_path = root / "release-artifacts.sha256.sig"
        bad_signature_path = root / "release-artifacts.sha256.bad.sig"
        input_path.write_text("0" * 64 + "  capyos64.bin\n", encoding="utf-8")
        proc = run_openssl(openssl, ["genpkey", "-algorithm", "Ed25519", "-out", str(private_key)])
        if proc.returncode != 0:
            return fail("self-test: openssl genpkey falhou")
        proc = run_openssl(openssl, ["pkey", "-in", str(private_key), "-pubout", "-out", str(public_key)])
        if proc.returncode != 0:
            return fail("self-test: exportacao de chave publica falhou")
        public_key_der = public_der(openssl, public_key)
        rc = require_ed25519_public_der(public_key_der)
        if rc != 0:
            return rc
        # require_ed25519_public_der returns 0 only when DER bytes are
        # populated. Use explicit check so `python -O` cannot strip it
        # (py/assert-stmt).
        if public_key_der is None:
            raise RuntimeError(
                "internal: require_ed25519_public_der returned 0 but DER is None (self-test)"
            )
        good_fingerprint = public_key_sha256_hex(public_key_der)
        colon_fingerprint = ":".join(good_fingerprint[i:i + 2] for i in range(0, len(good_fingerprint), 2))
        if not public_key_sha256_matches(public_key_der, good_fingerprint):
            return fail("self-test: fingerprint SHA-256 valido foi rejeitado")
        if not public_key_sha256_matches(public_key_der, colon_fingerprint):
            return fail("self-test: fingerprint SHA-256 com ':' foi rejeitado")
        bad_fingerprint = "0" * 64 if good_fingerprint != "0" * 64 else "1" * 64
        if public_key_sha256_matches(public_key_der, bad_fingerprint):
            return fail("self-test: fingerprint SHA-256 incorreto foi aceito")
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
                str(signature_path),
            ],
        )
        if proc.returncode != 0:
            return fail("self-test: assinatura Ed25519 falhou")
        if not signature_verify_ok(openssl, input_path, public_key, signature_path):
            return fail("self-test: assinatura valida foi rejeitada")
        bad = bytearray(signature_path.read_bytes())
        if not bad:
            return fail("self-test: assinatura vazia")
        bad[0] ^= 0x01
        bad_signature_path.write_bytes(bytes(bad))
        if signature_verify_ok(openssl, input_path, public_key, bad_signature_path):
            return fail("self-test: assinatura mutilada foi aceita")
    print("[ok] self-test Ed25519: assinatura valida aceita e assinatura mutilada rejeitada")
    return 0


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Verifica assinatura Ed25519 de artefatos de release CapyOS via OpenSSL.")
    parser.add_argument("--input", default="build/release-artifacts.sha256", type=Path)
    parser.add_argument("--signature", type=Path)
    parser.add_argument("--public-key", type=Path, default=os.environ.get("CAPYOS_RELEASE_PUBLIC_KEY"))
    parser.add_argument("--expected-public-key-sha256", default=os.environ.get("CAPYOS_RELEASE_PUBLIC_KEY_SHA256"))
    parser.add_argument("--openssl", default=os.environ.get("OPENSSL", "openssl"))
    parser.add_argument("--self-test", action="store_true")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if args.self_test:
        return run_self_test(args.openssl)
    if not args.public_key:
        return fail("informe --public-key ou CAPYOS_RELEASE_PUBLIC_KEY")

    input_path = Path(args.input)
    signature_path = (
        Path(args.signature)
        if args.signature
        else input_path.with_name(f"{input_path.name}.sig")
    )
    public_key = Path(args.public_key)

    for path, label in (
        (input_path, "arquivo de checksums"),
        (signature_path, "assinatura"),
        (public_key, "chave publica"),
    ):
        rc = require_file(path, label)
        if rc != 0:
            return rc

    public_key_der = public_der(args.openssl, public_key)
    rc = require_ed25519_public_der(public_key_der)
    if rc != 0:
        return rc
    # require_ed25519_public_der returns 0 only when DER bytes are
    # populated. Use explicit check so `python -O` cannot strip it
    # (py/assert-stmt).
    if public_key_der is None:
        raise RuntimeError(
            "internal: require_ed25519_public_der returned 0 but DER is None"
        )

    rc = require_expected_public_key_sha256(public_key_der, args.expected_public_key_sha256)
    if rc != 0:
        return rc

    rc = verify_signature(args.openssl, input_path, public_key, signature_path)
    if rc != 0:
        return rc

    print(f"[ok] assinatura Ed25519 valida para {input_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
