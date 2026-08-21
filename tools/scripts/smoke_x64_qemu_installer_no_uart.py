#!/usr/bin/env python3
"""QEMU/UEFI regression for installer input with no emulated UART.

The VM boots the official installer ISO with ``-serial none`` and a disposable
blank disk.  The harness waits for a loader marker on isa-debugcon, leaves the
target-selection prompt idle, then injects exactly ``0`` + Enter through QEMU's
HMP ``sendkey`` command.

Passing proves that the idle prompt did not consume a phantom byte (notably the
0xFF returned by an unmapped COM1 port) and that UEFI ConIn accepted the HMP
keyboard input.  Cancellation happens before any destructive installer step;
the target image must remain SHA-256 identical.
"""

from __future__ import annotations

import argparse
import os
import socket
import subprocess
import sys
import tempfile
import time
from pathlib import Path

from smoke_x64_common import (
    cleanup_file,
    create_runtime_ovmf_vars,
    print_log_tail,
    resolve_ovmf_or_raise,
    resolve_qemu_binary,
    run_build_if_requested,
    validate_iso_artifact,
)
from smoke_x64_iso_install import (
    file_sha256,
    prepare_exclusive_disk,
    require_safe_disk_path,
)
from smoke_x64_session import qemu_accelerator, reset_capture_file


REPO_ROOT = Path(__file__).resolve().parents[2]
TARGET_PROMPT_MARKER = "[installer-input] target-prompt"
SOURCE_CONIN_MARKER = "[installer-input] source=conin"
SOURCE_COM1_MARKER = "[installer-input] source=com1"
TARGET_CANCEL_MARKER = "[installer-input] target-cancel"

KEY_MAP = {
    "0": "0",
    "\n": "ret",
}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--iso",
        default="build/CapyOS-Installer-UEFI.iso",
        help="Official installer ISO path",
    )
    parser.add_argument("--qemu", default="qemu-system-x86_64")
    parser.add_argument("--ovmf", default=None, help="Path to OVMF_CODE.fd")
    parser.add_argument("--memory", type=int, default=1024)
    parser.add_argument(
        "--timeout",
        type=float,
        default=60.0,
        help="Bound for each prompt/cancel wait",
    )
    parser.add_argument(
        "--idle-seconds",
        type=float,
        default=2.0,
        help="Negative-observation window after the prompt is ready",
    )
    parser.add_argument(
        "--build",
        action="store_true",
        help="Build all64, iso-uefi and manifest64 before running",
    )
    parser.add_argument(
        "--log",
        default="build/ci/smoke_x64_qemu_installer_no_uart.log",
    )
    parser.add_argument(
        "--debugcon-log",
        default="build/ci/smoke_x64_qemu_installer_no_uart.debugcon.log",
    )
    parser.add_argument(
        "--qtree-log",
        default="build/ci/smoke_x64_qemu_installer_no_uart.qtree.log",
    )
    parser.add_argument(
        "--disk",
        default="build/ci/smoke_x64_qemu_installer_no_uart.img",
    )
    parser.add_argument("--disk-size", default="2G")
    parser.add_argument("--keep-disk", action="store_true")
    return parser.parse_args()


def chars_to_sendkey_lines(text: str) -> list[str]:
    commands: list[str] = []
    for char in text:
        token = KEY_MAP.get(char)
        if token is None:
            raise ValueError(f"unmapped installer-input character: {char!r}")
        commands.append(f"sendkey {token}\n")
    return commands


