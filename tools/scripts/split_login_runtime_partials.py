#!/usr/bin/env python3
"""
tools/scripts/split_login_runtime_partials.py

Extract struct definition groups from include/auth/login_runtime.h
into dedicated partial headers under include/auth/login_runtime/.

This script automates the mechanical splits that PRs 8-10 of the
dedicated monolith residual plan still need to perform. It encodes the
pattern established by the manual PRs 1-7 (2026-05-15):

    1. Read login_runtime.h and locate each named struct definition
       between `^struct <name>\\s*\\{` and the matching `^\\};` line.
    2. Build a new partial header at the configured path with the
       extracted definitions + a uniform header guard + a docstring
       describing the group + only `<stddef.h>` as include.
    3. Replace the first struct of the group in login_runtime.h with
       an `#include` directive pointing at the new partial header.
    4. Delete the remaining structs of the group from login_runtime.h.

The script is intentionally idempotent: running it twice with the
same arguments is a no-op when the structs have already been moved.

USAGE
=====

  # Apply all pending groups (PRs 8, 9, 10) at once.
  python3 tools/scripts/split_login_runtime_partials.py

  # Apply only one group.
  python3 tools/scripts/split_login_runtime_partials.py --only deadline_cleanup

  # Dry-run: report what would change without writing files.
  python3 tools/scripts/split_login_runtime_partials.py --dry-run

  # Apply a custom group from a YAML/JSON-like inline spec.
  python3 tools/scripts/split_login_runtime_partials.py \
      --group new_group --structs structA,structB,structC

EXIT CODES
==========

  0 : success (all requested groups applied or already at the target
      state).
  1 : login_runtime.h missing or unreadable.
  2 : at least one requested struct could not be located.
  3 : facade write failed.

LOCAL EXECUTION POLICY
======================

This script does NOT run any external tools. It only reads/writes
files inside the workspace. Safe to invoke in CI or on another
machine. See `docs/plans/STATUS.md` and
`docs/plans/active/monolith-residual-dedicated-plan.md` for context.
"""
from __future__ import annotations

import argparse
import re
import sys
from dataclasses import dataclass
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parent.parent.parent
FACADE = REPO_ROOT / "include" / "auth" / "login_runtime.h"
PARTIALS_DIR = REPO_ROOT / "include" / "auth" / "login_runtime"


@dataclass
class Group:
    """A single partial header to extract."""

    name: str  # filename without `.h`
    pr_id: str  # e.g. "8", "9", "10"
    structs: tuple[str, ...]  # struct names in the order they appear
    description: str  # one-line description for the docstring


