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
ED25519_SIGNATURE_SIZE = 64
KEY_MANIFEST_FORMAT = "capyos-release-public-key-manifest-v1"
PUBLICATION_MANIFEST_FORMAT = "capyos-release-publication-manifest-v1"
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


def manifest_value(value: str) -> str:
    return value.replace("\\", "/").replace("\r", "_").replace("\n", "_")


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
        normalized_digest = digest.lower()
        if digest != normalized_digest or any(c not in "0123456789abcdef" for c in normalized_digest):
            return fail(f"checksum SHA-256 invalido na linha: {line_no}"), []
        if not name or "\\" in name or chr(0) in name or not artifact_name_safe(name):
            return fail(f"caminho de artefato invalido na linha: {line_no}"), []
        if name in seen:
            return fail(f"arquivo de checksums contem caminho duplicado: {name}"), []
        artifact = artifact_root / name
        rc = require_file(artifact, f"artefato de release {name}")
        if rc != 0:
            return rc, []
        actual = sha256_file(artifact)
        if actual != normalized_digest:
            return fail(f"checksum SHA-256 divergente para {name}: {actual}"), []
        seen.add(name)
        entries.append((name, normalized_digest))
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


def load_key_manifest(path: Path) -> tuple[int, dict[str, str]]:
    rc = require_file(path, "manifesto da chave publica")
    if rc != 0:
        return rc, {}
    try:
        text = path.read_text(encoding="utf-8")
    except UnicodeDecodeError:
        return fail(f"manifesto da chave publica nao e UTF-8: {path}"), {}
    data: dict[str, str] = {}
    for line_no, line in enumerate(text.splitlines(), 1):
        if not line:
            return fail(f"manifesto da chave publica contem linha vazia: {line_no}"), {}
        if "=" not in line:
            return fail(f"manifesto da chave publica contem linha sem '=': {line_no}"), {}
        key, value = line.split("=", 1)
        if key not in KEY_MANIFEST_FIELDS:
            return fail(f"manifesto da chave publica contem campo desconhecido: {key}"), {}
        if key in data:
            return fail(f"manifesto da chave publica contem campo duplicado: {key}"), {}
        data[key] = value
    missing = sorted(KEY_MANIFEST_FIELDS - set(data))
    if missing:
        return fail("manifesto da chave publica sem campos: " + ", ".join(missing)), {}
    return 0, data


def validate_public_materials(checksums: Path, signature: Path, public_key: Path, expected_sha256: str | None, key_manifest: Path, artifact_root: Path, openssl: str) -> tuple[int, list[tuple[str, str]], str]:
    rc, entries = load_checksums(checksums, artifact_root)
    if rc != 0:
        return rc, [], ""
    rc = require_file(signature, "assinatura")
    if rc != 0:
        return rc, [], ""
    if signature.stat().st_size != ED25519_SIGNATURE_SIZE:
        return fail(f"assinatura Ed25519 raw deve ter {ED25519_SIGNATURE_SIZE} bytes"), [], ""
    rc = require_file(public_key, "chave publica")
    if rc != 0:
        return rc, [], ""
    expected = normalize_sha256_hex(expected_sha256)
    if expected is None:
        return fail("informe RELEASE_PUBLIC_KEY_SHA256/CAPYOS_RELEASE_PUBLIC_KEY_SHA256 ou --expected-public-key-sha256"), [], ""
    if expected == "":
        return fail("fingerprint SHA-256 invalido; use hex64 ou pares separados por ':'"), [], ""
    actual = public_key_sha256_hex(public_key, openssl)
    if actual is None:
        return fail("chave publica nao e Ed25519 raw/SPKI valida"), [], ""
    if actual != expected:
        return fail(f"fingerprint SHA-256 da chave publica inesperado: {actual}"), [], ""
    rc, data = load_key_manifest(key_manifest)
    if rc != 0:
        return rc, [], ""
    if data["format"] != KEY_MANIFEST_FORMAT:
        return fail("manifesto da chave publica tem formato inesperado"), [], ""
    if data["algorithm"] != "Ed25519" or data["public_key_encoding"] != "PEM/SPKI":
        return fail("manifesto da chave publica nao descreve Ed25519 PEM/SPKI"), [], ""
    if data["private_key_included"] != "no":
        return fail("manifesto da chave publica indica chave privada incluida"), [], ""
    if data["public_key_file"] != public_key.name:
        return fail("manifesto da chave publica aponta nome de chave diferente"), [], ""
    manifest_actual = normalize_sha256_hex(data["public_key_sha256"])
    manifest_expected = normalize_sha256_hex(data["expected_public_key_sha256"])
    if manifest_actual != actual or manifest_expected != expected or manifest_actual != manifest_expected:
        return fail("manifesto da chave publica diverge da chave/fingerprint esperados"), [], ""
    if not signature_verify_ok(openssl, checksums, public_key, signature):
        return fail("assinatura Ed25519 invalida para publicacao"), [], ""
    return 0, entries, actual


