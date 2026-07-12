#!/usr/bin/env python3
"""Shared, dependency-free helpers for the CapyOS signed update catalog."""

from __future__ import annotations

import hashlib
import os
import re
import stat
import subprocess
import tempfile
from datetime import date
from pathlib import Path

PINNED_PUBLIC_KEY_HEX = (
    "be230bddb4144dfbcfbf0f24495ed2c8"
    "c9acf3866fb48633f4d29e49de69ae6d"
)
ED25519_SPKI_PREFIX = bytes.fromhex("302a300506032b6570032100")
MANIFEST_MAX_BYTES = 767
PAYLOAD_MAX_BYTES = 8 * 1024 * 1024
FIELD_ORDER = (
    "available_version",
    "channel",
    "branch",
    "source",
    "published_at",
    "payload_url",
    "payload_sha256",
)
ALL_FIELDS = frozenset((*FIELD_ORDER, "signature_ed25519"))
FIELD_LIMITS = {
    "available_version": 39,
    "channel": 15,
    "branch": 15,
    "source": 95,
    "published_at": 23,
    "payload_url": 159,
    "payload_sha256": 64,
    "signature_ed25519": 128,
}
VERSION_RE = re.compile(
    r"^v?\d+\.\d+\.\d+(?:-(?:alpha|beta|rc)(?:\.\d+)?)?(?:\+[0-9A-Za-z.-]+)?$"
)
SOURCE_RE = re.compile(r"^github:[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$")
HEX64_RE = re.compile(r"^[0-9a-fA-F]{64}$")
HEX128_RE = re.compile(r"^[0-9a-fA-F]{128}$")


class ManifestError(ValueError):
    pass


def normalize_public_key_hex(value: str) -> str:
    normalized = value.strip().lower().replace(":", "")
    if not HEX64_RE.fullmatch(normalized):
        raise ManifestError("expected public key must be exactly 32 raw bytes (hex64)")
    return normalized


def ensure_regular_file(path: Path, label: str) -> None:
    if not path.is_file() or path.stat().st_size == 0:
        raise ManifestError(f"{label} missing or empty: {path}")


def require_private_key_mode(path: Path, allow_insecure: bool) -> None:
    if allow_insecure or os.name == "nt":
        return
    mode = stat.S_IMODE(path.stat().st_mode)
    if mode & 0o077:
        raise ManifestError(
            f"private key permissions are {mode:o}; use chmod 600 or --allow-insecure-key"
        )


def run_openssl(openssl: str, args: list[str]) -> subprocess.CompletedProcess[bytes]:
    try:
        return subprocess.run(
            [openssl, *args],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
        )
    except FileNotFoundError as exc:
        raise ManifestError(f"OpenSSL executable not found: {openssl}") from exc


def _raw_public_from_der(der: bytes, label: str) -> bytes:
    if len(der) != len(ED25519_SPKI_PREFIX) + 32 or not der.startswith(
        ED25519_SPKI_PREFIX
    ):
        raise ManifestError(f"{label} is not an Ed25519 SPKI key")
    return der[len(ED25519_SPKI_PREFIX) :]


def raw_public_from_private(openssl: str, private_key: Path) -> bytes:
    proc = run_openssl(
        openssl,
        ["pkey", "-in", str(private_key), "-pubout", "-outform", "DER"],
    )
    if proc.returncode != 0:
        raise ManifestError("OpenSSL could not read the Ed25519 private key")
    return _raw_public_from_der(proc.stdout, "private key")


def raw_public_from_public(openssl: str, public_key: Path) -> bytes:
    proc = run_openssl(
        openssl,
        [
            "pkey",
            "-pubin",
            "-in",
            str(public_key),
            "-pubout",
            "-outform",
            "DER",
        ],
    )
    if proc.returncode != 0:
        raise ManifestError("OpenSSL could not read the Ed25519 public key")
    return _raw_public_from_der(proc.stdout, "public key")


