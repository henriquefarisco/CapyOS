#!/usr/bin/env python3
"""
CapyOS x64 smoke for two-task ring-3 preemption (M4 phase 8f.5).

Boots a kernel built with `-DCAPYOS_PREEMPTIVE_SCHEDULER
-DCAPYOS_BOOT_RUN_HELLO -DCAPYOS_BOOT_RUN_TWO_BUSY` and userland
hello rebuilt with `-DCAPYOS_HELLO_BUSY`. The kernel spawns TWO
copies of the embedded hello, arms the second via the synthetic
IRET frame builder (phase 8f.4), iretqs into the first (rank=0),
and lets the scheduler dispatch the second on quantum exhaustion
(rank=1).

This is the canonical proof of ring-3 preemption under CapyOS:

  - Both `[busyU0]` and `[busyU1]` markers must appear at least
    BUSY_MIN times each, which proves:
      1. The synthetic IRET frame builder (8f.4) actually delivers
         a fresh user task to ring 3 via context_switch + iretq.
      2. The per-task RSP0 swap (8f.2) keeps each task's IRQ
         frames on its own kernel stack so they don't clobber
         each other.
      3. context_switch correctly preserves and restores ring-3
         state across multiple swaps.

Pass criteria (matched against the kernel debug-console log):

  * "[user_init] CAPYOS_BOOT_RUN_TWO_BUSY defined; spawning two."
    present (regression guard for the spawn helper).
  * `[busyU0]` >= BUSY_MIN occurrences.
  * `[busyU1]` >= BUSY_MIN occurrences.
  * `panic` absent.
  * "hello spawn returned without entering Ring 3." absent.

Usage (CI):
  make smoke-x64-preemptive-user-2task
"""

from __future__ import annotations

import argparse
import signal
import subprocess
import sys
import time
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO_ROOT / "tools" / "scripts"))

from smoke_x64_common import (  # noqa: E402
    cleanup_file,
    create_runtime_ovmf_vars,
    print_log_tail,
    provision_disk,
    resolve_ovmf_or_raise,
    resolve_qemu_binary,
)
from smoke_x64_session import choose_free_port, make_qemu_cmd  # noqa: E402

BUSY_MIN = 2

SUCCESS_MARKERS = (
    "[user_init] CAPYOS_BOOT_RUN_TWO_BUSY defined; spawning two.",
)
FAILURE_MARKERS = (
    "panic",
    "[user_init] hello spawn returned without entering Ring 3.",
)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--qemu", default="qemu-system-x86_64",
                        help="QEMU binary")
    parser.add_argument("--ovmf", default=None, help="OVMF_CODE.fd path")
    parser.add_argument("--memory", type=int, default=512,
                        help="Guest memory in MB")
    parser.add_argument("--timeout", type=float, default=60.0,
                        help="Seconds to wait for both busy markers")
    parser.add_argument("--busy-min", type=int, default=BUSY_MIN,
                        help="Min count for EACH of [busyU0]/[busyU1]")
    parser.add_argument(
        "--log",
        default="build/ci/smoke_x64_preemptive_user_2task.log",
        help="QEMU stdout/stderr log path")
    parser.add_argument(
        "--debugcon-log",
        default="build/ci/smoke_x64_preemptive_user_2task.debugcon.log",
        help="Kernel debug-console log path (port 0xE9)")
    parser.add_argument(
        "--disk",
        default="build/ci/smoke_x64_preemptive_user_2task.img",
        help="Provisioned GPT disk image path")
    parser.add_argument("--disk-size", default="2G",
                        help="Disk size for GPT provisioning")
    parser.add_argument("--keep-disk", action="store_true",
                        help="Do not delete provisioned disk image on exit")
    parser.add_argument("--storage-bus", choices=("sata", "nvme"),
                        default="sata", help="Storage bus")
    parser.add_argument("--volume-key",
                        default="CAPYOS-SMOKE-KEY-2026-0001",
                        help="Lab volume key persisted in CAPYCFG.BIN")
    parser.add_argument("--keyboard-layout", default="us",
                        help="Keyboard layout persisted in CAPYCFG.BIN")
    parser.add_argument("--language", default="en",
                        help="System language persisted in CAPYCFG.BIN")
    return parser.parse_args()


def busy_counts(text: str) -> tuple[int, int]:
    return text.count("[busyU0]"), text.count("[busyU1]")


def all_markers_present(text: str, busy_min: int) -> bool:
    if not all(m in text for m in SUCCESS_MARKERS):
        return False
    n0, n1 = busy_counts(text)
    return n0 >= busy_min and n1 >= busy_min


def any_failure_marker_present(text: str) -> str | None:
    for m in FAILURE_MARKERS:
        if m in text:
            return m
    return None


