#!/usr/bin/env python3
from __future__ import annotations

import argparse
import hashlib
import os
import shlex
import tempfile
from pathlib import Path

from release_ci_official_provisioning_contract import (
    fail,
    normalize_release_tag,
    ok,
    parse_version_yaml,
    reject_private_key_env,
    validate_smoke_args,
    validate_version_contract,
    value_from_arg_or_env,
)
from release_ci_smoke_readiness import (
    ci_log_path_safe,
    load_key_value,
    smoke_summary,
    validate_handoff,
)

EVIDENCE_FORMAT = "capyos-release-smoke-evidence-manifest-v1"
DEFAULT_MARKERS = (
    "[net] DHCP: lease acquired.",
    "[smoke] gui-session ready",
    "[smoke] mouse-events ready",
)
FAILURE_MARKERS = (
    "kernel panic",
    "panic:",
    "triple fault",
    "general protection fault",
)
PRIVATE_KEY_MARKERS = (
    "-----BEGIN PRIVATE KEY-----",
    "-----BEGIN OPENSSH PRIVATE KEY-----",
    "-----BEGIN EC PRIVATE KEY-----",
)


def option_values(tokens: list[str], name: str) -> list[str]:
    values: list[str] = []
    prefix = f"{name}="
    for index, token in enumerate(tokens):
        if token == name:
            values.append(tokens[index + 1] if index + 1 < len(tokens) else "")
        elif token.startswith(prefix):
            values.append(token[len(prefix):])
    return values


def marker_values(raw_args: str) -> tuple[int, tuple[str, ...]]:
    try:
        tokens = shlex.split(raw_args)
    except ValueError as exc:
        return fail(f"SMOKE_X64_VMWARE_ARGS invalido: {exc}"), ()
    extra_markers = tuple(option_values(tokens, "--marker"))
    markers = DEFAULT_MARKERS + tuple(
        marker for marker in extra_markers if marker not in DEFAULT_MARKERS
    )
    if any(not marker for marker in markers):
        return fail("SMOKE_X64_VMWARE_ARGS contem --marker vazio"), ()
    return 0, markers


def resolve_ci_log(repo_root: Path, value: str, label: str) -> tuple[int, Path]:
    if not ci_log_path_safe(value):
        return fail(f"{label} deve ser relativo em build/ci/*.log"), Path()
    return 0, (repo_root / value).resolve()


def read_evidence(path: Path, label: str, max_bytes: int) -> tuple[int, bytes, str]:
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
    return 0, data, data.decode("latin-1", errors="replace")


def manifest_value(value: str) -> str:
    return value.replace("\\", "/").replace("\r", "_").replace("\n", "_")


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def validate_no_private_material(texts: tuple[tuple[str, str], ...]) -> int:
    for label, text in texts:
        for marker in PRIVATE_KEY_MARKERS:
            if marker in text:
                return fail(f"evidencia contem marcador privado em {label}")
    return 0


def validate_markers(text: str, markers: tuple[str, ...], label: str) -> int:
    low = text.lower()
    missing = [marker for marker in markers if marker.lower() not in low]
    if missing:
        return fail(f"{label} sem markers obrigatorios: " + ", ".join(missing))
    for marker in FAILURE_MARKERS:
        if marker in low:
            return fail(f"{label} contem marker de falha: {marker}")
    return 0


def path_arg(value: Path | None, expected: str, label: str) -> tuple[int, str]:
    if value is None:
        return 0, expected
    actual = value.as_posix()
    if actual != expected:
        return fail(f"{label} diverge de SMOKE_X64_VMWARE_ARGS: {actual} != {expected}"), ""
    return 0, actual


