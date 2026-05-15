#!/usr/bin/env python3
from __future__ import annotations

import argparse
import hashlib
import os
import tempfile
from pathlib import Path

from release_ci_official_provisioning_contract import (
    fail,
    normalize_release_tag,
    ok,
    parse_version_yaml,
    reject_private_key_env,
    validate_version_contract,
    value_from_arg_or_env,
)
from release_ci_smoke_acceptance import (
    ACCEPTANCE_FORMAT,
    REQUIRED_GATES as ACCEPTANCE_REQUIRED_GATES,
    expected_acceptance,
)

PROMOTION_FORMAT = "capyos-release-smoke-promotion-manifest-v1"
PUBLICATION_FORMAT = "capyos-release-publication-manifest-v1"
HANDOFF_FORMAT = "capyos-release-official-handoff-manifest-v1"
PRIVATE_KEY_MARKERS = (
    "-----BEGIN PRIVATE KEY-----",
    "-----BEGIN OPENSSH PRIVATE KEY-----",
    "-----BEGIN EC PRIVATE KEY-----",
)
REQUIRED_GATES = (
    "release-ci-official-provisioning-contract",
    "release-ci-tag-gate",
    "release-publication-gate",
    "release-official-handoff-manifest",
    "verify-release-official-handoff-manifest",
    "release-ci-smoke-readiness",
    "smoke-x64-vmware-mouse-events",
    "release-ci-smoke-evidence",
    "verify-release-ci-smoke-evidence",
    "release-ci-smoke-acceptance",
    "verify-release-ci-smoke-acceptance",
)


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def manifest_value(value: str) -> str:
    return value.replace("\\", "/").replace("\r", "_").replace("\n", "_")


def read_manifest(path: Path, label: str, max_bytes: int) -> tuple[int, bytes, str, dict[str, str]]:
    if max_bytes <= 0:
        return fail(f"limite invalido para {label}"), b"", "", {}
    if not path.exists() or not path.is_file():
        return fail(f"{label} ausente: {path}"), b"", "", {}
    size = path.stat().st_size
    if size == 0:
        return fail(f"{label} vazio: {path}"), b"", "", {}
    if size > max_bytes:
        return fail(f"{label} excede limite de {max_bytes} bytes"), b"", "", {}
    data = path.read_bytes()
    try:
        text = data.decode("utf-8")
    except UnicodeDecodeError:
        return fail(f"{label} nao e UTF-8: {path}"), b"", "", {}
    if "\x00" in text:
        return fail(f"{label} contem byte NUL: {path}"), b"", "", {}
    for marker in PRIVATE_KEY_MARKERS:
        if marker in text:
            return fail(f"{label} contem marcador privado"), b"", "", {}
    parsed: dict[str, str] = {}
    for line_no, line in enumerate(text.splitlines(), 1):
        if not line:
            return fail(f"{label} contem linha vazia: {line_no}"), b"", "", {}
        if "=" not in line:
            return fail(f"{label} contem linha sem '=': {line_no}"), b"", "", {}
        key, value = line.split("=", 1)
        if key in parsed:
            return fail(f"{label} contem campo duplicado: {key}"), b"", "", {}
        parsed[key] = value
    return 0, data, text, parsed


def require_field(data: dict[str, str], field: str, expected: str, label: str) -> int:
    if data.get(field) != expected:
        return fail(f"{label} tem {field} divergente")
    return 0


def validate_release_fields(data: dict[str, str], current: str, extended: str, label: str) -> int:
    for field, expected in (
        ("release_tag", extended),
        ("version_current", current),
        ("version_extended", extended),
        ("track", "UEFI/GPT/x86_64"),
    ):
        rc = require_field(data, field, expected, label)
        if rc != 0:
            return rc
    return 0


def validate_gates(data: dict[str, str], label: str, required: tuple[str, ...]) -> int:
    try:
        count = int(data["required_gate_count"], 10)
    except (KeyError, ValueError):
        return fail(f"{label} tem required_gate_count invalido")
    if count <= 0:
        return fail(f"{label} sem gates obrigatorios")
    gates = {data.get(f"required_gate.{index}", "") for index in range(1, count + 1)}
    missing = [gate for gate in required if gate not in gates]
    if missing:
        return fail(f"{label} sem gates de promocao: " + ", ".join(missing))
    return 0


