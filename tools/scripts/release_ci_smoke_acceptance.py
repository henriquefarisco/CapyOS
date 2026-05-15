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
from release_ci_smoke_evidence import (
    EVIDENCE_FORMAT,
    expected_manifest,
    manifest_value,
)
from release_ci_smoke_readiness import load_key_value

ACCEPTANCE_FORMAT = "capyos-release-smoke-acceptance-manifest-v1"
PRIVATE_KEY_MARKERS = (
    "-----BEGIN PRIVATE KEY-----",
    "-----BEGIN OPENSSH PRIVATE KEY-----",
    "-----BEGIN EC PRIVATE KEY-----",
)
REQUIRED_GATES = (
    "release-ci-official-provisioning-contract",
    "release-ci-tag-gate",
    "release-publication-gate",
    "release-ci-smoke-readiness",
    "smoke-x64-vmware-mouse-events",
    "release-ci-smoke-evidence",
    "verify-release-ci-smoke-evidence",
)


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def read_text(path: Path, label: str, max_bytes: int) -> tuple[int, bytes, str]:
    if max_bytes <= 0:
        return fail(f"limite invalido para {label}"), b"", ""
    if not path.exists() or not path.is_file():
        return fail(f"{label} ausente: {path}"), b"", ""
    size = path.stat().st_size
    if size == 0:
        return fail(f"{label} vazio: {path}"), b"", ""
    if size > max_bytes:
        return fail(f"{label} excede limite de {max_bytes} bytes"), b"", ""
    data = path.read_bytes()
    try:
        text = data.decode("utf-8")
    except UnicodeDecodeError:
        return fail(f"{label} nao e UTF-8: {path}"), b"", ""
    if "\x00" in text:
        return fail(f"{label} contem byte NUL: {path}"), b"", ""
    return 0, data, text


def reject_private_markers(text: str, label: str) -> int:
    for marker in PRIVATE_KEY_MARKERS:
        if marker in text:
            return fail(f"{label} contem marcador privado")
    return 0


def require_decimal_positive(data: dict[str, str], field: str) -> int:
    value = data.get(field, "")
    if not value or not value.isdecimal() or int(value, 10) <= 0:
        return fail(f"manifesto de evidencia tem {field} invalido")
    return 0


def marker_fields(data: dict[str, str]) -> tuple[int, tuple[str, ...]]:
    try:
        count = int(data["marker_count"], 10)
    except (KeyError, ValueError):
        return fail("manifesto de evidencia tem marker_count invalido"), ()
    if count <= 0:
        return fail("manifesto de evidencia sem markers"), ()
    markers: list[str] = []
    for index in range(1, count + 1):
        key = f"marker.{index}"
        value = data.get(key, "")
        if not value:
            return fail(f"manifesto de evidencia tem marker ausente: {key}"), ()
        markers.append(value)
    return 0, tuple(markers)


def parse_manifest_text(text: str, label: str) -> tuple[int, dict[str, str]]:
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


def compare_evidence(args: argparse.Namespace, evidence_path: Path) -> tuple[int, dict[str, str], bytes, str]:
    rc, expected_text = expected_manifest(args)
    if rc != 0:
        return rc, {}, b"", ""
    rc, evidence_data, evidence_text = read_text(
        evidence_path,
        "manifesto de evidencia do smoke",
        args.max_evidence_bytes,
    )
    if rc != 0:
        return rc, {}, b"", ""
    rc = reject_private_markers(evidence_text, "manifesto de evidencia do smoke")
    if rc != 0:
        return rc, {}, b"", ""
    if evidence_text != expected_text:
        return fail("manifesto de evidencia do smoke diverge do estado esperado"), {}, b"", ""
    rc, expected_data = parse_manifest_text(expected_text, "manifesto esperado de evidencia")
    if rc != 0:
        return rc, {}, b"", ""
    rc, actual_data = parse_manifest_text(evidence_text, "manifesto de evidencia do smoke")
    if rc != 0:
        return rc, {}, b"", ""
    if actual_data != expected_data:
        return fail("manifesto de evidencia do smoke tem campos divergentes"), {}, b"", ""
    if actual_data.get("format") != EVIDENCE_FORMAT:
        return fail("manifesto de evidencia tem formato inesperado"), {}, b"", ""
    for field in ("smoke_serial_log_size", "smoke_summary_log_size"):
        rc = require_decimal_positive(actual_data, field)
        if rc != 0:
            return rc, {}, b"", ""
    rc, _markers = marker_fields(actual_data)
    if rc != 0:
        return rc, {}, b"", ""
    return 0, actual_data, evidence_data, evidence_text