# Groups still pending as of 2026-05-15 (after manual PRs 1-7).
# Order matters: groups must be applied in pipeline order so
# downstream stages can include their upstream partial header if a
# struct ever holds a value-typed member (currently none of the PR
# 8/9/10 groups depends on the previous, only the included
# `<stddef.h>` is needed).
PENDING_GROUPS: tuple[Group, ...] = (
    Group(
        name="deadline_cleanup",
        pr_id="8",
        structs=(
            "login_window_credential_screen_deadline_plan",
            "login_window_credential_screen_completion_plan",
            "login_window_credential_screen_ack_plan",
            "login_window_credential_screen_retire_plan",
            "login_window_credential_screen_cleanup_plan",
        ),
        description=(
            "Deadline / completion / ack / retire / cleanup plan structs. "
            "These close out the sync pipeline by arming deadline timers, "
            "reporting completion, acking, retiring resources and cleaning "
            "up scratch state."
        ),
    ),
    Group(
        name="seal_ledger",
        pr_id="8",
        structs=(
            "login_window_credential_screen_seal_plan",
            "login_window_credential_screen_audit_plan",
            "login_window_credential_screen_record_plan",
            "login_window_credential_screen_receipt_plan",
            "login_window_credential_screen_ledger_plan",
        ),
        description=(
            "Seal / audit / record / receipt / ledger plan structs. "
            "Translate cleanup output into immutable audit log entries, "
            "persisted records and signed receipts for the ledger."
        ),
    ),
    Group(
        name="journal_retention",
        pr_id="9",
        structs=(
            "login_window_credential_screen_journal_plan",
            "login_window_credential_screen_archive_plan",
            "login_window_credential_screen_retention_plan",
            "login_window_credential_screen_expiry_plan",
        ),
        description=(
            "Journal / archive / retention / expiry plan structs. "
            "Manage the long-term audit trail: persist to journal, "
            "archive, apply retention policy, expire stale entries."
        ),
    ),
    Group(
        name="purge_reclaim",
        pr_id="9",
        structs=(
            "login_window_credential_screen_purge_plan",
            "login_window_credential_screen_tombstone_plan",
            "login_window_credential_screen_compaction_plan",
            "login_window_credential_screen_reclaim_plan",
            "login_window_credential_screen_release_plan",
        ),
        description=(
            "Purge / tombstone / compaction / reclaim / release plan "
            "structs. Reclaim disk space from expired audit entries via "
            "purge, tombstone, compaction, reclaim and release stages."
        ),
    ),
    Group(
        name="gui_window",
        pr_id="10",
        structs=(
            "login_window_credential_screen_gui_plan",
            "login_window_credential_screen_window_plan",
            "login_window_credential_screen_window_surface_plan",
            "login_window_credential_screen_window_compositor_plan",
        ),
        description=(
            "GUI bootstrap + window primitive plans. The GUI plan turns "
            "release output into a window-level mount; the window/surface/"
            "compositor plans set up the per-window compositor pipeline."
        ),
    ),
    Group(
        name="window_display",
        pr_id="10",
        structs=(
            "login_window_credential_screen_window_damage_plan",
            "login_window_credential_screen_window_present_plan",
            "login_window_credential_screen_window_schedule_plan",
            "login_window_credential_screen_window_vsync_plan",
            "login_window_credential_screen_window_scanout_plan",
        ),
        description=(
            "Per-window damage / present / schedule / vsync / scanout "
            "plan structs. Mirror the top-level compositor pipeline but "
            "scoped per loginwindow."
        ),
    ),
    Group(
        name="window_output",
        pr_id="10",
        structs=(
            "login_window_credential_screen_window_display_plan",
            "login_window_credential_screen_window_output_plan",
            "login_window_credential_screen_window_blit_plan",
            "login_window_credential_screen_window_commit_plan",
            "login_window_credential_screen_window_flip_plan",
        ),
        description=(
            "Per-window display / output / blit / commit / flip plan "
            "structs. Carry the window-scoped pixel work to the actual "
            "scan-out engine and atomic commit / page flip primitives."
        ),
    ),
    Group(
        name="window_input",
        pr_id="10",
        structs=(
            "login_window_credential_screen_window_vblank_plan",
            "login_window_credential_screen_window_event_plan",
            "login_window_credential_screen_window_input_plan",
        ),
        description=(
            "Per-window vblank / event / input plan structs. The final "
            "leaves of the loginwindow GUI pipeline."
        ),
    ),
)


# ---------------------------------------------------------------- regex utils


STRUCT_HEADER_RE = re.compile(r"^struct\s+([A-Za-z_][A-Za-z0-9_]*)\s*\{\s*$",
                              re.MULTILINE)


def find_struct(content: str, name: str) -> tuple[int, int] | None:
    """Return (start_byte_offset, end_byte_offset) of `struct <name> { ... };`.

    end is the position of the trailing newline after the `};` line.
    Returns None if the struct is not present (already moved or never
    existed). Raises ValueError when the opener is present but the
    closing `};` is malformed.
    """
    match = re.search(rf"^struct\s+{re.escape(name)}\s*\{{\s*$", content,
                      re.MULTILINE)
    if not match:
        return None
    start = match.start()
    end_match = re.search(r"^};\s*$", content[match.end():], re.MULTILINE)
    if not end_match:
        raise ValueError(
            f"struct {name}: opening brace found but no matching close")
    end_offset = match.end() + end_match.end()
    if end_offset < len(content) and content[end_offset] == "\n":
        end_offset += 1
    return (start, end_offset)


