#!/usr/bin/env python3
"""
NoirOS x64 smoke test for login + core CLI commands in QEMU/UEFI.

I/O strategy:
- guest serial port is exposed as TCP server (`-serial tcp:127.0.0.1:<port>,server,nowait`)
- this script connects as client and drives login + commands
"""

from __future__ import annotations

import argparse
import os
import shutil
import socket
import subprocess
import sys
import threading
import time
from pathlib import Path


OVMF_CANDIDATES = (
    ("/usr/share/OVMF/OVMF_CODE_4M.fd", "/usr/share/OVMF/OVMF_VARS_4M.fd"),
    ("/usr/share/OVMF/OVMF_CODE.fd", "/usr/share/OVMF/OVMF_VARS.fd"),
    ("/usr/share/edk2/ovmf/OVMF_CODE.fd", "/usr/share/edk2/ovmf/OVMF_VARS.fd"),
    ("/usr/share/edk2-ovmf/x64/OVMF_CODE.fd", "/usr/share/edk2-ovmf/x64/OVMF_VARS.fd"),
)


class SmokeSession:
    def __init__(self, cmd: list[str], serial_port: int, log_path: Path, verbose: bool = False):
        self.cmd = cmd
        self.serial_port = serial_port
        self.log_path = log_path
        self.verbose = verbose

        self.proc: subprocess.Popen[bytes] | None = None
        self.sock: socket.socket | None = None

        self._lock = threading.Lock()
        self._buf = bytearray()

        self._proc_reader: threading.Thread | None = None
        self._serial_reader: threading.Thread | None = None

        self._logf = None

    def start(self) -> None:
        self.log_path.parent.mkdir(parents=True, exist_ok=True)
        self._logf = open(self.log_path, "wb")

        self.proc = subprocess.Popen(
            self.cmd,
            stdin=subprocess.DEVNULL,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
        )

        self._proc_reader = threading.Thread(target=self._read_proc_output, daemon=True)
        self._proc_reader.start()

        self.sock = self._connect_serial(timeout=20.0)
        self.sock.settimeout(0.3)

        self._serial_reader = threading.Thread(target=self._read_serial, daemon=True)
        self._serial_reader.start()

    def stop(self) -> None:
        if self.sock is not None:
            try:
                self.sock.close()
            except Exception:
                pass
            self.sock = None

        if self.proc is not None and self.proc.poll() is None:
            self.proc.terminate()
            try:
                self.proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.proc.kill()
                self.proc.wait(timeout=5)

        if self._proc_reader is not None:
            self._proc_reader.join(timeout=1)
        if self._serial_reader is not None:
            self._serial_reader.join(timeout=1)

        if self._logf is not None:
            self._logf.flush()
            self._logf.close()
            self._logf = None

    def _connect_serial(self, timeout: float) -> socket.socket:
        deadline = time.monotonic() + timeout
        last_exc: Exception | None = None
        while time.monotonic() < deadline:
            if self.proc is not None and self.proc.poll() is not None:
                raise RuntimeError(f"qemu exited early with code {self.proc.returncode}")
            try:
                return socket.create_connection(("127.0.0.1", self.serial_port), timeout=1.0)
            except OSError as exc:
                last_exc = exc
                time.sleep(0.1)
        raise RuntimeError(f"failed to connect serial tcp port {self.serial_port}: {last_exc}")

    def _read_proc_output(self) -> None:
        assert self.proc is not None
        assert self.proc.stdout is not None
        assert self._logf is not None
        while True:
            data = self.proc.stdout.read(1)
            if not data:
                return
            # Keep qemu diagnostics in the same log for troubleshooting.
            self._logf.write(data)

    def _read_serial(self) -> None:
        assert self.sock is not None
        assert self._logf is not None
        while True:
            try:
                data = self.sock.recv(4096)
            except socket.timeout:
                if self.proc is not None and self.proc.poll() is not None:
                    return
                continue
            except OSError:
                return
            if not data:
                return

            with self._lock:
                self._buf.extend(data)
            self._logf.write(data)
            if self.verbose:
                sys.stdout.write(data.decode("latin-1", errors="replace"))
                sys.stdout.flush()

    def marker(self) -> int:
        with self._lock:
            return len(self._buf)

    def text(self) -> str:
        with self._lock:
            return self._buf.decode("latin-1", errors="replace")

    def tail(self, max_bytes: int = 3000) -> str:
        with self._lock:
            data = self._buf[-max_bytes:]
        return data.decode("latin-1", errors="replace")

    def send_line(self, line: str) -> None:
        if self.sock is None:
            raise RuntimeError("serial socket is not connected")
        payload = line.encode("ascii", errors="ignore") + b"\r"
        self.sock.sendall(payload)

    def wait_for(self, pattern: str, timeout: float, start_at: int = 0) -> None:
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if self.proc is not None and self.proc.poll() is not None:
                raise RuntimeError(f"qemu exited early with code {self.proc.returncode}")
            buf = self.text()
            if pattern in buf[start_at:]:
                return
            time.sleep(0.05)
        raise TimeoutError(f"timeout waiting for pattern: {pattern!r}")


