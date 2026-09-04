#!/usr/bin/env python3
"""Authoritative inventory for the official CapyOS module index.

This module is intentionally data-only plus small URL helpers so both the
index builder and the post-publication verifier consume the same contract.
"""

from __future__ import annotations

import re
from dataclasses import dataclass
from typing import Final
from urllib.parse import urlsplit

GITHUB_OWNER: Final = "henriquefarisco"
EXPECTED_MODULE_COUNT: Final = 9
EXPECTED_RESOLVED_COUNT: Final = 7
CORE_ABI_VERSION: Final = 3
CORE_ABI_TOKEN: Final = f"capyos-base-v{CORE_ABI_VERSION}"
INDEX_FORMAT: Final = "capyos-modules-index-v2"
INDEX_EPOCH: Final = 1
INDEX_RELEASE_TAG: Final = f"modules-{CORE_ABI_TOKEN}"

# Keep these values in lockstep with include/services/capypkg.h and
# src/services/capypkg/internal/capypkg_internal.h.  The C structs reserve one
# byte for NUL, hence the Python value limits are capacity - 1.
RUNTIME_NAME_BYTES: Final = 63
RUNTIME_VERSION_BYTES: Final = 31
RUNTIME_SUMMARY_BYTES: Final = 95
RUNTIME_URL_BYTES: Final = 159
RUNTIME_PATH_BYTES: Final = 127
RUNTIME_REPO_BYTES: Final = 31
RUNTIME_MAX_DEPS: Final = 8
RUNTIME_MAX_ENTRIES: Final = 128
RUNTIME_INDEX_BYTES: Final = 16 * 1024
RUNTIME_PAYLOAD_BYTES: Final = 8 * 1024 * 1024

CANONICAL_FIELDS: Final = (
    "name",
    "version",
    "summary",
    "official",
    "payload_url",
    "payload_sha256",
    "payload_size",
    "signature_ed25519",
    "install_root",
    "provides_abi",
    "abi_version",
    "core_abi_min",
    "core_abi_max",
    "known_good",
    "depends",
    "repo",
)
ALLOWED_FIELDS: Final = frozenset(CANONICAL_FIELDS)

RELEASE_TAG_RE: Final = re.compile(r"^v[A-Za-z0-9][A-Za-z0-9._+-]{0,127}$")
PAYLOAD_SHA256_RE: Final = re.compile(r"^[0-9a-f]{64}$")


@dataclass(frozen=True)
class ModuleSpec:
    module_id: str
    repo: str
    version: str
    asset: str
    dependencies: tuple[str, ...]
    install_root: str
    provides_abi: str
    abi_version: str
    core_abi_min: int
    core_abi_max: int
    known_good: int
    official: int = 1
    release_repo: str | None = None
    uses_capyos_release_tag: bool = False
    published_payload_sha256: str | None = None
    published_payload_size: int | None = None

    @property
    def github_repo(self) -> str:
        return self.release_repo or self.repo


