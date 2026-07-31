#!/usr/bin/env python3
"""Verify the official CapyOS module index and all referenced payloads."""

from __future__ import annotations

import argparse
import hashlib
import math
import re
import sys
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Callable, Iterable, Optional
from urllib.error import HTTPError, URLError
from urllib.parse import urlsplit
from urllib.request import Request, urlopen

try:  # Package import used by unittest.
    from .modules_index_catalog import (
        ALLOWED_FIELDS,
        EXPECTED_MODULE_COUNT,
        MODULE_BY_ID,
        MODULE_SPECS,
        RUNTIME_INDEX_BYTES,
        RUNTIME_MAX_DEPS,
        RUNTIME_NAME_BYTES,
        RUNTIME_PATH_BYTES,
        RUNTIME_PAYLOAD_BYTES,
        RUNTIME_REPO_BYTES,
        RUNTIME_SUMMARY_BYTES,
        RUNTIME_URL_BYTES,
        RUNTIME_VERSION_BYTES,
        expected_payload_url,
        pinned_payload_metadata,
        release_tag_from_payload_url,
        validate_release_tag,
    )
except ImportError:  # Direct script/import from tools/scripts.
    from modules_index_catalog import (  # type: ignore
        ALLOWED_FIELDS,
        EXPECTED_MODULE_COUNT,
        MODULE_BY_ID,
        MODULE_SPECS,
        RUNTIME_INDEX_BYTES,
        RUNTIME_MAX_DEPS,
        RUNTIME_NAME_BYTES,
        RUNTIME_PATH_BYTES,
        RUNTIME_PAYLOAD_BYTES,
        RUNTIME_REPO_BYTES,
        RUNTIME_SUMMARY_BYTES,
        RUNTIME_URL_BYTES,
        RUNTIME_VERSION_BYTES,
        expected_payload_url,
        pinned_payload_metadata,
        release_tag_from_payload_url,
        validate_release_tag,
    )

MAX_INDEX_BYTES = RUNTIME_INDEX_BYTES
MAX_PAYLOAD_BYTES = RUNTIME_PAYLOAD_BYTES
READ_CHUNK_BYTES = 64 * 1024
DEFAULT_ATTEMPTS = 3
MAX_ATTEMPTS = 5
DEFAULT_BACKOFF_SECONDS = 0.5
MAX_BACKOFF_SECONDS = 30.0
DEFAULT_TIMEOUT_SECONDS = 30.0

NAME_RE = re.compile(r"^[A-Za-z0-9._-]{1,63}$")
SHA256_RE = re.compile(r"^[0-9a-f]{64}$")
SIGNATURE_RE = re.compile(r"^[0-9a-f]{128}$")
POSITIVE_DECIMAL_RE = re.compile(r"^[1-9][0-9]*$")
PRINTABLE_RE = re.compile(r"^[\x20-\x7e]*$")


class ModulesIndexError(RuntimeError):
    """The index or one of its payloads failed verification."""


class _HttpStatusError(RuntimeError):
    def __init__(self, status: int) -> None:
        super().__init__(status)
        self.status = status


@dataclass(frozen=True)
class ModuleEntry:
    # First five fields retain the original public construction order.
    name: str
    version: str
    payload_url: str
    payload_sha256: str
    payload_size: int
    summary: str = ""
    official: int = 1
    install_root: str = ""
    depends: tuple[str, ...] = ()
    repo: str = ""


@dataclass(frozen=True)
class VerificationResult:
    name: str
    payload_size: int
    attempts: int
    source: str = "remote"


def _safe_label(ordinal: int, name: str = "") -> str:
    if name and NAME_RE.fullmatch(name):
        return f"module {name}"
    return f"entry {ordinal}"


def _ascii_limit(label: str, key: str, value: str, limit: int) -> None:
    try:
        size = len(value.encode("ascii"))
    except UnicodeEncodeError as exc:
        raise ModulesIndexError(f"{label}: {key} is not printable ASCII") from exc
    if size > limit:
        raise ModulesIndexError(
            f"{label}: {key} exceeds the runtime limit of {limit} bytes"
        )


