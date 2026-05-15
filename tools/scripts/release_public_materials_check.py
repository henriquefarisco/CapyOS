#!/usr/bin/env python3
from __future__ import annotations

import argparse
import hashlib
import os
import subprocess
import sys
from pathlib import Path

ED25519_SPKI_PREFIX = bytes.fromhex("302a300506032b6570032100")
ED25519_SIGNATURE_SIZE = 64
MANIFEST_FORMAT = "capyos-release-public-key-manifest-v1"
MANIFEST_FIELDS = {
    "format",
    "algorithm",
    "public_key_encoding",
    "public_key_file",
    "public_key_sha256",
    "expected_public_key_sha256",
    "private_key_included",
}


def fail(message: str) -> int:
    print(f"[err] {message}", file=sys.stderr)
    return 1


def ok(message: str) -> None:
    print(f"[ok] {message}")


def require_file(path: Path, label: str) -> int:
    if not path.exists() or not path.is_file():
        return fail(f"{label} ausente: {path}")
    if path.stat().st_size == 0:
        return fail(f"{label} vazio: {path}")
    return 0


def normalize_sha256_hex(value: str | None) -> str | None:
    if not value:
        return None
    normalized = value.strip().lower().replace(":", "")
    if len(normalized) != 64 or any(c not in "0123456789abcdef" for c in normalized):
        return ""
    return normalized


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


def public_key_sha256_hex(public_key: Path, openssl: str) -> str | None:
    der = public_der(openssl, public_key)
    if not der or not der.startswith(ED25519_SPKI_PREFIX) or len(der) != len(ED25519_SPKI_PREFIX) + 32:
        return None
    return hashlib.sha256(der).hexdigest()


def artifact_name_safe(name: str) -> bool:
    candidate = Path(name)
    if candidate.is_absolute():
        return False
    return all(part not in ("", ".", "..") for part in candidate.parts)


def artifact_path(root: Path, name: str) -> Path:
    return root / name


def validate_checksums_file(path: Path, artifact_root: Path) -> int:
    rc = require_file(path, "arquivo de checksums")
    if rc != 0:
        return rc
    try:
        text = path.read_text(encoding="utf-8")
    except UnicodeDecodeError:
        return fail(f"arquivo de checksums nao e UTF-8: {path}")
    seen: set[str] = set()
    for line_no, line in enumerate(text.splitlines(), 1):
        if not line:
            return fail(f"arquivo de checksums contem linha vazia: {line_no}")
        if len(line) < 67 or line[64:66] != "  ":
            return fail(f"linha de checksum malformada: {line_no}")
        digest = line[:64]
        name = line[66:]
        normalized_digest = digest.lower()
        if digest != normalized_digest or any(c not in "0123456789abcdef" for c in normalized_digest):
            return fail(f"checksum SHA-256 invalido na linha: {line_no}")
        if not name:
            return fail(f"linha de checksum sem caminho: {line_no}")
        if "\\" in name or chr(0) in name or not artifact_name_safe(name):
            return fail(f"caminho de artefato invalido na linha: {line_no}")
        if name in seen:
            return fail(f"arquivo de checksums contem caminho duplicado: {name}")
        seen.add(name)
        artifact = artifact_path(artifact_root, name)
        rc = require_file(artifact, f"artefato de release {name}")
        if rc != 0:
            return rc
        actual = hashlib.sha256(artifact.read_bytes()).hexdigest()
        if actual != normalized_digest:
            return fail(f"checksum SHA-256 divergente para {name}: {actual}")
    if not seen:
        return fail("arquivo de checksums sem entradas")
    ok("arquivo de checksums publico e artefatos conferidos")
    return 0


def validate_signature_file(path: Path) -> int:
    rc = require_file(path, "assinatura")
    if rc != 0:
        return rc
    size = path.stat().st_size
    if size != ED25519_SIGNATURE_SIZE:
        return fail(f"assinatura Ed25519 raw deve ter {ED25519_SIGNATURE_SIZE} bytes: {size}")
    ok("assinatura Ed25519 raw presente")
    return 0


def signature_verify_ok(openssl: str, checksums: Path, public_key: Path, signature: Path) -> bool:
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
            str(checksums),
            "-sigfile",
            str(signature),
        ],
    )
    return proc.returncode == 0


