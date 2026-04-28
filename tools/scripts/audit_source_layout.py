#!/usr/bin/env python3
"""Audit CapyOS source layout for monoliths and boundary drift.

The goal is not to fail the current tree immediately. It gives us a stable,
repeatable map for incremental clean-code refactors.
"""

from __future__ import annotations

import argparse
import re
from collections import Counter, defaultdict
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable

INCLUDE_RE = re.compile(r'#\s*include\s+"([^"]+)"')


DEFAULT_ROOTS = ("src", "include", "tests", "tools", ".")
GENERATED_OR_VENDOR = (
    "build/",
    "third_party/",
    ".git/",
    ".codex/",
    ".claude/",
)

STATIC_DATA_EXCEPTIONS = {
    Path("src/security/tls_trust_anchors.c"),
}

EXT_LANGUAGE = {
    ".c": "c",
    ".h": "c-header",
    ".inc": "c-fragment",
    ".S": "asm",
    ".s": "asm",
    ".py": "python",
    ".sh": "shell",
    ".ld": "linker-script",
    ".md": "markdown",
    ".yaml": "yaml",
    ".yml": "yaml",
    ".json": "json",
    ".txt": "text",
}


@dataclass
class FileInfo:
    path: Path
    language: str
    lines: int


def is_ignored(path: Path) -> bool:
    text = path.as_posix()
    return any(text.startswith(prefix) or f"/{prefix}" in text for prefix in GENERATED_OR_VENDOR)


def iter_files(repo: Path, roots: Iterable[str]) -> Iterable[Path]:
    for root_name in roots:
        root = repo / root_name
        if not root.exists():
            continue
        iterator = root.iterdir() if root_name == "." else root.rglob("*")
        for path in iterator:
            if path.is_file() and not is_ignored(path.relative_to(repo)):
                if root_name == "." and path.suffix not in {".sh", ".py"}:
                    continue
                yield path


def count_lines(path: Path) -> int:
    try:
        with path.open("r", encoding="utf-8", errors="ignore") as handle:
            return sum(1 for _ in handle)
    except OSError:
        return 0


def collect(repo: Path, roots: Iterable[str]) -> list[FileInfo]:
    infos: list[FileInfo] = []
    for path in iter_files(repo, roots):
        language = EXT_LANGUAGE.get(path.suffix, "other")
        infos.append(FileInfo(path.relative_to(repo), language, count_lines(path)))
    return infos


def is_static_data_exception(path: Path) -> bool:
    return path in STATIC_DATA_EXCEPTIONS


def module_name(path: Path) -> str:
    parts = path.parts
    if not parts:
        return "."
    if parts[0] == "src" and len(parts) >= 2:
        if parts[1] == "arch" and len(parts) >= 3:
            return "/".join(parts[:3])
        return "/".join(parts[:2])
    if parts[0] == "include" and len(parts) >= 2:
        return "/".join(parts[:2])
    if parts[0] == "tools" and len(parts) >= 2:
        return "/".join(parts[:2])
    return parts[0]


def owning_module_of_internal_include(include_path: str) -> str | None:
    """Return the src/ module that owns an internal header, or None if not internal."""
    if "/internal/" not in include_path:
        return None
    parts = include_path.split("/")
    try:
        idx = parts.index("internal")
    except ValueError:
        return None
    prefix = parts[:idx]
    if not prefix:
        return None
    if prefix[0] == "arch" and len(prefix) >= 2:
        return "src/" + "/".join(prefix[:3])
    return "src/" + prefix[0]


def check_internal_boundary(repo: Path, infos: list[FileInfo]) -> list[str]:
    """Detect files that include another module's internal/ headers."""
    violations: list[str] = []
    for info in infos:
        if info.language not in {"c", "c-header", "c-fragment"}:
            continue
        if not info.path.parts or info.path.parts[0] != "src":
            continue
        file_mod = module_name(info.path)
        try:
            content = (repo / info.path).read_text(encoding="utf-8", errors="ignore")
        except OSError:
            continue
        for m in INCLUDE_RE.finditer(content):
            inc = m.group(1)
            if inc.startswith("."):
                continue
            owner = owning_module_of_internal_include(inc)
            if owner is None or owner == file_mod:
                continue
            violations.append(
                f"cross-module internal include: {info.path} "
                f"includes \"{inc}\" (owned by {owner})"
            )
    return violations


