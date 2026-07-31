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
        "0.0.10",
        "org.capyos.agent.core-0.0.10.bin",
        (),
        "/var/capypkg/org.capyos.agent.core",
        published_payload_sha256=(
            "36b33570cdd648c1649ad0ef48c661da"
            "7281da4d978cb241ba813a3ef4db348e"
        ),
        published_payload_size=133120,
    ),
    ModuleSpec(
        "org.capyos.ai.assistant",
        "CapyAI",
        "0.2.1",
        "org.capyos.ai.assistant-0.2.1.bin",
        (),
        "/var/capypkg/org.capyos.ai.assistant",
        release_repo="CapyOS",
        uses_capyos_release_tag=True,
    ),
    ModuleSpec(
        "org.capyos.browser.core",
        "CapyBrowser",
        "0.6.7",
        "org.capyos.browser.core-0.6.7.bin",
        ("org.capyos.codecs.image-basic",),
        "/var/capypkg/org.capyos.browser.core",
        published_payload_sha256=(
            "e0ae30e5c2e5322b551283602ecb56c1"
            "11298aff9b1c2bfcfa08d524a893571c"
        ),
        published_payload_size=307200,
    ),
    ModuleSpec(
        "org.capyos.browser.text",
        "CapyBrowser",
        "0.6.7",
        "org.capyos.browser.text-0.6.7.bin",
        (),
        "/var/capypkg/org.capyos.browser.text",
        published_payload_sha256=(
            "e0ae30e5c2e5322b551283602ecb56c1"
            "11298aff9b1c2bfcfa08d524a893571c"
        ),
        published_payload_size=307200,
    ),
    ModuleSpec(
        "org.capyos.codecs.image-basic",
        "CapyCodecs",
        "0.0.12",
        "org.capyos.codecs.image-basic-0.0.12.bin",
        (),
        "/var/capypkg/org.capyos.codecs.image-basic",
        published_payload_sha256=(
            "f13dabc089abc933269c9a1e548aca1b"
            "f02ca64a024fc81f4ade3d1018806aea"
        ),
        published_payload_size=174080,
    ),
    ModuleSpec(
        "org.capyos.ui.desktop-session",
        "CapyUI",
        "2.24.2",
        "org.capyos.ui.desktop-session.bin",
        ("org.capyos.ui.widget-core",),
        "/var/capypkg/org.capyos.ui.desktop-session",
        published_payload_sha256=(
            "b2a827a1e09950927d0f08ba099b3650"
            "f9cb37ce5e7b453d1a421e8d5c374f91"
        ),
        published_payload_size=1413120,
    ),
    ModuleSpec(
        "org.capyos.ui.widget-core",
        "CapyUI",
        "2.24.2",
        "org.capyos.ui.widget-core.bin",
        (),
        "/var/capypkg/org.capyos.ui.widget-core",
        published_payload_sha256=(
            "513abc63aac309e2704a1c529e5bf285"
            "c5d7a59e46eb366860037c286e66e83f"
        ),
        published_payload_size=1177600,
    ),
    ModuleSpec(
        "org.capyos.lang.runtime",
        "CapyLang",
        "0.1.12",
        "org.capyos.lang.runtime-0.1.12.bin",
        (),
        "/var/capypkg/org.capyos.lang.runtime",
        published_payload_sha256=(
            "e41e24d53634bce8926c3eb0814cd63e"
            "959c9970caf8cc48a66fd554cc3d4ba7"
        ),
        published_payload_size=952320,
    ),
    ModuleSpec(
        "org.capyos.benchmark.harness",
        "CapyBenchmark",
        "0.0.11",
        "org.capyos.benchmark.harness-0.0.11.bin",
        (),
        "/var/capypkg/org.capyos.benchmark.harness",
        published_payload_sha256=(
            "143e6b1d2ed5a6001f78a8450020bd1a"
            "7ad04e6cdb4f4e45c448901ce7ef1ebc"
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


_validate_catalog()
