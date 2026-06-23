#!/usr/bin/env python3
"""Audit the CapyOS release version declared across project metadata."""

from __future__ import annotations

import re
import sys
from pathlib import Path


def read_text(path: Path) -> str:
    try:
        return path.read_text(encoding="utf-8")
    except OSError as exc:
        raise RuntimeError(f"nao foi possivel ler {path}: {exc}") from exc


def require_match(pattern: str, text: str, label: str) -> str:
    match = re.search(pattern, text, flags=re.MULTILINE)
    if not match:
        raise RuntimeError(f"campo ausente: {label}")
    return match.group(1)


def require_contains(text: str, needle: str, label: str) -> None:
    if needle not in text:
        raise RuntimeError(f"{label} nao contem {needle!r}")


def require_channel_field(version_yaml: str, channel: str, field: str) -> str:
    pattern = (
        rf"^  {re.escape(channel)}:\n"
        rf"(?P<body>(?:    .*\n)+?)(?=^  [A-Za-z0-9_-]+:|\Z)"
    )
    match = re.search(pattern, version_yaml, flags=re.MULTILINE)
    if not match:
        raise RuntimeError(f"canal ausente: channels.{channel}")
    return require_match(
        rf"^\s*{re.escape(field)}:\s*([^\s]+)\s*$",
        match.group("body"),
        f"channels.{channel}.{field}",
    )


def main() -> int:
    repo = Path(__file__).resolve().parents[2]

    version_yaml = read_text(repo / "VERSION.yaml")
    header = read_text(repo / "include/core/version.h")
    readme = read_text(repo / "README.md")

    header_channel = require_match(
        r'^\s*#define\s+CAPYOS_VERSION_CHANNEL\s+"([^"]+)"',
        header,
        "CAPYOS_VERSION_CHANNEL",
    )
    current = require_channel_field(version_yaml, header_channel, "current")
    extended = require_channel_field(version_yaml, header_channel, "extended")

    header_extended = require_match(
        r'^\s*#define\s+CAPYOS_VERSION_EXTENDED\s+"([^"]+)"',
        header,
        "CAPYOS_VERSION_EXTENDED",
    )
    header_full = require_match(
        r'^\s*#define\s+CAPYOS_VERSION_FULL\s+"([^"]+)"',
        header,
        "CAPYOS_VERSION_FULL",
    )
    header_alpha = require_match(
        r'^\s*#define\s+CAPYOS_VERSION_ALPHA\s+"([^"]+)"',
        header,
        "CAPYOS_VERSION_ALPHA",
    )
    header_stable = require_match(
        r'^\s*#define\s+CAPYOS_VERSION_STABLE\s+"([^"]+)"',
        header,
        "CAPYOS_VERSION_STABLE",
    )

    errors: list[str] = []
    if header_extended != current:
        errors.append(
            f"CAPYOS_VERSION_EXTENDED={header_extended} difere de "
            f"{header_channel}.current={current}"
        )
    if header_alpha != current:
        if header_channel == "alpha":
            errors.append(f"CAPYOS_VERSION_ALPHA={header_alpha} difere de alpha.current={current}")
    if header_stable != current:
        if header_channel == "stable":
            errors.append(f"CAPYOS_VERSION_STABLE={header_stable} difere de stable.current={current}")
    if header_full != extended:
        errors.append(
            f"CAPYOS_VERSION_FULL={header_full} difere de "
            f"{header_channel}.extended={extended}"
        )

    try:
        require_contains(readme, f"Versao de referencia: `{current}`", "README.md")
        require_contains(readme, "docs/screenshots/CapyUI/", "README.md")
    except RuntimeError as exc:
        errors.append(str(exc))

    release_note = repo / "docs/releases" / f"capyos-{extended}.md"
    if not release_note.exists():
        errors.append(f"release note ausente: {release_note.relative_to(repo)}")
    else:
        note_text = read_text(release_note)
        if not note_text.startswith(f"# CapyOS {extended}\n"):
            errors.append(f"cabecalho da release note nao declara CapyOS {extended}")
        try:
            require_contains(note_text, f"`{extended}`", str(release_note.relative_to(repo)))
        except RuntimeError as exc:
            errors.append(str(exc))

    # Modules-index pin: the first-boot default in modules.c MUST match the
    # single-sourced pin in VERSION.yaml (modules_index.url). Makes the
    # otherwise-buried C constant explicit + audited and fails on drift (the
    # fragility class behind the alpha.286 install bug). Full resolve-at-publish
    # (signed token-addressed index) is sequenced to Etapa 8.
    try:
        declared_url = require_match(
            r'^modules_index:\n(?:[ \t].*\n)*?[ \t]+url:\s*"([^"]+)"',
            version_yaml,
            "modules_index.url",
        )
        modules_c = read_text(repo / "src/config/first_boot/modules.c")
        code_url = require_match(
            r'#\s*define\s+CAPYOS_DEFAULT_MODULES_INDEX_URL\s*\\[^\n]*\n\s*"([^"]+)"',
            modules_c,
            "CAPYOS_DEFAULT_MODULES_INDEX_URL",
        )
        if declared_url != code_url:
            errors.append(
                f"modules_index.url (VERSION.yaml)={declared_url} difere do "
                f"CAPYOS_DEFAULT_MODULES_INDEX_URL (modules.c)={code_url}"
            )
    except RuntimeError as exc:
        errors.append(str(exc))

    if errors:
        print("[err] auditoria de versao encontrou divergencias:")
        for error in errors:
            print(f"- {error}")
        return 1

    print(
        f"[ok] versao {header_channel} alinhada: "
        f"current={current} extended={extended}"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
