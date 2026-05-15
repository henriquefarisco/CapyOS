#!/usr/bin/env python3
from __future__ import annotations

import argparse
import hashlib
import os
import re
import shlex
import sys
from pathlib import Path

PRIVATE_KEY_ENV = (
    "RELEASE_PRIVATE_KEY",
    "CAPYOS_RELEASE_PRIVATE_KEY",
)
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
ARTIFACT_FIELD = re.compile(r"^artifact\.([1-9][0-9]*)\.(path|sha256)$")
ED25519_SIGNATURE_SIZE = 64


def fail(message: str) -> int:
    print(f"[err] {message}", file=sys.stderr)
    return 1


def ok(message: str) -> None:
    print(f"[ok] {message}")


def normalize_sha256_hex(value: str | None) -> str | None:
    if not value:
        return None
    normalized = value.strip().lower().replace(":", "")
    if len(normalized) != 64 or any(c not in "0123456789abcdef" for c in normalized):
        return ""
    return normalized


def require_sha256(value: str | None, label: str) -> tuple[int, str]:
    normalized = normalize_sha256_hex(value)
    if normalized is None or normalized == "":
        return fail(f"{label} SHA-256 invalido"), ""
    if value != normalized:
        return fail(f"{label} SHA-256 deve usar hex lowercase sem ':'"), ""
    return 0, normalized


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def require_file(path: Path, label: str) -> int:
    if not path.exists() or not path.is_file():
        return fail(f"{label} ausente: {path}")
    if path.stat().st_size == 0:
        return fail(f"{label} vazio: {path}")
    return 0


def reject_private_key_env() -> int:
    leaked = [name for name in PRIVATE_KEY_ENV if os.environ.get(name)]
    if leaked:
        return fail("contrato publico de CI nao aceita chave privada no ambiente: " + ", ".join(leaked))
    return 0


def material_name_safe(name: str) -> bool:
    if not name or "\\" in name or "/" in name or chr(0) in name:
        return False
    candidate = Path(name)
    return not candidate.is_absolute() and candidate.name == name and name not in (".", "..")


def artifact_name_safe(name: str) -> bool:
    candidate = Path(name)
    if candidate.is_absolute():
        return False
    return all(part not in ("", ".", "..") for part in candidate.parts)


def load_key_value(path: Path, label: str) -> tuple[int, dict[str, str]]:
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


def validate_key_manifest(path: Path, public_key: Path, expected_sha256: str) -> int:
    rc, data = load_key_value(path, "manifesto publico da chave")
    if rc != 0:
        return rc
    missing = sorted(KEY_MANIFEST_FIELDS - set(data))
    if missing:
        return fail("manifesto publico da chave sem campos: " + ", ".join(missing))
    for key in data:
        if key not in KEY_MANIFEST_FIELDS:
            return fail(f"manifesto publico da chave contem campo desconhecido: {key}")
    if data["format"] != KEY_MANIFEST_FORMAT:
        return fail("manifesto publico da chave tem formato inesperado")
    if data["algorithm"] != "Ed25519" or data["public_key_encoding"] != "PEM/SPKI":
        return fail("manifesto publico da chave nao descreve Ed25519 PEM/SPKI")
    if data["private_key_included"] != "no":
        return fail("manifesto publico da chave indica chave privada incluida")
    if data["public_key_file"] != public_key.name:
        return fail("manifesto publico da chave aponta nome de chave diferente")
    for field in ("public_key_sha256", "expected_public_key_sha256"):
        rc, _ = require_sha256(data[field], field)
        if rc != 0:
            return rc
    if data["public_key_sha256"] != expected_sha256 or data["expected_public_key_sha256"] != expected_sha256:
        return fail("manifesto publico da chave diverge do fingerprint esperado")
    ok("contrato da chave publica conferido")
    return 0


def load_checksums(path: Path) -> tuple[int, list[tuple[str, str]]]:
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
        rc, normalized = require_sha256(digest, f"linha {line_no}")
        if rc != 0:
            return rc, []
        if not name or "\\" in name or chr(0) in name or not artifact_name_safe(name):
            return fail(f"caminho de artefato invalido na linha: {line_no}"), []
        if name in seen:
            return fail(f"arquivo de checksums contem caminho duplicado: {name}"), []
        seen.add(name)
        entries.append((name, normalized))
    if not entries:
        return fail("arquivo de checksums sem entradas"), []
    return 0, entries