def choose_free_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.bind(("127.0.0.1", 0))
        return int(s.getsockname()[1])


def detect_ovmf(explicit: str | None) -> tuple[str, str]:
    if explicit:
        code = Path(explicit)
        if not code.is_file():
            raise FileNotFoundError(f"OVMF code image not found: {explicit}")
        vars_guess = code.with_name(code.name.replace("CODE", "VARS"))
        if not vars_guess.is_file():
            raise FileNotFoundError(f"OVMF vars template not found: {vars_guess}")
        return str(code), str(vars_guess)

    for code, vars_template in OVMF_CANDIDATES:
        if os.path.isfile(code) and os.path.isfile(vars_template):
            return code, vars_template

    raise FileNotFoundError(
        "OVMF not found. Use --ovmf /path/to/OVMF_CODE.fd or install ovmf package."
    )


def run(cmd: list[str], cwd: Path | None = None) -> None:
    proc = subprocess.run(cmd, cwd=str(cwd) if cwd else None)
    if proc.returncode != 0:
        raise RuntimeError(f"command failed ({proc.returncode}): {' '.join(cmd)}")


def smoke(session: SmokeSession, step_timeout: float, user: str, password: str) -> None:
    m = session.marker()
    session.wait_for("Usuario:", timeout=step_timeout, start_at=m)
    session.send_line(user)

    m = session.marker()
    session.wait_for("Senha:", timeout=step_timeout, start_at=m)
    session.send_line(password)

    m = session.marker()
    session.wait_for("Bem-vindo", timeout=step_timeout, start_at=m)
    session.wait_for("> ", timeout=step_timeout, start_at=m)

    def run_cmd(cmd: str, expect: str | None = None) -> None:
        mk = session.marker()
        session.send_line(cmd)
        if expect:
            session.wait_for(expect, timeout=step_timeout, start_at=mk)
        session.wait_for("> ", timeout=step_timeout, start_at=mk)

    # Use help flag to avoid paginator prompt ("-- mais --") in automated smoke.
    run_cmd("help-any -help", "Uso: help-any")
    run_cmd("mk-dir /tmp/smoke-cli", "[ok]")
    run_cmd("go /tmp/smoke-cli", "[ok]")
    run_cmd("mk-file smoke.txt", "[ok]")

    mk = session.marker()
    session.send_line("open smoke.txt")
    session.wait_for("open> ", timeout=step_timeout, start_at=mk)
    session.send_line("linha-1")
    session.wait_for("open> ", timeout=step_timeout, start_at=mk)
    session.send_line("linha-2")
    session.wait_for("open> ", timeout=step_timeout, start_at=mk)
    session.send_line(".wq")
    session.wait_for("arquivo salvo", timeout=step_timeout, start_at=mk)
    session.wait_for("> ", timeout=step_timeout, start_at=mk)

    run_cmd("print-file smoke.txt", "linha-2")
    run_cmd('find "linha-2" /tmp/smoke-cli', "linha-2")
    run_cmd("list", "smoke.txt")

    mk = session.marker()
    session.send_line("bye")
    session.wait_for("Encerrando sessao", timeout=step_timeout, start_at=mk)
    session.wait_for("Usuario:", timeout=step_timeout, start_at=mk)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="NoirOS x64 CLI smoke test (QEMU/UEFI)")
    parser.add_argument("--iso", default="build/NoirOS-Installer-UEFI.iso", help="UEFI ISO path")
    parser.add_argument("--qemu", default="qemu-system-x86_64", help="QEMU binary")
    parser.add_argument("--ovmf", default=None, help="Path to OVMF_CODE.fd")
    parser.add_argument("--memory", type=int, default=1024, help="Guest memory in MB")
    parser.add_argument("--step-timeout", type=float, default=45.0, help="Timeout per step in seconds")
    parser.add_argument("--build", action="store_true", help="Run make all64 && make iso-uefi first")
    parser.add_argument("--log", default="build/ci/smoke_x64_cli.log", help="Log file path")
    parser.add_argument("--user", default="admin", help="Login username")
    parser.add_argument("--password", default="admin", help="Login password")
    parser.add_argument("--verbose", action="store_true", help="Print live serial output")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    repo_root = Path(__file__).resolve().parents[2]
    iso_path = (repo_root / args.iso).resolve()
    log_path = (repo_root / args.log).resolve()

    qemu_bin = shutil.which(args.qemu)
    if not qemu_bin:
        print(f"[err] qemu not found in PATH: {args.qemu}", file=sys.stderr)
        return 2

    try:
        ovmf_code, ovmf_vars_template = detect_ovmf(args.ovmf)
    except FileNotFoundError as exc:
        print(f"[err] {exc}", file=sys.stderr)
        return 2

    ovmf_vars_runtime = None
    session = None
    try:
        if args.build:
            run(["make", "all64"], cwd=repo_root)
            run(["make", "iso-uefi"], cwd=repo_root)

        if not iso_path.exists():
            print(f"[err] ISO not found: {iso_path}", file=sys.stderr)
            return 2

        serial_port = choose_free_port()

        ovmf_vars_runtime = log_path.parent / "OVMF_VARS.runtime.fd"
        ovmf_vars_runtime.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(ovmf_vars_template, ovmf_vars_runtime)

        cmd = [
            qemu_bin,
            "-machine",
            "q35,accel=tcg",
            "-m",
            str(args.memory),
            "-boot",
            "d",
            "-cdrom",
            str(iso_path),
            "-drive",
            f"if=pflash,format=raw,readonly=on,file={ovmf_code}",
            "-drive",
            f"if=pflash,format=raw,file={ovmf_vars_runtime}",
            "-serial",
            f"tcp:127.0.0.1:{serial_port},server,nowait",
            "-display",
            "none",
            "-monitor",
            "none",
            "-no-reboot",
        ]

        print("[info] starting qemu smoke session...")
        print(f"[info] serial tcp: 127.0.0.1:{serial_port}")
        print(f"[info] log: {log_path}")

        session = SmokeSession(cmd=cmd, serial_port=serial_port, log_path=log_path, verbose=args.verbose)
        session.start()

        smoke(session=session, step_timeout=args.step_timeout, user=args.user, password=args.password)

        print("[ok] smoke x64 CLI passed.")
        return 0
    except Exception as exc:
        print(f"[err] smoke failed: {exc}", file=sys.stderr)
        if session is not None:
            print("----- serial tail -----", file=sys.stderr)
            print(session.tail(), file=sys.stderr)
            print("-----------------------", file=sys.stderr)
        else:
            try:
                tail = log_path.read_text(encoding="latin-1", errors="replace")[-2500:]
                print("----- log tail -----", file=sys.stderr)
                print(tail, file=sys.stderr)
                print("--------------------", file=sys.stderr)
            except Exception:
                pass
        return 1
    finally:
        if session is not None:
            session.stop()
        if ovmf_vars_runtime is not None:
            try:
                ovmf_vars_runtime.unlink(missing_ok=True)
            except Exception:
                pass


if __name__ == "__main__":
    raise SystemExit(main())
