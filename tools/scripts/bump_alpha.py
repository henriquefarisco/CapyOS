#!/usr/bin/env python3
"""Deterministic CapyOS alpha version bump.

Single source of truth for the repetitive, drift-prone version touchpoints that
`tools/scripts/audit_version_manifest.py` (`make version-audit`) enforces, plus
`docs/plans/STATUS.md`. Replaces the hand-written one-off `.bumpNNN.py` scripts.

What it updates (all byte-safe, with per-edit replacement-count assertions that
abort on any mismatch -- so a stale anchor fails loudly instead of silently
half-applying):

  - VERSION.yaml         : channels.alpha.current, .extended, .current_summary,
                           and a new newest-first entry under `history:`.
  - include/core/version.h : CAPYOS_VERSION_PRERELEASE / _EXTENDED / _FULL /
                           _ALPHA (4 macros).
  - README.md            : the "Versao de referencia:" line.
  - docs/plans/STATUS.md : the executive version line + the sister-repo section
                           header ("estado em alpha.NNN").
  - docs/releases/capyos-<extended>.md : scaffolded from docs/releases/_template.md
                           (only if absent) so version-audit's release-note check
                           passes; the author then fills in the body.

The CURRENT version is auto-detected from VERSION.yaml (channels.alpha.current +
.extended), so the caller only supplies the new alpha number and a summary.

Out of scope (still manual, by design -- they are paired-release / contract
specific, not every-release toil): sister repo VERSION bumps, the
compatibility-matrix rows, master-plan / readiness / architecture doc prose, and
the STATUS chronological "Atualizacao alpha.NNN" narrative bullet. The tool
prints a reminder for these.

Usage:
  python3 tools/scripts/bump_alpha.py --to 283 --summary-file /tmp/sum.txt
  python3 tools/scripts/bump_alpha.py --to 283 --summary "one-line summary" --dry-run
  make bump-alpha TO=283 SUMMARY="one-line summary"   # then runs version-audit
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]


def read(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def detect_current(version_yaml: str) -> tuple[str, int, str, str]:
    """Return (current, current_num, extended, date) from channels.alpha."""
    cur = re.search(r"^    current:\s*(0\.\d+\.\d+-alpha\.(\d+))\s*$",
                    version_yaml, re.MULTILINE)
    ext = re.search(r"^    extended:\s*(0\.\d+\.\d+-alpha\.\d+\+(\d{8}))\s*$",
                    version_yaml, re.MULTILINE)
    if not cur or not ext:
        raise SystemExit("bump_alpha: could not parse channels.alpha "
                         "current/extended from VERSION.yaml")
    return cur.group(1), int(cur.group(2)), ext.group(1), ext.group(2)


class Editor:
    """Applies counted-assert string edits across files; supports dry-run."""

    def __init__(self, dry_run: bool) -> None:
        self.dry_run = dry_run
        self.pending: list[tuple[Path, str]] = []
        self.ok = True

    def edit(self, path: Path, transforms: list[tuple[str, object, int]]) -> None:
        text = read(path)
        for desc, fn, want in transforms:
            text, got = fn(text)
            mark = "OK" if got == want else "MISMATCH"
            print(f"  [{mark}] {path.name}: {desc}: {got} (want {want})")
            if got != want:
                self.ok = False
                return
        self.pending.append((path, text))

    def commit(self) -> None:
        if not self.ok:
            raise SystemExit("bump_alpha: ABORTED -- an edit count did not match "
                             "(stale anchor or already bumped?). Nothing written.")
        if self.dry_run:
            print("[dry-run] no files written.")
            return
        for path, text in self.pending:
            path.write_text(text, encoding="utf-8")
            print(f"wrote {path.relative_to(REPO)}")


def sub_count(old: str, new: str):
    return lambda t: (t.replace(old, new), t.count(old))


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--to", type=int, required=True,
                    help="new alpha number (must be > current)")
    g = ap.add_mutually_exclusive_group(required=True)
    g.add_argument("--summary", help="one-line current_summary / history summary")
    g.add_argument("--summary-file", help="read the summary from this file")
    ap.add_argument("--date", help="build date YYYYMMDD (default: keep current)")
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()

    version_yaml_path = REPO / "VERSION.yaml"
    cur, cur_num, cur_ext, cur_date = detect_current(read(version_yaml_path))
    base = cur.rsplit("-alpha.", 1)[0]            # "0.8.0"
    date = args.date or cur_date
    if not re.fullmatch(r"\d{8}", date):
        raise SystemExit(f"bump_alpha: --date must be YYYYMMDD, got {date!r}")
    if args.to <= cur_num:
        raise SystemExit(f"bump_alpha: --to {args.to} must be > current {cur_num}")

    summary = (Path(args.summary_file).read_text(encoding="utf-8").strip()
               if args.summary_file else args.summary).strip()
    if '"' in summary:
        raise SystemExit("bump_alpha: summary must not contain a double quote "
                         "(it is embedded in a YAML double-quoted scalar).")

    new = f"{base}-alpha.{args.to}"               # 0.8.0-alpha.283
    new_ext = f"{new}+{date}"                      # 0.8.0-alpha.283+20260617
    hist_date = f"{date[0:4]}-{date[4:6]}-{date[6:8]}"
    print(f"bump: {cur_ext}  ->  {new_ext}")

    ed = Editor(args.dry_run)

    # --- VERSION.yaml ---
    hist_anchor = f"  history:\n      - version: {cur_ext}"
    hist_new = (f"  history:\n"
                f"      - version: {new_ext}\n"
                f"        date: {hist_date}\n"
                f'        summary: "{summary}"\n'
                f"      - version: {cur_ext}")
    ed.edit(version_yaml_path, [
        ("current", sub_count(f"current: {cur}\n", f"current: {new}\n"), 1),
        ("extended", sub_count(f"extended: {cur_ext}\n", f"extended: {new_ext}\n"), 1),
        ("current_summary",
         lambda t: re.subn(r'(?m)^    current_summary: ".*"$',
                           '    current_summary: "' + summary + '"', t, count=1), 1),
        ("history insert", sub_count(hist_anchor, hist_new), 1),
    ])

    # --- include/core/version.h --- (EXTENDED/FULL/ALPHA share "0.8.0-alpha.N";
    # PRERELEASE is the bare "alpha.N")
    ed.edit(REPO / "include/core/version.h", [
        ("0.8.0-alpha.N x3", sub_count(cur, new), 3),
        ("PRERELEASE", sub_count(f'"alpha.{cur_num}"', f'"alpha.{args.to}"'), 1),
    ])

    # --- README.md ---
    old_ref = (f"Versao de referencia: `{cur}` (build `{cur_ext}`; "
               f"canal `alpha`; ver `VERSION.yaml`)")
    new_ref = (f"Versao de referencia: `{new}` (build `{new_ext}`; "
               f"canal `alpha`; ver `VERSION.yaml`)")
    ed.edit(REPO / "README.md", [("ref line", sub_count(old_ref, new_ref), 1)])

    # --- docs/plans/STATUS.md ---
    ed.edit(REPO / "docs/plans/STATUS.md", [
        ("version line", sub_count(cur_ext, new_ext), 1),
        ("sister header", sub_count(f"estado em alpha.{cur_num}",
                                    f"estado em alpha.{args.to}"), 1),
    ])

    ed.commit()

    # --- release note scaffold (idempotent) ---
    note = REPO / "docs/releases" / f"capyos-{new_ext}.md"
    if note.exists():
        print(f"[skip] release note already exists: {note.relative_to(REPO)}")
    elif args.dry_run:
        print(f"[dry-run] would scaffold release note: {note.relative_to(REPO)}")
    else:
        tmpl_path = REPO / "docs/releases/_template.md"
        tmpl = read(tmpl_path) if tmpl_path.exists() else (
            "# CapyOS {VERSION}\n\n**Data:** {HDATE}\n**Versao:** `{VERSION}`\n\n"
            "## Resumo\n\n{SUMMARY}\n\n_Build: `{VERSION}`_\n")
        note.write_text(
            tmpl.replace("{VERSION}", new_ext)
                .replace("{HDATE}", hist_date)
                .replace("{SUMMARY}", summary),
            encoding="utf-8")
        print(f"scaffolded {note.relative_to(REPO)}  (fill in the body)")

    print("\nNext steps:")
    print("  1. Fill in the release note body.")
    print("  2. make version-audit   (self-check; `make bump-alpha` runs it)")
    print("  3. PAIRED/contract releases only: bump sister VERSION + the "
          "compatibility-matrix row + STATUS sister-table + master-plan prose.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