def poll_debugcon(debugcon_log: Path, timeout: float,
                  busy_min: int) -> tuple[bool, str | None]:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        try:
            text = debugcon_log.read_text(encoding="latin-1",
                                          errors="replace")
        except FileNotFoundError:
            text = ""
        bad = any_failure_marker_present(text)
        if bad is not None:
            return False, bad
        if all_markers_present(text, busy_min):
            return True, None
        time.sleep(0.1)
    return False, None


def main() -> int:
    args = parse_args()

    log_path = (REPO_ROOT / args.log).resolve()
    debugcon_log = (REPO_ROOT / args.debugcon_log).resolve()
    disk_path = (REPO_ROOT / args.disk).resolve()

    log_path.parent.mkdir(parents=True, exist_ok=True)
    debugcon_log.parent.mkdir(parents=True, exist_ok=True)
    disk_path.parent.mkdir(parents=True, exist_ok=True)

    debugcon_log.write_bytes(b"")

    try:
        qemu_bin = resolve_qemu_binary(args.qemu)
    except FileNotFoundError as exc:
        print(f"[err] {exc}", file=sys.stderr)
        return 2

    try:
        ovmf_code, ovmf_vars_template = resolve_ovmf_or_raise(args.ovmf)
    except FileNotFoundError as exc:
        print(f"[err] {exc}", file=sys.stderr)
        return 2

    bootx64 = (REPO_ROOT / "build" / "iso-uefi-root" / "EFI" /
               "BOOT" / "BOOTX64.EFI")
    kernel = REPO_ROOT / "build" / "capyos64.bin"
    manifest = REPO_ROOT / "build" / "manifest.bin"
    for required in (bootx64, kernel, manifest):
        if not required.exists():
            print(f"[err] missing build artifact: {required}\n"
                  f"      did you run `make all64 iso-uefi manifest64` "
                  f"with the 8f.5 flag set?", file=sys.stderr)
            return 2

    provision_disk(
        repo_root=REPO_ROOT,
        disk_path=disk_path,
        disk_size=args.disk_size,
        bootx64=bootx64,
        kernel=kernel,
        manifest=manifest,
        keyboard_layout=args.keyboard_layout,
        language=args.language,
        volume_key=args.volume_key,
    )

    ovmf_vars_runtime = create_runtime_ovmf_vars(log_path, ovmf_vars_template)

    serial_port = choose_free_port()
    cmd = make_qemu_cmd(
        qemu_bin=qemu_bin,
        ovmf_code=ovmf_code,
        ovmf_vars_runtime=ovmf_vars_runtime,
        disk_path=disk_path,
        serial_port=serial_port,
        memory_mb=args.memory,
        storage_bus=args.storage_bus,
        debugcon_log=debugcon_log,
    )

    print(f"[info] launching QEMU; debugcon={debugcon_log}")
    with log_path.open("wb") as log_fh:
        proc = subprocess.Popen(cmd, stdout=log_fh, stderr=subprocess.STDOUT)

        success, failure_reason = poll_debugcon(
            debugcon_log, args.timeout, args.busy_min)

        if proc.poll() is None:
            try:
                proc.send_signal(signal.SIGTERM)
                proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                proc.kill()

    text = debugcon_log.read_text(encoding="latin-1", errors="replace")
    found = [m for m in SUCCESS_MARKERS if m in text]
    missing = [m for m in SUCCESS_MARKERS if m not in text]
    n0, n1 = busy_counts(text)

    if success:
        print(f"[ok] preemptive-user-2task smoke passed "
              f"in <={args.timeout:.0f}s")
        for m in found:
            print(f"     + {m!r} present")
        print(f"     + [busyU0] count = {n0} (>= {args.busy_min})")
        print(f"     + [busyU1] count = {n1} (>= {args.busy_min})")
        rc = 0
    else:
        print("[err] preemptive-user-2task smoke failed",
              file=sys.stderr)
        if failure_reason is not None:
            print(f"      reason: failure marker {failure_reason!r}",
                  file=sys.stderr)
        for m in missing:
            print(f"      - missing: {m!r}", file=sys.stderr)
        if n0 < args.busy_min:
            print(f"      [busyU0] count = {n0} (< {args.busy_min})",
                  file=sys.stderr)
        if n1 < args.busy_min:
            print(f"      [busyU1] count = {n1} (< {args.busy_min}) "
                  f"-- second task never made progress", file=sys.stderr)
        print_log_tail(debugcon_log)
        rc = 1

    if not args.keep_disk:
        cleanup_file(disk_path)
    cleanup_file(ovmf_vars_runtime)

    return rc


if __name__ == "__main__":
    sys.exit(main())
