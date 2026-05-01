#!/usr/bin/env python3
"""
CapyOS x64 smoke for SYS_FORK + CoW (M5 phase A.7).

Boots a kernel built with `-DCAPYOS_PREEMPTIVE_SCHEDULER
-DCAPYOS_BOOT_RUN_HELLO` and the userland hello rebuilt with
`-DCAPYOS_HELLO_FORK`. The single embedded hello binary calls
`capy_fork()` once and then both branches loop forever emitting
distinct markers via SYS_WRITE.

This is the canonical proof that fork() + CoW work end-to-end:

  - The `[fork-parent]` marker proves the parent returned from
    sys_fork with a positive PID (the PID > 0 was branched on as
    `pid != 0`) and resumed normally past the syscall.
  - The `[fork-child]` marker proves the kernel armed the child
    task via `user_task_arm_for_fork`, the scheduler dispatched it
    via `x64_user_first_dispatch`, the synthetic IRET frame popped
    correctly into ring 3 with `rax = 0` and the parent's RIP/RSP,
    and the child's first stack write triggered a successful CoW
    page fault that materialised a private copy of the parent's
    stack page (M4 phase 7c).
  - `panic` MUST be absent: a CoW miss, a wrong child RSP/RIP, or a
    refcount imbalance would surface as a kernel panic.

Pass criteria (against the kernel debug-console log on port 0xE9):

  * `[fork-parent]` >= FORK_MIN occurrences.
  * `[fork-child]`  >= FORK_MIN occurrences.
  * `[fork-fail]` absent (capy_fork never returned -1).
  * `panic` absent.
  * "[user_init] hello spawn returned without entering Ring 3."
    absent.

Usage (CI):
  make smoke-x64-fork-cow
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

FORK_MIN = 2

FAILURE_MARKERS = (
    "panic",
    "[fork-fail]",
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
                        help="Seconds to wait for both fork markers")
    parser.add_argument("--fork-min", type=int, default=FORK_MIN,
                        help="Min count for EACH of "
                             "[fork-parent]/[fork-child]")
    parser.add_argument(
        "--log",
        default="build/ci/smoke_x64_fork_cow.log",
        help="QEMU stdout/stderr log path")
    parser.add_argument(
        "--debugcon-log",
        default="build/ci/smoke_x64_fork_cow.debugcon.log",
        help="Kernel debug-console log path (port 0xE9)")
    parser.add_argument(
        "--disk",
        default="build/ci/smoke_x64_fork_cow.img",
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


def fork_counts(text: str) -> tuple[int, int]:
    return text.count("[fork-parent]"), text.count("[fork-child]")


def all_markers_present(text: str, fork_min: int) -> bool:
    n_parent, n_child = fork_counts(text)
    return n_parent >= fork_min and n_child >= fork_min


def any_failure_marker_present(text: str) -> str | None:
    for m in FAILURE_MARKERS:
        if m in text:
            return m
    return None


def poll_debugcon(debugcon_log: Path, timeout: float,
                  fork_min: int) -> tuple[bool, str | None]:
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
        if all_markers_present(text, fork_min):
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
                  f"with the M5 phase A.7 flag set?", file=sys.stderr)
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
            debugcon_log, args.timeout, args.fork_min)

        if proc.poll() is None:
            try:
                proc.send_signal(signal.SIGTERM)
                proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                proc.kill()

    text = debugcon_log.read_text(encoding="latin-1", errors="replace")
    n_parent, n_child = fork_counts(text)

    if success:
        print(f"[ok] fork-cow smoke passed in <={args.timeout:.0f}s")
        print(f"     + [fork-parent] count = {n_parent} "
              f"(>= {args.fork_min})")
        print(f"     + [fork-child]  count = {n_child} "
              f"(>= {args.fork_min})")
        rc = 0
    else:
        print("[err] fork-cow smoke failed", file=sys.stderr)
        if failure_reason is not None:
            print(f"      reason: failure marker {failure_reason!r}",
                  file=sys.stderr)
        if n_parent < args.fork_min:
            print(f"      [fork-parent] count = {n_parent} "
                  f"(< {args.fork_min}) -- parent never resumed past fork",
                  file=sys.stderr)
        if n_child < args.fork_min:
            print(f"      [fork-child]  count = {n_child} "
                  f"(< {args.fork_min}) -- child never reached ring 3 "
                  f"(possible CoW or IRET frame failure)",
                  file=sys.stderr)
        print_log_tail(debugcon_log)
        rc = 1

    if not args.keep_disk:
        cleanup_file(disk_path)
    cleanup_file(ovmf_vars_runtime)

    return rc


if __name__ == "__main__":
    sys.exit(main())
