#!/usr/bin/env python3
"""
CapyOS x64 smoke for capysh interactive shell (M5 phase E.6).

Boots a kernel built with `-DCAPYOS_PREEMPTIVE_SCHEDULER
-DCAPYOS_BOOT_RUN_HELLO -DCAPYOS_BOOT_RUN_CAPYSH`. The kernel
spawns `/bin/capysh` directly into ring 3. The smoke connects to
the QEMU HMP monitor via a unix socket and sends a sequence of
`sendkey` commands that type:

    help<ENTER>
    pid<ENTER>
    exectarget<ENTER>
    exit<ENTER>

Pass criteria (debug-console log):

  * `[capysh] CapyOS interactive shell` (banner) present.
  * `help               list builtins` (one of the help-text lines)
    present, proving capysh's `help` builtin ran.
  * `pid=` present, proving capysh issued SYS_GETPID via capylibc
    and formatted the result.
  * `[exec-ok]` present, proving capysh's `exectarget` builtin
    completed the full fork+exec+wait round-trip with the embedded
    /bin/exectarget binary.
  * `panic` absent.
  * `[capysh] unknown:` absent (regression: typing went into the
    wrong line buffer).

The harness overrides QEMU's monitor argument (the standard helper
hardcodes `-monitor none`) so it can drive `sendkey`. It uses HMP
over a Unix socket; QMP would require richer JSON handling and is
not necessary here.
"""

from __future__ import annotations

import argparse
import os
import signal
import socket as pysock
import subprocess
import sys
import tempfile
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
from smoke_x64_session import choose_free_port  # noqa: E402

SUCCESS_MARKERS = (
    "[capysh] CapyOS interactive shell",
    "help               list builtins",
    "pid=",
    "[exec-ok]",
)
FAILURE_MARKERS = (
    "panic",
    "[capysh] unknown:",
    "[capysh] fork failed",
    "[capysh] exec failed",
)

# Map a typed character to a QEMU HMP `sendkey` token. Only the
# subset capysh exercises is enumerated; unknown chars raise.
KEY_MAP = {
    " ": "spc",
    "\n": "ret",
    "-": "minus",
    "_": "shift-minus",
    "/": "slash",
    "0": "0", "1": "1", "2": "2", "3": "3", "4": "4",
    "5": "5", "6": "6", "7": "7", "8": "8", "9": "9",
}
for ch in "abcdefghijklmnopqrstuvwxyz":
    KEY_MAP[ch] = ch


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--qemu", default="qemu-system-x86_64")
    parser.add_argument("--ovmf", default=None)
    parser.add_argument("--memory", type=int, default=512)
    parser.add_argument("--timeout", type=float, default=60.0)
    parser.add_argument("--log", default="build/ci/smoke_x64_capysh.log")
    parser.add_argument("--debugcon-log",
                        default="build/ci/smoke_x64_capysh.debugcon.log")
    parser.add_argument("--disk",
                        default="build/ci/smoke_x64_capysh.img")
    parser.add_argument("--disk-size", default="2G")
    parser.add_argument("--keep-disk", action="store_true")
    parser.add_argument("--storage-bus", choices=("sata", "nvme"),
                        default="sata")
    parser.add_argument("--volume-key",
                        default="CAPYOS-SMOKE-KEY-2026-0001")
    parser.add_argument("--keyboard-layout", default="us")
    parser.add_argument("--language", default="en")
    parser.add_argument("--banner-wait-seconds", type=float, default=20.0)
    return parser.parse_args()


def chars_to_sendkey_lines(text: str) -> list[str]:
    out: list[str] = []
    for ch in text:
        token = KEY_MAP.get(ch)
        if token is None:
            raise ValueError(f"unmapped char {ch!r} in capysh script")
        out.append(f"sendkey {token}\n")
    return out


def hmp_send(sock: pysock.socket, payload: str) -> None:
    sock.sendall(payload.encode("ascii"))


def hmp_drain(sock: pysock.socket, timeout: float = 0.2) -> str:
    sock.settimeout(timeout)
    chunks: list[bytes] = []
    try:
        while True:
            data = sock.recv(4096)
            if not data:
                break
            chunks.append(data)
    except (pysock.timeout, BlockingIOError):
        pass
    return b"".join(chunks).decode("latin-1", errors="replace")


def wait_for_marker(debugcon_log: Path, marker: str,
                    timeout: float) -> bool:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        try:
            text = debugcon_log.read_text(encoding="latin-1",
                                          errors="replace")
        except FileNotFoundError:
            text = ""
        if marker in text:
            return True
        time.sleep(0.1)
    return False


