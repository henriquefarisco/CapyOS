#!/usr/bin/env python3
"""migrate_to_capyui.py — alpha.241 source migration helper.

Moves the desktop/window/apps C source subtrees from this CapyOS repo
into the sibling `CapyUI` repository so CapyUI becomes the canonical
publisher of the `org.capyos.ui.desktop-session` capypkg module.

Headers (.h) intentionally STAY in CapyOS/include/ because they are
the shared ABI contract between CapyOS-as-consumer and CapyUI-as-
producer. The CapyOS Makefile already adds `-Iinclude` so cross-
compiled sources living in `../CapyUI/src/...` resolve the same
contracts the kernel itself uses.

The script is idempotent:
  - if `dst` exists and `src` does not       -> already migrated, no-op;
  - if `src` exists and `dst` does not       -> copy src->dst, leave src
                                                 as a stub pointing to dst;
  - if BOTH exist                            -> compares contents; if equal
                                                 just stub the src, if
                                                 different prints a warning
                                                 and bails;
  - if NEITHER exists                        -> hard error.

Usage:
    python3 tools/scripts/migrate_to_capyui.py --dry-run    # preview
    python3 tools/scripts/migrate_to_capyui.py --apply      # do it

The script never deletes files outright; it overwrites the source
copy with a tiny stub C file that documents the new home and stays
out of the build (the CapyOS Makefile's cross-repo path takes
precedence once CAPYUI_DIR is set).

Designed for the alpha.241 wizard + module-bootstrap slice.
"""

from __future__ import annotations

import argparse
import os
import shutil
import sys
from pathlib import Path
from typing import Iterable, List, Tuple

# (relative_src_in_capyos, relative_dst_in_capyui) pairs.
# Headers (.h) stay in CapyOS/include/ on purpose; see module docstring.
FILE_MIGRATIONS: List[Tuple[str, str]] = [
    # --- gui/desktop subtree --------------------------------------
    ("src/gui/desktop/desktop.c",                 "src/desktop/desktop.c"),
    ("src/gui/desktop/desktop_runtime.c",         "src/desktop/desktop_runtime.c"),
    ("src/gui/desktop/desktop_mouse.c",           "src/desktop/desktop_mouse.c"),
    ("src/gui/desktop/desktop_icons.c",           "src/desktop/desktop_icons.c"),
    ("src/gui/desktop/desktop_icons_context.c",   "src/desktop/desktop_icons_context.c"),
    ("src/gui/desktop/desktop_smoke_readiness.c", "src/desktop/desktop_smoke_readiness.c"),
    ("src/gui/desktop/taskbar.c",                 "src/desktop/taskbar.c"),
    ("src/gui/desktop/taskbar_menu.c",            "src/desktop/taskbar_menu.c"),
    ("src/gui/desktop/taskbar_menu_input.c",      "src/desktop/taskbar_menu_input.c"),
    ("src/gui/desktop/internal/desktop_icons_internal.h",
     "src/desktop/internal/desktop_icons_internal.h"),
    ("src/gui/desktop/internal/taskbar_internal.h",
     "src/desktop/internal/taskbar_internal.h"),
    # --- gui/window subtree ---------------------------------------
    ("src/gui/window/window_manager.c",      "src/window/window_manager.c"),
    ("src/gui/window/window_dispatcher.c",   "src/window/window_dispatcher.c"),
    ("src/gui/window/notification.c",        "src/window/notification.c"),
    # --- apps subtree ---------------------------------------------
    ("src/apps/calculator.c",                "src/apps/calculator.c"),
    ("src/apps/file_manager.c",              "src/apps/file_manager.c"),
    ("src/apps/file_manager_view.c",         "src/apps/file_manager_view.c"),
    ("src/apps/file_manager_dnd.c",          "src/apps/file_manager_dnd.c"),
    ("src/apps/text_editor.c",               "src/apps/text_editor.c"),
    ("src/apps/task_manager.c",              "src/apps/task_manager.c"),
    ("src/apps/settings.c",                  "src/apps/settings.c"),
    ("src/apps/settings_view.c",             "src/apps/settings_view.c"),
    ("src/apps/settings_actions.c",          "src/apps/settings_actions.c"),
    ("src/apps/internal/file_manager_internal.h",
     "src/apps/internal/file_manager_internal.h"),
    ("src/apps/internal/settings_internal.h",
     "src/apps/internal/settings_internal.h"),
]

STUB_BANNER = (
    "/* MIGRATED: this source moved to CapyUI/{dst} as part of the\n"
    " * alpha.241 desktop-session hygiene slice. The CapyOS Makefile\n"
    " * compiles the canonical copy from $(CAPYUI_DIR)/{dst} via the\n"
    " * cross-repo build path; this file stays only as a forwarding\n"
    " * stub. Safe to delete once `make all64 PROFILE=full` succeeds\n"
    " * end-to-end with the sibling repo in place.\n"
    " *\n"
    " * If you came here looking for the implementation, open the\n"
    " * file referenced above.\n"
    " */\n"
)