def build_manifest(checksums: Path, signature: Path, public_key: Path, public_key_sha256: str, expected_public_key_sha256: str, key_manifest: Path, entries: list[tuple[str, str]], release_id: str | None) -> str:
    lines = [
        f"format={PUBLICATION_MANIFEST_FORMAT}",
        "signature_algorithm=Ed25519",
        "checksum_algorithm=SHA-256",
    ]
    if release_id:
        lines.append(f"release_id={manifest_value(release_id)}")
    lines.extend([
        f"checksums_file={manifest_value(checksums.name)}",
        f"checksums_sha256={sha256_file(checksums)}",
        f"signature_file={manifest_value(signature.name)}",
        f"signature_sha256={sha256_file(signature)}",
        f"public_key_file={manifest_value(public_key.name)}",
        f"public_key_sha256={public_key_sha256}",
        f"expected_public_key_sha256={expected_public_key_sha256}",
        f"public_key_manifest_file={manifest_value(key_manifest.name)}",
        f"public_key_manifest_sha256={sha256_file(key_manifest)}",
        "private_key_included=no",
        f"artifact_count={len(entries)}",
    ])
    for index, (name, digest) in enumerate(entries, 1):
        prefix = f"artifact.{index}"
        lines.append(f"{prefix}.path={manifest_value(name)}")
        lines.append(f"{prefix}.sha256={digest}")
    return "\n".join(lines) + "\n"


def atomic_write_text(path: Path, text: str, force: bool) -> int:
    if path.exists() and not force:
        return fail(f"saida ja existe: {path} (use --force para sobrescrever)")
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp_name = ""
    try:
        with tempfile.NamedTemporaryFile("w", dir=str(path.parent), prefix=f".{path.name}.", delete=False, encoding="utf-8") as tmp:
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
    parser = argparse.ArgumentParser(description="Gera manifesto publico deterministico de publicacao da release CapyOS.")
    parser.add_argument("--checksums", type=Path, default=Path("build/release-artifacts.sha256"))
    parser.add_argument("--signature", type=Path)
    parser.add_argument("--artifact-root", type=Path, default=Path("."))
    parser.add_argument("--public-key", type=Path, default=os.environ.get("RELEASE_PUBLIC_KEY") or os.environ.get("CAPYOS_RELEASE_PUBLIC_KEY"))
    parser.add_argument("--expected-public-key-sha256", default=os.environ.get("RELEASE_PUBLIC_KEY_SHA256") or os.environ.get("CAPYOS_RELEASE_PUBLIC_KEY_SHA256"))
    parser.add_argument("--public-key-manifest", type=Path, default=os.environ.get("RELEASE_PUBLIC_KEY_MANIFEST") or os.environ.get("CAPYOS_RELEASE_PUBLIC_KEY_MANIFEST") or "build/release-public-key.manifest")
    parser.add_argument("--output", type=Path, default=Path("build/release-publication.manifest"))
    parser.add_argument("--release-id", default=os.environ.get("RELEASE_PUBLICATION_ID") or os.environ.get("CAPYOS_RELEASE_PUBLICATION_ID"))
    parser.add_argument("--openssl", default=os.environ.get("OPENSSL", "openssl"))
    parser.add_argument("--force", action="store_true")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    checksums = args.checksums.expanduser()
    signature = args.signature.expanduser() if args.signature else checksums.with_name(f"{checksums.name}.sig")
    public_key = args.public_key.expanduser() if args.public_key else None
    key_manifest = args.public_key_manifest.expanduser()
    artifact_root = args.artifact_root.expanduser()
    if public_key is None:
        return fail("informe --public-key, RELEASE_PUBLIC_KEY ou CAPYOS_RELEASE_PUBLIC_KEY")
    rc, entries, public_key_sha256 = validate_public_materials(
        checksums,
        signature,
        public_key,
        args.expected_public_key_sha256,
        key_manifest,
        artifact_root,
        args.openssl,
    )
    if rc != 0:
        return rc
    expected_public_key_sha256 = normalize_sha256_hex(args.expected_public_key_sha256)
    # By contract `require_*` upstream rejected None/empty fingerprints
    # before reaching here. Use explicit checks instead of `assert` so
    # `python -O` cannot silently strip the invariant (py/assert-stmt).
    if expected_public_key_sha256 is None or expected_public_key_sha256 == "":
        raise RuntimeError(
            "internal: expected_public_key_sha256 missing after upstream validation"
        )
    manifest = build_manifest(checksums, signature, public_key, public_key_sha256, expected_public_key_sha256, key_manifest, entries, args.release_id)
    rc = atomic_write_text(args.output, manifest, args.force)
    if rc != 0:
        return rc
    ok(f"manifesto publico de publicacao gravado em {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
