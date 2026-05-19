#!/usr/bin/env python3
"""build_modules_index.py — aggregate per-repo manifests into a single
capypkg index that the CapyOS in-tree adapter can consume.

Scans sibling repositories (default: the six external repos listed in
`docs/reference/integration/compatibility-matrix.md`) for a
`build/capypkg/*.manifest` file produced by each repo's
`make package` target, then concatenates them with `---\\n` separators
into a single index file.

The output is line-oriented `key=value` matching the contract in:
  docs/reference/integration/capypkg-publisher-manifest-format.md

This script is host-only and does NOT touch the kernel or run any
network requests. It is meant to run on a build machine that has
checked out CapyOS + the sibling repos side by side, e.g.:

    /workspace/CapyOS/
    /workspace/CapyAgent/
    /workspace/CapyBrowser/
    /workspace/CapyCodecs/
    /workspace/CapyUI/
    /workspace/CapyLang/
    /workspace/CapyBenchmark/

Typical use:

    cd /workspace/CapyOS
    python3 tools/scripts/build_modules_index.py \\
        --output build/capypkg/modules-index.txt

After running, publish both `build/capypkg/modules-index.txt` AND
each per-repo `build/capypkg/<name>-<version>.bin` to HTTPS endpoints
matching the `payload_url` declared in the corresponding manifest.

The script enforces light invariants the in-tree adapter would
otherwise reject (alphabet of `name`, HTTPS `payload_url`, hex
length of `payload_sha256`, presence of required fields). On error
it exits non-zero and prints what failed so the publisher pipeline
can fail fast.
"""

from __future__ import annotations

import argparse
import os
import re
import sys
from pathlib import Path
from typing import Dict, List, Optional, Tuple

DEFAULT_REPOS: Tuple[str, ...] = (
    "CapyAgent",
    "CapyBrowser",
    "CapyCodecs",
    "CapyUI",
    "CapyLang",
    "CapyBenchmark",
)

REQUIRED_FIELDS: Tuple[str, ...] = (
    "name",
    "version",
    "payload_url",
    "payload_sha256",
)

NAME_RE = re.compile(r"^[a-zA-Z0-9._-]{1,63}$")
SHA256_RE = re.compile(r"^[0-9a-f]{64}$")
SIG_RE = re.compile(r"^[0-9a-f]{128}$")
HTTPS_RE = re.compile(r"^https://")
PRINTABLE_RE = re.compile(r"^[\x20-\x7e]*$")


class ManifestError(RuntimeError):
    pass


def find_manifest_files(repo_root: Path, repo_name: str) -> List[Path]:
    """Return every `*.manifest` file under `<repo>/build/capypkg/`."""
    capypkg = repo_root / repo_name / "build" / "capypkg"
    if not capypkg.is_dir():
        # CapyLang uses target/capypkg/ because cargo owns build/.
        capypkg_alt = repo_root / repo_name / "target" / "capypkg"
        if capypkg_alt.is_dir():
            capypkg = capypkg_alt
        else:
            return []
    return sorted(capypkg.glob("*.manifest"))


def parse_manifest(path: Path) -> Dict[str, str]:
    fields: Dict[str, str] = {}
    text = path.read_text(encoding="utf-8")
    for line_no, raw_line in enumerate(text.splitlines(), start=1):
        line = raw_line.rstrip("\r")
        if not line or line.startswith("#") or line == "---":
            continue
        if "=" not in line:
            raise ManifestError(
                f"{path}:{line_no} expected key=value, got: {line!r}")
        key, value = line.split("=", 1)
        key = key.strip()
        value = value.strip()
        if not PRINTABLE_RE.match(value):
            raise ManifestError(
                f"{path}:{line_no} value carries non-printable bytes")
        fields[key] = value
    return fields


