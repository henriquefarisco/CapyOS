#!/usr/bin/env python3
from __future__ import annotations

import argparse
import shlex
from pathlib import Path

from release_ci_official_provisioning_contract import (
    fail,
    normalize_release_tag,
    normalize_sha256_hex,
    ok,
    parse_version_yaml,
    reject_private_key_env,
    validate_smoke_args,
    validate_version_contract,
    value_from_arg_or_env,
)

HANDOFF_FORMAT = "capyos-release-official-handoff-manifest-v1"
REQUIRED_GATES = (
    "release-ci-official-provisioning-contract",
    "release-ci-tag-gate",
    "release-publication-gate",
    "smoke-x64-vmware-mouse-events",
)
ED25519_SIGNATURE_SIZE = 64
PRIVATE_KEY_MARKERS = (
    "-----BEGIN PRIVATE KEY-----",
    "-----BEGIN OPENSSH PRIVATE KEY-----",
    "-----BEGIN EC PRIVATE KEY-----",
)
REQUIRED_FIELDS = {
    "format",
    "release_tag",
    "version_current",
    "version_extended",
    "track",
    "signature_algorithm",
    "checksum_algorithm",
    "checksums_file",
    "checksums_sha256",
    "signature_file",
    "signature_sha256",
    "signature_size",
    "public_key_file",
    "public_key_sha256",
    "expected_public_key_sha256",
    "public_key_manifest_file",
    "public_key_manifest_sha256",
    "publication_manifest_file",
    "publication_manifest_sha256",
    "private_key_included",
    "vm_powered_on",
    "make_executed",
    "git_executed",
    "artifact_count",
    "smoke_provider",
    "smoke_serial_log",
    "smoke_summary_log",
    "smoke_vm_identifier_configured",
    "required_gate_count",
    "generated_by",
}
SHA_FIELDS = {
    "checksums_sha256",
    "signature_sha256",
    "public_key_sha256",
    "expected_public_key_sha256",
    "public_key_manifest_sha256",
    "publication_manifest_sha256",
}
MATERIAL_NAME_FIELDS = {
    "checksums_file",
    "signature_file",
    "public_key_file",
    "public_key_manifest_file",
    "publication_manifest_file",
}


def read_text(path: Path, label: str) -> tuple[int, str]:
    if not path.exists() or not path.is_file():
        return fail(f"{label} ausente: {path}"), ""
    if path.stat().st_size == 0:
        return fail(f"{label} vazio: {path}"), ""
    try:
        text = path.read_text(encoding="utf-8")
    except UnicodeDecodeError:
        return fail(f"{label} nao e UTF-8: {path}"), ""
    if "\x00" in text:
        return fail(f"{label} contem byte NUL: {path}"), ""
    return 0, text


def load_key_value(path: Path, label: str) -> tuple[int, dict[str, str]]:
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


def require_sha256(value: str, label: str) -> int:
    normalized = normalize_sha256_hex(value)
    if normalized is None or normalized == "":
        return fail(f"{label} SHA-256 invalido")
    if value != normalized:
        return fail(f"{label} SHA-256 deve usar hex lowercase sem ':'")
    return 0


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


def ci_log_path_safe(value: str) -> bool:
    if not value or "\\" in value or "\x00" in value:
        return False
    path = Path(value)
    if path.is_absolute():
        return False
    parts = path.parts
    return len(parts) >= 3 and parts[0] == "build" and parts[1] == "ci" and value.endswith(".log")


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
        "smoke_provider": provider,
        "smoke_serial_log": serial_log,
        "smoke_summary_log": summary_log,
        "smoke_vm_identifier_configured": "yes",
    }


def validate_private_material_absent(data: dict[str, str]) -> int:
    for key, value in data.items():
        for marker in PRIVATE_KEY_MARKERS:
            if marker in value:
                return fail(f"manifesto de prontidao contem marcador privado em {key}")
    return 0


def validate_artifacts(data: dict[str, str], allowed: set[str]) -> int:
    try:
        artifact_count = int(data["artifact_count"])
    except ValueError:
        return fail("manifesto de handoff tem artifact_count invalido")
    if artifact_count <= 0:
        return fail("manifesto de handoff sem artefatos")
    seen: set[str] = set()
    for index in range(1, artifact_count + 1):
        path_key = f"artifact.{index}.path"
        sha_key = f"artifact.{index}.sha256"
        allowed.update((path_key, sha_key))
        if path_key not in data or sha_key not in data:
            return fail(f"manifesto de handoff tem artefato incompleto: {index}")
        if not artifact_name_safe(data[path_key]):
            return fail(f"manifesto de handoff tem caminho de artefato invalido: {index}")
        if data[path_key] in seen:
            return fail(f"manifesto de handoff tem artefato duplicado: {data[path_key]}")
        seen.add(data[path_key])
        rc = require_sha256(data[sha_key], sha_key)
        if rc != 0:
            return rc
    return 0