# External metadata is pinned to the digest and size reported by each immutable
# GitHub Release asset.  CapyAI is the sole exception: its payload is staged in
# the new CapyOS release and therefore remains derived from the local package.
MODULE_SPECS: Final[tuple[ModuleSpec, ...]] = (
    ModuleSpec(
        "org.capyos.agent.core",
        "CapyAgent",
        "0.1.0",
        "org.capyos.agent.core-0.1.0.bin",
        (),
        "/var/capypkg/org.capyos.agent.core",
        "capy-agent-component-index", "2", 3, 3, 1,
        published_payload_sha256=(
            "515326e19dbce4990abc2d75399f0a22"
            "5c52521485eb21bfeae1e176d4528f5d"
        ),
        published_payload_size=143360,
    ),
    ModuleSpec(
        "org.capyos.ai.assistant",
        "CapyAI",
        "0.2.2",
        "org.capyos.ai.assistant-0.2.2.bin",
        (),
        "/var/capypkg/org.capyos.ai.assistant",
        "capy-ai-core", "0", 3, 3, 1,
        published_payload_sha256=(
            "a64f4ec21521ac5f863174d868b2923e"
            "f83c2326dde7a35fbd149957305efa4c"
        ),
        published_payload_size=37753,
    ),
    ModuleSpec(
        "org.capyos.browser.core",
        "CapyBrowser",
        "0.6.8",
        "org.capyos.browser.core-0.6.8.bin",
        ("org.capyos.codecs.image-basic",),
        "/var/capypkg/org.capyos.browser.core",
        "capy-browser-core", "1", 3, 3, 1,
        published_payload_sha256=(
            "bb50beb17740f2855e0477c507b0da7e"
            "eda80504611fa82641d0d2a90ed772f8"
        ),
        published_payload_size=307200,
    ),
    ModuleSpec(
        "org.capyos.browser.text",
        "CapyBrowser",
        "0.6.8",
        "org.capyos.browser.text-0.6.8.bin",
        (),
        "/var/capypkg/org.capyos.browser.text",
        "capy-browser-core", "1", 3, 3, 1,
        published_payload_sha256=(
            "bb50beb17740f2855e0477c507b0da7e"
            "eda80504611fa82641d0d2a90ed772f8"
        ),
        published_payload_size=307200,
    ),
    ModuleSpec(
        "org.capyos.codecs.image-basic",
        "CapyCodecs",
        "0.0.13",
        "org.capyos.codecs.image-basic-0.0.13.bin",
        (),
        "/var/capypkg/org.capyos.codecs.image-basic",
        "capy-codec-image", "2", 3, 3, 1,
        published_payload_sha256=(
            "c79a7496d7959d9f20dbd197748a2846"
            "406a17b45e4dfc774494f2c18568ccd4"
        ),
        published_payload_size=174080,
    ),
    ModuleSpec(
        "org.capyos.ui.desktop-session",
        "CapyUI",
        "2.25.0",
        "org.capyos.ui.desktop-session.bin",
        ("org.capyos.ui.widget-core",),
        "/var/capypkg/org.capyos.ui.desktop-session",
        "capy-ui-desktop-session", "1", 3, 3, 1,
        published_payload_sha256=(
            "2066d92ca32d0e56ab264eaa4b5f5c31"
            "a563e7ab8977745ce6efc8dd200cdb97"
        ),
        published_payload_size=1423360,
    ),
    ModuleSpec(
        "org.capyos.ui.widget-core",
        "CapyUI",
        "2.25.0",
        "org.capyos.ui.widget-core.bin",
        (),
        "/var/capypkg/org.capyos.ui.widget-core",
        "capy-ui-widget", "2.22", 3, 3, 1,
        published_payload_sha256=(
            "547ef9af818249c24ddce75271f6bf34"
            "fb001ee885a3a6eece29d07b755f010d"
        ),
        published_payload_size=1177600,
    ),
    ModuleSpec(
        "org.capyos.lang.runtime",
        "CapyLang",
        "0.1.13",
        "org.capyos.lang.runtime-0.1.13.bin",
        (),
        "/var/capypkg/org.capyos.lang.runtime",
        "capy-lang-host", "0", 3, 3, 0,
        published_payload_sha256=(
            "c6fa1866ce298f11dace478e600c86d1"
            "6c57542722e493cbe72a745771bdf6ab"
        ),
        published_payload_size=952320,
    ),
    ModuleSpec(
        "org.capyos.benchmark.harness",
        "CapyBenchmark",
        "0.0.12",
        "org.capyos.benchmark.harness-0.0.12.bin",
        (),
        "/var/capypkg/org.capyos.benchmark.harness",
        "capy-benchmark-report", "1", 3, 3, 0,
        published_payload_sha256=(
            "28ca2e27dabae47e0c3dc1acbce4dc84"
            "8d408ec5cf17c64fc788eab40e9daf9a"
        ),
        published_payload_size=61440,
    ),
)

MODULE_BY_ID: Final = {spec.module_id: spec for spec in MODULE_SPECS}
DEFAULT_REPOS: Final = tuple(dict.fromkeys(spec.repo for spec in MODULE_SPECS))


def validate_release_tag(tag: str) -> str:
    if not RELEASE_TAG_RE.fullmatch(tag):
        raise ValueError("release tag must match v<immutable-version>")
    return tag


def expected_release_tag(spec: ModuleSpec, capyos_release_tag: str | None) -> str:
    if spec.uses_capyos_release_tag:
        if capyos_release_tag is None:
            raise ValueError("CapyAI requires the CapyOS release tag")
        return validate_release_tag(capyos_release_tag)
    return f"v{spec.version}"


