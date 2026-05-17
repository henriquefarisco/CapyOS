#!/usr/bin/env python3
from __future__ import annotations

import argparse
import os
import re
import subprocess
import sys
from pathlib import Path

PRIVATE_KEY_ENV = ("RELEASE_PRIVATE_KEY", "CAPYOS_RELEASE_PRIVATE_KEY")
TAG_SAFE = re.compile(r"^[A-Za-z0-9._+:-]+$")


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


def value_from_arg_or_env(value: str | None, *names: str) -> str | None:
    if value:
        return value
    for name in names:
        env = os.environ.get(name)
        if env:
            return env
    return None


def reject_private_key_env() -> int:
    leaked = [name for name in PRIVATE_KEY_ENV if os.environ.get(name)]
    if leaked:
        return fail("gate CI/tag publico nao aceita variavel de chave privada no ambiente: " + ", ".join(leaked))
    return 0


def parse_version_yaml(path: Path) -> tuple[int, str, str]:
    if not path.exists() or not path.is_file():
        return fail(f"VERSION.yaml ausente: {path}"), "", ""
    try:
        text = path.read_text(encoding="utf-8")
    except UnicodeDecodeError:
        return fail(f"VERSION.yaml nao e UTF-8: {path}"), "", ""
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
    if not path.exists() or not path.is_file():
        return fail(f"version.h ausente: {path}"), {}
    try:
        text = path.read_text(encoding="utf-8")
    except UnicodeDecodeError:
        return fail(f"version.h nao e UTF-8: {path}"), {}
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
    readme = args.readme.expanduser()
    if not readme.exists() or not readme.is_file():
        return fail(f"README ausente: {readme}")
    try:
        readme_text = readme.read_text(encoding="utf-8")
    except UnicodeDecodeError:
        return fail(f"README nao e UTF-8: {readme}")
    if f"Versao de referencia: `{current}`" not in readme_text:
        return fail("README nao referencia a versao current")
    release_note = args.release_note.expanduser() if args.release_note else Path("docs/releases") / f"capyos-{extended}.md"
    if not release_note.exists() or not release_note.is_file():
        return fail(f"release note ausente: {release_note}")
    tag = normalize_release_tag(args.release_tag)
    if tag is None:
        return fail("informe RELEASE_TAG, CI_COMMIT_TAG, GITHUB_REF_NAME, GITHUB_REF ou --release-tag")
    if not TAG_SAFE.fullmatch(tag):
        return fail("release tag contem caracteres inseguros")
    if tag != extended:
        return fail(f"release tag diverge da versao publica: {tag} != {extended}")
    ok("contrato de versao/tag conferido")
    return 0


def require_public_inputs(public_key: Path | None, expected_sha256: str | None, smoke_args: str | None) -> tuple[int, str]:
    if public_key is None:
        return fail("informe --public-key, RELEASE_PUBLIC_KEY ou CAPYOS_RELEASE_PUBLIC_KEY"), ""
    expected = normalize_sha256_hex(expected_sha256)
    if expected is None:
        return fail("informe --expected-public-key-sha256, RELEASE_PUBLIC_KEY_SHA256 ou CAPYOS_RELEASE_PUBLIC_KEY_SHA256"), ""
    if expected == "":
        return fail("fingerprint SHA-256 invalido; use hex64 ou pares separados por ':'"), ""
    if not smoke_args:
        return fail("informe --smoke-vmware-args ou SMOKE_X64_VMWARE_ARGS"), ""
    return 0, expected


def run_stage(name: str, command: list[str]) -> int:
    print(f"[tag-gate] {name}")
    proc = subprocess.run(command, check=False)
    if proc.returncode != 0:
        return fail(f"gate CI/tag falhou em {name}: rc={proc.returncode}")
    return 0


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Executa gate publico de CI/tag da release CapyOS.")
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
    parser.add_argument("--artifact-root", type=Path, default=Path("."))
    parser.add_argument("--materials-root", type=Path)
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
    parser.add_argument("--openssl", default=os.environ.get("OPENSSL", "openssl"))
    parser.add_argument("--require-vmware-files", action="store_true")
    return parser.parse_args()


