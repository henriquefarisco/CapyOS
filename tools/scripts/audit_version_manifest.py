#!/usr/bin/env python3
"""Audit the CapyOS release version declared across project metadata."""

from __future__ import annotations

import re
import sys
from pathlib import Path


MODULES_INDEX_URL_TEMPLATE = (
    "https://github.com/henriquefarisco/CapyOS/releases/download/"
    "modules-{token}/modules-index.txt"
)


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


def require_unique_match(pattern: str, text: str, label: str) -> str:
    matches = list(re.finditer(pattern, text, flags=re.MULTILINE))
    if not matches:
        raise RuntimeError(f"campo ausente: {label}")
    if len(matches) != 1:
        raise RuntimeError(f"campo duplicado: {label}")
    return matches[0].group(1)


def require_contains(text: str, needle: str, label: str) -> None:
    if needle not in text:
        raise RuntimeError(f"{label} nao contem {needle!r}")


def require_channel_field(version_yaml: str, channel: str, field: str) -> str:
    pattern = (
        rf"^  {re.escape(channel)}:\n"
        rf"(?P<body>(?:    .*\n)+?)(?=^  [A-Za-z0-9_-]+:|^[A-Za-z0-9_-]+:|\Z)"
    )
    match = re.search(pattern, version_yaml, flags=re.MULTILINE)
    if not match:
        raise RuntimeError(f"canal ausente: channels.{channel}")
    return require_match(
        rf"^\s*{re.escape(field)}:\s*([^\s]+)\s*$",
        match.group("body"),
        f"channels.{channel}.{field}",
    )


def require_top_level_mapping_field(
    version_yaml: str, section: str, field: str
) -> str:
    section_matches = list(
        re.finditer(
            rf"^{re.escape(section)}:[ \t]*(?:#.*)?$",
            version_yaml,
            flags=re.MULTILINE,
        )
    )
    label = f"{section}.{field}"
    if not section_matches:
        raise RuntimeError(f"secao ausente: {section}")
    if len(section_matches) != 1:
        raise RuntimeError(f"secao duplicada: {section}")

    body_lines: list[str] = []
    for line in version_yaml[section_matches[0].end() :].splitlines():
        if line.strip() and not line.startswith((" ", "\t", "#")):
            break
        body_lines.append(line)

    raw_value = require_unique_match(
        rf"^  {re.escape(field)}:[ \t]*(.*?)[ \t]*$",
        "\n".join(body_lines),
        label,
    ).strip()
    if len(raw_value) >= 2 and raw_value[0] in {'"', "'"}:
        if raw_value[-1] != raw_value[0]:
            raise RuntimeError(f"valor YAML malformado: {label}")
        raw_value = raw_value[1:-1]
    elif " #" in raw_value:
        raw_value = raw_value.split(" #", 1)[0].rstrip()
    if not raw_value or any(char.isspace() for char in raw_value):
        raise RuntimeError(f"valor YAML invalido: {label}")
    return raw_value


def canonical_modules_index_url(token: str) -> str:
    return MODULES_INDEX_URL_TEMPLATE.format(token=token)


def audit_modules_index_contract(
    version_yaml: str, modules_c: str, makefile: str,
    compatibility_matrix: str | None = None,
) -> list[str]:
    """Validate the ABI-token module index contract and every literal copy."""

    errors: list[str] = []
    expected_token: str | None = None
    expected_url: str | None = None

    try:
        if compatibility_matrix is None:
            declared = require_top_level_mapping_field(
                version_yaml, "modules_index", "token"
            )
            match = re.fullmatch(r"capyos-base-v([1-9][0-9]*)", declared)
            if not match:
                raise RuntimeError("modules_index.token deve ser capyos-base-v<N>")
            expected_token = declared
        else:
            abi_version = require_unique_match(
                r"^\|[ \t]*`capyos-base`[ \t]*\|[ \t]*CapyOS[ \t]*\|[ \t]*v([1-9][0-9]*)[ \t]*\|",
                compatibility_matrix,
                "compatibility-matrix capyos-base ABI",
            )
            expected_token = f"capyos-base-v{abi_version}"
        expected_url = canonical_modules_index_url(expected_token)
    except RuntimeError as exc:
        errors.append(str(exc))

    try:
        declared_token = require_top_level_mapping_field(
            version_yaml, "modules_index", "token"
        )
        if expected_token is not None and declared_token != expected_token:
            errors.append(
                f"modules_index.token={declared_token} difere da ABI "
                f"autoritativa={expected_token}"
            )
    except RuntimeError as exc:
        errors.append(str(exc))

    try:
        declared_url = require_top_level_mapping_field(
            version_yaml, "modules_index", "url"
        )
        if expected_url is not None and declared_url != expected_url:
            errors.append(
                f"modules_index.url={declared_url} difere da URL canonica "
                f"derivada do token={expected_url}"
            )
    except RuntimeError as exc:
        errors.append(str(exc))

    try:
        runtime_url = require_unique_match(
            r"^[ \t]*#define[ \t]+CAPYOS_DEFAULT_MODULES_INDEX_URL"
            r"(?:[ \t]*\\[ \t]*\r?\n)?[ \t]*\"([^\"\r\n]+)\"",
            modules_c,
            "CAPYOS_DEFAULT_MODULES_INDEX_URL",
        )
        if expected_url is not None and runtime_url != expected_url:
            errors.append(
                f"CAPYOS_DEFAULT_MODULES_INDEX_URL={runtime_url} difere da "
                f"URL canonica={expected_url}"
            )
    except RuntimeError as exc:
        errors.append(str(exc))

    try:
        smoke_url = require_unique_match(
            r"^[ \t]*SMOKE_X64_MODULES_INDEX_URL[ \t]*\?="
            r"[ \t]*([^\s#]+)[ \t]*(?:#.*)?$",
            makefile,
            "SMOKE_X64_MODULES_INDEX_URL",
        )
        if expected_url is not None and smoke_url != expected_url:
            errors.append(
                f"SMOKE_X64_MODULES_INDEX_URL={smoke_url} difere da "
                f"URL canonica={expected_url}"
            )
    except RuntimeError as exc:
        errors.append(str(exc))

    return errors


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

    # The immutable modules-index release pin is duplicated only where a
    # consumer needs a literal. Audit all copies against stable.extended so a
    # version bump cannot leave first boot or the network smoke on stale data.
    modules_c = read_text(repo / "src/config/first_boot/modules.c")
    makefile = read_text(repo / "Makefile")
    compatibility_matrix = read_text(
        repo / "docs/reference/integration/compatibility-matrix.md"
    )
    errors.extend(
        audit_modules_index_contract(
            version_yaml, modules_c, makefile, compatibility_matrix
        )
    )

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
