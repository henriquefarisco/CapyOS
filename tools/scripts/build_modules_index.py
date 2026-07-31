#!/usr/bin/env python3
"""Build the immutable CapyOS index from the official module catalog.

The publisher manifests and their local payloads are treated as untrusted
inputs.  Nothing is written until all nine catalog entries pass validation.
"""

from __future__ import annotations

import argparse
import hashlib
import re
import sys
from pathlib import Path
from typing import Dict, List, Optional, Tuple
from urllib.parse import urlsplit

try:  # Package import used by unittest.
    from .modules_index_catalog import (
        ALLOWED_FIELDS,
        CANONICAL_FIELDS,
        DEFAULT_REPOS,
        MODULE_BY_ID,
        MODULE_SPECS,
        RUNTIME_INDEX_BYTES,
        RUNTIME_MAX_DEPS,
        RUNTIME_MAX_ENTRIES,
        RUNTIME_NAME_BYTES,
        RUNTIME_PATH_BYTES,
        RUNTIME_PAYLOAD_BYTES,
        RUNTIME_REPO_BYTES,
        RUNTIME_SUMMARY_BYTES,
        RUNTIME_URL_BYTES,
        RUNTIME_VERSION_BYTES,
        ModuleSpec,
        expected_payload_url,
        pinned_payload_metadata,
        validate_release_tag,
    )
except ImportError:  # Direct script/import from tools/scripts.
    from modules_index_catalog import (  # type: ignore
        ALLOWED_FIELDS,
        CANONICAL_FIELDS,
        DEFAULT_REPOS,
        MODULE_BY_ID,
        MODULE_SPECS,
        RUNTIME_INDEX_BYTES,
        RUNTIME_MAX_DEPS,
        RUNTIME_MAX_ENTRIES,
        RUNTIME_NAME_BYTES,
        RUNTIME_PATH_BYTES,
        RUNTIME_PAYLOAD_BYTES,
        RUNTIME_REPO_BYTES,
        RUNTIME_SUMMARY_BYTES,
        RUNTIME_URL_BYTES,
        RUNTIME_VERSION_BYTES,
        ModuleSpec,
        expected_payload_url,
        pinned_payload_metadata,
        validate_release_tag,
    )

REQUIRED_FIELDS: Tuple[str, ...] = (
    "name",
    "version",
    "summary",
    "payload_url",
    "payload_sha256",
    "payload_size",
    "install_root",
    "depends",
)

NAME_RE = re.compile(r"^[A-Za-z0-9._-]{1,63}$")
SHA256_RE = re.compile(r"^[0-9a-f]{64}$")
SIG_RE = re.compile(r"^[0-9a-f]{128}$")
POSITIVE_DECIMAL_RE = re.compile(r"^[1-9][0-9]*$")
PRINTABLE_RE = re.compile(r"^[\x20-\x7e]*$")
CAPYOS_VERSION_RE = re.compile(
    r'^\s*#define\s+CAPYOS_VERSION_FULL\s+"([^"]+)"\s*$'
)


class ManifestError(RuntimeError):
    pass


def find_manifest_files(repo_root: Path, repo_name: str) -> List[Path]:
    """Return manifests from build/capypkg and then target/capypkg.

    Looking in both locations fixes the CapyLang fallback when an empty
    build/capypkg directory happens to exist while Cargo emitted the package
    under target/capypkg.
    """
    found: list[Path] = []
    for relative in (Path("build/capypkg"), Path("target/capypkg")):
        directory = repo_root / repo_name / relative
        if directory.is_dir():
            found.extend(sorted(directory.glob("*.manifest")))
    return found


def parse_manifest(path: Path) -> Dict[str, str]:
    fields: Dict[str, str] = {}
    separator_seen = False
    text = path.read_text(encoding="utf-8")
    for line_no, raw_line in enumerate(text.split("\n"), start=1):
        line = raw_line[:-1] if raw_line.endswith("\r") else raw_line
        if not line or line.startswith("#"):
            continue
        if line == "---":
            if separator_seen:
                raise ManifestError(f"{path}:{line_no}: duplicate separator")
            separator_seen = True
            continue
        if separator_seen:
            raise ManifestError(f"{path}:{line_no}: data after separator")
        if "=" not in line:
            raise ManifestError(
                f"{path}:{line_no}: expected key=value, got {line!r}"
            )
        key, value = line.split("=", 1)
        if not key or key != key.strip() or value != value.strip():
            raise ManifestError(
                f"{path}:{line_no}: fields must use canonical key=value"
            )
        if key not in ALLOWED_FIELDS:
            raise ManifestError(f"{path}:{line_no}: unknown field {key!r}")
        if key in fields:
            raise ManifestError(f"{path}:{line_no}: duplicate field {key!r}")
        if not PRINTABLE_RE.fullmatch(value):
            raise ManifestError(f"{path}:{line_no}: non-printable value")
        fields[key] = value
    return fields