def _validate_https_url(url: str, label: str) -> None:
    try:
        parsed = urlsplit(url)
        port = parsed.port
    except ValueError as exc:
        raise ModulesIndexError(f"{label}: malformed payload_url") from exc
    if (
        parsed.scheme != "https"
        or not parsed.hostname
        or parsed.username is not None
        or parsed.password is not None
        or parsed.fragment
        or (port is not None and not 1 <= port <= 65535)
    ):
        raise ModulesIndexError(
            f"{label}: payload_url must be credential-free HTTPS"
        )


def _parse_dependencies(value: str, label: str) -> tuple[str, ...]:
    if not value:
        return ()
    deps = tuple(value.split(","))
    if any(not dep or dep != dep.strip() for dep in deps):
        raise ModulesIndexError(f"{label}: depends is not canonical")
    if len(deps) > RUNTIME_MAX_DEPS:
        raise ModulesIndexError(
            f"{label}: depends exceeds CAPYPKG_MAX_DEPS ({RUNTIME_MAX_DEPS})"
        )
    if len(set(deps)) != len(deps):
        raise ModulesIndexError(f"{label}: duplicate dependency")
    for dep in deps:
        if not NAME_RE.fullmatch(dep) or set(dep) <= {"."}:
            raise ModulesIndexError(f"{label}: invalid dependency")
    return deps


def _entry_from_fields(fields: dict[str, str], ordinal: int) -> ModuleEntry:
    label = _safe_label(ordinal, fields.get("name", ""))
    required = (
        "name",
        "version",
        "summary",
        "official",
        "payload_url",
        "payload_sha256",
        "payload_size",
        "install_root",
    )
    for key in required:
        if not fields.get(key):
            raise ModulesIndexError(f"{label}: missing required field {key}")

    name = fields["name"]
    if not NAME_RE.fullmatch(name) or set(name) <= {"."}:
        raise ModulesIndexError(f"entry {ordinal}: invalid module name")
    label = _safe_label(ordinal, name)
    _ascii_limit(label, "name", name, RUNTIME_NAME_BYTES)
    _ascii_limit(label, "version", fields["version"], RUNTIME_VERSION_BYTES)
    _ascii_limit(label, "summary", fields["summary"], RUNTIME_SUMMARY_BYTES)
    _ascii_limit(label, "payload_url", fields["payload_url"], RUNTIME_URL_BYTES)
    _ascii_limit(label, "install_root", fields["install_root"], RUNTIME_PATH_BYTES)

    _validate_https_url(fields["payload_url"], label)
    if not SHA256_RE.fullmatch(fields["payload_sha256"]):
        raise ModulesIndexError(f"{label}: invalid payload_sha256")
    signature = fields.get("signature_ed25519", "")
    if signature and not SIGNATURE_RE.fullmatch(signature):
        raise ModulesIndexError(f"{label}: invalid signature_ed25519")

    size_text = fields["payload_size"]
    if not POSITIVE_DECIMAL_RE.fullmatch(size_text):
        raise ModulesIndexError(
            f"{label}: payload_size must be a canonical positive decimal"
        )
    if len(size_text) > len(str(MAX_PAYLOAD_BYTES)):
        raise ModulesIndexError(f"{label}: payload_size exceeds 8 MiB")
    size = int(size_text)
    if size > MAX_PAYLOAD_BYTES:
        raise ModulesIndexError(f"{label}: payload_size exceeds 8 MiB")

    install_root = fields["install_root"]
    if (
        not install_root.startswith("/")
        or ".." in install_root.split("/")
        or not (
            install_root == "/var/capypkg"
            or install_root.startswith("/var/capypkg/")
            or install_root.startswith("/opt/")
        )
    ):
        raise ModulesIndexError(f"{label}: invalid install_root")

    if fields["official"] != "1":
        raise ModulesIndexError(f"{label}: official must be 1")
    repo = fields.get("repo", "")
    if repo:
        if not NAME_RE.fullmatch(repo):
            raise ModulesIndexError(f"{label}: invalid repo")
        _ascii_limit(label, "repo", repo, RUNTIME_REPO_BYTES)

    return ModuleEntry(
        name=name,
        version=fields["version"],
        payload_url=fields["payload_url"],
        payload_sha256=fields["payload_sha256"],
        payload_size=size,
        summary=fields["summary"],
        official=1,
        install_root=install_root,
        depends=_parse_dependencies(fields.get("depends", ""), label),
        repo=repo,
    )