def validate_publication_manifest(path: Path, checksums: Path, signature: Path, public_key: Path, key_manifest: Path, expected_sha256: str, checksum_entries: list[tuple[str, str]]) -> int:
    rc, data = load_key_value(path, "manifesto publico de publicacao")
    if rc != 0:
        return rc
    artifact_fields: dict[int, dict[str, str]] = {}
    for key in data:
        if key in PUBLICATION_REQUIRED_FIELDS or key == "release_id":
            continue
        match = ARTIFACT_FIELD.match(key)
        if not match:
            return fail(f"manifesto publico de publicacao contem campo desconhecido: {key}")
        artifact_fields.setdefault(int(match.group(1)), {})[match.group(2)] = data[key]
    missing = sorted(PUBLICATION_REQUIRED_FIELDS - set(data))
    if missing:
        return fail("manifesto publico de publicacao sem campos: " + ", ".join(missing))
    if data["format"] != PUBLICATION_MANIFEST_FORMAT:
        return fail("manifesto publico de publicacao tem formato inesperado")
    if data["signature_algorithm"] != "Ed25519" or data["checksum_algorithm"] != "SHA-256":
        return fail("manifesto publico de publicacao tem algoritmos inesperados")
    if data["private_key_included"] != "no":
        return fail("manifesto publico de publicacao indica chave privada incluida")
    for field in ("checksums_file", "signature_file", "public_key_file", "public_key_manifest_file"):
        if not material_name_safe(data[field]):
            return fail(f"manifesto publico de publicacao tem nome invalido: {field}")
    if data["checksums_file"] != checksums.name:
        return fail("manifesto publico de publicacao aponta arquivo de checksums diferente")
    if data["signature_file"] != signature.name:
        return fail("manifesto publico de publicacao aponta assinatura diferente")
    if data["public_key_file"] != public_key.name:
        return fail("manifesto publico de publicacao aponta chave publica diferente")
    if data["public_key_manifest_file"] != key_manifest.name:
        return fail("manifesto publico de publicacao aponta manifesto da chave diferente")
    for field in ("checksums_sha256", "signature_sha256", "public_key_sha256", "expected_public_key_sha256", "public_key_manifest_sha256"):
        rc, _ = require_sha256(data[field], field)
        if rc != 0:
            return rc
    if data["checksums_sha256"] != sha256_file(checksums):
        return fail("manifesto publico de publicacao diverge do arquivo de checksums")
    if data["signature_sha256"] != sha256_file(signature):
        return fail("manifesto publico de publicacao diverge da assinatura")
    if data["public_key_manifest_sha256"] != sha256_file(key_manifest):
        return fail("manifesto publico de publicacao diverge do manifesto da chave")
    if data["public_key_sha256"] != expected_sha256 or data["expected_public_key_sha256"] != expected_sha256:
        return fail("manifesto publico de publicacao diverge do fingerprint esperado")
    try:
        artifact_count = int(data["artifact_count"])
    except ValueError:
        return fail("manifesto publico de publicacao tem artifact_count invalido")
    if artifact_count != len(checksum_entries):
        return fail("manifesto publico de publicacao tem contagem de artefatos divergente")
    if set(artifact_fields) != set(range(1, artifact_count + 1)):
        return fail("manifesto publico de publicacao contem indices de artefato inconsistentes")
    manifest_entries: list[tuple[str, str]] = []
    for index in range(1, artifact_count + 1):
        fields = artifact_fields[index]
        if set(fields) != {"path", "sha256"}:
            return fail(f"manifesto publico de publicacao tem artefato incompleto: {index}")
        path_value = fields["path"]
        rc, digest = require_sha256(fields["sha256"], f"artifact.{index}")
        if rc != 0:
            return rc
        if not path_value or "\\" in path_value or chr(0) in path_value or not artifact_name_safe(path_value):
            return fail(f"manifesto publico de publicacao tem caminho de artefato invalido: {index}")
        manifest_entries.append((path_value, digest))
    if manifest_entries != checksum_entries:
        return fail("manifesto publico de publicacao diverge do contrato de checksums")
    ok("contrato do manifesto publico de publicacao conferido")
    return 0


def option_value(tokens: list[str], name: str) -> str | None:
    prefix = f"{name}="
    for index, token in enumerate(tokens):
        if token == name:
            if index + 1 >= len(tokens):
                return ""
            return tokens[index + 1]
        if token.startswith(prefix):
            return token[len(prefix):]
    return None