def _ascii_limit(path: Path, key: str, value: str, limit: int) -> None:
    if len(value.encode("ascii")) > limit:
        raise ManifestError(
            f"{path}: {key} exceeds the runtime limit of {limit} bytes"
        )


def _path_has_dotdot(path: str) -> bool:
    return ".." in path.split("/")


def manifest_dependencies(path: Path, fields: Dict[str, str]) -> tuple[str, ...]:
    value = fields.get("depends", "")
    if not value:
        return ()
    deps = tuple(value.split(","))
    if any(not dep or dep != dep.strip() for dep in deps):
        raise ManifestError(f"{path}: depends must be a canonical comma list")
    if len(deps) > RUNTIME_MAX_DEPS:
        raise ManifestError(
            f"{path}: depends exceeds CAPYPKG_MAX_DEPS ({RUNTIME_MAX_DEPS})"
        )
    if len(set(deps)) != len(deps):
        raise ManifestError(f"{path}: duplicate dependency")
    for dep in deps:
        if not NAME_RE.fullmatch(dep) or set(dep) <= {"."}:
            raise ManifestError(f"{path}: invalid dependency {dep!r}")
    return deps


def validate_manifest(path: Path, fields: Dict[str, str]) -> None:
    unknown = set(fields) - ALLOWED_FIELDS
    if unknown:
        raise ManifestError(f"{path}: unknown field {sorted(unknown)[0]!r}")
    for required in REQUIRED_FIELDS:
        if required not in fields:
            raise ManifestError(f"{path}: missing required field {required!r}")
        if required != "depends" and not fields[required]:
            raise ManifestError(f"{path}: empty required field {required!r}")

    name = fields["name"]
    if not NAME_RE.fullmatch(name) or set(name) <= {"."}:
        raise ManifestError(f"{path}: invalid module name {name!r}")
    _ascii_limit(path, "name", name, RUNTIME_NAME_BYTES)
    _ascii_limit(path, "version", fields["version"], RUNTIME_VERSION_BYTES)
    _ascii_limit(path, "summary", fields["summary"], RUNTIME_SUMMARY_BYTES)
    _ascii_limit(path, "payload_url", fields["payload_url"], RUNTIME_URL_BYTES)
    _ascii_limit(path, "install_root", fields["install_root"], RUNTIME_PATH_BYTES)

    try:
        parsed = urlsplit(fields["payload_url"])
        port = parsed.port
    except ValueError as exc:
        raise ManifestError(f"{path}: malformed payload_url") from exc
    if (
        parsed.scheme != "https"
        or not parsed.hostname
        or parsed.username is not None
        or parsed.password is not None
        or parsed.fragment
        or (port is not None and not 1 <= port <= 65535)
    ):
        raise ManifestError(f"{path}: payload_url must be credential-free HTTPS")

    if not SHA256_RE.fullmatch(fields["payload_sha256"]):
        raise ManifestError(f"{path}: invalid payload_sha256")
    signature = fields.get("signature_ed25519", "")
    if signature and not SIG_RE.fullmatch(signature):
        raise ManifestError(f"{path}: invalid signature_ed25519")

    size_text = fields["payload_size"]
    if not POSITIVE_DECIMAL_RE.fullmatch(size_text):
        raise ManifestError(
            f"{path}: payload_size must be a canonical positive decimal"
        )
    if len(size_text) > len(str(RUNTIME_PAYLOAD_BYTES)):
        raise ManifestError(f"{path}: payload_size exceeds 8 MiB")
    if int(size_text) > RUNTIME_PAYLOAD_BYTES:
        raise ManifestError(f"{path}: payload_size exceeds 8 MiB")

    install_root = fields["install_root"]
    if (
        not install_root.startswith("/")
        or _path_has_dotdot(install_root)
        or not (
            install_root == "/var/capypkg"
            or install_root.startswith("/var/capypkg/")
            or install_root.startswith("/opt/")
        )
    ):
        raise ManifestError(f"{path}: install_root is outside runtime scope")

    manifest_dependencies(path, fields)
    if fields.get("official", "1") != "1":
        raise ManifestError(f"{path}: official must be 1")
    if "repo" in fields:
        repo = fields["repo"]
        if not repo or not NAME_RE.fullmatch(repo):
            raise ManifestError(f"{path}: invalid repo field")
        _ascii_limit(path, "repo", repo, RUNTIME_REPO_BYTES)


