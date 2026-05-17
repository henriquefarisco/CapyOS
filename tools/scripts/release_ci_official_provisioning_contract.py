#!/usr/bin/env python3
from __future__ import annotations

import argparse
import base64
import binascii
import hashlib
import os
import re
import shlex
import sys
from pathlib import Path

PRIVATE_KEY_ENV = ("RELEASE_PRIVATE_KEY", "CAPYOS_RELEASE_PRIVATE_KEY")
PRIVATE_KEY_MARKERS = (
    "-----BEGIN PRIVATE KEY-----",
    "-----BEGIN OPENSSH PRIVATE KEY-----",
    "-----BEGIN EC PRIVATE KEY-----",
)
ED25519_SPKI_PREFIX = bytes.fromhex("302a300506032b6570032100")
KEY_MANIFEST_FORMAT = "capyos-release-public-key-manifest-v1"
KEY_MANIFEST_FIELDS = {
    "format",
    "algorithm",
    "public_key_encoding",
    "public_key_file",
    "public_key_sha256",
    "expected_public_key_sha256",
    "private_key_included",
}
TAG_SAFE = re.compile(r"^[A-Za-z0-9._+:-]+$")


def fail(message: str) -> int:
    print(f"[err] {message}", file=sys.stderr)
    return 1


def ok(message: str) -> None:
    print(f"[ok] {message}")


def value_from_arg_or_env(value: str | None, *names: str) -> str | None:
    if value:
        return value
    for name in names:
        env = os.environ.get(name)
        if env:
            return env
    return None


def normalize_sha256_hex(value: str | None) -> str | None:
    if not value:
        return None
    normalized = value.strip().lower().replace(":", "")
    if len(normalized) != 64 or any(c not in "0123456789abcdef" for c in normalized):
        return ""
    return normalized


def require_manifest_sha256(value: str, label: str) -> tuple[int, str]:
    normalized = normalize_sha256_hex(value)
    if normalized is None or normalized == "":
        return fail(f"{label} SHA-256 invalido"), ""
    if value != normalized:
        return fail(f"{label} SHA-256 deve usar hex lowercase sem ':'"), ""
    return 0, normalized


def reject_private_key_env() -> int:
    leaked = [name for name in PRIVATE_KEY_ENV if os.environ.get(name)]
    if leaked:
        return fail("contrato oficial nao aceita chave privada no ambiente: " + ", ".join(leaked))
    return 0


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


def public_key_der_from_pem(path: Path) -> tuple[int, bytes]:
    rc, text = read_text(path, "chave publica oficial")
    if rc != 0:
        return rc, b""
    for marker in PRIVATE_KEY_MARKERS:
        if marker in text:
            return fail("arquivo informado contem marcador de chave privada"), b""
    begin = "-----BEGIN PUBLIC KEY-----"
    end = "-----END PUBLIC KEY-----"
    if begin not in text or end not in text:
        return fail("chave publica oficial deve estar em PEM/SPKI PUBLIC KEY"), b""
    payload = text.split(begin, 1)[1].split(end, 1)[0]
    compact = "".join(line.strip() for line in payload.splitlines() if line.strip())
    try:
        der = base64.b64decode(compact.encode("ascii"), validate=True)
    except (UnicodeEncodeError, binascii.Error):
        return fail("chave publica oficial contem PEM/base64 invalido"), b""
    if not der.startswith(ED25519_SPKI_PREFIX) or len(der) != len(ED25519_SPKI_PREFIX) + 32:
        return fail("chave publica oficial nao e Ed25519 PEM/SPKI valida"), b""
    return 0, der


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


def validate_key_manifest(path: Path, public_key: Path, expected_sha256: str, actual_sha256: str) -> int:
    rc, data = load_key_value(path, "manifesto publico da chave oficial")
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
        rc, _ = require_manifest_sha256(data[field], field)
        if rc != 0:
            return rc
    if data["public_key_sha256"] != actual_sha256:
        return fail("manifesto publico da chave diverge da chave oficial")
    if data["expected_public_key_sha256"] != expected_sha256:
        return fail("manifesto publico da chave diverge do fingerprint pinado")
    if data["public_key_sha256"] != data["expected_public_key_sha256"]:
        return fail("manifesto publico da chave contem fingerprints divergentes")
    ok("chave publica oficial e manifesto conferidos")
    return 0


def parse_version_yaml(path: Path) -> tuple[int, str, str]:
    rc, text = read_text(path, "VERSION.yaml")
    if rc != 0:
        return rc, "", ""
    current = ""
    extended = ""
    for line in text.splitlines():
        stripped = line.strip()
        if stripped.startswith("current:") and not current:
            current = stripped.split(":", 1)[1].strip().strip('"')
        if stripped.startswith("extended:") and not extended:
            extended = stripped.split(":", 1)[1].strip().strip('"')
    if not current or not extended:
        return fail("VERSION.yaml sem current/extended"), "", ""
    if not extended.startswith(current + "+"):
        return fail("VERSION.yaml extended nao deriva de current"), "", ""
    return 0, current, extended