def validate_manifest(path: Path, fields: Dict[str, str]) -> None:
    for required in REQUIRED_FIELDS:
        if required not in fields or not fields[required]:
            raise ManifestError(
                f"{path}: missing required field {required!r}")
    name = fields["name"]
    if not NAME_RE.match(name):
        raise ManifestError(
            f"{path}: name {name!r} violates [a-zA-Z0-9._-] alphabet")
    if set(name) <= {"."}:
        raise ManifestError(f"{path}: dot-only name {name!r} rejected")
    if not HTTPS_RE.match(fields["payload_url"]):
        raise ManifestError(
            f"{path}: payload_url must start with https://")
    if not SHA256_RE.match(fields["payload_sha256"]):
        raise ManifestError(
            f"{path}: payload_sha256 must be 64 lowercase hex chars")
    sig = fields.get("signature_ed25519", "")
    if sig and not SIG_RE.match(sig):
        raise ManifestError(
            f"{path}: signature_ed25519 must be 128 lowercase hex chars")
    install_root = fields.get("install_root", "")
    if install_root:
        if not (install_root == "/var/capypkg" or
                install_root.startswith("/var/capypkg/") or
                install_root.startswith("/opt/")):
            raise ManifestError(
                f"{path}: install_root must be under /var/capypkg or /opt/")
        if "/.." in install_root or install_root.endswith("/.."):
            raise ManifestError(
                f"{path}: install_root contains '..' segment")
    payload_size = fields.get("payload_size", "")
    if payload_size:
        if not payload_size.isdigit():
            raise ManifestError(
                f"{path}: payload_size must be decimal digits")
        if int(payload_size) > 8 * 1024 * 1024:
            raise ManifestError(
                f"{path}: payload_size exceeds CAPYPKG_PAYLOAD_MAX (8 MiB)")
    depends = fields.get("depends", "")
    if depends:
        for token in depends.split(","):
            token = token.strip()
            if not token:
                continue
            if not NAME_RE.match(token):
                raise ManifestError(
                    f"{path}: dependency {token!r} violates alphabet")


def emit_entry(fields: Dict[str, str], out_lines: List[str]) -> None:
    """Emit a manifest entry in the deterministic field order the
    in-tree adapter recognises. Unknown keys are still forwarded so
    forward-compat keys survive aggregation."""
    canonical = ("name", "version", "summary", "payload_url",
                 "payload_sha256", "payload_size",
                 "signature_ed25519", "install_root", "depends")
    for key in canonical:
        if key in fields and fields[key] != "":
            out_lines.append(f"{key}={fields[key]}")
    for key, value in fields.items():
        if key in canonical:
            continue
        if value == "":
            continue
        out_lines.append(f"{key}={value}")
    out_lines.append("---")


def build_index(repo_root: Path, repos: List[str],
                output: Path) -> int:
    out_lines: List[str] = [
        "# CapyOS modules index, generated by build_modules_index.py",
        f"# CapyOS reference version pinned in",
        f"#   docs/reference/integration/compatibility-matrix.md",
    ]
    total = 0
    for repo in repos:
        manifests = find_manifest_files(repo_root, repo)
        if not manifests:
            print(f"[warn] {repo}: no build/capypkg/*.manifest found "
                  "(did you run `make package` in that repo?)",
                  file=sys.stderr)
            continue
        for manifest_path in manifests:
            fields = parse_manifest(manifest_path)
            validate_manifest(manifest_path, fields)
            emit_entry(fields, out_lines)
            total += 1
            print(f"[ok] {repo}: {fields['name']}@{fields['version']} "
                  f"({fields['payload_sha256'][:12]}...)")
    if total == 0:
        print("[error] no manifests aggregated; refusing to write empty index",
              file=sys.stderr)
        return 1
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text("\n".join(out_lines) + "\n", encoding="utf-8")
    print(f"[ok] wrote {total} entries to {output}")
    return 0


def main(argv: Optional[List[str]] = None) -> int:
    parser = argparse.ArgumentParser(
        description="Aggregate per-repo capypkg manifests into a single "
                    "index file the CapyOS adapter can consume.")
    parser.add_argument(
        "--workspace", default=None,
        help="Path to the parent directory containing CapyOS and the "
             "sibling external repositories. Defaults to the parent of "
             "the CapyOS repo this script lives in.")
    parser.add_argument(
        "--output", default="build/capypkg/modules-index.txt",
        help="Output path for the aggregated index (default: "
             "build/capypkg/modules-index.txt).")
    parser.add_argument(
        "--repos", nargs="+", default=list(DEFAULT_REPOS),
        help="Sibling repos to aggregate. Defaults to the six external "
             "repos listed in compatibility-matrix.md.")
    args = parser.parse_args(argv)

    script_dir = Path(__file__).resolve().parent
    # tools/scripts/ → tools/ → CapyOS/
    capyos_root = script_dir.parent.parent
    workspace = Path(args.workspace) if args.workspace \
        else capyos_root.parent

    output = Path(args.output)
    if not output.is_absolute():
        output = capyos_root / output

    try:
        return build_index(workspace, args.repos, output)
    except ManifestError as exc:
        print(f"[error] {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