def make_no_uart_qemu_cmd(
    *,
    qemu_bin: str,
    ovmf_code: str,
    ovmf_vars_runtime: Path,
    iso_path: Path,
    disk_path: Path,
    memory_mb: int,
    debugcon_log: Path,
    monitor_socket: Path,
) -> list[str]:
    """Build the intentionally UART-free QEMU command under host contract."""
    return [
        qemu_bin,
        "-machine",
        f"q35,accel={qemu_accelerator()}",
        "-m",
        str(memory_mb),
        "-boot",
        "once=d,menu=off",
        "-drive",
        f"if=pflash,format=raw,readonly=on,file={ovmf_code}",
        "-drive",
        f"if=pflash,format=raw,file={ovmf_vars_runtime}",
        "-serial",
        "none",
        "-display",
        "none",
        "-monitor",
        f"unix:{monitor_socket},server,nowait",
        "-no-reboot",
        "-nic",
        "none",
        "-global",
        "isa-debugcon.iobase=0xe9",
        "-debugcon",
        f"file:{debugcon_log}",
        "-drive",
        f"format=raw,file={disk_path}",
        "-drive",
        f"file={iso_path},media=cdrom,readonly=on",
    ]


def installer_input_trace_passes(text: str) -> bool:
    """Accept one prompt -> ConIn latch -> exact-line cancellation trace."""
    if SOURCE_COM1_MARKER in text:
        return False
    if text.count(TARGET_PROMPT_MARKER) != 1:
        return False
    if text.count(SOURCE_CONIN_MARKER) != 1:
        return False
    if text.count(TARGET_CANCEL_MARKER) != 1:
        return False
    return (
        text.index(TARGET_PROMPT_MARKER)
        < text.index(SOURCE_CONIN_MARKER)
        < text.index(TARGET_CANCEL_MARKER)
    )


def qtree_proves_no_isa_serial(text: str) -> bool:
    """Require a real qtree response before accepting UART absence."""
    normalized = text.lower()
    if "unknown command" in normalized or "command not found" in normalized:
        return False
    return (
        "bus: main-system-bus" in normalized
        and "type system" in normalized
        and "dev:" in normalized
        and "isa-serial" not in normalized
    )


def read_debugcon(path: Path) -> str:
    try:
        return path.read_text(encoding="latin-1", errors="replace")
    except FileNotFoundError:
        return ""


def wait_for_marker(
    path: Path,
    marker: str,
    proc: subprocess.Popen[bytes],
    timeout: float,
) -> str:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        text = read_debugcon(path)
        if marker in text:
            return text
        if proc.poll() is not None:
            text = read_debugcon(path)
            if marker in text:
                return text
            raise RuntimeError(f"QEMU exited early with code {proc.returncode}")
        time.sleep(0.05)
    text = read_debugcon(path)
    if marker in text:
        return text
    raise TimeoutError(f"timeout waiting for debugcon marker: {marker!r}")


def require_idle_prompt(
    path: Path,
    proc: subprocess.Popen[bytes],
    idle_seconds: float,
) -> None:
    deadline = time.monotonic() + idle_seconds
    while time.monotonic() < deadline:
        text = read_debugcon(path)
        if text.count(TARGET_PROMPT_MARKER) != 1:
            raise RuntimeError("installer target prompt repeated before input")
        if TARGET_CANCEL_MARKER in text:
            raise RuntimeError("installer cancelled before HMP input")
        if SOURCE_CONIN_MARKER in text:
            raise RuntimeError("UEFI ConIn latched before HMP input")
        if SOURCE_COM1_MARKER in text:
            raise RuntimeError("installer latched COM1 while no UART was present")
        if proc.poll() is not None:
            raise RuntimeError(f"QEMU exited while prompt was idle: {proc.returncode}")
        time.sleep(0.05)


def connect_hmp(
    monitor_socket: Path,
    proc: subprocess.Popen[bytes],
    timeout: float,
) -> socket.socket:
    if not hasattr(socket, "AF_UNIX"):
        raise RuntimeError("this QEMU smoke requires Unix-domain socket support")
    deadline = time.monotonic() + timeout
    last_error: OSError | None = None
    while time.monotonic() < deadline:
        if proc.poll() is not None:
            raise RuntimeError(f"QEMU exited before HMP connected: {proc.returncode}")
        candidate = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        try:
            candidate.connect(str(monitor_socket))
            candidate.settimeout(0.3)
            hmp_drain(candidate, timeout=0.5)
            return candidate
        except OSError as exc:
            last_error = exc
            candidate.close()
            time.sleep(0.05)
    raise RuntimeError(f"failed to connect QEMU HMP monitor: {last_error}")