def _validate_catalog(
    entries: list[ModuleEntry], release_tag: str | None
) -> None:
    by_name = {entry.name: entry for entry in entries}
    expected_names = set(MODULE_BY_ID)
    actual_names = set(by_name)
    if actual_names != expected_names:
        raise ModulesIndexError(
            "modules index inventory differs from the official catalog"
        )

    if release_tag is None:
        ai_spec = next(spec for spec in MODULE_SPECS if spec.uses_capyos_release_tag)
        try:
            release_tag = release_tag_from_payload_url(
                ai_spec, by_name[ai_spec.module_id].payload_url
            )
        except ValueError as exc:
            raise ModulesIndexError(
                f"module {ai_spec.module_id}: invalid release URL"
            ) from exc
    else:
        try:
            release_tag = validate_release_tag(release_tag)
        except ValueError as exc:
            raise ModulesIndexError(str(exc)) from exc

    for spec in MODULE_SPECS:
        entry = by_name[spec.module_id]
        checks = [
            (entry.version, spec.version, "version"),
            (entry.install_root, spec.install_root, "install_root"),
            (entry.depends, spec.dependencies, "depends"),
            (entry.payload_url, expected_payload_url(spec, release_tag), "payload_url"),
        ]
        pinned_metadata = pinned_payload_metadata(spec)
        if pinned_metadata is not None:
            pinned_sha256, pinned_size = pinned_metadata
            checks.extend(
                (
                    (entry.payload_sha256, pinned_sha256, "payload_sha256"),
                    (entry.payload_size, pinned_size, "payload_size"),
                )
            )
        for actual, expected, field in checks:
            if actual != expected:
                raise ModulesIndexError(
                    f"module {spec.module_id}: {field} differs from catalog"
                )
        if entry.official != spec.official:
            raise ModulesIndexError(
                f"module {spec.module_id}: official differs from catalog"
            )
        if entry.repo and entry.repo != spec.repo:
            raise ModulesIndexError(
                f"module {spec.module_id}: repo differs from catalog"
            )

    for entry in entries:
        for dependency in entry.depends:
            if dependency not in by_name:
                raise ModulesIndexError(
                    f"module {entry.name}: dependency set is not closed"
                )


