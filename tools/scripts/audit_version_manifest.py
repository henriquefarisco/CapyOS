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


def main() -> int:
    repo = Path(__file__).resolve().parents[2]

    version_yaml = read_text(repo / "VERSION.yaml")
    header = read_text(repo / "include/core/version.h")
    readme = read_text(repo / "README.md")

    current = require_match(r"^\s*current:\s*([^\s]+)\s*$", version_yaml, "channels.alpha.current")
    extended = require_match(r"^\s*extended:\s*([^\s]+)\s*$", version_yaml, "channels.alpha.extended")

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

    errors: list[str] = []
    if header_extended != current:
        errors.append(f"CAPYOS_VERSION_EXTENDED={header_extended} difere de alpha.current={current}")
    if header_alpha != current:
        errors.append(f"CAPYOS_VERSION_ALPHA={header_alpha} difere de alpha.current={current}")
    if header_full != extended:
        errors.append(f"CAPYOS_VERSION_FULL={header_full} difere de alpha.extended={extended}")

    try:
        require_contains(readme, f"Versao de referencia: `{current}`", "README.md")
        require_contains(readme, f"docs/screenshots/{current}/", "README.md")
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

    if errors:
        print("[err] auditoria de versao encontrou divergencias:")
        for error in errors:
            print(f"- {error}")
        return 1

    print(f"[ok] versao alpha alinhada: current={current} extended={extended}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
