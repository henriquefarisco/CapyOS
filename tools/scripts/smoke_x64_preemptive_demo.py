#!/usr/bin/env python3
"""
CapyOS x64 smoke for the two-task kernel-mode preemption demo
(M4 phase 8e).

Boots a kernel built with `-DCAPYOS_PREEMPTIVE_SCHEDULER
-DCAPYOS_PREEMPTIVE_DEMO` in QEMU/UEFI and verifies that BOTH busy
tasks make progress, which is the canonical proof that:

  - The APIC tick is delivered (phase 8c IRQ install).
  - The preemptive policy actually preempts on quantum exhaustion
    (phase 8a/8d).
  - context_switch correctly swaps two kernel-mode contexts so each
    task RESUMES from where it was preempted (phase 2 contract).
  - The phase 8e first-task trampoline drops the boot stack and
    actually executes the first task's entry body.

Without phase 8e (or with a broken trampoline / context_switch /
APIC tick), at least one of the two markers would be missing.

Pass criteria (matched against the kernel debug-console log):

  * "[demo:enter]" present (the demo function reached the trampoline).
  * "[busyA]" present at least once.
  * "[busyB]" present at least once.
  * "panic" absent.

Usage (CI):
  make smoke-x64-preemptive-demo

Manual (developer):
  make clean
  make all64 EXTRA_CFLAGS64='-DCAPYOS_PREEMPTIVE_SCHEDULER \
                            -DCAPYOS_PREEMPTIVE_DEMO'
  make iso-uefi
  make manifest64
  python3 tools/scripts/smoke_x64_preemptive_demo.py
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

SUCCESS_MARKERS = (
    "[demo:enter]",
    "[busyA]",
    "[busyB]",
)
FAILURE_MARKERS = (
    "panic",
    "[demo:alloc]",  # task_create returned NULL; demo cannot run
)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--qemu", default="qemu-system-x86_64",
                        help="QEMU binary")
    parser.add_argument("--ovmf", default=None, help="OVMF_CODE.fd path")
    parser.add_argument("--memory", type=int, default=512,
                        help="Guest memory in MB")
    parser.add_argument("--timeout", type=float, default=45.0,
                        help="Seconds to wait for both busy markers")
    parser.add_argument(
        "--log",
        default="build/ci/smoke_x64_preemptive_demo.log",
        help="QEMU stdout/stderr log path")
    parser.add_argument(
        "--debugcon-log",
        default="build/ci/smoke_x64_preemptive_demo.debugcon.log",
        help="Kernel debug-console log path (port 0xE9)")
    parser.add_argument(
        "--disk",
        default="build/ci/smoke_x64_preemptive_demo.img",
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
                  f"with EXTRA_CFLAGS64='-DCAPYOS_PREEMPTIVE_SCHEDULER "
                  f"-DCAPYOS_PREEMPTIVE_DEMO'?", file=sys.stderr)
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

    # Phase 8e additional check: count occurrences of each busy marker.
    # A working preemption demo emits each marker MULTIPLE times, not
    # just once. A single-shot appearance could mean the first task
    # ran exactly one marker and then crashed; multiple markers
    # confirm the preemption loop is steady-state.
    busy_a_count = text.count("[busyA]")
    busy_b_count = text.count("[busyB]")
    BUSY_MIN = 2

    if success and (busy_a_count < BUSY_MIN or busy_b_count < BUSY_MIN):
        success = False
        failure_reason = (f"each busy marker must appear at least "
                          f"{BUSY_MIN} times; saw busyA={busy_a_count} "
                          f"busyB={busy_b_count}")

    if success:
        print(f"[ok] preemptive demo passed in <={args.timeout:.0f}s")
        for m in found:
            print(f"     + {m!r} present")
        print(f"     + busyA marker count = {busy_a_count}")
        print(f"     + busyB marker count = {busy_b_count}")
        rc = 0
    else:
        print("[err] preemptive demo failed", file=sys.stderr)
        if failure_reason is not None:
            print(f"      reason: {failure_reason}", file=sys.stderr)
        for m in missing:
            print(f"      - missing: {m!r}", file=sys.stderr)
        print(f"      busyA marker count = {busy_a_count}",
              file=sys.stderr)
        print(f"      busyB marker count = {busy_b_count}",
              file=sys.stderr)
        print_log_tail(debugcon_log)
        rc = 1

    if not args.keep_disk:
        cleanup_file(disk_path)
    cleanup_file(ovmf_vars_runtime)

    return rc


if __name__ == "__main__":
    sys.exit(main())