def build_acceptance_manifest(
    current: str,
    extended: str,
    handoff_path: Path,
    handoff_data: bytes,
    evidence_path: Path,
    evidence_data: bytes,
    evidence: dict[str, str],
) -> str:
    rc, markers = marker_fields(evidence)
    if rc != 0:
        raise ValueError("invalid evidence marker fields")
    lines = [
        f"format={ACCEPTANCE_FORMAT}",
        f"release_tag={extended}",
        f"version_current={current}",
        f"version_extended={extended}",
        "track=UEFI/GPT/x86_64",
        f"handoff_manifest_file={manifest_value(handoff_path.name)}",
        f"handoff_manifest_sha256={sha256_bytes(handoff_data)}",
        f"smoke_evidence_manifest_file={manifest_value(evidence_path.name)}",
        f"smoke_evidence_manifest_sha256={sha256_bytes(evidence_data)}",
        f"smoke_evidence_format={EVIDENCE_FORMAT}",
        f"smoke_provider={manifest_value(evidence['smoke_provider'])}",
        f"smoke_serial_log={manifest_value(evidence['smoke_serial_log'])}",
        f"smoke_summary_log={manifest_value(evidence['smoke_summary_log'])}",
        f"marker_count={len(markers)}",
    ]
    for index, marker in enumerate(markers, 1):
        lines.append(f"marker.{index}={manifest_value(marker)}")
    lines.extend([
        f"required_gate_count={len(REQUIRED_GATES)}",
    ])
    for index, gate in enumerate(REQUIRED_GATES, 1):
        lines.append(f"required_gate.{index}={gate}")
    lines.extend([
        "public_release_smoke_accepted=yes",
        "private_key_included=no",
        "vm_powered_on_by_gate=no",
        "make_executed=no",
        "git_executed=no",
        "generated_by=release_ci_smoke_acceptance.py",
    ])
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
    parser = argparse.ArgumentParser(description="Gera/verifica aceitacao publica da evidencia do smoke VMware oficial F2.")
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
    parser.add_argument("--smoke-evidence-manifest", type=Path, default=Path("build/release-smoke-evidence.manifest"))
    parser.add_argument("--smoke-vmware-args", default=value_from_arg_or_env(None, "SMOKE_X64_VMWARE_ARGS"))
    parser.add_argument("--serial-log", type=Path)
    parser.add_argument("--summary-log", type=Path)
    parser.add_argument("--output", type=Path, default=Path("build/release-smoke-acceptance.manifest"))
    parser.add_argument("--max-serial-bytes", type=int, default=33554432)
    parser.add_argument("--max-summary-bytes", type=int, default=1048576)
    parser.add_argument("--max-evidence-bytes", type=int, default=1048576)
    parser.add_argument("--max-handoff-bytes", type=int, default=1048576)
    parser.add_argument("--force", action="store_true")
    parser.add_argument("--verify", action="store_true")
    return parser.parse_args()


def expected_acceptance(args: argparse.Namespace) -> tuple[int, str]:
    repo_root = Path(__file__).resolve().parents[2]
    rc = validate_version_contract(args)
    if rc != 0:
        return rc, ""
    rc, current, extended = parse_version_yaml(args.version_yaml.expanduser())
    if rc != 0:
        return rc, ""
    tag = normalize_release_tag(args.release_tag)
    if tag != extended:
        return fail("release tag diverge da versao estendida na aceitacao"), ""
    handoff_path = args.handoff_manifest.expanduser()
    if not handoff_path.is_absolute():
        handoff_path = (repo_root / handoff_path).resolve()
    evidence_path = args.smoke_evidence_manifest.expanduser()
    if not evidence_path.is_absolute():
        evidence_path = (repo_root / evidence_path).resolve()
    rc, handoff_data, handoff_text = read_text(
        handoff_path,
        "manifesto oficial de handoff",
        args.max_handoff_bytes,
    )
    if rc != 0:
        return rc, ""
    rc = reject_private_markers(handoff_text, "manifesto oficial de handoff")
    if rc != 0:
        return rc, ""
    rc, _handoff = load_key_value(handoff_path, "manifesto oficial de handoff")
    if rc != 0:
        return rc, ""
    rc, evidence, evidence_data, _evidence_text = compare_evidence(args, evidence_path)
    if rc != 0:
        return rc, ""
    return 0, build_acceptance_manifest(
        current,
        extended,
        handoff_path,
        handoff_data,
        evidence_path,
        evidence_data,
        evidence,
    )


def main() -> int:
    args = parse_args()
    rc = reject_private_key_env()
    if rc != 0:
        return rc
    rc, manifest = expected_acceptance(args)
    if rc != 0:
        return rc
    repo_root = Path(__file__).resolve().parents[2]
    output = args.output.expanduser()
    if not output.is_absolute():
        output = (repo_root / output).resolve()
    if args.verify:
        rc, existing_data, existing_text = read_text(
            output,
            "manifesto de aceitacao da evidencia do smoke",
            args.max_evidence_bytes,
        )
        if rc != 0:
            return rc
        rc = reject_private_markers(existing_text, "manifesto de aceitacao da evidencia do smoke")
        if rc != 0:
            return rc
        if existing_text != manifest:
            return fail("manifesto de aceitacao da evidencia diverge do estado esperado")
        if sha256_bytes(existing_data) != sha256_bytes(manifest.encode("utf-8")):
            return fail("manifesto de aceitacao da evidencia tem hash inesperado")
        ok("manifesto de aceitacao da evidencia do smoke conferido")
        return 0
    rc = atomic_write_text(output, manifest, args.force)
    if rc != 0:
        return rc
    ok(f"manifesto de aceitacao da evidencia do smoke escrito em {output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
