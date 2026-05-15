#!/usr/bin/env python3
from __future__ import annotations

import argparse
import hashlib
import os
import re
import subprocess
import sys
from pathlib import Path

ED25519_SPKI_PREFIX = bytes.fromhex("302a300506032b6570032100")
ED25519_SIGNATURE_SIZE = 64
KEY_MANIFEST_FORMAT = "capyos-release-public-key-manifest-v1"
PUBLICATION_MANIFEST_FORMAT = "capyos-release-publication-manifest-v1"
ARTIFACT_FIELD = re.compile(r"^artifact\.([1-9][0-9]*)\.(path|sha256)$")
PUBLICATION_REQUIRED_FIELDS = {
    "format",
    "signature_algorithm",
    "checksum_algorithm",
    "checksums_file",
    "checksums_sha256",
    "signature_file",
    "signature_sha256",
    "public_key_file",
    "public_key_sha256",
    "expected_public_key_sha256",
    "public_key_manifest_file",
    "public_key_manifest_sha256",
    "private_key_included",
    "artifact_count",
}
KEY_MANIFEST_FIELDS = {
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


def require_sha256_field(value: str, label: str) -> tuple[int, str]:
    normalized = normalize_sha256_hex(value)
    if normalized is None or normalized == "":
        return fail(f"{label} SHA-256 invalido"), ""
    if value != normalized:
        return fail(f"{label} SHA-256 deve usar hex lowercase sem ':'"), ""
    return 0, normalized


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


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def artifact_name_safe(name: str) -> bool:
    candidate = Path(name)
    if candidate.is_absolute():
        return False
    return all(part not in ("", ".", "..") for part in candidate.parts)


def material_name_safe(name: str) -> bool:
    if not name or "\\" in name or "/" in name or chr(0) in name:
        return False
    candidate = Path(name)
    return not candidate.is_absolute() and candidate.name == name and name not in (".", "..")


def load_key_value_manifest(path: Path, label: str) -> tuple[int, dict[str, str]]:
    rc = require_file(path, label)
    if rc != 0:
        return rc, {}
    try:
        text = path.read_text(encoding="utf-8")
    except UnicodeDecodeError:
        return fail(f"{label} nao e UTF-8: {path}"), {}
    data: dict[str, str] = {}
    for line_no, line in enumerate(text.splitlines(), 1):
        if not line:
            return fail(f"{label} contem linha vazia: {line_no}"), {}
        if "=" not in line:
            return fail(f"{label} contem linha sem '=': {line_no}"), {}
        key, value = line.split("=", 1)
        if key in data:
            return fail(f"{label} contem campo duplicado: {key}"), {}
        data[key] = value
    return 0, data


def load_publication_manifest(path: Path) -> tuple[int, dict[str, str], list[tuple[str, str]]]:
    rc, data = load_key_value_manifest(path, "manifesto publico de publicacao")
    if rc != 0:
        return rc, {}, []
    artifact_fields: dict[int, dict[str, str]] = {}
    for key in data:
        if key in PUBLICATION_REQUIRED_FIELDS or key == "release_id":
            continue
        match = ARTIFACT_FIELD.match(key)
        if not match:
            return fail(f"manifesto publico de publicacao contem campo desconhecido: {key}"), {}, []
        index = int(match.group(1))
        artifact_fields.setdefault(index, {})[match.group(2)] = data[key]
    missing = sorted(PUBLICATION_REQUIRED_FIELDS - set(data))
    if missing:
        return fail("manifesto publico de publicacao sem campos: " + ", ".join(missing)), {}, []
    if data["format"] != PUBLICATION_MANIFEST_FORMAT:
        return fail("manifesto publico de publicacao tem formato inesperado"), {}, []
    if data["signature_algorithm"] != "Ed25519":
        return fail("manifesto publico de publicacao tem assinatura inesperada"), {}, []
    if data["checksum_algorithm"] != "SHA-256":
        return fail("manifesto publico de publicacao tem checksum inesperado"), {}, []
    if data["private_key_included"] != "no":
        return fail("manifesto publico de publicacao indica chave privada incluida"), {}, []
    try:
        artifact_count = int(data["artifact_count"])
    except ValueError:
        return fail("manifesto publico de publicacao tem artifact_count invalido"), {}, []
    if artifact_count <= 0:
        return fail("manifesto publico de publicacao sem artefatos"), {}, []
    if set(artifact_fields) != set(range(1, artifact_count + 1)):
        return fail("manifesto publico de publicacao contem indices de artefato inconsistentes"), {}, []
    entries: list[tuple[str, str]] = []
    for index in range(1, artifact_count + 1):
        fields = artifact_fields[index]
        if set(fields) != {"path", "sha256"}:
            return fail(f"manifesto publico de publicacao tem artefato incompleto: {index}"), {}, []
        name = fields["path"]
        rc, digest = require_sha256_field(fields["sha256"], f"artifact.{index}")
        if rc != 0:
            return rc, {}, []
        if not name or "\\" in name or chr(0) in name or not artifact_name_safe(name):
            return fail(f"manifesto publico de publicacao tem caminho de artefato invalido: {index}"), {}, []
        entries.append((name, digest))
    for field in (
        "checksums_file",
        "signature_file",
        "public_key_file",
        "public_key_manifest_file",
    ):
        if not material_name_safe(data[field]):
            return fail(f"manifesto publico de publicacao tem nome invalido: {field}"), {}, []
    for field in (
        "checksums_sha256",
        "signature_sha256",
        "public_key_sha256",
        "expected_public_key_sha256",
        "public_key_manifest_sha256",
    ):
        rc, _ = require_sha256_field(data[field], field)
        if rc != 0:
            return rc, {}, []
    return 0, data, entries


def load_checksums(path: Path, artifact_root: Path) -> tuple[int, list[tuple[str, str]]]:
    rc = require_file(path, "arquivo de checksums")
    if rc != 0:
        return rc, []
    try:
        text = path.read_text(encoding="utf-8")
    except UnicodeDecodeError:
        return fail(f"arquivo de checksums nao e UTF-8: {path}"), []
    entries: list[tuple[str, str]] = []
    seen: set[str] = set()
    for line_no, line in enumerate(text.splitlines(), 1):
        if not line:
            return fail(f"arquivo de checksums contem linha vazia: {line_no}"), []
        if len(line) < 67 or line[64:66] != "  ":
            return fail(f"linha de checksum malformada: {line_no}"), []
        digest = line[:64]
        name = line[66:]
        rc, normalized = require_sha256_field(digest, f"linha {line_no}")
        if rc != 0:
            return rc, []
        if not name or "\\" in name or chr(0) in name or not artifact_name_safe(name):
            return fail(f"caminho de artefato invalido na linha: {line_no}"), []
        if name in seen:
            return fail(f"arquivo de checksums contem caminho duplicado: {name}"), []
        artifact = artifact_root / name
        rc = require_file(artifact, f"artefato de release {name}")
        if rc != 0:
            return rc, []
        actual = sha256_file(artifact)
        if actual != normalized:
            return fail(f"checksum SHA-256 divergente para {name}: {actual}"), []
        seen.add(name)
        entries.append((name, normalized))
    if not entries:
        return fail("arquivo de checksums sem entradas"), []
    return 0, entries


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


def load_public_key_manifest(path: Path) -> tuple[int, dict[str, str]]:
    rc, data = load_key_value_manifest(path, "manifesto da chave publica")
    if rc != 0:
        return rc, {}
    missing = sorted(KEY_MANIFEST_FIELDS - set(data))
    if missing:
        return fail("manifesto da chave publica sem campos: " + ", ".join(missing)), {}
    for key in data:
        if key not in KEY_MANIFEST_FIELDS:
            return fail(f"manifesto da chave publica contem campo desconhecido: {key}"), {}
    return 0, data


def verify_material_hash(path: Path, expected: str, label: str) -> int:
    rc = require_file(path, label)
    if rc != 0:
        return rc
    actual = sha256_file(path)
    if actual != expected:
        return fail(f"SHA-256 divergente para {label}: {actual}")
    return 0


def verify_publication_manifest(manifest: Path, materials_root: Path, artifact_root: Path, pinned_sha256: str | None, openssl: str) -> int:
    rc, data, manifest_entries = load_publication_manifest(manifest)
    if rc != 0:
        return rc
    checksums = materials_root / data["checksums_file"]
    signature = materials_root / data["signature_file"]
    public_key = materials_root / data["public_key_file"]
    key_manifest = materials_root / data["public_key_manifest_file"]
    for path, field, label in (
        (checksums, "checksums_sha256", "arquivo de checksums"),
        (signature, "signature_sha256", "assinatura"),
        (key_manifest, "public_key_manifest_sha256", "manifesto da chave publica"),
    ):
        rc = verify_material_hash(path, data[field], label)
        if rc != 0:
            return rc
    if signature.stat().st_size != ED25519_SIGNATURE_SIZE:
        return fail(f"assinatura Ed25519 raw deve ter {ED25519_SIGNATURE_SIZE} bytes")
    rc = require_file(public_key, "chave publica")
    if rc != 0:
        return rc
    public_key_sha256 = public_key_sha256_hex(public_key, openssl)
    if public_key_sha256 is None:
        return fail("chave publica nao e Ed25519 raw/SPKI valida")
    if public_key_sha256 != data["public_key_sha256"]:
        return fail("manifesto publico de publicacao diverge da chave publica")
    if public_key_sha256 != data["expected_public_key_sha256"]:
        return fail("manifesto publico de publicacao diverge do fingerprint esperado")
    pinned = normalize_sha256_hex(pinned_sha256)
    if pinned == "":
        return fail("fingerprint SHA-256 externo invalido; use hex64 ou pares separados por ':'")
    if pinned is not None and pinned != data["expected_public_key_sha256"]:
        return fail("fingerprint SHA-256 externo diverge do manifesto de publicacao")
    rc, key_data = load_public_key_manifest(key_manifest)
    if rc != 0:
        return rc
    if key_data["format"] != KEY_MANIFEST_FORMAT:
        return fail("manifesto da chave publica tem formato inesperado")
    if key_data["algorithm"] != "Ed25519" or key_data["public_key_encoding"] != "PEM/SPKI":
        return fail("manifesto da chave publica nao descreve Ed25519 PEM/SPKI")
    if key_data["private_key_included"] != "no":
        return fail("manifesto da chave publica indica chave privada incluida")
    if key_data["public_key_file"] != public_key.name:
        return fail("manifesto da chave publica aponta nome de chave diferente")
    if key_data["public_key_sha256"] != data["public_key_sha256"]:
        return fail("manifesto da chave publica diverge do manifesto de publicacao")
    if key_data["expected_public_key_sha256"] != data["expected_public_key_sha256"]:
        return fail("fingerprint esperado diverge entre manifestos publicos")
    rc, checksum_entries = load_checksums(checksums, artifact_root)
    if rc != 0:
        return rc
    if checksum_entries != manifest_entries:
        return fail("manifesto publico de publicacao diverge do arquivo de checksums")
    if not signature_verify_ok(openssl, checksums, public_key, signature):
        return fail("assinatura Ed25519 invalida para manifesto de publicacao")
    ok("manifesto publico de publicacao conferido")
    return 0


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Verifica manifesto publico de publicacao da release CapyOS.")
    parser.add_argument("--manifest", type=Path, default=Path("build/release-publication.manifest"))
    parser.add_argument("--materials-root", type=Path)
    parser.add_argument("--artifact-root", type=Path, default=Path("."))
    parser.add_argument("--expected-public-key-sha256", default=os.environ.get("RELEASE_PUBLIC_KEY_SHA256") or os.environ.get("CAPYOS_RELEASE_PUBLIC_KEY_SHA256"))
    parser.add_argument("--openssl", default=os.environ.get("OPENSSL", "openssl"))
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    manifest = args.manifest.expanduser()
    materials_root = args.materials_root.expanduser() if args.materials_root else manifest.parent
    artifact_root = args.artifact_root.expanduser()
    return verify_publication_manifest(manifest, materials_root, artifact_root, args.expected_public_key_sha256, args.openssl)


if __name__ == "__main__":
    raise SystemExit(main())
