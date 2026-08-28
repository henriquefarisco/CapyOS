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


def qemu_accelerator() -> str:
    """Return the explicitly selected, allowlisted QEMU accelerator.

    TCG remains the portable CI default. Local release gates may opt into KVM
    with CAPYOS_QEMU_ACCEL=kvm when /dev/kvm is available.
    """
    accelerator = os.environ.get("CAPYOS_QEMU_ACCEL", "tcg").strip().lower()
    if accelerator not in {"tcg", "kvm"}:
        raise ValueError(
            "CAPYOS_QEMU_ACCEL must be one of: tcg, kvm; "
            f"got {accelerator!r}"
        )
    return accelerator


def reset_capture_file(path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with open(path, "wb"):
        pass


def text_contains_pattern(
    text: str, pattern: str, *, ignore_line_breaks: bool = False
) -> bool:
    if ignore_line_breaks:
        text = text.replace("\r", "").replace("\n", "")
        pattern = pattern.replace("\r", "").replace("\n", "")
    return pattern in text


class SmokeSession:
    def __init__(
        self,
        cmd: list[str],
        serial_port: int,
        log_path: Path,
        verbose: bool = False,
        debugcon_log_path: Path | None = None,
    ):
        self.cmd = cmd
        self.serial_port = serial_port
        self.log_path = log_path
        self.verbose = verbose
        self.debugcon_log_path = debugcon_log_path

        self.proc: subprocess.Popen[bytes] | None = None
        self.sock: socket.socket | None = None

        self._lock = threading.Lock()
        self._buf = bytearray()
        self._debugcon_buf = bytearray()

        self._proc_reader: threading.Thread | None = None
        self._serial_reader: threading.Thread | None = None
        self._debugcon_reader: threading.Thread | None = None

        self._logf = None

    def start(self) -> None:
        self.log_path.parent.mkdir(parents=True, exist_ok=True)
        try:
            self._logf = open(self.log_path, "wb")
            if self.debugcon_log_path is not None:
                # QEMU opens file: debugcon asynchronously. Empty it before
                # spawning so the reader can never ingest evidence from a
                # previous attempt during that startup race.
                reset_capture_file(self.debugcon_log_path)
            self.proc = subprocess.Popen(
                self.cmd,
                stdin=subprocess.DEVNULL,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
            )
            self._proc_reader = threading.Thread(
                target=self._read_proc_output, daemon=True
            )
            self._proc_reader.start()
            self.sock = self._connect_serial(timeout=25.0)
            self.sock.settimeout(0.3)
            self._serial_reader = threading.Thread(
                target=self._read_serial, daemon=True
            )
            self._serial_reader.start()
            if self.debugcon_log_path is not None:
                self._debugcon_reader = threading.Thread(
                    target=self._read_debugcon, daemon=True
                )
                self._debugcon_reader.start()
        except BaseException:
            self.stop()
            raise

    def stop(self) -> None:
        if self.sock is not None:
            try:
                self.sock.close()
            except OSError:
                # Best-effort cleanup; narrowed from `Exception` to satisfy
                # py/catch-too-general-exception. socket.close raises OSError
                # on shutdown races; nothing else escapes from the C layer.
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
        if self._debugcon_reader is not None:
            self._debugcon_reader.join(timeout=1)

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
        # Explicit checks instead of `assert` so `python -O` cannot strip
        # them (py/assert-stmt). Reader threads are only spawned by
        # `start()` after these attributes are populated, so reaching
        # this point with any of them None indicates an internal bug.
        if self.proc is None or self.proc.stdout is None or self._logf is None:
            raise RuntimeError(
                "smoke session reader started before proc/stdout/logf were ready"
            )
        while True:
            data = self.proc.stdout.read(1)
            if not data:
                return
            self._logf.write(data)

    def _read_serial(self) -> None:
        # See _read_proc_output: keep checks live under `python -O`.
        if self.sock is None or self._logf is None:
            raise RuntimeError(
                "smoke session serial reader started before sock/logf were ready"
            )
        while True:
            try:
                data = self.sock.recv(4096)
            except socket.timeout:
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

    def _read_debugcon(self) -> None:
        if self.debugcon_log_path is None or self._logf is None:
            return
        offset = 0
        while True:
            try:
                with self.debugcon_log_path.open("rb") as fp:
                    fp.seek(offset)
                    data = fp.read()
                    offset = fp.tell()
            except OSError:
                data = b""
            if data:
                use_for_interaction = self._record_debugcon_data(data)
                if use_for_interaction and self.verbose:
                    sys.stdout.write(data.decode("latin-1", errors="replace"))
                    sys.stdout.flush()
            if self.proc is not None and self.proc.poll() is not None:
                return
            time.sleep(0.05)

    def _record_debugcon_data(self, data: bytes) -> bool:
        """Keep debugcon isolated so mirrored channels cannot interleave."""
        if not data:
            return False
        with self._lock:
            self._debugcon_buf.extend(data)
        return True

    def _drain_readers_after_exit(self) -> None:
        for reader in (
            self._proc_reader,
            self._serial_reader,
            self._debugcon_reader,
        ):
            if reader is not None:
                reader.join(timeout=1.0)

    def marker(self) -> tuple[int, int]:
        with self._lock:
            return len(self._buf), len(self._debugcon_buf)

    @staticmethod
    def _channel_offsets(start_at: int | tuple[int, int]) -> tuple[int, int]:
        if isinstance(start_at, tuple):
            return start_at
        return start_at, start_at

    def _channel_texts_since(
        self, start_at: int | tuple[int, int]
    ) -> tuple[str, str]:
        serial_offset, debugcon_offset = self._channel_offsets(start_at)
        with self._lock:
            serial = self._buf[serial_offset:].decode("latin-1", errors="replace")
            debugcon = self._debugcon_buf[debugcon_offset:].decode(
                "latin-1", errors="replace"
            )
        return serial, debugcon

    def text_since(self, start_at: int | tuple[int, int]) -> str:
        return "\n".join(self._channel_texts_since(start_at))

    def serial_text_since(self, start_at: int | tuple[int, int]) -> str:
        """Return only the interactive serial channel after a marker.

        Debugcon can trail the serial reader and replay an older shell prompt
        after a command marker. Command completion must therefore use the
        interactive channel, while general boot-marker waits may still merge
        both channels through ``text_since``/``wait_for``.
        """
        return self._channel_texts_since(start_at)[0]

    def text(self) -> str:
        with self._lock:
            serial = self._buf.decode("latin-1", errors="replace")
            debugcon = self._debugcon_buf.decode("latin-1", errors="replace")
        return serial + "\n" + debugcon

    def tail(self, max_bytes: int = 3500) -> str:
        with self._lock:
            serial = self._buf[-max_bytes:].decode("latin-1", errors="replace")
            debugcon = self._debugcon_buf[-max_bytes:].decode(
                "latin-1", errors="replace"
            )
        return serial + "\n" + debugcon

    def send_byte(self, value: int) -> None:
        if self.sock is None:
            raise RuntimeError("serial socket is not connected")
        if value < 0 or value > 255:
            raise ValueError("serial byte must be in range 0..255")
        self.sock.sendall(bytes([value]))

    def send_line(self, line: str) -> None:
        self.send_text(line, newline=True)

    def send_firmware_line(self, line: str) -> None:
        """Send a UEFI line with LF; firmware readers accept it reliably."""
        if self.sock is None:
            raise RuntimeError("serial socket is not connected")
        payload = line.encode("ascii", errors="ignore") + b"\n"
        for index, byte in enumerate(payload):
            self.sock.sendall(bytes([byte]))
            if index + 1 < len(payload):
                time.sleep(SERIAL_CHAR_DELAY)

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

    def wait_for(
        self,
        pattern: str,
        timeout: float,
        start_at: int | tuple[int, int] = 0,
        ignore_line_breaks: bool = False,
    ) -> None:
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if any(
                text_contains_pattern(
                    chunk, pattern, ignore_line_breaks=ignore_line_breaks
                )
                for chunk in self._channel_texts_since(start_at)
            ):
                return
            if self.proc is not None and self.proc.poll() is not None:
                self._drain_readers_after_exit()
                if any(
                    text_contains_pattern(
                        chunk, pattern, ignore_line_breaks=ignore_line_breaks
                    )
                    for chunk in self._channel_texts_since(start_at)
                ):
                    return
                raise RuntimeError(f"qemu exited early with code {self.proc.returncode}")
            time.sleep(0.05)
        if any(
            text_contains_pattern(chunk, pattern, ignore_line_breaks=ignore_line_breaks)
            for chunk in self._channel_texts_since(start_at)
        ):
            return
        raise TimeoutError(f"timeout waiting for pattern: {pattern!r}")

    def wait_for_any(
        self,
        patterns: list[str],
        timeout: float,
        start_at: int | tuple[int, int] = 0,
    ) -> str:
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            for pattern in patterns:
                if any(pattern in chunk for chunk in self._channel_texts_since(start_at)):
                    return pattern
            if self.proc is not None and self.proc.poll() is not None:
                self._drain_readers_after_exit()
                for pattern in patterns:
                    if any(pattern in chunk for chunk in self._channel_texts_since(start_at)):
                        return pattern
                raise RuntimeError(f"qemu exited early with code {self.proc.returncode}")
            time.sleep(0.05)
        for pattern in patterns:
            if any(pattern in chunk for chunk in self._channel_texts_since(start_at)):
                return pattern
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
    networking: bool = False,
    extra_disks: tuple[Path, ...] = (),
) -> list[str]:
    cmd = [
        qemu_bin,
        "-machine",
        f"q35,accel={qemu_accelerator()}",
        "-m",
        str(memory_mb),
        "-boot",
        f"once={'d' if boot_from == 'cdrom' else 'c'},menu=off",
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

    if networking:
        # E1000 NIC on QEMU user-mode networking (SLIRP): the guest gets a
        # DHCP lease (10.0.2.15), gateway/DNS 10.0.2.2/10.0.2.3 and outbound
        # NAT. The e1000 model matches the official VMware + E1000 platform so
        # the same kernel NIC driver path is exercised; no inbound host ports
        # are forwarded. Used by the networked Etapa 6 smokes (capybrowse-text).
        cmd.extend(["-netdev", "user,id=net0", "-device", "e1000,netdev=net0"])
        if debugcon_log is not None:
            # Capture all guest NIC traffic to a pcap next to the logs for
            # network debugging (ARP / DHCP / TCP handshakes). Harmless to
            # leave on for the dev smokes.
            pcap = debugcon_log.with_name("qemu_net.pcap")
            cmd.extend(
                ["-object", f"filter-dump,id=netdump,netdev=net0,file={pcap}"])
    else:
        # Isolated SLIRP NIC: DHCP still works (ISO smoke network_mode=dhcp
        # persistence check), but restrict=on blocks host/internet egress so
        # best-effort net-fetch demos fail fast instead of hanging on a real,
        # slow-under-TCG TLS handshake. Explicit NIC also avoids inheriting
        # QEMU implicit default user-net NIC with full internet access.
        cmd.extend(["-netdev", "user,id=net0,restrict=on",
                    "-device", "e1000,netdev=net0"])

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

    for index, extra_disk in enumerate(extra_disks, start=1):
        if storage_bus == "nvme":
            cmd.extend(
                [
                    "-drive",
                    f"if=none,id=disk{index},format=raw,file={extra_disk}",
                    "-device",
                    f"nvme,serial=CAPYOSNVME{index + 1:02d},drive=disk{index}",
                ]
            )
        else:
            cmd.extend(["-drive", f"format=raw,file={extra_disk}"])

    if iso_path is not None:
        cmd.extend(["-drive", f"file={iso_path},media=cdrom,readonly=on"])

    return cmd