def hmp_send(sock: socket.socket, payload: str) -> None:
    sock.sendall(payload.encode("ascii"))


def hmp_drain(sock: socket.socket, timeout: float = 0.2) -> str:
    sock.settimeout(timeout)
    chunks: list[bytes] = []
    try:
        while True:
            data = sock.recv(4096)
            if not data:
                break
            chunks.append(data)
    except (socket.timeout, BlockingIOError):
        pass
    return b"".join(chunks).decode("latin-1", errors="replace")


def hmp_command(sock: socket.socket, command: str, timeout: float = 3.0) -> str:
    """Run one HMP command and collect output through the next prompt."""
    hmp_send(sock, command.rstrip("\n") + "\n")
    deadline = time.monotonic() + timeout
    chunks: list[bytes] = []
    while time.monotonic() < deadline:
        remaining = deadline - time.monotonic()
        sock.settimeout(max(0.01, min(0.2, remaining)))
        try:
            data = sock.recv(4096)
        except socket.timeout:
            continue
        if not data:
            raise RuntimeError(f"HMP disconnected while running {command!r}")
        chunks.append(data)
        if b"(qemu)" in b"".join(chunks):
            return b"".join(chunks).decode("latin-1", errors="replace")
    raise TimeoutError(f"timeout waiting for HMP command: {command!r}")


def stop_qemu(proc: subprocess.Popen[bytes] | None) -> None:
    if proc is None or proc.poll() is not None:
        return
    proc.terminate()
    try:
        proc.wait(timeout=5)
    except subprocess.TimeoutExpired:
        proc.kill()
        proc.wait(timeout=5)