def parse_version_header(path: Path) -> tuple[int, dict[str, str]]:
    rc, text = read_text(path, "include/core/version.h")
    if rc != 0:
        return rc, {}
    values: dict[str, str] = {}
    for line in text.splitlines():
        parts = line.strip().split(maxsplit=2)
        if len(parts) == 3 and parts[0] == "#define" and parts[2].startswith('"') and parts[2].endswith('"'):
            values[parts[1]] = parts[2].strip('"')
    required = ("CAPYOS_VERSION_PRERELEASE", "CAPYOS_VERSION_EXTENDED", "CAPYOS_VERSION_FULL", "CAPYOS_VERSION_ALPHA")
    missing = [name for name in required if name not in values]
    if missing:
        return fail("version.h sem macros: " + ", ".join(missing)), {}
    return 0, values


def normalize_release_tag(tag: str | None) -> str | None:
    if not tag:
        return None
    normalized = tag.strip()
    if normalized.startswith("refs/tags/"):
        normalized = normalized[len("refs/tags/"):]
    if normalized.startswith("v"):
        normalized = normalized[1:]
    return normalized


def validate_version_contract(args: argparse.Namespace) -> int:
    rc, current, extended = parse_version_yaml(args.version_yaml.expanduser())
    if rc != 0:
        return rc
    rc, header = parse_version_header(args.version_header.expanduser())
    if rc != 0:
        return rc
    if header["CAPYOS_VERSION_ALPHA"] != current:
        return fail("CAPYOS_VERSION_ALPHA diverge de VERSION.yaml current")
    if header["CAPYOS_VERSION_EXTENDED"] != current:
        return fail("CAPYOS_VERSION_EXTENDED diverge de VERSION.yaml current")
    if header["CAPYOS_VERSION_FULL"] != extended:
        return fail("CAPYOS_VERSION_FULL diverge de VERSION.yaml extended")
    prerelease = current.split("-", 1)[1] if "-" in current else ""
    if header["CAPYOS_VERSION_PRERELEASE"] != prerelease:
        return fail("CAPYOS_VERSION_PRERELEASE diverge de VERSION.yaml current")
    rc, readme = read_text(args.readme.expanduser(), "README")
    if rc != 0:
        return rc
    if f"Versao de referencia: `{current}`" not in readme:
        return fail("README nao referencia a versao current")
    release_note = args.release_note.expanduser() if args.release_note else Path("docs/releases") / f"capyos-{extended}.md"
    rc = require_file(release_note, "release note")
    if rc != 0:
        return rc
    tag = normalize_release_tag(args.release_tag)
    if tag is None:
        return fail("informe RELEASE_TAG, CI_COMMIT_TAG, GITHUB_REF_NAME, GITHUB_REF ou --release-tag")
    if not TAG_SAFE.fullmatch(tag):
        return fail("release tag contem caracteres inseguros")
    if tag != extended:
        return fail(f"release tag diverge da versao publica: {tag} != {extended}")
    ok("contrato oficial de versao/tag conferido")
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


def option_values(tokens: list[str], name: str) -> list[str]:
    values: list[str] = []
    prefix = f"{name}="
    for index, token in enumerate(tokens):
        if token == name:
            values.append(tokens[index + 1] if index + 1 < len(tokens) else "")
        elif token.startswith(prefix):
            values.append(token[len(prefix):])
    return values


def singleton_option(tokens: list[str], name: str, required: bool) -> tuple[int, str | None]:
    values = option_values(tokens, name)
    if len(values) > 1:
        return fail(f"{name} duplicado no smoke oficial"), None
    if not values:
        if required:
            return fail(f"{name} ausente no smoke oficial"), None
        return 0, None
    if values[0] == "":
        return fail(f"{name} sem valor no smoke oficial"), None
    return 0, values[0]


def ci_log_path_safe(value: str) -> bool:
    if not value or "\\" in value or "\x00" in value:
        return False
    path = Path(value)
    if path.is_absolute():
        return False
    parts = path.parts
    if any(part in ("", ".", "..") for part in parts):
        return False
    return len(parts) >= 3 and parts[0] == "build" and parts[1] == "ci" and value.endswith(".log")


def validate_positive_float(raw: str | None, label: str, minimum: float, maximum: float) -> int:
    if raw is None:
        return 0
    if raw == "":
        return fail(f"{label} sem valor")
    try:
        value = float(raw)
    except ValueError:
        return fail(f"{label} invalido")
    if value < minimum or value > maximum:
        return fail(f"{label} fora do intervalo [{minimum}, {maximum}]")
    return 0


def validate_marker_values(tokens: list[str]) -> int:
    for marker in option_values(tokens, "--marker"):
        if not marker or "\n" in marker or "\r" in marker or "\x00" in marker:
            return fail("--marker invalido para smoke oficial")
    return 0


