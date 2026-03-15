#!/usr/bin/env python3
"""Reusable smoke-session helpers for CAPYOS x64 smoke tests."""

from __future__ import annotations

import os
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
SERIAL_CHAR_DELAY = 0.002


class SmokeSession:
    def __init__(
        self, cmd: list[str], serial_port: int, log_path: Path, verbose: bool = False
    ):
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

        self.sock = self._connect_serial(timeout=25.0)
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

    def tail(self, max_bytes: int = 3500) -> str:
        with self._lock:
            data = self._buf[-max_bytes:]
        return data.decode("latin-1", errors="replace")

    def send_line(self, line: str) -> None:
        self.send_text(line, newline=True)

    def send_text(self, text: str, newline: bool = False) -> None:
        if self.sock is None:
            raise RuntimeError("serial socket is not connected")
        payload = text.encode("ascii", errors="ignore")
        if newline:
            payload += b"\r"
        for i, byte in enumerate(payload):
            self.sock.sendall(bytes([byte]))
            if i + 1 < len(payload):
                time.sleep(SERIAL_CHAR_DELAY)

    def wait_for(self, pattern: str, timeout: float, start_at: int = 0) -> None:
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if self.proc is not None and self.proc.poll() is not None:
                raise RuntimeError(f"qemu exited early with code {self.proc.returncode}")
            if pattern in self.text()[start_at:]:
                return
            time.sleep(0.05)
        raise TimeoutError(f"timeout waiting for pattern: {pattern!r}")

    def wait_for_any(self, patterns: list[str], timeout: float, start_at: int = 0) -> str:
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if self.proc is not None and self.proc.poll() is not None:
                raise RuntimeError(f"qemu exited early with code {self.proc.returncode}")
            chunk = self.text()[start_at:]
            for p in patterns:
                if p in chunk:
                    return p
            time.sleep(0.05)
        raise TimeoutError(f"timeout waiting for patterns: {patterns!r}")


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


def run_command(cmd: list[str], cwd: Path | None = None) -> None:
    proc = subprocess.run(cmd, cwd=str(cwd) if cwd else None)
    if proc.returncode != 0:
        raise RuntimeError(f"command failed ({proc.returncode}): {' '.join(cmd)}")


def make_qemu_cmd(
    qemu_bin: str,
    ovmf_code: str,
    ovmf_vars_runtime: Path,
    disk_path: Path,
    serial_port: int,
    memory_mb: int,
    storage_bus: str = "sata",
    debugcon_log: Path | None = None,
    iso_path: Path | None = None,
    boot_from: str = "disk",
) -> list[str]:
    cmd = [
        qemu_bin,
        "-machine",
        "q35,accel=tcg",
        "-m",
        str(memory_mb),
        "-boot",
        "d" if boot_from == "cdrom" else "c",
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

    if debugcon_log is not None:
        cmd.extend(
            [
                "-global",
                "isa-debugcon.iobase=0xe9",
                "-debugcon",
                f"file:{debugcon_log}",
            ]
        )

    if storage_bus == "nvme":
        cmd.extend(
            [
                "-drive",
                f"if=none,id=disk0,format=raw,file={disk_path}",
                "-device",
                "nvme,serial=CAPYOSNVME01,drive=disk0",
            ]
        )
    else:
        cmd.extend(["-drive", f"format=raw,file={disk_path}"])

    if iso_path is not None:
        cmd.extend(["-drive", f"file={iso_path},media=cdrom,readonly=on"])

    return cmd