def validate_declarations(data: dict[str, str], label: str, expected: dict[str, str]) -> int:
    for field, value in expected.items():
        rc = require_field(data, field, value, label)
        if rc != 0:
            return rc
    return 0


def build_manifest(
    current: str,
    extended: str,
    publication_path: Path,
    publication_data: bytes,
    handoff_path: Path,
    handoff_data: bytes,
    acceptance_path: Path,
    acceptance_data: bytes,
    acceptance: dict[str, str],
) -> str:
    lines = [
        f"format={PROMOTION_FORMAT}",
        f"release_tag={extended}",
        f"version_current={current}",
        f"version_extended={extended}",
        "track=UEFI/GPT/x86_64",
        f"publication_manifest_file={manifest_value(publication_path.name)}",
        f"publication_manifest_sha256={sha256_bytes(publication_data)}",
        f"handoff_manifest_file={manifest_value(handoff_path.name)}",
        f"handoff_manifest_sha256={sha256_bytes(handoff_data)}",
        f"smoke_acceptance_manifest_file={manifest_value(acceptance_path.name)}",
        f"smoke_acceptance_manifest_sha256={sha256_bytes(acceptance_data)}",
        f"smoke_acceptance_format={ACCEPTANCE_FORMAT}",
        f"smoke_provider={manifest_value(acceptance['smoke_provider'])}",
        f"smoke_serial_log={manifest_value(acceptance['smoke_serial_log'])}",
        f"smoke_summary_log={manifest_value(acceptance['smoke_summary_log'])}",
        f"required_gate_count={len(REQUIRED_GATES)}",
    ]
    for index, gate in enumerate(REQUIRED_GATES, 1):
        lines.append(f"required_gate.{index}={gate}")
    lines.extend([
        "public_release_promotable=yes",
        "private_key_included=no",
        "vm_powered_on_by_gate=no",
        "make_executed=no",
        "git_executed=no",
        "generated_by=release_ci_smoke_promotion.py",
    ])
    return "\n".join(lines) + "\n"


def atomic_write(path: Path, text: str, force: bool) -> int:
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
    parser = argparse.ArgumentParser(description="Gera/verifica promocao publica pos-smoke VMware F2.")
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
    parser.add_argument("--publication-manifest", type=Path, default=Path("build/release-publication.manifest"))
    parser.add_argument("--handoff-manifest", type=Path, default=Path("build/release-official-handoff.manifest"))
    parser.add_argument("--smoke-acceptance-manifest", type=Path, default=Path("build/release-smoke-acceptance.manifest"))
    parser.add_argument("--smoke-evidence-manifest", type=Path, default=Path("build/release-smoke-evidence.manifest"))
    parser.add_argument("--smoke-vmware-args", default=value_from_arg_or_env(None, "SMOKE_X64_VMWARE_ARGS"))
    parser.add_argument("--serial-log", type=Path)
    parser.add_argument("--summary-log", type=Path)
    parser.add_argument("--output", type=Path, default=Path("build/release-smoke-promotion.manifest"))
    parser.add_argument("--max-manifest-bytes", type=int, default=1048576)
    parser.add_argument("--max-evidence-bytes", type=int, default=1048576)
    parser.add_argument("--max-handoff-bytes", type=int, default=1048576)
    parser.add_argument("--max-serial-bytes", type=int, default=33554432)
    parser.add_argument("--max-summary-bytes", type=int, default=1048576)
    parser.add_argument("--force", action="store_true")
    parser.add_argument("--verify", action="store_true")
    return parser.parse_args()