def build_manifest(
    current: str,
    extended: str,
    handoff_manifest: Path,
    handoff_data: bytes,
    serial_log_value: str,
    serial_data: bytes,
    summary_log_value: str,
    summary_data: bytes,
    smoke: dict[str, str],
    markers: tuple[str, ...],
) -> str:
    lines = [
        f"format={EVIDENCE_FORMAT}",
        f"release_tag={extended}",
        f"version_current={current}",
        f"version_extended={extended}",
        "track=UEFI/GPT/x86_64",
        f"handoff_manifest_file={manifest_value(handoff_manifest.name)}",
        f"handoff_manifest_sha256={sha256_bytes(handoff_data)}",
        f"smoke_provider={manifest_value(smoke['smoke_provider'])}",
        f"smoke_serial_log={manifest_value(serial_log_value)}",
        f"smoke_serial_log_sha256={sha256_bytes(serial_data)}",
        f"smoke_serial_log_size={len(serial_data)}",
        f"smoke_summary_log={manifest_value(summary_log_value)}",
        f"smoke_summary_log_sha256={sha256_bytes(summary_data)}",
        f"smoke_summary_log_size={len(summary_data)}",
        f"marker_count={len(markers)}",
    ]
    for index, marker in enumerate(markers, 1):
        lines.append(f"marker.{index}={manifest_value(marker)}")
    lines.extend([
        "private_key_included=no",
        "vm_powered_on_by_verifier=no",
        "make_executed=no",
        "git_executed=no",
        "evidence_checked=yes",
        "generated_by=release_ci_smoke_evidence.py",
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
    parser = argparse.ArgumentParser(description="Gera/verifica evidencia publica do smoke VMware oficial F2.")
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
    parser.add_argument("--serial-log", type=Path)
    parser.add_argument("--summary-log", type=Path)
    parser.add_argument("--output", type=Path, default=Path("build/release-smoke-evidence.manifest"))
    parser.add_argument("--max-serial-bytes", type=int, default=33554432)
    parser.add_argument("--max-summary-bytes", type=int, default=1048576)
    parser.add_argument("--force", action="store_true")
    parser.add_argument("--verify", action="store_true")
    return parser.parse_args()


def expected_manifest(args: argparse.Namespace) -> tuple[int, str]:
    repo_root = Path(__file__).resolve().parents[2]
    rc = validate_version_contract(args)
    if rc != 0:
        return rc, ""
    rc, current, extended = parse_version_yaml(args.version_yaml.expanduser())
    if rc != 0:
        return rc, ""
    tag = normalize_release_tag(args.release_tag)
    if tag != extended:
        return fail("release tag diverge da versao estendida na evidencia"), ""
    if not args.smoke_vmware_args:
        return fail("informe --smoke-vmware-args ou SMOKE_X64_VMWARE_ARGS"), ""
    rc = validate_smoke_args(args.smoke_vmware_args)
    if rc != 0:
        return rc, ""
    rc, markers = marker_values(args.smoke_vmware_args)
    if rc != 0:
        return rc, ""
    rc, smoke = smoke_summary(args.smoke_vmware_args)
    if rc != 0:
        return rc, ""
    handoff_path = args.handoff_manifest.expanduser()
    if not handoff_path.is_absolute():
        handoff_path = (repo_root / handoff_path).resolve()
    rc, handoff = load_key_value(handoff_path, "manifesto oficial de handoff")
    if rc != 0:
        return rc, ""
    rc = validate_handoff(handoff, current, extended, smoke)
    if rc != 0:
        return rc, ""
    rc, handoff_data, handoff_text = read_evidence(handoff_path, "manifesto oficial de handoff", 1048576)
    if rc != 0:
        return rc, ""
    rc, serial_log_value = path_arg(args.serial_log, smoke["smoke_serial_log"], "--serial-log")
    if rc != 0:
        return rc, ""
    rc, summary_log_value = path_arg(args.summary_log, smoke["smoke_summary_log"], "--summary-log")
    if rc != 0:
        return rc, ""
    rc, serial_log = resolve_ci_log(repo_root, serial_log_value, "serial log")
    if rc != 0:
        return rc, ""
    rc, summary_log = resolve_ci_log(repo_root, summary_log_value, "summary log")
    if rc != 0:
        return rc, ""
    rc, serial_data, serial_text = read_evidence(serial_log, "serial log do smoke", args.max_serial_bytes)
    if rc != 0:
        return rc, ""
    rc, summary_data, summary_text = read_evidence(summary_log, "summary log do smoke", args.max_summary_bytes)
    if rc != 0:
        return rc, ""
    rc = validate_no_private_material((
        ("handoff", handoff_text),
        ("serial log", serial_text),
        ("summary log", summary_text),
    ))
    if rc != 0:
        return rc, ""
    rc = validate_markers(serial_text, markers, "serial log")
    if rc != 0:
        return rc, ""
    rc = validate_markers(summary_text, markers, "summary log")
    if rc != 0:
        return rc, ""
    return 0, build_manifest(
        current,
        extended,
        handoff_path,
        handoff_data,
        serial_log_value,
        serial_data,
        summary_log_value,
        summary_data,
        smoke,
        markers,
    )


def main() -> int:
    args = parse_args()
    rc = reject_private_key_env()
    if rc != 0:
        return rc
    rc, manifest = expected_manifest(args)
    if rc != 0:
        return rc
    repo_root = Path(__file__).resolve().parents[2]
    output = args.output.expanduser()
    if not output.is_absolute():
        output = (repo_root / output).resolve()
    if args.verify:
        rc, existing = load_key_value(output, "manifesto de evidencia do smoke")
        if rc != 0:
            return rc
        expected = dict(line.split("=", 1) for line in manifest.splitlines())
        if existing != expected:
            return fail("manifesto de evidencia do smoke diverge das evidencias atuais")
        ok("manifesto de evidencia do smoke conferido")
        return 0
    rc = atomic_write_text(output, manifest, args.force)
    if rc != 0:
        return rc
    ok(f"manifesto de evidencia do smoke escrito em {output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