def render_partial_header(group: Group, struct_bodies: list[str]) -> str:
    """Render a complete partial header file for `group`."""
    guard = f"AUTH_LOGIN_RUNTIME_{group.name.upper()}_H"
    structs_list = "\n".join(f" *   - struct {name}" for name in group.structs)
    bodies = "\n\n".join(body.rstrip() for body in struct_bodies)
    return (
        f"#ifndef {guard}\n"
        f"#define {guard}\n"
        f"\n"
        f"/*\n"
        f" * include/auth/login_runtime/{group.name}.h\n"
        f" *\n"
        f" * Internal partial header carrying the following struct(s):\n"
        f" *\n"
        f"{structs_list}\n"
        f" *\n"
        f" * {group.description}\n"
        f" *\n"
        f" * Standalone-includable: only primitives + const char* fields.\n"
        f" * PR B+C+D #{group.pr_id} of the dedicated plan extracts these\n"
        f" * definitions unchanged byte-for-byte from `auth/login_runtime.h`.\n"
        f" */\n"
        f"\n"
        f"#include <stddef.h>\n"
        f"\n"
        f"{bodies}\n"
        f"\n"
        f"#endif /* {guard} */\n"
    )


def find_if0_block_around(content: str, struct_start: int) -> tuple[int, int] | None:
    """Find an `#if 0 ... #endif` block that contains `struct_start`.

    Returns (block_start, block_end) where block_start is the start of
    the `#if 0` line and block_end is just after the `#endif` line.
    Returns None when the offset is not inside any `#if 0` block.

    The function scans backwards from `struct_start` for the nearest
    `#if 0` (without an intervening `#endif`) and forwards for the
    matching `#endif`. This is a heuristic appropriate for the small,
    well-bounded `#if 0` markers the manual PR pattern leaves around.
    """
    prefix = content[:struct_start]
    if_match: re.Match[str] | None = None
    for m in re.finditer(r"^#if 0\b[^\n]*\n", prefix, re.MULTILINE):
        if_match = m
    if if_match is None:
        return None
    # Make sure no `#endif` sits between the `#if 0` and struct_start.
    if re.search(r"^#endif\b", prefix[if_match.end():], re.MULTILINE):
        return None
    end_match = re.search(r"^#endif\b[^\n]*\n", content[struct_start:],
                          re.MULTILINE)
    if end_match is None:
        return None
    return (if_match.start(), struct_start + end_match.end())