class MigrationError(RuntimeError):
    pass


def find_repo_roots(script_path: Path) -> Tuple[Path, Path]:
    """Return (capyos_root, capyui_root)."""
    # tools/scripts/<this> -> tools/ -> capyos
    capyos = script_path.resolve().parent.parent.parent
    capyui = capyos.parent / "CapyUI"
    if not capyos.joinpath("Makefile").is_file():
        raise MigrationError(
            f"CapyOS root not found (no Makefile at {capyos}).")
    if not capyui.is_dir():
        raise MigrationError(
            f"Sibling CapyUI repo not found at {capyui}. "
            "Clone it next to CapyOS first.")
    return capyos, capyui


def stub_for_path(dst_rel: str) -> str:
    """Compose the stub body for an overwritten source file."""
    return STUB_BANNER.replace("{dst}", dst_rel)


def needs_copy(src: Path, dst: Path) -> bool:
    if not dst.exists():
        return True
    if src.exists():
        return src.read_bytes() != dst.read_bytes()
    return False


def is_stub(path: Path) -> bool:
    if not path.is_file():
        return False
    try:
        head = path.read_text(encoding="utf-8", errors="replace")[:256]
    except OSError:
        return False
    return "/* MIGRATED:" in head


def ensure_parent(path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)


def process_pair(capyos: Path, capyui: Path,
                 src_rel: str, dst_rel: str,
                 apply: bool, log: List[str]) -> None:
    src = capyos / src_rel
    dst = capyui / dst_rel
    src_exists = src.is_file()
    dst_exists = dst.is_file()
    src_is_stub = is_stub(src) if src_exists else False

    if not src_exists and not dst_exists:
        raise MigrationError(
            f"Neither {src} nor {dst} exist; migration target lost.")

    if dst_exists and (not src_exists or src_is_stub):
        log.append(f"  [skip] {src_rel} already migrated")
        return

    if src_exists and not dst_exists:
        log.append(f"  [copy] {src_rel} -> CapyUI/{dst_rel}")
        if apply:
            ensure_parent(dst)
            shutil.copy2(src, dst)
            stub_path = src
            stub_body = stub_for_path(dst_rel)
            stub_path.write_text(stub_body, encoding="utf-8")
        return

    # both exist; verify they are identical (or src is the stub already)
    if src_exists and dst_exists:
        if src.read_bytes() == dst.read_bytes():
            log.append(f"  [stub] {src_rel} matches CapyUI; replacing with stub")
            if apply:
                src.write_text(stub_for_path(dst_rel), encoding="utf-8")
            return
        raise MigrationError(
            f"DIVERGED: {src_rel} differs from {dst_rel}. "
            "Reconcile manually before re-running.")


def parse_args(argv: List[str]) -> argparse.Namespace:
    p = argparse.ArgumentParser(description=__doc__)
    g = p.add_mutually_exclusive_group(required=True)
    g.add_argument("--dry-run", action="store_true",
                   help="print what would happen, do not touch files")
    g.add_argument("--apply", action="store_true",
                   help="execute the migration")
    p.add_argument("--capyos", type=Path, default=None,
                   help="override CapyOS root (default: autodetect)")
    p.add_argument("--capyui", type=Path, default=None,
                   help="override CapyUI root (default: ../CapyUI)")
    return p.parse_args(argv)


def main(argv: List[str]) -> int:
    args = parse_args(argv)
    try:
        auto_capyos, auto_capyui = find_repo_roots(Path(__file__))
    except MigrationError as exc:
        print(f"[error] {exc}", file=sys.stderr)
        return 2
    capyos = args.capyos.resolve() if args.capyos else auto_capyos
    capyui = args.capyui.resolve() if args.capyui else auto_capyui

    print(f"[info] CapyOS root: {capyos}")
    print(f"[info] CapyUI root: {capyui}")
    print(f"[info] mode: {'apply' if args.apply else 'dry-run'}")
    print()

    log: List[str] = []
    errors = 0
    for src_rel, dst_rel in FILE_MIGRATIONS:
        try:
            process_pair(capyos, capyui, src_rel, dst_rel,
                         apply=args.apply, log=log)
        except MigrationError as exc:
            log.append(f"  [error] {src_rel}: {exc}")
            errors += 1

    print("Plan:")
    for line in log:
        print(line)
    print()
    if errors:
        print(f"[error] {errors} divergence(s) detected; nothing else applied",
              file=sys.stderr)
        return 1
    if args.apply:
        print("[ok] migration applied. Verify with:")
        print("       make layout-audit")
        print("       make all64 PROFILE=full")
        print()
        print("Once the build is green you can delete the forwarding stubs:")
        print("       find src/gui/desktop src/gui/window src/apps -name '*.c' \\")
        print("         -exec grep -l '/* MIGRATED:' {} + | xargs rm")
    else:
        print("[ok] dry-run complete; re-run with --apply to perform the migration.")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