def print_section(title: str) -> None:
    print(f"\n## {title}")


def language_family(language: str) -> str:
    if language in {"c", "c-header", "c-fragment"}:
        return "c-family"
    return language


def main() -> int:
    parser = argparse.ArgumentParser(description="Audit source organization.")
    parser.add_argument("--repo", default=".", help="Repository root")
    parser.add_argument("--max-c-lines", type=int, default=900)
    parser.add_argument("--max-test-lines", type=int, default=900)
    parser.add_argument("--top", type=int, default=20)
    parser.add_argument("--strict", action="store_true", help="Exit non-zero when warnings exist")
    args = parser.parse_args()

    repo = Path(args.repo).resolve()
    infos = collect(repo, DEFAULT_ROOTS)
    warnings: list[str] = []

    by_lang = Counter(info.language for info in infos)
    by_module_lines: dict[str, int] = defaultdict(int)
    by_module_files: dict[str, int] = defaultdict(int)
    by_module_langs: dict[str, Counter[str]] = defaultdict(Counter)

    for info in infos:
        mod = module_name(info.path)
        by_module_lines[mod] += info.lines
        by_module_files[mod] += 1
        by_module_langs[mod][info.language] += 1

    print("# CapyOS Source Layout Audit")
    print(f"Repo: {repo}")
    print(f"Files scanned: {len(infos)}")

    print_section("Languages")
    for lang, count in by_lang.most_common():
        print(f"- {lang}: {count}")

    print_section("Largest Modules")
    for mod, lines in sorted(by_module_lines.items(), key=lambda item: item[1], reverse=True)[: args.top]:
        langs = ", ".join(f"{lang}:{count}" for lang, count in by_module_langs[mod].most_common())
        print(f"- {mod}: {lines} lines, {by_module_files[mod]} files ({langs})")

    print_section("Largest Files")
    largest = sorted(infos, key=lambda info: info.lines, reverse=True)[: args.top]
    for info in largest:
        print(f"- {info.path}: {info.lines} lines [{info.language}]")

    print_section("Warnings")
    for info in infos:
        if info.language in {"c", "c-header", "c-fragment"} and not is_static_data_exception(info.path):
            limit = args.max_test_lines if info.path.parts and info.path.parts[0] == "tests" else args.max_c_lines
            if info.lines > limit:
                warnings.append(f"monolith: {info.path} has {info.lines} lines (limit {limit})")
        if (info.path.parts and info.path.parts[0] == "src" and
                info.path.suffix == ".h" and "internal" not in info.path.parts):
            warnings.append(f"internal header in src: {info.path} (prefer include/<module>/ or src/<module>/internal/)")

    for mod, langs in sorted(by_module_langs.items()):
        code_langs = {
            language_family(lang)
            for lang in langs
            if lang in {"c", "c-header", "c-fragment", "asm", "python", "shell"}
        }
        if len(code_langs) > 2 and not mod.startswith("tools"):
            warnings.append(f"mixed implementation languages in {mod}: {', '.join(sorted(code_langs))}")

    warnings.extend(check_internal_boundary(repo, infos))

    if warnings:
        for warning in warnings:
            print(f"- {warning}")
    else:
        print("- none")

    print_section("Recommended Next Splits")
    for info in largest:
        if (info.language in {"c", "c-header", "c-fragment"} and
                info.lines > args.max_c_lines and
                not is_static_data_exception(info.path)):
            print(f"- split {info.path} by responsibility before adding new features")

    return 1 if args.strict and warnings else 0


if __name__ == "__main__":
    raise SystemExit(main())