def expected_promotion(args: argparse.Namespace) -> tuple[int, str]:
    repo_root = Path(__file__).resolve().parents[2]
    rc = validate_version_contract(args)
    if rc != 0:
        return rc, ""
    rc, current, extended = parse_version_yaml(args.version_yaml.expanduser())
    if rc != 0:
        return rc, ""
    if normalize_release_tag(args.release_tag) != extended:
        return fail("release tag diverge da versao estendida na promocao"), ""
    publication_path = args.publication_manifest.expanduser()
    handoff_path = args.handoff_manifest.expanduser()
    acceptance_path = args.smoke_acceptance_manifest.expanduser()
    if not publication_path.is_absolute():
        publication_path = (repo_root / publication_path).resolve()
    if not handoff_path.is_absolute():
        handoff_path = (repo_root / handoff_path).resolve()
    if not acceptance_path.is_absolute():
        acceptance_path = (repo_root / acceptance_path).resolve()
    rc, publication_data, _publication_text, publication = read_manifest(
        publication_path,
        "manifesto publico de publicacao",
        args.max_manifest_bytes,
    )
    if rc != 0:
        return rc, ""
    rc, handoff_data, _handoff_text, handoff = read_manifest(
        handoff_path,
        "manifesto oficial de handoff",
        args.max_manifest_bytes,
    )
    if rc != 0:
        return rc, ""
    rc, acceptance_data, acceptance_text, acceptance = read_manifest(
        acceptance_path,
        "manifesto de aceitacao do smoke",
        args.max_manifest_bytes,
    )
    if rc != 0:
        return rc, ""
    for data, label, fmt in (
        (publication, "manifesto publico de publicacao", PUBLICATION_FORMAT),
        (handoff, "manifesto oficial de handoff", HANDOFF_FORMAT),
        (acceptance, "manifesto de aceitacao do smoke", ACCEPTANCE_FORMAT),
    ):
        rc = require_field(data, "format", fmt, label)
        if rc != 0:
            return rc, ""
    rc = validate_release_fields(handoff, current, extended, "manifesto oficial de handoff")
    if rc != 0:
        return rc, ""
    rc = validate_release_fields(acceptance, current, extended, "manifesto de aceitacao do smoke")
    if rc != 0:
        return rc, ""
    if publication.get("release_id") and publication["release_id"] != extended:
        return fail("manifesto publico de publicacao tem release_id divergente"), ""
    rc = validate_declarations(publication, "manifesto publico de publicacao", {"private_key_included": "no"})
    if rc != 0:
        return rc, ""
    rc = validate_declarations(
        handoff,
        "manifesto oficial de handoff",
        {
            "private_key_included": "no",
            "vm_powered_on": "no",
            "make_executed": "no",
            "git_executed": "no",
        },
    )
    if rc != 0:
        return rc, ""
    rc = validate_declarations(
        acceptance,
        "manifesto de aceitacao do smoke",
        {
            "public_release_smoke_accepted": "yes",
            "private_key_included": "no",
            "vm_powered_on_by_gate": "no",
            "make_executed": "no",
            "git_executed": "no",
        },
    )
    if rc != 0:
        return rc, ""
    if handoff.get("publication_manifest_sha256") != sha256_bytes(publication_data):
        return fail("handoff diverge do manifesto publico de publicacao"), ""
    if acceptance.get("handoff_manifest_sha256") != sha256_bytes(handoff_data):
        return fail("aceitacao diverge do handoff oficial"), ""
    rc, expected_acceptance_text = expected_acceptance(args)
    if rc != 0:
        return rc, ""
    if acceptance_text != expected_acceptance_text:
        return fail("manifesto de aceitacao diverge do estado esperado"), ""
    rc = validate_gates(acceptance, "manifesto de aceitacao do smoke", ACCEPTANCE_REQUIRED_GATES)
    if rc != 0:
        return rc, ""
    return 0, build_manifest(
        current,
        extended,
        publication_path,
        publication_data,
        handoff_path,
        handoff_data,
        acceptance_path,
        acceptance_data,
        acceptance,
    )


def main() -> int:
    args = parse_args()
    rc = reject_private_key_env()
    if rc != 0:
        return rc
    rc, manifest = expected_promotion(args)
    if rc != 0:
        return rc
    repo_root = Path(__file__).resolve().parents[2]
    output = args.output.expanduser()
    if not output.is_absolute():
        output = (repo_root / output).resolve()
    if args.verify:
        rc, _data, text, _parsed = read_manifest(output, "manifesto de promocao pos-smoke", args.max_manifest_bytes)
        if rc != 0:
            return rc
        if text != manifest:
            return fail("manifesto de promocao pos-smoke diverge do estado esperado")
        ok("manifesto de promocao pos-smoke conferido")
        return 0
    rc = atomic_write(output, manifest, args.force)
    if rc != 0:
        return rc
    ok(f"manifesto de promocao pos-smoke escrito em {output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