def parse_modules_index(
    text: str,
    expected_count: int = EXPECTED_MODULE_COUNT,
    *,
    release_tag: str | None = None,
) -> list[ModuleEntry]:
    """Parse and validate the complete official inventory before I/O."""
    if expected_count != EXPECTED_MODULE_COUNT:
        raise ValueError(
            f"expected_count must equal the official count {EXPECTED_MODULE_COUNT}"
        )
    try:
        index_size = len(text.encode("utf-8"))
    except UnicodeEncodeError as exc:
        raise ModulesIndexError("modules index is not valid UTF-8") from exc
    if index_size > MAX_INDEX_BYTES:
        raise ModulesIndexError(
            f"modules index exceeds the {MAX_INDEX_BYTES}-byte runtime limit"
        )

    entries: list[ModuleEntry] = []
    fields: dict[str, str] = {}
    seen_names: set[str] = set()

    def finish_entry(line_no: int) -> None:
        nonlocal fields
        if not fields:
            raise ModulesIndexError(f"index line {line_no}: empty entry")
        ordinal = len(entries) + 1
        entry = _entry_from_fields(fields, ordinal)
        if entry.name in seen_names:
            raise ModulesIndexError(
                f"entry {ordinal}: duplicate module name {entry.name}"
            )
        seen_names.add(entry.name)
        entries.append(entry)
        fields = {}

    for line_no, line in enumerate(text.split("\n"), start=1):
        if not line or line.startswith("#"):
            continue
        if "\r" in line or line != line.strip():
            raise ModulesIndexError(
                f"index line {line_no}: non-canonical whitespace"
            )
        if line == "---":
            finish_entry(line_no)
            continue
        if "=" not in line:
            raise ModulesIndexError(f"index line {line_no}: expected key=value")
        key, value = line.split("=", 1)
        if not key or key != key.strip() or value != value.strip():
            raise ModulesIndexError(f"index line {line_no}: non-canonical field")
        if key not in ALLOWED_FIELDS:
            raise ModulesIndexError(f"index line {line_no}: unknown field")
        if key in fields:
            raise ModulesIndexError(f"index line {line_no}: duplicate field")
        if not PRINTABLE_RE.fullmatch(value):
            raise ModulesIndexError(f"index line {line_no}: non-printable value")
        fields[key] = value
    if fields:
        finish_entry(len(text.split("\n")))

    if len(entries) != expected_count or len(seen_names) != expected_count:
        raise ModulesIndexError(
            f"modules index must contain exactly {expected_count} unique modules"
        )
    _validate_catalog(entries, release_tag)
    return entries


def read_modules_index(path: Path) -> str:
    try:
        raw = path.read_bytes()
    except OSError as exc:
        raise ModulesIndexError("unable to read modules index") from exc
    if len(raw) > MAX_INDEX_BYTES:
        raise ModulesIndexError(
            f"modules index exceeds the {MAX_INDEX_BYTES}-byte runtime limit"
        )
    try:
        return raw.decode("utf-8")
    except UnicodeDecodeError as exc:
        raise ModulesIndexError("modules index is not valid UTF-8") from exc


def _response_status(response: object) -> int:
    status = getattr(response, "status", None)
    if status is None:
        getcode = getattr(response, "getcode", None)
        status = getcode() if callable(getcode) else 200
    return int(status)


def _retryable_status(status: int, retry_404_for_propagation: bool) -> bool:
    if status == 404:
        return retry_404_for_propagation
    return status in (408, 425, 429) or 500 <= status <= 599


def _fetch_once(
    entry: ModuleEntry,
    opener: Callable[..., object],
    timeout: float,
) -> None:
    request = Request(
        entry.payload_url,
        headers={
            "Accept": "application/octet-stream",
            "User-Agent": "CapyOS-modules-index-verifier/2",
        },
        method="GET",
    )
    response = opener(request, timeout=timeout)
    with response:  # type: ignore[attr-defined]
        status = _response_status(response)
        if status >= 400:
            raise _HttpStatusError(status)

        effective_url = getattr(response, "geturl", lambda: entry.payload_url)()
        _validate_https_url(effective_url, f"module {entry.name}")

        digest = hashlib.sha256()
        total = 0
        while True:
            remaining_with_sentinel = entry.payload_size - total + 1
            chunk = response.read(min(READ_CHUNK_BYTES, remaining_with_sentinel))
            if not chunk:
                break
            if not isinstance(chunk, bytes):
                raise ModulesIndexError(
                    f"module {entry.name}: payload stream returned non-bytes"
                )
            total += len(chunk)
            if total > entry.payload_size:
                raise ModulesIndexError(
                    f"module {entry.name}: payload size mismatch"
                )
            digest.update(chunk)

    if total != entry.payload_size:
        raise ModulesIndexError(f"module {entry.name}: payload size mismatch")
    if digest.hexdigest() != entry.payload_sha256:
        raise ModulesIndexError(f"module {entry.name}: payload SHA-256 mismatch")


