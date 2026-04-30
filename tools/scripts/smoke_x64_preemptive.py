#!/usr/bin/env python3
"""
CapyOS x64 smoke for the preemptive scheduler wiring (M4 phase 8b).

Boots a kernel built with `-DCAPYOS_PREEMPTIVE_SCHEDULER` in QEMU/UEFI and
verifies that the boot path actually flips the scheduler policy from the
default cooperative mode to PRIORITY and marks `sched_running=1` before
the APIC 100Hz timer starts firing `scheduler_tick`.

This smoke is the wiring verification for phase 8b. It does NOT yet
demonstrate that two competing tasks are actually preempted on quantum
exhaustion — that end-to-end demo lands in phase 8c with a dedicated
two-task user binary. What this smoke does prove:

  - The CAPYOS_PREEMPTIVE_SCHEDULER macro reaches kernel_main.c (the
    `#ifdef` block in `boot_metrics_stage_end -> APIC arm` is emitted).
  - `scheduler_init(SCHED_POLICY_PRIORITY)` is called on the boot path.
  - `apic_timer_start(100)` is reached and emits the existing
    "Preemptive tick armed at 100Hz." log.
  - `scheduler_set_running(1)` is called after the APIC arm so any
    cooperative `scheduler_yield()` from the existing kernel tasks
    actually invokes `schedule()` instead of returning early.
  - The kernel does NOT panic when the policy is flipped (regression
    guard for the preemptive tick path coverage).

Usage (CI):
  make smoke-x64-preemptive

Manual (developer):
  make clean
  make all64 EXTRA_CFLAGS64='-DCAPYOS_PREEMPTIVE_SCHEDULER'
  make iso-uefi
  make manifest64
  python3 tools/scripts/smoke_x64_preemptive.py

Pass criteria (matched against the debug-console log file):
  * "[scheduler] Policy=PRIORITY (preemptive flip enabled)." present.
  * "[scheduler] Preemptive tick armed at 100Hz." present.
  * "[scheduler] Marked as running (sched_running=1)." present.
  * "panic" absent.

Failure handling:
  Any criterion missed -> exit code 1 + tail of debugcon log to stderr.
  QEMU process is killed once all success markers appear or after the
  configured timeout, whichever happens first.
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

from smoke_x64_common import (  # noqa: E402  (sys.path tweak above)
    cleanup_file,
    create_runtime_ovmf_vars,
    print_log_tail,
    provision_disk,
    resolve_ovmf_or_raise,
    resolve_qemu_binary,
)
from smoke_x64_session import choose_free_port, make_qemu_cmd  # noqa: E402

SUCCESS_MARKERS = (
    "[scheduler] Policy=PRIORITY (preemptive flip enabled).",
    "[scheduler] Preemptive tick armed at 100Hz.",
    "[scheduler] Marked as running (sched_running=1).",
    # M4 phase 8c regression guard: the IRQ 0 handler must be installed
    # before apic_timer_start, otherwise apic_timer_set_callback wires
    # scheduler_tick to a callback that the IDT stub at vector 32 never
    # reaches (the dispatcher finds g_irq_handlers[0]=NULL, sends a
    # spurious PIC EOI, and the tick is silently dropped).
    "[scheduler] APIC IRQ handler installed at IRQ 0.",
    # M4 phase 8d: the bounded observation soak in kernel_main enables
    # interrupts (`sti`), spins until apic_timer_ticks() reaches a small
    # floor (or the budget exhausts), and disables them again. The
    # printed tick count is the proof that IRQ delivery actually fires
    # scheduler_tick end-to-end. The smoke checks the prefix; a
    # post-condition below asserts the trailing decimal is non-zero.
    "[scheduler] APIC ticks observed=",
)
FAILURE_MARKERS = (
    "panic",
)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--qemu", default="qemu-system-x86_64",
                        help="QEMU binary")
    parser.add_argument("--ovmf", default=None, help="OVMF_CODE.fd path")
    parser.add_argument("--memory", type=int, default=512,
                        help="Guest memory in MB")
    parser.add_argument("--timeout", type=float, default=30.0,
                        help="Seconds to wait for all success markers")
    parser.add_argument("--log",
                        default="build/ci/smoke_x64_preemptive.log",
                        help="QEMU stdout/stderr log path")
    parser.add_argument("--debugcon-log",
                        default="build/ci/smoke_x64_preemptive.debugcon.log",
                        help="Kernel debug-console log path (port 0xE9)")
    parser.add_argument("--disk",
                        default="build/ci/smoke_x64_preemptive.img",
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


def all_markers_present(text: str) -> bool:
    return all(m in text for m in SUCCESS_MARKERS)


def any_failure_marker_present(text: str) -> str | None:
    for m in FAILURE_MARKERS:
        if m in text:
            return m
    return None


def poll_debugcon(debugcon_log: Path,
                  timeout: float) -> tuple[bool, str | None]:
    """Returns (success, failure_reason). Reads `debugcon_log` every
    100 ms and short-circuits once either all success markers appear
    (success=True) or any failure marker appears (success=False with
    a reason). Times out with success=False, reason=None."""
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
        if all_markers_present(text):
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

    # Truncate any prior debugcon so the polling loop only sees
    # output from this boot.
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
                  f"with EXTRA_CFLAGS64='-DCAPYOS_PREEMPTIVE_SCHEDULER'?",
                  file=sys.stderr)
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

        success, failure_reason = poll_debugcon(debugcon_log, args.timeout)

        if proc.poll() is None:
            try:
                proc.send_signal(signal.SIGTERM)
                proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                proc.kill()

    text = debugcon_log.read_text(encoding="latin-1", errors="replace")
    found = [m for m in SUCCESS_MARKERS if m in text]
    missing = [m for m in SUCCESS_MARKERS if m not in text]

    # M4 phase 8d post-condition: the printed `APIC ticks observed=N`
    # must have N > 0. A zero count means the soak ran but no IRQ
    # actually fired -- which would mean either the IDT install is
    # broken, the APIC programming is wrong, or x64_interrupts_enable
    # silently failed. Without this assertion the smoke would still
    # pass with N=0 since the prefix is present.
    apic_ticks_value: int | None = None
    apic_ticks_prefix = "[scheduler] APIC ticks observed="
    apic_idx = text.rfind(apic_ticks_prefix)
    if apic_idx >= 0:
        tail = text[apic_idx + len(apic_ticks_prefix):]
        digits = ""
        for ch in tail:
            if ch.isdigit():
                digits += ch
            else:
                break
        if digits:
            try:
                apic_ticks_value = int(digits)
            except ValueError:
                apic_ticks_value = None

    if success and (apic_ticks_value is None or apic_ticks_value <= 0):
        success = False
        if apic_ticks_value is None:
            failure_reason = ("APIC ticks observed= prefix found but no "
                              "decimal followed it")
        else:
            failure_reason = (f"APIC ticks observed={apic_ticks_value} "
                              f"means IRQ delivery is broken")

    if success:
        print(f"[ok] preemptive smoke passed in <={args.timeout:.0f}s")
        for m in found:
            print(f"     + {m!r} present")
        if apic_ticks_value is not None:
            print(f"     + APIC ticks observed={apic_ticks_value} (>0)")
        rc = 0
    else:
        print("[err] preemptive smoke failed", file=sys.stderr)
        if failure_reason is not None:
            print(f"      reason: failure marker {failure_reason!r} "
                  f"appeared", file=sys.stderr)
        for m in missing:
            print(f"      - missing: {m!r}", file=sys.stderr)
        print_log_tail(debugcon_log)
        rc = 1

    if not args.keep_disk:
        cleanup_file(disk_path)
    cleanup_file(ovmf_vars_runtime)

    return rc


if __name__ == "__main__":
    sys.exit(main())