def _sha256_hex(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def validate_catalog_manifest(
    path: Path,
    fields: Dict[str, str],
    spec: ModuleSpec,
    release_tag: str,
) -> Path:
    validate_manifest(path, fields)
    expected_values = {
        "name": spec.module_id,
        "version": spec.version,
        "payload_url": expected_payload_url(spec, release_tag),
        "install_root": spec.install_root,
        "depends": ",".join(spec.dependencies),
    }
    for key, expected in expected_values.items():
        if fields.get(key, "") != expected:
            raise ManifestError(
                f"{path}: {key}={fields.get(key, '')!r}, expected {expected!r}"
            )
    if fields.get("official", "1") != str(spec.official):
        raise ManifestError(f"{path}: official must be {spec.official}")
    if "repo" in fields and fields["repo"] != spec.repo:
        raise ManifestError(
            f"{path}: repo={fields['repo']!r}, expected {spec.repo!r}"
        )

    missing_deps = set(spec.dependencies) - set(MODULE_BY_ID)
    if missing_deps:
        raise ManifestError(f"{path}: dependency set is not closed")

    payload = path.parent / spec.asset
    if not payload.is_file():
        raise ManifestError(f"{path}: local payload is missing: {spec.asset}")
    actual_size = payload.stat().st_size
    expected_size = int(fields["payload_size"])
    if actual_size != expected_size:
        raise ManifestError(
            f"{path}: local payload size mismatch "
            f"(actual={actual_size}, expected={expected_size})"
        )
    actual_sha = _sha256_hex(payload)
    if actual_sha != fields["payload_sha256"]:
        raise ManifestError(f"{path}: local payload SHA-256 mismatch")
    return payload


def emit_entry(fields: Dict[str, str], out_lines: List[str]) -> None:
    rendered = dict(fields)
    rendered.setdefault("official", "1")
    for key in CANONICAL_FIELDS:
        value = rendered.get(key, "")
        if value != "":
            out_lines.append(f"{key}={value}")
    out_lines.append("---")


def _capyos_release_tag(workspace: Path) -> str:
    version_header = workspace / "CapyOS/include/core/version.h"
    try:
        lines = version_header.read_text(encoding="utf-8").splitlines()
    except OSError as exc:
        raise ManifestError(
            f"unable to derive release tag from {version_header}"
        ) from exc
    for line in lines:
        match = CAPYOS_VERSION_RE.fullmatch(line)
        if match:
            try:
                return validate_release_tag(f"v{match.group(1)}")
            except ValueError as exc:
                raise ManifestError(f"invalid CAPYOS_VERSION_FULL in {version_header}") from exc
    raise ManifestError(f"CAPYOS_VERSION_FULL not found in {version_header}")


def _validate_repo_selection(repos: List[str]) -> None:
    if len(repos) != len(set(repos)):
        raise ManifestError("repository selection contains duplicates")
    expected = set(DEFAULT_REPOS)
    actual = set(repos)
    if actual != expected:
        missing = sorted(expected - actual)
        unknown = sorted(actual - expected)
        detail = []
        if missing:
            detail.append(f"missing={','.join(missing)}")
        if unknown:
            detail.append(f"unknown={','.join(unknown)}")
        raise ManifestError(
            "repository selection must match the official catalog ("
            + "; ".join(detail)
            + ")"
        )


def build_index(
    repo_root: Path,
    repos: List[str],
    output: Path,
    release_tag: str | None = None,
) -> int:
    _validate_repo_selection(repos)
    if release_tag is None:
        release_tag = _capyos_release_tag(repo_root)
    try:
        release_tag = validate_release_tag(release_tag)
    except ValueError as exc:
        raise ManifestError(str(exc)) from exc

    manifests_by_id: dict[str, tuple[Path, Dict[str, str]]] = {}
    for repo in repos:
        repo_dir = repo_root / repo
        if not repo_dir.is_dir():
            raise ManifestError(f"{repo}: repository directory is missing")
        manifest_paths = find_manifest_files(repo_root, repo)
        if not manifest_paths:
            raise ManifestError(f"{repo}: capypkg manifest is missing")
        for path in manifest_paths:
            fields = parse_manifest(path)
            module_id = fields.get("name", "")
            spec = MODULE_BY_ID.get(module_id)
            if spec is None:
                raise ManifestError(f"{path}: module is not in the official catalog")
            if spec.repo != repo:
                raise ManifestError(
                    f"{path}: module belongs to {spec.repo}, not {repo}"
                )
            if module_id in manifests_by_id:
                raise ManifestError(f"{path}: duplicate module {module_id}")
            manifests_by_id[module_id] = (path, fields)

    expected_ids = set(MODULE_BY_ID)
    missing_ids = expected_ids - set(manifests_by_id)
    if missing_ids:
        raise ManifestError(
            "missing official manifest(s): " + ", ".join(sorted(missing_ids))
        )
    if len(manifests_by_id) > RUNTIME_MAX_ENTRIES:
        raise ManifestError("module count exceeds CAPYPKG_MAX_AVAILABLE")

    out_lines: List[str] = [
        "# CapyOS modules index, generated by build_modules_index.py",
        f"# Official inventory: {len(MODULE_SPECS)} immutable modules",
    ]
    for spec in MODULE_SPECS:
        path, fields = manifests_by_id[spec.module_id]
        validate_catalog_manifest(path, fields, spec, release_tag)
        rendered = dict(fields)
        rendered["official"] = "1"
        pinned_metadata = pinned_payload_metadata(spec)
        if pinned_metadata is not None:
            pinned_sha256, pinned_size = pinned_metadata
            rendered["payload_sha256"] = pinned_sha256
            rendered["payload_size"] = str(pinned_size)
            # A signature emitted by a local rebuild cannot authenticate the
            # different immutable payload referenced by the official index.
            rendered.pop("signature_ed25519", None)
        emit_entry(rendered, out_lines)
        print(
            f"[ok] {spec.repo}: {spec.module_id}@{spec.version} "
            f"({rendered['payload_sha256'][:12]}...)"
        )

    index_text = "\n".join(out_lines) + "\n"
    if len(index_text.encode("utf-8")) > RUNTIME_INDEX_BYTES:
        raise ManifestError(
            f"modules index exceeds the runtime limit of {RUNTIME_INDEX_BYTES} bytes"
        )
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(index_text, encoding="utf-8")
    print(f"[ok] wrote {len(MODULE_SPECS)} entries to {output}")
    return 0


def main(argv: Optional[List[str]] = None) -> int:
    parser = argparse.ArgumentParser(
        description="Build the exact nine-entry official CapyOS module index."
    )
    parser.add_argument(
        "--workspace",
        default=None,
        help="parent directory containing CapyOS and all sibling repositories",
    )
    parser.add_argument(
        "--output",
        default="build/capypkg/modules-index.txt",
        help="output index path",
    )
    parser.add_argument(
        "--repos",
        nargs="+",
        default=list(DEFAULT_REPOS),
        help="compatibility option; must contain the exact official repo set",
    )
    parser.add_argument(
        "--release-tag",
        default=None,
        help=(
            "immutable CapyOS v* tag used by the CapyAI asset; defaults to "
            "CAPYOS_VERSION_FULL"
        ),
    )
    args = parser.parse_args(argv)

    script_dir = Path(__file__).resolve().parent
    capyos_root = script_dir.parent.parent
    workspace = Path(args.workspace) if args.workspace else capyos_root.parent
    output = Path(args.output)
    if not output.is_absolute():
        output = capyos_root / output

    try:
        return build_index(
            workspace,
            list(args.repos),
            output,
            release_tag=args.release_tag,
        )
    except (ManifestError, OSError, UnicodeError) as exc:
        print(f"[error] {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