def public_der_from_raw(raw_public: bytes) -> bytes:
    if len(raw_public) != 32:
        raise ManifestError("Ed25519 public key must contain 32 raw bytes")
    return ED25519_SPKI_PREFIX + raw_public


def sign_bytes(openssl: str, private_key: Path, body: bytes) -> bytes:
    with tempfile.TemporaryDirectory(prefix="capyos-update-sign-") as tmp:
        root = Path(tmp)
        body_path = root / "manifest.body"
        signature_path = root / "manifest.sig"
        body_path.write_bytes(body)
        proc = run_openssl(
            openssl,
            [
                "pkeyutl",
                "-sign",
                "-rawin",
                "-inkey",
                str(private_key),
                "-in",
                str(body_path),
                "-out",
                str(signature_path),
            ],
        )
        if proc.returncode != 0:
            raise ManifestError("OpenSSL Ed25519 signing failed")
        signature = signature_path.read_bytes()
    if len(signature) != 64:
        raise ManifestError("OpenSSL returned a non-64-byte Ed25519 signature")
    return signature


def verify_signature(openssl: str, raw_public: bytes, body: bytes, signature: bytes) -> None:
    if len(signature) != 64:
        raise ManifestError("Ed25519 signature must contain 64 raw bytes")
    with tempfile.TemporaryDirectory(prefix="capyos-update-verify-") as tmp:
        root = Path(tmp)
        body_path = root / "manifest.body"
        signature_path = root / "manifest.sig"
        public_path = root / "update-public.der"
        body_path.write_bytes(body)
        signature_path.write_bytes(signature)
        public_path.write_bytes(public_der_from_raw(raw_public))
        proc = run_openssl(
            openssl,
            [
                "pkeyutl",
                "-verify",
                "-rawin",
                "-pubin",
                "-keyform",
                "DER",
                "-inkey",
                str(public_path),
                "-in",
                str(body_path),
                "-sigfile",
                str(signature_path),
            ],
        )
    if proc.returncode != 0:
        raise ManifestError("Ed25519 signature verification failed")


def payload_sha256(path: Path) -> str:
    ensure_regular_file(path, "payload")
    size = path.stat().st_size
    if size > PAYLOAD_MAX_BYTES:
        raise ManifestError(
            f"payload is {size} bytes; runtime HTTP limit is {PAYLOAD_MAX_BYTES} bytes"
        )
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _validate_ascii_value(key: str, value: str) -> None:
    if not value:
        raise ManifestError(f"{key} must not be empty")
    try:
        encoded = value.encode("ascii")
    except UnicodeEncodeError as exc:
        raise ManifestError(f"{key} must be printable ASCII") from exc
    if any(byte < 0x20 or byte > 0x7E for byte in encoded):
        raise ManifestError(f"{key} contains a control byte")
    if len(encoded) > FIELD_LIMITS[key]:
        raise ManifestError(
            f"{key} exceeds runtime limit ({len(encoded)} > {FIELD_LIMITS[key]})"
        )


def validate_fields(fields: dict[str, str], require_signature: bool) -> None:
    required = set(FIELD_ORDER)
    if require_signature:
        required.add("signature_ed25519")
    missing = sorted(key for key in required if not fields.get(key))
    if missing:
        raise ManifestError(f"missing manifest fields: {', '.join(missing)}")
    unknown = sorted(set(fields) - ALL_FIELDS)
    if unknown:
        raise ManifestError(f"unknown manifest fields: {', '.join(unknown)}")
    for key, value in fields.items():
        _validate_ascii_value(key, value)
    if not VERSION_RE.fullmatch(fields["available_version"]):
        raise ManifestError("available_version is not supported CapyOS semver")
    if fields["channel"] not in ("stable", "develop"):
        raise ManifestError("channel must be stable or develop")
    if not SOURCE_RE.fullmatch(fields["source"]):
        raise ManifestError("source must use github:owner/repository")
    try:
        parsed_date = date.fromisoformat(fields["published_at"])
    except ValueError as exc:
        raise ManifestError("published_at must be a real YYYY-MM-DD date") from exc
    if parsed_date.isoformat() != fields["published_at"]:
        raise ManifestError("published_at must use canonical YYYY-MM-DD")
    url = fields["payload_url"]
    if not (url.startswith("https://") or url.startswith("/system/update/")):
        raise ManifestError("payload_url must use https:// or /system/update/")
    if url in ("https://", "/system/update/") or ".." in url or any(
        char.isspace() for char in url
    ):
        raise ManifestError("payload_url is malformed")
    if not HEX64_RE.fullmatch(fields["payload_sha256"]):
        raise ManifestError("payload_sha256 must be exactly hex64")
    if require_signature and not HEX128_RE.fullmatch(fields["signature_ed25519"]):
        raise ManifestError("signature_ed25519 must be exactly hex128")


