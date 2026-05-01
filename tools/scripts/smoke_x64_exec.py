#!/usr/bin/env python3
"""
CapyOS x64 smoke for SYS_EXEC (M5 phase B.7).

Boots a kernel built with `-DCAPYOS_BOOT_RUN_HELLO`, the userland
hello rebuilt with `-DCAPYOS_HELLO_EXEC`, and the second user
binary `exectarget` linked into the kernel image.

The hello binary writes `[before-exec]` once, then calls
`capy_exec("/bin/exectarget", NULL)`. On success the kernel
overrides the syscall return frame so sysret lands at
exectarget's `_start`, which writes `[exec-ok]` and exits
cleanly. The smoke validates the full chain end-to-end:

  - `[before-exec]` proves hello reached ring 3 and got past the
    SYS_WRITE round-trip prior to exec.
  - `[exec-ok]` proves:
      * embedded_progs_lookup resolved the path correctly,
      * process_exec_replace built the new AS, loaded the new ELF,
        and switched CR3 onto it,
      * sysret rewrite of frame->rcx/rsp/r11 worked: the user CPU
        landed at the new entry point with the new user RSP,
      * exectarget's SYS_WRITE + crt0's SYS_EXIT round-trips
        succeeded against the new AS.
  - `[exec-failed]` is the regression marker hello prints if
    capy_exec ever returns; it MUST be absent.
  - `panic` MUST be absent.

Usage (CI):
  make smoke-x64-exec
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
    "[before-exec]",
    "[exec-ok]",
)
FAILURE_MARKERS = (
    "panic",
    "[exec-failed]",
    "[user_init] hello spawn returned without entering Ring 3.",
)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--qemu", default="qemu-system-x86_64")
    parser.add_argument("--ovmf", default=None)
    parser.add_argument("--memory", type=int, default=512)
    parser.add_argument("--timeout", type=float, default=45.0)
    parser.add_argument(
        "--log",
        default="build/ci/smoke_x64_exec.log")
    parser.add_argument(
        "--debugcon-log",
        default="build/ci/smoke_x64_exec.debugcon.log")
    parser.add_argument(
        "--disk",
        default="build/ci/smoke_x64_exec.img")
    parser.add_argument("--disk-size", default="2G")
    parser.add_argument("--keep-disk", action="store_true")
    parser.add_argument("--storage-bus", choices=("sata", "nvme"),
                        default="sata")
    parser.add_argument("--volume-key",
                        default="CAPYOS-SMOKE-KEY-2026-0001")
    parser.add_argument("--keyboard-layout", default="us")
    parser.add_argument("--language", default="en")
    return parser.parse_args()


def all_markers_present(text: str) -> bool:
    return all(m in text for m in SUCCESS_MARKERS)


def any_failure_marker_present(text: str) -> str | None:
    for m in FAILURE_MARKERS:
        if m in text:
            return m
    return None


def poll_debugcon(debugcon_log: Path, timeout: float
                  ) -> tuple[bool, str | None]:
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
                  f"with the M5 phase B.7 flag set?", file=sys.stderr)
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
        print(f"[ok] exec smoke passed in <={args.timeout:.0f}s")
        for m in found:
            print(f"     + {m!r} present")
        rc = 0
    else:
        print("[err] exec smoke failed", file=sys.stderr)
        if failure_reason is not None:
            print(f"      reason: failure marker {failure_reason!r}",
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