def main() -> int:
    args = parse_args()
    if args.memory <= 0 or args.timeout <= 0 or args.idle_seconds <= 0:
        print("[err] memory, timeout and idle-seconds must be positive", file=sys.stderr)
        return 2

    log_path = (REPO_ROOT / args.log).resolve()
    debugcon_log = (REPO_ROOT / args.debugcon_log).resolve()
    qtree_log = (REPO_ROOT / args.qtree_log).resolve()
    disk_path = (REPO_ROOT / args.disk).resolve()
    log_path.parent.mkdir(parents=True, exist_ok=True)
    debugcon_log.parent.mkdir(parents=True, exist_ok=True)
    qtree_log.parent.mkdir(parents=True, exist_ok=True)

    ovmf_vars_runtime: Path | None = None
    monitor_dir: Path | None = None
    monitor_socket: Path | None = None
    proc: subprocess.Popen[bytes] | None = None
    hmp: socket.socket | None = None
    disk_created = False
    smoke_completed = False
    evidence_initialized = False
    disk_hash_before = ""
    disk_hash_after = ""
    iso_hash = ""

    try:
        run_build_if_requested(REPO_ROOT, args.build)
        qemu_bin = resolve_qemu_binary(args.qemu)
        ovmf_code, ovmf_vars_template = resolve_ovmf_or_raise(args.ovmf)
        iso_path = validate_iso_artifact(REPO_ROOT, args.iso)
        iso_hash = file_sha256(iso_path)

        named_paths = (log_path, debugcon_log, qtree_log, disk_path)
        if len(set(named_paths)) != len(named_paths) or disk_path == iso_path:
            raise ValueError("disk, ISO and evidence paths must be distinct")
        require_safe_disk_path(REPO_ROOT, disk_path)
        prepare_exclusive_disk(disk_path, args.disk_size)
        disk_created = True
        disk_hash_before = file_sha256(disk_path)

        reset_capture_file(log_path)
        reset_capture_file(debugcon_log)
        reset_capture_file(qtree_log)
        evidence_initialized = True
        ovmf_vars_runtime = create_runtime_ovmf_vars(
            log_path, ovmf_vars_template
        )
        monitor_dir = Path(tempfile.mkdtemp(prefix="capyos_no_uart_hmp_"))
        monitor_socket = monitor_dir / "hmp.sock"
        cmd = make_no_uart_qemu_cmd(
            qemu_bin=qemu_bin,
            ovmf_code=ovmf_code,
            ovmf_vars_runtime=ovmf_vars_runtime,
            iso_path=iso_path,
            disk_path=disk_path,
            memory_mb=args.memory,
            debugcon_log=debugcon_log,
            monitor_socket=monitor_socket,
        )

        print("[info] booting official UEFI ISO with -serial none")
        print(f"[info] ISO: {iso_path}")
        print(f"[info] ISO sha256: {iso_hash}")
        print(f"[info] disk sha256 before: {disk_hash_before}")
        print(f"[info] debugcon evidence: {debugcon_log}")
        print(f"[info] QEMU device-tree evidence: {qtree_log}")
        with log_path.open("wb") as log_file:
            proc = subprocess.Popen(
                cmd,
                stdin=subprocess.DEVNULL,
                stdout=log_file,
                stderr=subprocess.STDOUT,
            )
            try:
                wait_for_marker(
                    debugcon_log, TARGET_PROMPT_MARKER, proc, args.timeout
                )
                hmp = connect_hmp(monitor_socket, proc, timeout=5.0)
                qtree = hmp_command(hmp, "info qtree")
                qtree_log.write_text(qtree, encoding="latin-1")
                if not qtree_proves_no_isa_serial(qtree):
                    raise RuntimeError(
                        "QEMU qtree did not prove a valid isa-serial-free topology"
                    )
                print("[info] QEMU qtree confirms isa-serial is absent")
                require_idle_prompt(
                    debugcon_log, proc, idle_seconds=args.idle_seconds
                )
                for command in chars_to_sendkey_lines("0\n"):
                    response = hmp_command(hmp, command)
                    if "unknown command" in response.lower():
                        raise RuntimeError(f"QEMU rejected HMP input: {command!r}")
                    time.sleep(0.15)
                text = wait_for_marker(
                    debugcon_log, TARGET_CANCEL_MARKER, proc, args.timeout
                )
                if not installer_input_trace_passes(text):
                    raise RuntimeError(
                        "debugcon trace did not contain one prompt followed "
                        "by one cancellation"
                    )
            finally:
                if hmp is not None:
                    hmp.close()
                    hmp = None
                stop_qemu(proc)

        disk_hash_after = file_sha256(disk_path)
        if disk_hash_after != disk_hash_before:
            raise RuntimeError("installer no-UART cancel path changed target disk")
        smoke_completed = True
    except Exception as exc:
        print(f"[err] QEMU installer no-UART smoke failed: {exc}", file=sys.stderr)
        if disk_created and not disk_hash_after:
            try:
                disk_hash_after = file_sha256(disk_path)
            except OSError as hash_exc:
                print(f"[err] could not hash preserved disk: {hash_exc}", file=sys.stderr)
        if disk_hash_before:
            print(f"[evidence] disk sha256 before: {disk_hash_before}", file=sys.stderr)
        if disk_hash_after:
            print(f"[evidence] disk sha256 after:  {disk_hash_after}", file=sys.stderr)
        if evidence_initialized:
            print_log_tail(log_path)
            print_log_tail(debugcon_log)
            print_log_tail(qtree_log)
        return 1
    finally:
        stop_qemu(proc)
        if monitor_socket is not None:
            cleanup_file(monitor_socket)
        if monitor_dir is not None:
            try:
                os.rmdir(monitor_dir)
            except OSError:
                pass
        if smoke_completed:
            cleanup_file(ovmf_vars_runtime)
            if not args.keep_disk:
                cleanup_file(disk_path)
        else:
            if disk_created:
                print(f"[evidence] preserved disk: {disk_path}", file=sys.stderr)
            if ovmf_vars_runtime is not None:
                print(
                    f"[evidence] preserved OVMF vars: {ovmf_vars_runtime}",
                    file=sys.stderr,
                )

    print("[ok] no-UART installer prompt stayed clean and accepted ConIn input")
    print(f"[ok] disk sha256 unchanged: {disk_hash_after}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