def validate_required_gates(data: dict[str, str], allowed: set[str]) -> int:
    try:
        required_gate_count = int(data["required_gate_count"])
    except ValueError:
        return fail("manifesto de handoff tem required_gate_count invalido")
    if required_gate_count != len(REQUIRED_GATES):
        return fail("manifesto de handoff tem quantidade de gates inesperada")
    for index, gate in enumerate(REQUIRED_GATES, 1):
        key = f"required_gate.{index}"
        allowed.add(key)
        if data.get(key) != gate:
            return fail(f"manifesto de handoff tem gate obrigatorio divergente: {index}")
    return 0


def validate_handoff(data: dict[str, str], current: str, extended: str, smoke: dict[str, str]) -> int:
    missing = sorted(REQUIRED_FIELDS - set(data))
    if missing:
        return fail("manifesto de handoff sem campos: " + ", ".join(missing))
    if data["format"] != HANDOFF_FORMAT:
        return fail("manifesto de handoff tem formato inesperado")
    if data["release_tag"] != extended:
        return fail("manifesto de handoff diverge da versao estendida")
    if data["version_current"] != current or data["version_extended"] != extended:
        return fail("manifesto de handoff diverge de VERSION.yaml")
    if data["track"] != "UEFI/GPT/x86_64":
        return fail("manifesto de handoff tem trilha de release inesperada")
    if data["signature_algorithm"] != "Ed25519" or data["checksum_algorithm"] != "SHA-256":
        return fail("manifesto de handoff tem algoritmos inesperados")
    if data["signature_size"] != str(ED25519_SIGNATURE_SIZE):
        return fail("manifesto de handoff tem tamanho de assinatura inesperado")
    if data["public_key_sha256"] != data["expected_public_key_sha256"]:
        return fail("manifesto de handoff tem fingerprints de chave divergentes")
    if data["generated_by"] != "release_official_handoff_manifest.py":
        return fail("manifesto de handoff tem gerador inesperado")
    for field in SHA_FIELDS:
        rc = require_sha256(data[field], field)
        if rc != 0:
            return rc
    for field in MATERIAL_NAME_FIELDS:
        if not material_name_safe(data[field]):
            return fail(f"manifesto de handoff tem nome de material invalido: {field}")
    if data["private_key_included"] != "no":
        return fail("manifesto de handoff indica chave privada incluida")
    if data["vm_powered_on"] != "no":
        return fail("manifesto de handoff nao pode indicar VM ligada antes do readiness")
    if data["make_executed"] != "no" or data["git_executed"] != "no":
        return fail("manifesto de handoff deve declarar make/git nao executados")
    for field, expected in smoke.items():
        if data[field] != expected:
            return fail(f"manifesto de handoff diverge de SMOKE_X64_VMWARE_ARGS: {field}")
    if not ci_log_path_safe(data["smoke_serial_log"]):
        return fail("manifesto de handoff tem smoke_serial_log inseguro")
    if not ci_log_path_safe(data["smoke_summary_log"]):
        return fail("manifesto de handoff tem smoke_summary_log inseguro")
    allowed = set(REQUIRED_FIELDS)
    rc = validate_artifacts(data, allowed)
    if rc != 0:
        return rc
    rc = validate_required_gates(data, allowed)
    if rc != 0:
        return rc
    extra = sorted(set(data) - allowed)
    if extra:
        return fail("manifesto de handoff contem campos desconhecidos: " + ", ".join(extra))
    return validate_private_material_absent(data)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Valida prontidao oficial do smoke VMware F2 sem ligar VM.")
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
    parser.add_argument("--handoff-manifest", type=Path, default=Path("build/release-official-handoff.manifest"))
    parser.add_argument("--smoke-vmware-args", default=value_from_arg_or_env(None, "SMOKE_X64_VMWARE_ARGS"))
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    rc = reject_private_key_env()
    if rc != 0:
        return rc
    rc = validate_version_contract(args)
    if rc != 0:
        return rc
    rc, current, extended = parse_version_yaml(args.version_yaml.expanduser())
    if rc != 0:
        return rc
    tag = normalize_release_tag(args.release_tag)
    if tag != extended:
        return fail("release tag diverge da versao estendida no readiness")
    if not args.smoke_vmware_args:
        return fail("informe --smoke-vmware-args ou SMOKE_X64_VMWARE_ARGS")
    rc = validate_smoke_args(args.smoke_vmware_args)
    if rc != 0:
        return rc
    rc, smoke = smoke_summary(args.smoke_vmware_args)
    if rc != 0:
        return rc
    rc, handoff = load_key_value(args.handoff_manifest.expanduser(), "manifesto oficial de handoff")
    if rc != 0:
        return rc
    rc = validate_handoff(handoff, current, extended, smoke)
    if rc != 0:
        return rc
    ok("prontidao oficial do smoke VMware conferida sem ligar VM")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