def validate_smoke_args(raw_args: str | None) -> int:
    if not raw_args:
        return fail("SMOKE_X64_VMWARE_ARGS ausente no contrato de CI")
    try:
        tokens = shlex.split(raw_args)
    except ValueError as exc:
        return fail(f"SMOKE_X64_VMWARE_ARGS invalido: {exc}")
    if "..." in tokens:
        return fail("SMOKE_X64_VMWARE_ARGS contem placeholder '...'; informe argumentos reais de VMware")
    for forbidden in ("--dry-run", "--no-artifact-check", "--no-poweroff", "--no-tool-check"):
        if forbidden in tokens:
            return fail(f"SMOKE_X64_VMWARE_ARGS nao deve usar {forbidden} em CI de release")
    provider = option_value(tokens, "--provider")
    if provider not in ("vmrun", "govc"):
        return fail("SMOKE_X64_VMWARE_ARGS deve informar --provider vmrun ou --provider govc")
    if provider == "vmrun":
        if not option_value(tokens, "--vmx"):
            return fail("provider vmrun exige --vmx")
        ok("contrato VMware vmrun conferido")
        return 0
    if not option_value(tokens, "--vm-name"):
        return fail("provider govc exige --vm-name")
    if not option_value(tokens, "--govc-serial-log"):
        return fail("provider govc exige --govc-serial-log")
    missing_env = [name for name in ("GOVC_URL", "GOVC_USERNAME", "GOVC_DATACENTER") if not os.environ.get(name)]
    if not os.environ.get("GOVC_PASSWORD") and not os.environ.get("GOVC_PASSWORD_FILE"):
        missing_env.append("GOVC_PASSWORD ou GOVC_PASSWORD_FILE")
    if missing_env:
        return fail("provider govc exige ambiente: " + ", ".join(missing_env))
    ok("contrato VMware govc conferido")
    return 0


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Valida contrato publico de CI para publicacao da release CapyOS.")
    parser.add_argument("--checksums", type=Path, default=Path("build/release-artifacts.sha256"))
    parser.add_argument("--signature", type=Path)
    parser.add_argument("--public-key", type=Path, default=os.environ.get("RELEASE_PUBLIC_KEY") or os.environ.get("CAPYOS_RELEASE_PUBLIC_KEY"))
    parser.add_argument("--expected-public-key-sha256", default=os.environ.get("RELEASE_PUBLIC_KEY_SHA256") or os.environ.get("CAPYOS_RELEASE_PUBLIC_KEY_SHA256"))
    parser.add_argument("--public-key-manifest", type=Path, default=os.environ.get("RELEASE_PUBLIC_KEY_MANIFEST") or os.environ.get("CAPYOS_RELEASE_PUBLIC_KEY_MANIFEST") or "build/release-public-key.manifest")
    parser.add_argument("--publication-manifest", type=Path, default=Path("build/release-publication.manifest"))
    parser.add_argument("--smoke-vmware-args", default=os.environ.get("SMOKE_X64_VMWARE_ARGS"))
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    rc = reject_private_key_env()
    if rc != 0:
        return rc
    checksums = args.checksums.expanduser()
    signature = args.signature.expanduser() if args.signature else checksums.with_name(f"{checksums.name}.sig")
    public_key = args.public_key.expanduser() if args.public_key else None
    key_manifest = args.public_key_manifest.expanduser()
    publication_manifest = args.publication_manifest.expanduser()
    if public_key is None:
        return fail("informe --public-key, RELEASE_PUBLIC_KEY ou CAPYOS_RELEASE_PUBLIC_KEY")
    expected = normalize_sha256_hex(args.expected_public_key_sha256)
    if expected is None:
        return fail("informe --expected-public-key-sha256, RELEASE_PUBLIC_KEY_SHA256 ou CAPYOS_RELEASE_PUBLIC_KEY_SHA256")
    if expected == "":
        return fail("fingerprint SHA-256 invalido; use hex64 ou pares separados por ':'")
    for path, label in (
        (checksums, "arquivo de checksums"),
        (signature, "assinatura"),
        (public_key, "chave publica"),
        (key_manifest, "manifesto publico da chave"),
        (publication_manifest, "manifesto publico de publicacao"),
    ):
        rc = require_file(path, label)
        if rc != 0:
            return rc
    if signature.stat().st_size != ED25519_SIGNATURE_SIZE:
        return fail(f"assinatura Ed25519 raw deve ter {ED25519_SIGNATURE_SIZE} bytes")
    rc = validate_key_manifest(key_manifest, public_key, expected)
    if rc != 0:
        return rc
    rc, checksum_entries = load_checksums(checksums)
    if rc != 0:
        return rc
    rc = validate_publication_manifest(publication_manifest, checksums, signature, public_key, key_manifest, expected, checksum_entries)
    if rc != 0:
        return rc
    rc = validate_smoke_args(args.smoke_vmware_args)
    if rc != 0:
        return rc
    ok("contrato publico de CI/publicacao conferido")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
