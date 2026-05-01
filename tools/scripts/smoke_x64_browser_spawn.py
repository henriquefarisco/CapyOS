#!/usr/bin/env python3
"""
CapyOS x64 smoke for the F3.3e browser-isolation end-to-end path.

Boots a kernel built with
`-DCAPYOS_PREEMPTIVE_SCHEDULER -DCAPYOS_BOOT_RUN_BROWSER_SMOKE` in
QEMU/UEFI and validates that the chrome/runtime stack performs a
full IPC round-trip with the embedded `/bin/capybrowser` ring 3
process:

    chrome (kernel)  ──NAVIGATE──▶  capybrowser  (ring 3)
                    ◀─NAV_STARTED + 3× NAV_PROGRESS + FRAME + NAV_READY
                    ──PING────────▶
                    ◀─PONG─────────
                    ──SHUTDOWN────▶
                                          (engine exits, EOF on pipe)

Validates:
  - Phase 5d (kernel_main wiring) extended with the new
    CAPYOS_BOOT_RUN_BROWSER_SMOKE branch.
  - F3.3a IPC codec (header round-trip across pipe boundary).
  - F3.3b stub `/bin/capybrowser` actually executes in ring 3.
  - F3.3d chrome dispatcher consumes events correctly.
  - F3.3d watchdog (PING -> PONG) works under preemptive scheduler.
  - F3.3d spawn helper sets up two pipes + FDs 0/1 correctly.
  - Ring-3 IPC with kernel-side compositor task interleaves under
    APIC ticks (M4 phase 8 preemption foundation).

Usage (CI):
  make smoke-x64-browser-spawn

Manual (developer):
  make clean
  make all64 EXTRA_CFLAGS64='-DCAPYOS_PREEMPTIVE_SCHEDULER -DCAPYOS_BOOT_RUN_BROWSER_SMOKE'
  make iso-uefi
  make manifest64
  python3 tools/scripts/smoke_x64_browser_spawn.py

Pass criteria (all must be present in the kernel debugcon log):

  - "[browser-smoke] spawn pid=" : engine spawned with PID assigned
  - "[browser-smoke] navigate-sent" : NAVIGATE encoded + written
  - "[browser-smoke] event NAV_STARTED" : engine started navigation
  - "[browser-smoke] event FRAME" : pixels delivered through pipe
  - "[browser-smoke] event NAV_READY" : navigation completed
  - "[browser-smoke] event PONG" : watchdog round-trip works
  - "[browser-smoke] shutdown-sent" : graceful shutdown initiated
  - "[browser-smoke] engine-eof" : engine exited cleanly
  - "[browser-smoke] OK" : poller validated full sequence

Failure markers (any present -> immediate failure):

  - "panic"
  - "[browser-smoke] FAIL"

Failure handling: any criterion missed within --timeout seconds ->
exit code 1 + tail of debugcon log to stderr.
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
    "[browser-smoke] spawn pid=",
    "[browser-smoke] navigate-sent",
    "[browser-smoke] event NAV_STARTED",
    "[browser-smoke] event FRAME",
    "[browser-smoke] event NAV_READY",
    "[browser-smoke] event PONG",
    "[browser-smoke] shutdown-sent",
    "[browser-smoke] engine-eof",
    "[browser-smoke] OK",
)

FAILURE_MARKERS = (
    "panic",
    "[browser-smoke] FAIL",
)


def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--qemu", default="qemu-system-x86_64",
                   help="QEMU binary")
    p.add_argument("--ovmf", default=None, help="OVMF_CODE.fd path")
    p.add_argument("--memory", type=int, default=512,
                   help="Guest memory in MB")
    p.add_argument("--timeout", type=float, default=45.0,
                   help="Seconds to wait for all success markers")
    p.add_argument("--log",
                   default="build/ci/smoke_x64_browser_spawn.log",
                   help="QEMU stdout/stderr log path")
    p.add_argument("--debugcon-log",
                   default="build/ci/smoke_x64_browser_spawn.debugcon.log",
                   help="Kernel debug-console log path (port 0xE9)")
    p.add_argument("--disk",
                   default="build/ci/smoke_x64_browser_spawn.img",
                   help="Provisioned GPT disk image path")
    p.add_argument("--disk-size", default="2G",
                   help="Disk size for GPT provisioning")
    p.add_argument("--keep-disk", action="store_true",
                   help="Do not delete provisioned disk image on exit")
    p.add_argument("--storage-bus", choices=("sata", "nvme"),
                   default="sata", help="Storage bus")
    p.add_argument("--volume-key",
                   default="CAPYOS-SMOKE-KEY-2026-0001",
                   help="Lab volume key persisted in CAPYCFG.BIN")
    p.add_argument("--keyboard-layout", default="us",
                   help="Keyboard layout persisted in CAPYCFG.BIN")
    p.add_argument("--language", default="en",
                   help="System language persisted in CAPYCFG.BIN")
    return p.parse_args()


def all_markers_present(text: str) -> bool:
    return all(m in text for m in SUCCESS_MARKERS)


def any_failure_marker(text: str) -> str | None:
    for m in FAILURE_MARKERS:
        if m in text:
            return m
    return None


def poll_debugcon(debugcon_log: Path,
                  timeout: float) -> tuple[bool, str | None]:
    """Returns (success, failure_reason). Polls every 100 ms."""
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        try:
            text = debugcon_log.read_text(encoding="latin-1",
                                          errors="replace")
        except FileNotFoundError:
            text = ""
        bad = any_failure_marker(text)
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

    bootx64 = REPO_ROOT / "build" / "iso-uefi-root" / "EFI" / "BOOT" / "BOOTX64.EFI"
    kernel = REPO_ROOT / "build" / "capyos64.bin"
    manifest = REPO_ROOT / "build" / "manifest.bin"
    for required in (bootx64, kernel, manifest):
        if not required.exists():
            print(f"[err] missing build artifact: {required}\n"
                  f"      did you run `make all64 iso-uefi manifest64` "
                  f"with EXTRA_CFLAGS64="
                  f"'-DCAPYOS_PREEMPTIVE_SCHEDULER "
                  f"-DCAPYOS_BOOT_RUN_BROWSER_SMOKE'?",
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

    if success:
        print(f"[ok] browser-spawn smoke passed in <={args.timeout:.0f}s")
        for m in found:
            print(f"     + {m!r}")
        rc = 0
    else:
        print("[err] browser-spawn smoke failed", file=sys.stderr)
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
    raise SystemExit(main())
