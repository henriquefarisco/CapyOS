#!/usr/bin/env python3
from __future__ import annotations

import argparse
import hashlib
import os
import shlex
import shutil
import subprocess
import sys
from pathlib import Path

ED25519_SPKI_PREFIX = bytes.fromhex("302a300506032b6570032100")
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


def require_file(path: Path, label: str) -> int:
    if not path.exists() or not path.is_file():
        return fail(f"{label} ausente: {path}")
    if path.stat().st_size == 0:
        return fail(f"{label} vazio: {path}")
    return 0


def validate_public_key(public_key_path: str | None, expected_sha256: str | None, openssl: str) -> int:
    if not public_key_path:
        return fail("RELEASE_PUBLIC_KEY/CAPYOS_RELEASE_PUBLIC_KEY ausente")
    public_key = Path(public_key_path).expanduser()
    rc = require_file(public_key, "chave publica")
    if rc != 0:
        return rc
    normalized = normalize_sha256_hex(expected_sha256)
    if normalized is None:
        return fail("RELEASE_PUBLIC_KEY_SHA256/CAPYOS_RELEASE_PUBLIC_KEY_SHA256 ausente")
    if normalized == "":
        return fail("fingerprint SHA-256 invalido; use hex64 ou pares separados por ':'")
    actual = public_key_sha256_hex(public_key, openssl)
    if actual is None:
        return fail("chave publica nao e Ed25519 raw/SPKI valida")
    if actual != normalized:
        return fail(f"fingerprint SHA-256 da chave publica inesperado: {actual}")
    ok("chave publica Ed25519 e fingerprint SHA-256 conferidos")
    return 0


def load_public_key_manifest(manifest_path: str | None) -> tuple[int, dict[str, str]]:
    if not manifest_path:
        return fail("RELEASE_PUBLIC_KEY_MANIFEST/CAPYOS_RELEASE_PUBLIC_KEY_MANIFEST ausente"), {}
    manifest = Path(manifest_path).expanduser()
    rc = require_file(manifest, "manifesto da chave publica")
    if rc != 0:
        return rc, {}
    try:
        text = manifest.read_text(encoding="utf-8")
    except UnicodeDecodeError:
        return fail(f"manifesto da chave publica nao e UTF-8: {manifest}"), {}
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


def validate_public_key_manifest(manifest_path: str | None, public_key_path: str | None, expected_sha256: str | None, openssl: str) -> int:
    if not public_key_path:
        return fail("RELEASE_PUBLIC_KEY/CAPYOS_RELEASE_PUBLIC_KEY ausente para validar manifesto")
    public_key = Path(public_key_path).expanduser()
    expected = normalize_sha256_hex(expected_sha256)
    if expected is None:
        return fail("RELEASE_PUBLIC_KEY_SHA256/CAPYOS_RELEASE_PUBLIC_KEY_SHA256 ausente para validar manifesto")
    if expected == "":
        return fail("fingerprint SHA-256 invalido para validar manifesto")
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
    actual = public_key_sha256_hex(public_key, openssl)
    if actual is None:
        return fail("chave publica do manifesto nao e Ed25519 raw/SPKI valida")
    if manifest_actual != actual:
        return fail("manifesto da chave publica diverge da chave publica informada")
    if manifest_expected != expected:
        return fail("manifesto da chave publica diverge do fingerprint esperado")
    if manifest_actual != manifest_expected:
        return fail("manifesto da chave publica contem fingerprints divergentes")
    ok("manifesto publico da chave de release conferido")
    return 0


def option_value(tokens: list[str], name: str) -> str | None:
    prefix = f"{name}="
    for i, token in enumerate(tokens):
        if token == name:
            if i + 1 >= len(tokens):
                return ""
            return tokens[i + 1]
        if token.startswith(prefix):
            return token[len(prefix):]
    return None


def has_flag(tokens: list[str], name: str) -> bool:
    return name in tokens


def validate_vmware_args(raw_args: str | None, require_tools: bool, require_files: bool) -> int:
    if not raw_args:
        return fail("SMOKE_X64_VMWARE_ARGS ausente")
    try:
        tokens = shlex.split(raw_args)
    except ValueError as exc:
        return fail(f"SMOKE_X64_VMWARE_ARGS invalido: {exc}")
    for forbidden in ("--dry-run", "--no-artifact-check", "--no-poweroff"):
        if has_flag(tokens, forbidden):
            return fail(f"SMOKE_X64_VMWARE_ARGS nao deve usar {forbidden} em CI de release")
    provider = option_value(tokens, "--provider")
    if provider not in ("vmrun", "govc"):
        return fail("SMOKE_X64_VMWARE_ARGS deve informar --provider vmrun ou --provider govc")
    if provider == "vmrun":
        vmx = option_value(tokens, "--vmx")
        if not vmx:
            return fail("provider vmrun exige --vmx")
        if require_tools and shutil.which("vmrun") is None:
            return fail("vmrun nao encontrado no PATH")
        if require_files and not Path(vmx).expanduser().exists():
            return fail(f"VMX ausente: {vmx}")
        ok("argumentos VMware vmrun conferidos")
        return 0
    vm_name = option_value(tokens, "--vm-name")
    serial_log = option_value(tokens, "--govc-serial-log")
    if not vm_name:
        return fail("provider govc exige --vm-name")
    if not serial_log:
        return fail("provider govc exige --govc-serial-log")
    missing_env = [name for name in ("GOVC_URL", "GOVC_USERNAME", "GOVC_DATACENTER") if not os.environ.get(name)]
    if not os.environ.get("GOVC_PASSWORD") and not os.environ.get("GOVC_PASSWORD_FILE"):
        missing_env.append("GOVC_PASSWORD ou GOVC_PASSWORD_FILE")
    if missing_env:
        return fail("provider govc exige ambiente: " + ", ".join(missing_env))
    if require_tools and shutil.which("govc") is None:
        return fail("govc nao encontrado no PATH")
    ok("argumentos VMware govc conferidos")
    return 0


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Preflight de CI/release CapyOS F2")
    parser.add_argument("--release-public-key")
    parser.add_argument("--release-public-key-sha256")
    parser.add_argument("--release-public-key-manifest")
    parser.add_argument("--smoke-vmware-args")
    parser.add_argument("--openssl", default=os.environ.get("OPENSSL", "openssl"))
    parser.add_argument("--no-tool-check", action="store_true")
    parser.add_argument("--require-vmware-files", action="store_true")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    public_key = value_from_arg_or_env(args.release_public_key, "RELEASE_PUBLIC_KEY", "CAPYOS_RELEASE_PUBLIC_KEY")
    fingerprint = value_from_arg_or_env(args.release_public_key_sha256, "RELEASE_PUBLIC_KEY_SHA256", "CAPYOS_RELEASE_PUBLIC_KEY_SHA256")
    key_manifest = value_from_arg_or_env(args.release_public_key_manifest, "RELEASE_PUBLIC_KEY_MANIFEST", "CAPYOS_RELEASE_PUBLIC_KEY_MANIFEST")
    vmware_args = value_from_arg_or_env(args.smoke_vmware_args, "SMOKE_X64_VMWARE_ARGS")
    rc = validate_public_key(public_key, fingerprint, args.openssl)
    if rc != 0:
        return rc
    rc = validate_public_key_manifest(key_manifest, public_key, fingerprint, args.openssl)
    if rc != 0:
        return rc
    rc = validate_vmware_args(vmware_args, not args.no_tool_check, args.require_vmware_files)
    if rc != 0:
        return rc
    ok("preflight F2 de CI/release concluido")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
