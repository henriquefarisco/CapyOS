#!/usr/bin/env python3
"""
CapyOS x64 smoke for the embedded user binary `hello` (M4 phase 5e).

Boots a kernel built with `-DCAPYOS_BOOT_RUN_HELLO` in QEMU/UEFI and
verifies that the boot path actually drops into Ring 3, executes the
embedded `hello` binary, and writes "hello, capyland" to the kernel
debug console (port 0xE9).

This is the first end-to-end verification of:

  - Phase 3   : SYSCALL/SYSRET MSR + GDT user descriptors.
  - Phase 3.5 : enter_user_mode IRET frame + per-CPU GS_BASE area.
  - Phase 4   : the fault classifier does NOT fire on a well-behaved
                user binary (regression guard).
  - Phase 5a  : capylibc syscall stubs.
  - Phase 5b  : hello.elf statically linked through capylibc.
  - Phase 5c  : objcopy-embedded blob + kernel_spawn_embedded_hello.
  - Phase 5d  : kernel_main wiring under #ifdef CAPYOS_BOOT_RUN_HELLO.

Usage (CI):
  make smoke-x64-hello-user

Manual (developer):
  make clean
  make all64 EXTRA_CFLAGS64='-DCAPYOS_BOOT_RUN_HELLO'
  make iso-uefi
  make manifest64
  python3 tools/scripts/smoke_x64_hello_user.py

Pass criteria (matched against the debug-console log file):
  * "[user_init] CAPYOS_BOOT_RUN_HELLO defined;" present.
  * "hello, capyland" present.
  * "panic" absent.
  * "[user_init] hello spawn returned without entering Ring 3."
    absent (regression guard for ELF / spawn failures).

Failure handling:
  Any criterion missed -> exit code 1 + tail of debugcon log to stderr.
  QEMU process is killed once both success markers appear or after the
  configured timeout, whichever happens first.
"""

from __future__ import annotations

import argparse
import os
import shutil
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
    "[user_init] CAPYOS_BOOT_RUN_HELLO defined;",
    "hello, capyland",
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
    parser.add_argument("--timeout", type=float, default=30.0,
                        help="Seconds to wait for both success markers")
    parser.add_argument("--log",
                        default="build/ci/smoke_x64_hello_user.log",
                        help="QEMU stdout/stderr log path")
    parser.add_argument("--debugcon-log",
                        default="build/ci/smoke_x64_hello_user.debugcon.log",
                        help="Kernel debug-console log path (port 0xE9)")
    parser.add_argument("--disk",
                        default="build/ci/smoke_x64_hello_user.img",
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


def poll_debugcon(debugcon_log: Path, timeout: float) -> tuple[bool, str | None]:
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

    bootx64 = REPO_ROOT / "build" / "iso-uefi-root" / "EFI" / "BOOT" / "BOOTX64.EFI"
    kernel = REPO_ROOT / "build" / "capyos64.bin"
    manifest = REPO_ROOT / "build" / "manifest.bin"
    for required in (bootx64, kernel, manifest):
        if not required.exists():
            print(f"[err] missing build artifact: {required}\n"
                  f"      did you run `make all64 iso-uefi manifest64` "
                  f"with EXTRA_CFLAGS64='-DCAPYOS_BOOT_RUN_HELLO'?",
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

    # Even though this smoke only consumes the kernel debug-console
    # (port 0xE9), `make_qemu_cmd` always emits a TCP serial config.
    # Pick a free port so concurrent CI runs do not collide; we
    # never connect to the resulting socket.
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
        print(f"[ok] hello-user smoke passed in <={args.timeout:.0f}s")
        for m in found:
            print(f"     + {m!r} present")
        rc = 0
    else:
        print("[err] hello-user smoke failed", file=sys.stderr)
        if failure_reason is not None:
            print(f"      reason: failure marker {failure_reason!r} appeared",
                  file=sys.stderr)
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
