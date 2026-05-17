#!/usr/bin/env python3
from __future__ import annotations

import argparse
import hashlib
import os
import shlex
import sys
import tempfile
from pathlib import Path

from release_ci_official_provisioning_contract import (
    fail,
    normalize_sha256_hex,
    ok,
    public_key_der_from_pem,
    reject_private_key_env,
    validate_key_manifest,
    validate_smoke_args,
    validate_version_contract,
    value_from_arg_or_env,
)

HANDOFF_FORMAT = "capyos-release-official-handoff-manifest-v1"
PUBLICATION_FORMAT = "capyos-release-publication-manifest-v1"
ED25519_SIGNATURE_SIZE = 64
PUBLICATION_REQUIRED = {
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
REQUIRED_GATES = (
    "release-ci-official-provisioning-contract",
    "release-ci-tag-gate",
    "release-publication-gate",
    "smoke-x64-vmware-mouse-events",
)


def require_file(path: Path, label: str) -> int:
    if not path.exists() or not path.is_file():
        return fail(f"{label} ausente: {path}")
    if path.stat().st_size == 0:
        return fail(f"{label} vazio: {path}")
    return 0


def read_text(path: Path, label: str) -> tuple[int, str]:
    rc = require_file(path, label)
    if rc != 0:
        return rc, ""
    try:
        text = path.read_text(encoding="utf-8")
    except UnicodeDecodeError:
        return fail(f"{label} nao e UTF-8: {path}"), ""
    if "\x00" in text:
        return fail(f"{label} contem byte NUL: {path}"), ""
    return 0, text


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def manifest_value(value: str) -> str:
    return value.replace("\\", "/").replace("\r", "_").replace("\n", "_")


def material_name_safe(name: str) -> bool:
    if not name or "\\" in name or "/" in name or "\x00" in name:
        return False
    candidate = Path(name)
    return not candidate.is_absolute() and candidate.name == name and name not in (".", "..")


def artifact_name_safe(name: str) -> bool:
    if not name or "\\" in name or "\x00" in name:
        return False
    candidate = Path(name)
    if candidate.is_absolute():
        return False
    return all(part not in ("", ".", "..") for part in candidate.parts)


def require_sha256(value: str, label: str) -> tuple[int, str]:
    normalized = normalize_sha256_hex(value)
    if normalized is None or normalized == "":
        return fail(f"{label} SHA-256 invalido"), ""
    if value != normalized:
        return fail(f"{label} SHA-256 deve usar hex lowercase sem ':'"), ""
    return 0, normalized


def load_kv(path: Path, label: str) -> tuple[int, dict[str, str]]:
    rc, text = read_text(path, label)
    if rc != 0:
        return rc, {}
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


def load_checksums(path: Path) -> tuple[int, list[tuple[str, str]]]:
    rc, text = read_text(path, "arquivo de checksums")
    if rc != 0:
        return rc, []
    entries: list[tuple[str, str]] = []
    seen: set[str] = set()
    for line_no, line in enumerate(text.splitlines(), 1):
        if len(line) < 67 or line[64:66] != "  ":
            return fail(f"linha de checksum malformada: {line_no}"), []
        rc, digest = require_sha256(line[:64], f"linha {line_no}")
        if rc != 0:
            return rc, []
        name = line[66:]
        if not artifact_name_safe(name):
            return fail(f"caminho de artefato invalido na linha: {line_no}"), []
        if name in seen:
            return fail(f"arquivo de checksums contem caminho duplicado: {name}"), []
        seen.add(name)
        entries.append((name, digest))
    if not entries:
        return fail("arquivo de checksums sem entradas"), []
    return 0, entries


def load_publication_manifest(path: Path) -> tuple[int, dict[str, str], list[tuple[str, str]]]:
    rc, data = load_kv(path, "manifesto publico de publicacao")
    if rc != 0:
        return rc, {}, []
    missing = sorted(PUBLICATION_REQUIRED - set(data))
    if missing:
        return fail("manifesto publico de publicacao sem campos: " + ", ".join(missing)), {}, []
    if data["format"] != PUBLICATION_FORMAT:
        return fail("manifesto publico de publicacao tem formato inesperado"), {}, []
    if data["signature_algorithm"] != "Ed25519" or data["checksum_algorithm"] != "SHA-256":
        return fail("manifesto publico de publicacao tem algoritmos inesperados"), {}, []
    if data["private_key_included"] != "no":
        return fail("manifesto publico de publicacao indica chave privada incluida"), {}, []
    try:
        artifact_count = int(data["artifact_count"])
    except ValueError:
        return fail("manifesto publico de publicacao tem artifact_count invalido"), {}, []
    if artifact_count <= 0:
        return fail("manifesto publico de publicacao sem artefatos"), {}, []
    entries: list[tuple[str, str]] = []
    allowed = set(PUBLICATION_REQUIRED) | {"release_id"}
    for index in range(1, artifact_count + 1):
        path_key = f"artifact.{index}.path"
        sha_key = f"artifact.{index}.sha256"
        allowed.update((path_key, sha_key))
        if path_key not in data or sha_key not in data:
            return fail(f"manifesto publico de publicacao tem artefato incompleto: {index}"), {}, []
        if not artifact_name_safe(data[path_key]):
            return fail(f"manifesto publico de publicacao tem caminho invalido: {index}"), {}, []
        rc, digest = require_sha256(data[sha_key], f"artifact.{index}")
        if rc != 0:
            return rc, {}, []
        entries.append((data[path_key], digest))
    extra = sorted(set(data) - allowed)
    if extra:
        return fail("manifesto publico de publicacao contem campos desconhecidos: " + ", ".join(extra)), {}, []
    for field in ("checksums_file", "signature_file", "public_key_file", "public_key_manifest_file"):
        if not material_name_safe(data[field]):
            return fail(f"manifesto publico de publicacao tem nome invalido: {field}"), {}, []
    for field in (
        "checksums_sha256",
        "signature_sha256",
        "public_key_sha256",
        "expected_public_key_sha256",
        "public_key_manifest_sha256",
    ):
        rc, _ = require_sha256(data[field], field)
        if rc != 0:
            return rc, {}, []
    return 0, data, entries


def option_value(tokens: list[str], name: str) -> str | None:
    prefix = f"{name}="
    for index, token in enumerate(tokens):
        if token == name:
            return tokens[index + 1] if index + 1 < len(tokens) else ""
        if token.startswith(prefix):
            return token[len(prefix):]
    return None


def smoke_summary(raw_args: str) -> tuple[int, dict[str, str]]:
    try:
        tokens = shlex.split(raw_args)
    except ValueError as exc:
        return fail(f"SMOKE_X64_VMWARE_ARGS invalido: {exc}"), {}
    provider = option_value(tokens, "--provider") or ""
    serial_log = option_value(tokens, "--serial-log") or ""
    summary_log = option_value(tokens, "--summary-log")
    if summary_log is None:
        summary_log = "build/ci/smoke_x64_vmware.summary.log"
    return 0, {
        "provider": provider,
        "serial_log": serial_log,
        "summary_log": summary_log,
        "vm_identifier_configured": "yes",
    }


def validate_publication_links(
    data: dict[str, str],
    entries: list[tuple[str, str]],
    checksums: Path,
    signature: Path,
    public_key: Path,
    public_key_manifest: Path,
    expected_key_sha256: str,
    actual_key_sha256: str,
    checksum_entries: list[tuple[str, str]],
    release_tag: str,
) -> int:
    if data.get("release_id") and data["release_id"] != release_tag:
        return fail("manifesto publico de publicacao tem release_id divergente")
    if data["checksums_file"] != checksums.name or data["signature_file"] != signature.name:
        return fail("manifesto publico de publicacao aponta materiais divergentes")
    if data["public_key_file"] != public_key.name or data["public_key_manifest_file"] != public_key_manifest.name:
        return fail("manifesto publico de publicacao aponta chave/manifesto divergente")
    if data["checksums_sha256"] != sha256_file(checksums):
        return fail("manifesto publico de publicacao diverge dos checksums")
    if data["signature_sha256"] != sha256_file(signature):
        return fail("manifesto publico de publicacao diverge da assinatura")
    if data["public_key_manifest_sha256"] != sha256_file(public_key_manifest):
        return fail("manifesto publico de publicacao diverge do manifesto da chave")
    if data["public_key_sha256"] != actual_key_sha256:
        return fail("manifesto publico de publicacao diverge da chave publica")
    if data["expected_public_key_sha256"] != expected_key_sha256:
        return fail("manifesto publico de publicacao diverge do fingerprint pinado")
    if entries != checksum_entries:
        return fail("manifesto publico de publicacao diverge do arquivo de checksums")
    return 0


def build_manifest(
    current: str,
    extended: str,
    checksums: Path,
    signature: Path,
    public_key: Path,
    public_key_manifest: Path,
    publication_manifest: Path,
    key_sha256: str,
    expected_key_sha256: str,
    entries: list[tuple[str, str]],
    smoke: dict[str, str],
) -> str:
    lines = [
        f"format={HANDOFF_FORMAT}",
        f"release_tag={extended}",
        f"version_current={current}",
        f"version_extended={extended}",
        "track=UEFI/GPT/x86_64",
        "signature_algorithm=Ed25519",
        "checksum_algorithm=SHA-256",
        f"checksums_file={manifest_value(checksums.name)}",
        f"checksums_sha256={sha256_file(checksums)}",
        f"signature_file={manifest_value(signature.name)}",
        f"signature_sha256={sha256_file(signature)}",
        f"signature_size={signature.stat().st_size}",
        f"public_key_file={manifest_value(public_key.name)}",
        f"public_key_sha256={key_sha256}",
        f"expected_public_key_sha256={expected_key_sha256}",
        f"public_key_manifest_file={manifest_value(public_key_manifest.name)}",
        f"public_key_manifest_sha256={sha256_file(public_key_manifest)}",
        f"publication_manifest_file={manifest_value(publication_manifest.name)}",
        f"publication_manifest_sha256={sha256_file(publication_manifest)}",
        "private_key_included=no",
        "vm_powered_on=no",
        "make_executed=no",
        "git_executed=no",
        f"artifact_count={len(entries)}",
    ]
    for index, (name, digest) in enumerate(entries, 1):
        lines.append(f"artifact.{index}.path={manifest_value(name)}")
        lines.append(f"artifact.{index}.sha256={digest}")
    lines.extend([
        f"smoke_provider={manifest_value(smoke['provider'])}",
        f"smoke_serial_log={manifest_value(smoke['serial_log'])}",
        f"smoke_summary_log={manifest_value(smoke['summary_log'])}",
        f"smoke_vm_identifier_configured={smoke['vm_identifier_configured']}",
        f"required_gate_count={len(REQUIRED_GATES)}",
    ])
    for index, gate in enumerate(REQUIRED_GATES, 1):
        lines.append(f"required_gate.{index}={gate}")
    lines.append("generated_by=release_official_handoff_manifest.py")
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
            Path(tmp_name).unlink(missing_ok=True)
    return 0


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Gera/verifica manifesto oficial de handoff F2 da release CapyOS.")
    parser.add_argument(
        "--release-tag",
        default=value_from_arg_or_env(
            None,
            "RELEASE_TAG",
            "CI_COMMIT_TAG",
            "GITHUB_REF_NAME",
            "GITHUB_REF",
        ),
    )
    parser.add_argument("--version-yaml", type=Path, default=Path("VERSION.yaml"))
    parser.add_argument("--version-header", type=Path, default=Path("include/core/version.h"))
    parser.add_argument("--readme", type=Path, default=Path("README.md"))
    parser.add_argument("--release-note", type=Path)
    parser.add_argument("--checksums", type=Path, default=Path("build/release-artifacts.sha256"))
    parser.add_argument("--signature", type=Path)
    parser.add_argument(
        "--public-key",
        type=Path,
        default=value_from_arg_or_env(None, "RELEASE_PUBLIC_KEY", "CAPYOS_RELEASE_PUBLIC_KEY"),
    )
    parser.add_argument(
        "--expected-public-key-sha256",
        default=value_from_arg_or_env(
            None,
            "RELEASE_PUBLIC_KEY_SHA256",
            "CAPYOS_RELEASE_PUBLIC_KEY_SHA256",
        ),
    )
    parser.add_argument(
        "--public-key-manifest",
        type=Path,
        default=value_from_arg_or_env(
            None,
            "RELEASE_PUBLIC_KEY_MANIFEST",
            "CAPYOS_RELEASE_PUBLIC_KEY_MANIFEST",
        ) or "build/release-public-key.manifest",
    )
    parser.add_argument("--publication-manifest", type=Path, default=Path("build/release-publication.manifest"))
    parser.add_argument("--smoke-vmware-args", default=value_from_arg_or_env(None, "SMOKE_X64_VMWARE_ARGS"))
    parser.add_argument("--output", type=Path, default=Path("build/release-official-handoff.manifest"))
    parser.add_argument("--force", action="store_true")
    parser.add_argument("--verify", action="store_true")
    return parser.parse_args()


def expected_manifest(args: argparse.Namespace) -> tuple[int, str]:
    rc, current, extended = validate_version_contract(args)
    if rc != 0:
        return rc, ""
    checksums = args.checksums.expanduser()
    signature = args.signature.expanduser() if args.signature else checksums.with_name(f"{checksums.name}.sig")
    public_key = args.public_key.expanduser() if args.public_key else None
    if public_key is None:
        return fail("informe --public-key, RELEASE_PUBLIC_KEY ou CAPYOS_RELEASE_PUBLIC_KEY"), ""
    expected = normalize_sha256_hex(args.expected_public_key_sha256)
    if expected is None:
        return fail("informe fingerprint publico esperado da chave oficial"), ""
    if expected == "":
        return fail("fingerprint SHA-256 invalido; use hex64 ou pares separados por ':'"), ""
    rc, checksum_entries = load_checksums(checksums)
    if rc != 0:
        return rc, ""
    rc = require_file(signature, "assinatura")
    if rc != 0:
        return rc, ""
    if signature.stat().st_size != ED25519_SIGNATURE_SIZE:
        return fail(f"assinatura Ed25519 raw deve ter {ED25519_SIGNATURE_SIZE} bytes"), ""
    rc, der = public_key_der_from_pem(public_key)
    if rc != 0:
        return rc, ""
    actual = hashlib.sha256(der).hexdigest()
    if actual != expected:
        return fail(f"fingerprint SHA-256 da chave oficial inesperado: {actual}"), ""
    public_key_manifest = args.public_key_manifest.expanduser()
    rc = validate_key_manifest(public_key_manifest, public_key, expected, actual)
    if rc != 0:
        return rc, ""
    publication_manifest = args.publication_manifest.expanduser()
    rc, publication_data, publication_entries = load_publication_manifest(publication_manifest)
    if rc != 0:
        return rc, ""
    rc = validate_publication_links(
        publication_data,
        publication_entries,
        checksums,
        signature,
        public_key,
        public_key_manifest,
        expected,
        actual,
        checksum_entries,
        extended,
    )
    if rc != 0:
        return rc, ""
    rc = validate_smoke_args(args.smoke_vmware_args)
    if rc != 0:
        return rc, ""
    # validate_smoke_args returns 0 only when args.smoke_vmware_args is
    # populated. Use an explicit RuntimeError instead of `assert` so
    # `python -O` cannot silently strip the invariant (py/assert-stmt).
    if args.smoke_vmware_args is None:
        raise RuntimeError(
            "internal: validate_smoke_args returned 0 but smoke_vmware_args is None"
        )
    rc, smoke = smoke_summary(args.smoke_vmware_args)
    if rc != 0:
        return rc, ""
    return 0, build_manifest(
        current,
        extended,
        checksums,
        signature,
        public_key,
        public_key_manifest,
        publication_manifest,
        actual,
        expected,
        checksum_entries,
        smoke,
    )


def main() -> int:
    args = parse_args()
    rc = reject_private_key_env()
    if rc != 0:
        return rc
    rc, manifest = expected_manifest(args)
    if rc != 0:
        return rc
    output = args.output.expanduser()
    if args.verify:
        rc, existing = read_text(output, "manifesto oficial de handoff")
        if rc != 0:
            return rc
        if existing != manifest:
            return fail("manifesto oficial de handoff diverge dos materiais publicos")
        ok("manifesto oficial de handoff conferido")
        return 0
    rc = atomic_write_text(output, manifest, args.force)
    if rc != 0:
        return rc
    ok(f"manifesto oficial de handoff escrito em {output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