def validate_smoke_args(raw_args: str | None) -> int:
    if not raw_args:
        return fail("SMOKE_X64_VMWARE_ARGS ausente no provisionamento oficial")
    try:
        tokens = shlex.split(raw_args)
    except ValueError as exc:
        return fail(f"SMOKE_X64_VMWARE_ARGS invalido: {exc}")
    if "..." in tokens:
        return fail("SMOKE_X64_VMWARE_ARGS contem placeholder '...'; informe argumentos reais de VMware")
    forbidden = ("--dry-run", "--no-artifact-check", "--no-poweroff", "--no-tool-check", "--gui")
    for flag in forbidden:
        if flag in tokens:
            return fail(f"SMOKE_X64_VMWARE_ARGS nao deve usar {flag} no smoke oficial")
    rc, provider = singleton_option(tokens, "--provider", True)
    if rc != 0:
        return rc
    if provider not in ("vmrun", "govc"):
        return fail("SMOKE_X64_VMWARE_ARGS deve informar --provider vmrun ou --provider govc")
    rc, serial_log = singleton_option(tokens, "--serial-log", True)
    if rc != 0:
        return rc
    # singleton_option(required=True) returns 0 only when value is set.
    # Use explicit check so `python -O` cannot strip it (py/assert-stmt).
    if serial_log is None:
        raise RuntimeError(
            "internal: singleton_option(--serial-log) returned 0 but value is None"
        )
    if not ci_log_path_safe(serial_log):
        return fail("smoke oficial exige --serial-log relativo em build/ci/*.log")
    rc, summary_log = singleton_option(tokens, "--summary-log", False)
    if rc != 0:
        return rc
    if summary_log is not None and not ci_log_path_safe(summary_log):
        return fail("--summary-log deve ser relativo em build/ci/*.log")
    rc, timeout = singleton_option(tokens, "--timeout", False)
    if rc != 0:
        return rc
    rc = validate_positive_float(timeout, "--timeout", 30.0, 900.0)
    if rc != 0:
        return rc
    rc, poll = singleton_option(tokens, "--poll", False)
    if rc != 0:
        return rc
    rc = validate_positive_float(poll, "--poll", 0.5, 30.0)
    if rc != 0:
        return rc
    rc = validate_marker_values(tokens)
    if rc != 0:
        return rc
    if provider == "vmrun":
        rc, vmx = singleton_option(tokens, "--vmx", True)
        if rc != 0:
            return rc
        if vmx is None:  # required=True invariant (py/assert-stmt)
            raise RuntimeError(
                "internal: singleton_option(--vmx) returned 0 but value is None"
            )
        _ = vmx  # keep referenced; consumed by the smoke runner downstream
        ok("contrato oficial VMware vmrun conferido")
        return 0
    rc, vm_name = singleton_option(tokens, "--vm-name", True)
    if rc != 0:
        return rc
    if vm_name is None:  # required=True invariant (py/assert-stmt)
        raise RuntimeError(
            "internal: singleton_option(--vm-name) returned 0 but value is None"
        )
    _ = vm_name  # consumed downstream
    rc, govc_serial_log = singleton_option(tokens, "--govc-serial-log", True)
    if rc != 0:
        return rc
    if govc_serial_log is None:  # required=True invariant (py/assert-stmt)
        raise RuntimeError(
            "internal: singleton_option(--govc-serial-log) returned 0 but value is None"
        )
    _ = govc_serial_log  # consumed downstream
    missing_env = [name for name in ("GOVC_URL", "GOVC_USERNAME", "GOVC_DATACENTER") if not os.environ.get(name)]
    if not os.environ.get("GOVC_PASSWORD") and not os.environ.get("GOVC_PASSWORD_FILE"):
        missing_env.append("GOVC_PASSWORD ou GOVC_PASSWORD_FILE")
    if missing_env:
        return fail("provider govc exige ambiente oficial: " + ", ".join(missing_env))
    ok("contrato oficial VMware govc conferido")
    return 0


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Valida provisionamento publico oficial de CI/release CapyOS.")
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
    public_key = args.public_key.expanduser() if args.public_key else None
    if public_key is None:
        return fail("informe --public-key, RELEASE_PUBLIC_KEY ou CAPYOS_RELEASE_PUBLIC_KEY")
    expected = normalize_sha256_hex(args.expected_public_key_sha256)
    if expected is None:
        return fail("informe --expected-public-key-sha256, RELEASE_PUBLIC_KEY_SHA256 ou CAPYOS_RELEASE_PUBLIC_KEY_SHA256")
    if expected == "":
        return fail("fingerprint SHA-256 invalido; use hex64 ou pares separados por ':'")
    rc, der = public_key_der_from_pem(public_key)
    if rc != 0:
        return rc
    actual = hashlib.sha256(der).hexdigest()
    if actual != expected:
        return fail(f"fingerprint SHA-256 da chave oficial inesperado: {actual}")
    public_key_manifest = args.public_key_manifest.expanduser()
    rc = validate_key_manifest(public_key_manifest, public_key, expected, actual)
    if rc != 0:
        return rc
    rc = validate_smoke_args(args.smoke_vmware_args)
    if rc != 0:
        return rc
    ok("provisionamento publico oficial de CI/release conferido")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