def _verify_local_payload(entry: ModuleEntry, path: Path) -> VerificationResult:
    if path.is_symlink() or not path.is_file():
        raise ModulesIndexError(f"module {entry.name}: invalid local payload")
    try:
        size = path.stat().st_size
    except OSError as exc:
        raise ModulesIndexError(
            f"module {entry.name}: unable to read local payload"
        ) from exc
    if size != entry.payload_size:
        raise ModulesIndexError(f"module {entry.name}: payload size mismatch")
    digest = hashlib.sha256()
    try:
        with path.open("rb") as handle:
            for chunk in iter(lambda: handle.read(READ_CHUNK_BYTES), b""):
                digest.update(chunk)
    except OSError as exc:
        raise ModulesIndexError(
            f"module {entry.name}: unable to read local payload"
        ) from exc
    if digest.hexdigest() != entry.payload_sha256:
        raise ModulesIndexError(f"module {entry.name}: payload SHA-256 mismatch")
    return VerificationResult(entry.name, size, 0, "local")


def verify_payload(
    entry: ModuleEntry,
    *,
    opener: Callable[..., object] = urlopen,
    attempts: int = DEFAULT_ATTEMPTS,
    backoff_seconds: float = DEFAULT_BACKOFF_SECONDS,
    timeout: float = DEFAULT_TIMEOUT_SECONDS,
    sleeper: Callable[[float], None] = time.sleep,
    retry_404_for_propagation: bool = False,
) -> VerificationResult:
    if not 1 <= attempts <= MAX_ATTEMPTS:
        raise ValueError(f"attempts must be between 1 and {MAX_ATTEMPTS}")
    if (
        not math.isfinite(backoff_seconds)
        or not 0 <= backoff_seconds <= MAX_BACKOFF_SECONDS
    ):
        raise ValueError(
            f"backoff_seconds must be between 0 and {MAX_BACKOFF_SECONDS}"
        )
    if not math.isfinite(timeout) or timeout <= 0:
        raise ValueError("timeout must be positive")

    for attempt in range(1, attempts + 1):
        try:
            _fetch_once(entry, opener, timeout)
            return VerificationResult(entry.name, entry.payload_size, attempt)
        except (HTTPError, _HttpStatusError) as exc:
            status = int(exc.code) if isinstance(exc, HTTPError) else exc.status
            retryable = _retryable_status(status, retry_404_for_propagation)
            if not retryable or attempt == attempts:
                suffix = (
                    ""
                    if status == 404 and not retry_404_for_propagation
                    else f" after {attempt} attempt(s)"
                )
                raise ModulesIndexError(
                    f"module {entry.name}: payload returned HTTP {status}{suffix}"
                ) from None
        except (URLError, TimeoutError, ConnectionError, OSError):
            if attempt == attempts:
                raise ModulesIndexError(
                    f"module {entry.name}: network failure after "
                    f"{attempt} attempt(s)"
                ) from None

        sleeper(
            min(
                backoff_seconds * (2 ** (attempt - 1)),
                MAX_BACKOFF_SECONDS,
            )
        )

    raise AssertionError("bounded retry loop exhausted unexpectedly")


def verify_modules_index_assets(
    text: str,
    *,
    opener: Callable[..., object] = urlopen,
    attempts: int = DEFAULT_ATTEMPTS,
    backoff_seconds: float = DEFAULT_BACKOFF_SECONDS,
    timeout: float = DEFAULT_TIMEOUT_SECONDS,
    sleeper: Callable[[float], None] = time.sleep,
    local_assets_dir: Path | None = None,
    release_tag: str | None = None,
    retry_404_for_propagation: bool = False,
) -> list[VerificationResult]:
    entries = parse_modules_index(text, release_tag=release_tag)
    if local_assets_dir is not None and not local_assets_dir.is_dir():
        raise ModulesIndexError("local assets directory does not exist")

    results: list[VerificationResult] = []
    for entry in entries:
        spec = MODULE_BY_ID[entry.name]
        local_path = (
            local_assets_dir / spec.asset if local_assets_dir is not None else None
        )
        if local_path is not None and local_path.exists():
            results.append(_verify_local_payload(entry, local_path))
            continue
        results.append(
            verify_payload(
                entry,
                opener=opener,
                attempts=attempts,
                backoff_seconds=backoff_seconds,
                timeout=timeout,
                sleeper=sleeper,
                retry_404_for_propagation=retry_404_for_propagation,
            )
        )
    return results