def build_preflight_command(
    python: str,
    script_dir: Path,
    public_key: Path,
    expected: str,
    public_key_manifest: Path,
    smoke_vmware_args: str,
    openssl: str,
    require_vmware_files: bool,
) -> list[str]:
    command = [
        python,
        str(script_dir / "release_ci_preflight.py"),
        "--release-public-key",
        str(public_key),
        "--release-public-key-sha256",
        expected,
        "--release-public-key-manifest",
        str(public_key_manifest),
        "--smoke-vmware-args",
        smoke_vmware_args,
        "--openssl",
        openssl,
    ]
    if require_vmware_files:
        command.append("--require-vmware-files")
    return command


def build_contract_command(
    python: str,
    script_dir: Path,
    checksums: Path,
    signature: Path,
    public_key: Path,
    expected: str,
    public_key_manifest: Path,
    publication_manifest: Path,
    smoke_vmware_args: str,
) -> list[str]:
    return [
        python,
        str(script_dir / "release_ci_publication_contract.py"),
        "--checksums",
        str(checksums),
        "--signature",
        str(signature),
        "--public-key",
        str(public_key),
        "--expected-public-key-sha256",
        expected,
        "--public-key-manifest",
        str(public_key_manifest),
        "--publication-manifest",
        str(publication_manifest),
        "--smoke-vmware-args",
        smoke_vmware_args,
    ]


def build_publication_command(
    python: str,
    script_dir: Path,
    checksums: Path,
    signature: Path,
    artifact_root: Path,
    public_key: Path,
    expected: str,
    public_key_manifest: Path,
    publication_manifest: Path,
    materials_root: Path | None,
    openssl: str,
) -> list[str]:
    command = [
        python,
        str(script_dir / "release_publication_gate.py"),
        "--checksums",
        str(checksums),
        "--signature",
        str(signature),
        "--artifact-root",
        str(artifact_root),
        "--public-key",
        str(public_key),
        "--expected-public-key-sha256",
        expected,
        "--public-key-manifest",
        str(public_key_manifest),
        "--publication-manifest",
        str(publication_manifest),
        "--openssl",
        openssl,
    ]
    if materials_root is not None:
        command.extend(["--materials-root", str(materials_root)])
    return command


def main() -> int:
    args = parse_args()
    rc = reject_private_key_env()
    if rc != 0:
        return rc
    rc = validate_version_contract(args)
    if rc != 0:
        return rc
    checksums = args.checksums.expanduser()
    signature = args.signature.expanduser() if args.signature else checksums.with_name(f"{checksums.name}.sig")
    public_key = Path(args.public_key).expanduser() if args.public_key else None
    public_key_manifest = args.public_key_manifest.expanduser()
    publication_manifest = args.publication_manifest.expanduser()
    rc, expected = require_public_inputs(public_key, args.expected_public_key_sha256, args.smoke_vmware_args)
    if rc != 0:
        return rc
    # require_public_inputs returns 0 only when both public_key and
    # smoke_vmware_args are populated. Use explicit checks so
    # `python -O` cannot strip the invariant (py/assert-stmt).
    if public_key is None or args.smoke_vmware_args is None:
        raise RuntimeError(
            "internal: require_public_inputs returned 0 but inputs are None"
        )
    script_dir = Path(__file__).resolve().parent
    python = sys.executable or "python3"
    artifact_root = args.artifact_root.expanduser()
    materials_root = args.materials_root.expanduser() if args.materials_root else None
    preflight = build_preflight_command(
        python,
        script_dir,
        public_key,
        expected,
        public_key_manifest,
        args.smoke_vmware_args,
        args.openssl,
        args.require_vmware_files,
    )
    contract = build_contract_command(
        python,
        script_dir,
        checksums,
        signature,
        public_key,
        expected,
        public_key_manifest,
        publication_manifest,
        args.smoke_vmware_args,
    )
    publication = build_publication_command(
        python,
        script_dir,
        checksums,
        signature,
        artifact_root,
        public_key,
        expected,
        public_key_manifest,
        publication_manifest,
        materials_root,
        args.openssl,
    )
    for name, command in (
        ("preflight publico de CI", preflight),
        ("contrato publico de CI/publicacao", contract),
        ("gate publico agregado de publicacao", publication),
    ):
        rc = run_stage(name, command)
        if rc != 0:
            return rc
    ok("gate publico de CI/tag concluido")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