def canonical_body(fields: dict[str, str]) -> bytes:
    validate_fields(fields, require_signature=False)
    return "".join(f"{key}={fields[key]}\n" for key in FIELD_ORDER).encode("ascii")


def capture_runtime_signed_text(raw: bytes) -> tuple[bytes, list[str]]:
    """Mirror update_agent_manifest_capture_signed_text byte-for-byte."""
    signed = bytearray()
    signatures: list[str] = []
    start = 0
    while start < len(raw):
        end = start
        while end < len(raw) and raw[end] not in (0x0A, 0x0D):
            end += 1
        line_end = end
        while end < len(raw) and raw[end] in (0x0A, 0x0D):
            end += 1
        line = raw[start:line_end]
        if line.startswith(b"signature_ed25519="):
            try:
                signatures.append(line.split(b"=", 1)[1].decode("ascii"))
            except UnicodeDecodeError as exc:
                raise ManifestError("signature line is not ASCII") from exc
        else:
            signed.extend(raw[start:end])
        start = end
    return bytes(signed), signatures


def parse_manifest(raw: bytes) -> tuple[dict[str, str], bytes]:
    if not raw or len(raw) > MANIFEST_MAX_BYTES:
        raise ManifestError(
            f"manifest size must be 1..{MANIFEST_MAX_BYTES} bytes"
        )
    if any(byte not in (0x0A, 0x0D) and not (0x20 <= byte <= 0x7E) for byte in raw):
        raise ManifestError("manifest contains non-printable or non-ASCII bytes")
    signed, signatures = capture_runtime_signed_text(raw)
    if len(signatures) != 1:
        raise ManifestError("manifest must contain exactly one signature_ed25519 line")
    fields: dict[str, str] = {}
    for line in re.split(br"[\r\n]+", raw):
        if not line:
            continue
        if b"=" not in line:
            raise ManifestError("manifest contains a line without '='")
        key_bytes, value_bytes = line.split(b"=", 1)
        key = key_bytes.decode("ascii")
        value = value_bytes.decode("ascii")
        if key not in ALL_FIELDS:
            raise ManifestError(f"unknown manifest field: {key}")
        if key in fields:
            raise ManifestError(f"duplicate manifest field: {key}")
        fields[key] = value
    validate_fields(fields, require_signature=True)
    canonical = canonical_body(fields)
    canonical_manifest = canonical + (
        f"signature_ed25519={fields['signature_ed25519'].lower()}\n".encode("ascii")
    )
    if raw != canonical_manifest:
        raise ManifestError(
            "manifest is runtime-compatible but not canonical LF/order/signature-last form"
        )
    if signed != canonical:
        raise ManifestError("signed bytes differ from the runtime canonical capture")
    return fields, signed


def atomic_write(path: Path, data: bytes, force: bool) -> None:
    if path.exists() and not force:
        raise ManifestError(f"output already exists: {path} (use --force)")
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp_name: str | None = None
    try:
        with tempfile.NamedTemporaryFile(
            dir=path.parent, prefix=f".{path.name}.", delete=False
        ) as handle:
            handle.write(data)
            handle.flush()
            os.fsync(handle.fileno())
            tmp_name = handle.name
        os.replace(tmp_name, path)
        tmp_name = None
    finally:
        if tmp_name:
            Path(tmp_name).unlink(missing_ok=True)
