#!/usr/bin/env python3
from __future__ import annotations

import argparse
import os
import subprocess
import sys
from pathlib import Path

PRIVATE_KEY_ENV = (
    "RELEASE_PRIVATE_KEY",
    "CAPYOS_RELEASE_PRIVATE_KEY",
)


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


def reject_private_key_env() -> int:
    leaked = [name for name in PRIVATE_KEY_ENV if os.environ.get(name)]
    if leaked:
        return fail("gate publico nao aceita variavel de chave privada no ambiente: " + ", ".join(leaked))
    return 0


def require_public_inputs(public_key: Path | None, expected_sha256: str | None) -> tuple[int, str]:
    if public_key is None:
        return fail("informe --public-key, RELEASE_PUBLIC_KEY ou CAPYOS_RELEASE_PUBLIC_KEY"), ""
    expected = normalize_sha256_hex(expected_sha256)
    if expected is None:
        return fail("informe --expected-public-key-sha256, RELEASE_PUBLIC_KEY_SHA256 ou CAPYOS_RELEASE_PUBLIC_KEY_SHA256"), ""
    if expected == "":
        return fail("fingerprint SHA-256 invalido; use hex64 ou pares separados por ':'"), ""
    return 0, expected


def run_stage(name: str, command: list[str]) -> int:
    print(f"[gate] {name}")
    proc = subprocess.run(command, check=False)
    if proc.returncode != 0:
        return fail(f"gate publico falhou em {name}: rc={proc.returncode}")
    return 0


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Executa gate publico agregado de publicacao da release CapyOS.")
    parser.add_argument("--checksums", type=Path, default=Path("build/release-artifacts.sha256"))
    parser.add_argument("--signature", type=Path)
    parser.add_argument("--artifact-root", type=Path, default=Path("."))
    parser.add_argument("--materials-root", type=Path)
    parser.add_argument("--public-key", type=Path, default=os.environ.get("RELEASE_PUBLIC_KEY") or os.environ.get("CAPYOS_RELEASE_PUBLIC_KEY"))
    parser.add_argument("--expected-public-key-sha256", default=os.environ.get("RELEASE_PUBLIC_KEY_SHA256") or os.environ.get("CAPYOS_RELEASE_PUBLIC_KEY_SHA256"))
    parser.add_argument("--public-key-manifest", type=Path, default=os.environ.get("RELEASE_PUBLIC_KEY_MANIFEST") or os.environ.get("CAPYOS_RELEASE_PUBLIC_KEY_MANIFEST") or "build/release-public-key.manifest")
    parser.add_argument("--publication-manifest", type=Path, default=Path("build/release-publication.manifest"))
    parser.add_argument("--openssl", default=os.environ.get("OPENSSL", "openssl"))
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    rc = reject_private_key_env()
    if rc != 0:
        return rc
    checksums = args.checksums.expanduser()
    signature = args.signature.expanduser() if args.signature else checksums.with_name(f"{checksums.name}.sig")
    artifact_root = args.artifact_root.expanduser()
    public_key = Path(args.public_key).expanduser() if args.public_key else None
    public_key_manifest = args.public_key_manifest.expanduser()
    publication_manifest = args.publication_manifest.expanduser()
    materials_root = args.materials_root.expanduser() if args.materials_root else None
    rc, expected = require_public_inputs(public_key, args.expected_public_key_sha256)
    if rc != 0:
        return rc
    # require_public_inputs returns 0 only when public_key is set. Use
    # explicit check so `python -O` cannot strip the invariant
    # (py/assert-stmt).
    if public_key is None:
        raise RuntimeError(
            "internal: require_public_inputs returned 0 but public_key is None"
        )
    script_dir = Path(__file__).resolve().parent
    python = sys.executable or "python3"
    stages = [
        (
            "assinatura Ed25519",
            [
                python,
                str(script_dir / "verify_release_signature.py"),
                "--input",
                str(checksums),
                "--signature",
                str(signature),
                "--public-key",
                str(public_key),
                "--expected-public-key-sha256",
                expected,
                "--openssl",
                args.openssl,
            ],
        ),
        (
            "materiais publicos",
            [
                python,
                str(script_dir / "release_public_materials_check.py"),
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
                "--openssl",
                args.openssl,
            ],
        ),
    ]
    publication_command = [
        python,
        str(script_dir / "verify_release_publication_manifest.py"),
        "--manifest",
        str(publication_manifest),
        "--artifact-root",
        str(artifact_root),
        "--expected-public-key-sha256",
        expected,
        "--openssl",
        args.openssl,
    ]
    if materials_root is not None:
        publication_command.extend(["--materials-root", str(materials_root)])
    stages.append(("manifesto publico de publicacao", publication_command))
    for name, command in stages:
        rc = run_stage(name, command)
        if rc != 0:
            return rc
    ok("gate publico agregado de publicacao concluido")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