def apply_group(group: Group, facade_text: str,
                dry_run: bool) -> tuple[str, str | None, bool]:
    """Apply one group to facade content.

    Returns (new_facade_text, partial_header_text_or_None, did_change).
    `partial_header_text_or_None` is None when the group has already
    been applied (every struct of the group has been removed from the
    facade), in which case did_change is False.

    Special case: when the structs are wrapped inside an `#if 0` /
    `#endif` block (a state left by a partial manual wiring), the
    block is deleted as a single unit and the partial header is
    regenerated from the freshly extracted bodies.
    """
    locations: list[tuple[int, int, str]] = []  # (start, end, name)
    missing: list[str] = []
    for name in group.structs:
        loc = find_struct(facade_text, name)
        if loc is None:
            missing.append(name)
        else:
            locations.append((loc[0], loc[1], name))

    if not locations and len(missing) == len(group.structs):
        # Already applied: nothing to do.
        return facade_text, None, False

    if missing:
        raise SystemExit(
            f"[err] group {group.name}: some structs are missing while "
            f"others are still inline: {missing}. The facade is in a "
            "partial state; refusing to continue.")

    # All structs are present: extract them and build the partial.
    locations.sort()
    struct_bodies = [facade_text[start:end].rstrip() + "\n"
                     for start, end, _ in locations]
    partial_text = render_partial_header(group, struct_bodies)

    first_start = locations[0][0]
    first_end = locations[0][1]

    # Detect the `#if 0` wrap left by partial manual wiring.
    if0_block = find_if0_block_around(facade_text, first_start)
    if if0_block is not None:
        if0_start, if0_end = if0_block
        # Confirm the LAST struct of the group is inside the same block.
        last_struct_end = locations[-1][1]
        if last_struct_end <= if0_end:
            # Delete the entire `#if 0 ... #endif` wrapper; the include
            # has been added by the manual wiring already, so we just
            # collapse the dead code.
            new_facade = facade_text[:if0_start] + facade_text[if0_end:]
            return new_facade, partial_text, True

    # Normal path: insert include where the first struct lived and
    # remove the others.
    include_line = (
        f"/* Partial header for {group.name} group (PR B+C+D #{group.pr_id}). */\n"
        f"#include \"auth/login_runtime/{group.name}.h\"\n"
        f"\n"
    )
    new_facade = facade_text
    # Remove later structs from back to front so offsets stay valid.
    for start, end, _ in reversed(locations[1:]):
        new_facade = new_facade[:start] + new_facade[end:]
    # Replace the first occurrence with the include directive.
    new_facade = (new_facade[:first_start]
                  + include_line
                  + new_facade[first_end:])
    return new_facade, partial_text, True


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--only", action="append", default=[],
                        help="apply only the named group(s); may be passed "
                             "multiple times. defaults to all pending groups")
    parser.add_argument("--group", default=None,
                        help="custom group name (used with --structs)")
    parser.add_argument("--structs", default=None,
                        help="comma-separated struct names for --group")
    parser.add_argument("--pr-id", default="custom",
                        help="PR identifier inserted in the partial header "
                             "docstring (with --group)")
    parser.add_argument("--description", default="(custom group)",
                        help="one-line description for the partial header "
                             "docstring (with --group)")
    parser.add_argument("--dry-run", action="store_true",
                        help="report changes without writing files")
    args = parser.parse_args()

    if not FACADE.exists():
        print(f"[err] facade not found: {FACADE}", file=sys.stderr)
        return 1

    if args.group:
        if not args.structs:
            print("[err] --group requires --structs", file=sys.stderr)
            return 1
        groups: tuple[Group, ...] = (Group(
            name=args.group,
            pr_id=args.pr_id,
            structs=tuple(s.strip() for s in args.structs.split(",") if s.strip()),
            description=args.description,
        ),)
    elif args.only:
        wanted = set(args.only)
        groups = tuple(g for g in PENDING_GROUPS if g.name in wanted)
        unknown = wanted - {g.name for g in PENDING_GROUPS}
        if unknown:
            print(f"[err] unknown group(s): {sorted(unknown)}", file=sys.stderr)
            return 1
    else:
        groups = PENDING_GROUPS

    facade_text = FACADE.read_text(encoding="utf-8")
    original_lines = facade_text.count("\n")
    summary: list[tuple[str, int, int, bool]] = []

    PARTIALS_DIR.mkdir(parents=True, exist_ok=True)
    for group in groups:
        try:
            new_facade, partial_text, did_change = apply_group(
                group, facade_text, args.dry_run)
        except SystemExit as exc:
            print(str(exc), file=sys.stderr)
            return 2
        if not did_change:
            summary.append((group.name, 0, 0, False))
            continue
        partial_path = PARTIALS_DIR / f"{group.name}.h"
        if args.dry_run:
            print(f"[dry-run] would write {partial_path} "
                  f"({partial_text.count(chr(10))} lines)")
            print(f"[dry-run] would shrink facade by "
                  f"{facade_text.count(chr(10)) - new_facade.count(chr(10))} lines")
        else:
            partial_path.write_text(partial_text, encoding="utf-8")
        before = facade_text.count("\n")
        after = new_facade.count("\n")
        summary.append((group.name, before - after,
                        partial_text.count("\n"), True))
        facade_text = new_facade

    if not args.dry_run:
        try:
            FACADE.write_text(facade_text, encoding="utf-8")
        except OSError as exc:
            print(f"[err] facade write failed: {exc}", file=sys.stderr)
            return 3

    print(f"[ok] facade lines: {original_lines} -> {facade_text.count(chr(10))}")
    for name, shrink, partial_lines, did in summary:
        if not did:
            print(f"  - {name}: already applied (skipped)")
        else:
            print(f"  - {name}: -{shrink} facade lines / "
                  f"+{partial_lines} partial lines")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