def _bounded_attempts(value: str) -> int:
    parsed = int(value)
    if not 1 <= parsed <= MAX_ATTEMPTS:
        raise argparse.ArgumentTypeError(
            f"must be between 1 and {MAX_ATTEMPTS}"
        )
    return parsed


def _non_negative_float(value: str) -> float:
    parsed = float(value)
    if (
        not math.isfinite(parsed)
        or not 0 <= parsed <= MAX_BACKOFF_SECONDS
    ):
        raise argparse.ArgumentTypeError(
            f"must be between 0 and {MAX_BACKOFF_SECONDS}"
        )
    return parsed


def _positive_float(value: str) -> float:
    parsed = float(value)
    if not math.isfinite(parsed) or parsed <= 0:
        raise argparse.ArgumentTypeError("must be positive")
    return parsed


def main(argv: Optional[Iterable[str]] = None) -> int:
    parser = argparse.ArgumentParser(
        description=(
            "Validate the exact official module inventory and verify each "
            "payload locally or over HTTPS."
        )
    )
    parser.add_argument(
        "--index",
        type=Path,
        default=Path("build/capypkg/modules-index.txt"),
        help="local modules-index path",
    )
    parser.add_argument(
        "--attempts",
        type=_bounded_attempts,
        default=DEFAULT_ATTEMPTS,
        help=f"total attempts for transient failures (1-{MAX_ATTEMPTS})",
    )
    parser.add_argument(
        "--backoff-seconds",
        type=_non_negative_float,
        default=DEFAULT_BACKOFF_SECONDS,
        help="initial exponential backoff delay",
    )
    parser.add_argument(
        "--timeout",
        type=_positive_float,
        default=DEFAULT_TIMEOUT_SECONDS,
        help="per-request timeout in seconds",
    )
    parser.add_argument(
        "--release-tag",
        default=None,
        help="expected immutable CapyOS v* tag for the CapyAI asset",
    )
    parser.add_argument(
        "--local-assets-dir",
        "--local-payload-dir",
        dest="local_assets_dir",
        type=Path,
        default=None,
        help=(
            "directory containing pre-publication assets; matching files are "
            "verified locally and other modules remain remote"
        ),
    )
    parser.add_argument(
        "--retry-404",
        "--retry-404-for-propagation",
        dest="retry_404_for_propagation",
        action="store_true",
        help="retry bounded HTTP 404 responses while release assets propagate",
    )
    args = parser.parse_args(list(argv) if argv is not None else None)

    try:
        text = read_modules_index(args.index)
        results = verify_modules_index_assets(
            text,
            attempts=args.attempts,
            backoff_seconds=args.backoff_seconds,
            timeout=args.timeout,
            local_assets_dir=args.local_assets_dir,
            release_tag=args.release_tag,
            retry_404_for_propagation=args.retry_404_for_propagation,
        )
    except ModulesIndexError as exc:
        print(f"[error] {exc}", file=sys.stderr)
        return 2

    for result in results:
        if result.source == "local":
            print(f"[ok] {result.name}: verified {result.payload_size} local bytes")
        else:
            print(
                f"[ok] {result.name}: verified {result.payload_size} bytes "
                f"in {result.attempts} attempt(s)"
            )
    print(f"[ok] verified {len(results)} official module payloads")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