def make_qemu_cmd_with_monitor(
    qemu_bin: str,
    ovmf_code: str,
    ovmf_vars_runtime: Path,
    disk_path: Path,
    serial_port: int,
    memory_mb: int,
    storage_bus: str,
    debugcon_log: Path,
    monitor_socket: Path,
) -> list[str]:
    """Build a QEMU command line with an HMP unix-socket monitor.

    Mirrors the `make_qemu_cmd` helper from smoke_x64_session but
    swaps `-monitor none` for `-monitor unix:...,server,nowait` so
    the harness can drive sendkey commands. """
    cmd = [
        qemu_bin,
        "-machine", "q35,accel=tcg",
        "-m", str(memory_mb),
        "-boot", "c",
        "-drive", f"if=pflash,format=raw,readonly=on,file={ovmf_code}",
        "-drive", f"if=pflash,format=raw,file={ovmf_vars_runtime}",
        "-serial", f"tcp:127.0.0.1:{serial_port},server,nowait",
        "-display", "none",
        "-monitor", f"unix:{monitor_socket},server,nowait",
        "-no-reboot",
        "-global", "isa-debugcon.iobase=0xe9",
        "-debugcon", f"file:{debugcon_log}",
    ]
    if storage_bus == "nvme":
        cmd.extend([
            "-drive", f"if=none,id=nvme0,file={disk_path},format=raw",
            "-device", "nvme,drive=nvme0,serial=CAPYNVME001",
        ])
    else:
        cmd.extend([
            "-drive",
            f"if=none,id=sata0,file={disk_path},format=raw",
            "-device", "ahci,id=ahci0",
            "-device", "ide-hd,drive=sata0,bus=ahci0.0",
        ])
    return cmd


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
                  f"with the M5 phase E.6 flag set?", file=sys.stderr)
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

    monitor_dir = Path(tempfile.mkdtemp(prefix="capysh_monitor_"))
    monitor_socket = monitor_dir / "qmp.sock"

    cmd = make_qemu_cmd_with_monitor(
        qemu_bin=qemu_bin,
        ovmf_code=ovmf_code,
        ovmf_vars_runtime=ovmf_vars_runtime,
        disk_path=disk_path,
        serial_port=serial_port,
        memory_mb=args.memory,
        storage_bus=args.storage_bus,
        debugcon_log=debugcon_log,
        monitor_socket=monitor_socket,
    )

    print(f"[info] launching QEMU; debugcon={debugcon_log}")
    rc = 1
    failure_reason: str | None = None
    with log_path.open("wb") as log_fh:
        proc = subprocess.Popen(cmd, stdout=log_fh, stderr=subprocess.STDOUT)
        try:
            # Wait for capysh banner before injecting keys; otherwise
            # bytes go into the early TTY and are lost.
            banner_seen = wait_for_marker(
                debugcon_log,
                "[capysh] CapyOS interactive shell",
                timeout=args.banner_wait_seconds,
            )
            if not banner_seen:
                failure_reason = "capysh banner did not appear in time"
            else:
                # Connect to HMP monitor and drive the script.
                # The unix socket is created by QEMU when the monitor
                # comes up; small retry loop handles startup race.
                sock = pysock.socket(pysock.AF_UNIX, pysock.SOCK_STREAM)
                connected = False
                deadline = time.monotonic() + 5.0
                while time.monotonic() < deadline:
                    try:
                        sock.connect(str(monitor_socket))
                        connected = True
                        break
                    except (FileNotFoundError, ConnectionRefusedError):
                        time.sleep(0.1)
                if not connected:
                    failure_reason = "could not connect to QEMU HMP monitor"
                else:
                    hmp_drain(sock, timeout=0.5)
                    # Type the script with small inter-line delays so
                    # capysh has time to print the prompt between
                    # commands. The kernel TTY also echoes; capysh
                    # reads the same bytes via stdin_buf.
                    for line in [
                        "help\n",
                        "pid\n",
                        "exectarget\n",
                        "exit\n",
                    ]:
                        for payload in chars_to_sendkey_lines(line):
                            hmp_send(sock, payload)
                            time.sleep(0.04)
                        # Pause between lines so the kernel has time
                        # to process and capysh to print before the
                        # next batch starts queueing in stdin_buf.
                        time.sleep(1.5)
                        hmp_drain(sock, timeout=0.1)
                    sock.close()

                # After typing, give the kernel up to `timeout` to
                # surface every success marker.
                deadline = time.monotonic() + args.timeout
                while time.monotonic() < deadline:
                    text = debugcon_log.read_text(encoding="latin-1",
                                                  errors="replace")
                    bad = next(
                        (m for m in FAILURE_MARKERS if m in text), None)
                    if bad is not None:
                        failure_reason = f"failure marker {bad!r}"
                        break
                    if all(m in text for m in SUCCESS_MARKERS):
                        rc = 0
                        break
                    time.sleep(0.2)
                else:
                    failure_reason = "success markers did not all appear"
        finally:
            if proc.poll() is None:
                try:
                    proc.send_signal(signal.SIGTERM)
                    proc.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    proc.kill()

    text = debugcon_log.read_text(encoding="latin-1", errors="replace")
    found = [m for m in SUCCESS_MARKERS if m in text]
    missing = [m for m in SUCCESS_MARKERS if m not in text]
    if rc == 0:
        print(f"[ok] capysh smoke passed in <={args.timeout:.0f}s")
        for m in found:
            print(f"     + {m!r} present")
    else:
        print("[err] capysh smoke failed", file=sys.stderr)
        if failure_reason:
            print(f"      reason: {failure_reason}", file=sys.stderr)
        for m in missing:
            print(f"      - missing: {m!r}", file=sys.stderr)
        print_log_tail(debugcon_log)

    if not args.keep_disk:
        cleanup_file(disk_path)
    cleanup_file(ovmf_vars_runtime)
    try:
        if monitor_socket.exists():
            monitor_socket.unlink()
        os.rmdir(monitor_dir)
    except OSError:
        pass
    return rc


if __name__ == "__main__":
    sys.exit(main())