def expected_payload_url(
    spec: ModuleSpec, capyos_release_tag: str | None = None
) -> str:
    tag = expected_release_tag(spec, capyos_release_tag)
    return (
        f"https://github.com/{GITHUB_OWNER}/{spec.github_repo}/releases/"
        f"download/{tag}/{spec.asset}"
    )


def resolved_payload_url(spec: ModuleSpec) -> str:
    """Return the immutable ABI-token release URL emitted to CapyOS clients."""
    return (
        f"https://github.com/{GITHUB_OWNER}/CapyOS/releases/download/"
        f"{INDEX_RELEASE_TAG}/{spec.asset}"
    )


def pinned_payload_metadata(spec: ModuleSpec) -> tuple[str, int] | None:
    """Return immutable public metadata, or ``None`` for the new CapyAI asset."""
    if spec.uses_capyos_release_tag:
        return None
    digest = spec.published_payload_sha256
    size = spec.published_payload_size
    if digest is None or size is None:
        raise ValueError(f"{spec.module_id}: published payload metadata is missing")
    return digest, size


def release_tag_from_payload_url(spec: ModuleSpec, url: str) -> str:
    """Return the tag only when *url* is the exact expected GitHub asset URL."""
    try:
        parsed = urlsplit(url)
    except ValueError as exc:
        raise ValueError("malformed payload URL") from exc
    if (
        parsed.scheme != "https"
        or parsed.netloc != "github.com"
        or parsed.query
        or parsed.fragment
    ):
        raise ValueError("payload URL is not an immutable GitHub release URL")
    prefix = f"/{GITHUB_OWNER}/{spec.github_repo}/releases/download/"
    if not parsed.path.startswith(prefix):
        raise ValueError("payload URL repository is incorrect")
    remainder = parsed.path[len(prefix):]
    parts = remainder.split("/")
    if len(parts) != 2 or parts[1] != spec.asset:
        raise ValueError("payload URL asset is incorrect")
    tag = validate_release_tag(parts[0])
    if not spec.uses_capyos_release_tag and tag != f"v{spec.version}":
        raise ValueError("payload URL tag is incorrect")
    return tag


def _validate_catalog() -> None:
    if len(MODULE_SPECS) != EXPECTED_MODULE_COUNT:
        raise RuntimeError("official module catalog must contain nine entries")
    if len(MODULE_BY_ID) != len(MODULE_SPECS):
        raise RuntimeError("official module catalog contains duplicate IDs")
    ids = set(MODULE_BY_ID)
    assets: set[tuple[str, str, str]] = set()
    for spec in MODULE_SPECS:
        if spec.official != 1:
            raise RuntimeError(f"{spec.module_id}: official must be 1")
        if not spec.provides_abi or not spec.abi_version:
            raise RuntimeError(f"{spec.module_id}: ABI metadata is missing")
        if not 0 < spec.core_abi_min <= spec.core_abi_max:
            raise RuntimeError(f"{spec.module_id}: invalid core ABI range")
        if spec.known_good not in (0, 1):
            raise RuntimeError(f"{spec.module_id}: known_good must be 0 or 1")
        if len(spec.dependencies) > RUNTIME_MAX_DEPS:
            raise RuntimeError(f"{spec.module_id}: too many dependencies")
        missing = set(spec.dependencies) - ids
        if missing:
            raise RuntimeError(f"{spec.module_id}: dependency set is not closed")
        if spec.uses_capyos_release_tag:
            if (
                spec.published_payload_sha256 is not None
                or spec.published_payload_size is not None
            ):
                raise RuntimeError(
                    f"{spec.module_id}: release-local payload must not be pinned"
                )
        else:
            digest = spec.published_payload_sha256
            size = spec.published_payload_size
            if digest is None or not PAYLOAD_SHA256_RE.fullmatch(digest):
                raise RuntimeError(
                    f"{spec.module_id}: invalid published payload SHA-256"
                )
            if (
                not isinstance(size, int)
                or isinstance(size, bool)
                or not 1 <= size <= RUNTIME_PAYLOAD_BYTES
            ):
                raise RuntimeError(
                    f"{spec.module_id}: invalid published payload size"
                )
        identity = (spec.github_repo, spec.version, spec.asset)
        if identity in assets:
            raise RuntimeError(f"{spec.module_id}: duplicate release asset")
        assets.add(identity)
    if sum(spec.known_good for spec in MODULE_SPECS) != EXPECTED_RESOLVED_COUNT:
        raise RuntimeError("resolved catalog count does not match policy")


_validate_catalog()