def load_public_key_manifest(manifest_path: Path) -> tuple[int, dict[str, str]]:
    rc = require_file(manifest_path, "manifesto da chave publica")
    if rc != 0:
        return rc, {}
    try:
        text = manifest_path.read_text(encoding="utf-8")
    except UnicodeDecodeError:
        return fail(f"manifesto da chave publica nao e UTF-8: {manifest_path}"), {}
    data: dict[str, str] = {}
    for line_no, line in enumerate(text.splitlines(), 1):
        if not line:
            return fail(f"manifesto da chave publica contem linha vazia: {line_no}"), {}
        if "=" not in line:
            return fail(f"manifesto da chave publica contem linha sem '=': {line_no}"), {}
        key, value = line.split("=", 1)
        if key not in MANIFEST_FIELDS:
            return fail(f"manifesto da chave publica contem campo desconhecido: {key}"), {}
        if key in data:
            return fail(f"manifesto da chave publica contem campo duplicado: {key}"), {}
        data[key] = value
    missing = sorted(MANIFEST_FIELDS - set(data))
    if missing:
        return fail("manifesto da chave publica sem campos: " + ", ".join(missing)), {}
    return 0, data


def validate_public_key_and_manifest(public_key: Path, manifest_path: Path, expected_sha256: str | None, openssl: str) -> int:
    rc = require_file(public_key, "chave publica")
    if rc != 0:
        return rc
    expected = normalize_sha256_hex(expected_sha256)
    if expected is None:
        return fail("informe RELEASE_PUBLIC_KEY_SHA256/CAPYOS_RELEASE_PUBLIC_KEY_SHA256 ou --expected-public-key-sha256")
    if expected == "":
        return fail("fingerprint SHA-256 invalido; use hex64 ou pares separados por ':'")
    actual = public_key_sha256_hex(public_key, openssl)
    if actual is None:
        return fail("chave publica nao e Ed25519 raw/SPKI valida")
    if actual != expected:
        return fail(f"fingerprint SHA-256 da chave publica inesperado: {actual}")
    rc, data = load_public_key_manifest(manifest_path)
    if rc != 0:
        return rc
    if data["format"] != MANIFEST_FORMAT:
        return fail("manifesto da chave publica tem formato inesperado")
    if data["algorithm"] != "Ed25519":
        return fail("manifesto da chave publica tem algoritmo inesperado")
    if data["public_key_encoding"] != "PEM/SPKI":
        return fail("manifesto da chave publica tem encoding inesperado")
    if data["private_key_included"] != "no":
        return fail("manifesto da chave publica indica chave privada incluida")
    if data["public_key_file"] != public_key.name:
        return fail("manifesto da chave publica aponta nome de chave diferente")
    manifest_actual = normalize_sha256_hex(data["public_key_sha256"])
    manifest_expected = normalize_sha256_hex(data["expected_public_key_sha256"])
    if not manifest_actual or not manifest_expected:
        return fail("manifesto da chave publica contem fingerprint SHA-256 invalido")
    if manifest_actual != actual:
        return fail("manifesto da chave publica diverge da chave publica informada")
    if manifest_expected != expected:
        return fail("manifesto da chave publica diverge do fingerprint esperado")
    if manifest_actual != manifest_expected:
        return fail("manifesto da chave publica contem fingerprints divergentes")
    ok("chave publica, fingerprint e manifesto conferidos")
    return 0


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Confere pacote publico de release CapyOS sem chave privada.")
    parser.add_argument("--checksums", type=Path, default=Path("build/release-artifacts.sha256"))
    parser.add_argument("--signature", type=Path)
    parser.add_argument("--artifact-root", type=Path, default=Path("."))
    parser.add_argument("--public-key", type=Path, default=os.environ.get("RELEASE_PUBLIC_KEY") or os.environ.get("CAPYOS_RELEASE_PUBLIC_KEY"))
    parser.add_argument("--expected-public-key-sha256", default=os.environ.get("RELEASE_PUBLIC_KEY_SHA256") or os.environ.get("CAPYOS_RELEASE_PUBLIC_KEY_SHA256"))
    parser.add_argument("--public-key-manifest", type=Path, default=os.environ.get("RELEASE_PUBLIC_KEY_MANIFEST") or os.environ.get("CAPYOS_RELEASE_PUBLIC_KEY_MANIFEST") or "build/release-public-key.manifest")
    parser.add_argument("--openssl", default=os.environ.get("OPENSSL", "openssl"))
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    checksums = args.checksums.expanduser()
    signature = args.signature.expanduser() if args.signature else checksums.with_name(f"{checksums.name}.sig")
    public_key = args.public_key.expanduser() if args.public_key else None
    manifest = args.public_key_manifest.expanduser()
    if public_key is None:
        return fail("informe --public-key, RELEASE_PUBLIC_KEY ou CAPYOS_RELEASE_PUBLIC_KEY")
    rc = validate_checksums_file(checksums, args.artifact_root.expanduser())
    if rc != 0:
        return rc
    rc = validate_signature_file(signature)
    if rc != 0:
        return rc
    rc = validate_public_key_and_manifest(public_key, manifest, args.expected_public_key_sha256, args.openssl)
    if rc != 0:
        return rc
    if not signature_verify_ok(args.openssl, checksums, public_key, signature):
        return fail("assinatura Ed25519 invalida para o pacote publico")
    ok("pacote publico de release conferido")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
