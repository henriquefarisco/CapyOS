from __future__ import annotations

import argparse
import hashlib
import os
import re
import shutil
import socket
import struct
import subprocess
import sys
import time
import uuid
from pathlib import Path

from smoke_x64_vmware_installer_contract import parse_flat_extent, sha256_file


LOOPBACK_ADDRESS = "127.0.0.1"
RFB_VERSION_33 = b"RFB 003.003\n"
RFB_VERSION_38 = b"RFB 003.008\n"
RFB_SECURITY_NONE = 1
RFB_ENCODING_RAW = 0
RFB_ENCODING_DESKTOP_SIZE = -223
KEYSYM_ZERO = 0x0030
KEYSYM_RETURN = 0xFF0D
CANCEL_KEY_SEQUENCE = (KEYSYM_ZERO, KEYSYM_RETURN)
SERIAL_DEVICE_RE = re.compile(r"(?im)^\s*serial\d+\.")
SAFE_EVIDENCE_KEY_RE = re.compile(r"[a-z0-9_]+\Z")
VNC_LISTENER_PREFIX = "MKSSocketListener: Started VNC server, listening at"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="CapyOS VMware installer no-UART input regression gate"
    )
    parser.add_argument("--iso", type=Path, required=True)
    parser.add_argument(
        "--vmrun",
        type=Path,
        default=Path(r"C:\Program Files\VMware\VMware Workstation\vmrun.exe"),
    )
    parser.add_argument(
        "--vdiskmanager",
        type=Path,
        default=Path(
            r"C:\Program Files\VMware\VMware Workstation\vmware-vdiskmanager.exe"
        ),
    )
    parser.add_argument(
        "--vmcli",
        type=Path,
        default=Path(r"C:\Program Files\VMware\VMware Workstation\vmcli.exe"),
    )
    parser.add_argument(
        "--work-root", type=Path, default=Path("build/ci/vmware-installer-no-uart")
    )
    parser.add_argument(
        "--evidence",
        type=Path,
        default=Path("build/ci/vmware-installer-no-uart-evidence.manifest"),
    )
    parser.add_argument("--target-size", default="2GB")
    parser.add_argument("--guard-size", default="3GB")
    parser.add_argument("--step-timeout", type=float, default=90.0)
    parser.add_argument("--boot-grace", type=float, default=8.0)
    parser.add_argument("--idle-seconds", type=float, default=8.0)
    parser.add_argument("--keep-vm", action="store_true")
    return parser.parse_args()


def resolve_under_safe_root(path: Path, safe_root: Path) -> Path:
    resolved = path.resolve()
    safe = safe_root.resolve()
    try:
        resolved.relative_to(safe)
    except ValueError as exc:
        raise ValueError(f"path must stay under {safe}: {resolved}") from exc
    return resolved


def validate_vmx_value(value: str) -> None:
    if not value or any(char in value for char in ('"', "\r", "\n")):
        raise ValueError("VMX value contains unsupported characters")


def render_no_uart_vmx(
    *,
    display_name: str,
    iso_path: Path,
    target_descriptor: Path,
    guard_descriptor: Path,
    vnc_port: int,
) -> str:
    values = (
        display_name,
        str(iso_path),
        str(target_descriptor),
        str(guard_descriptor),
    )
    for value in values:
        validate_vmx_value(value)
    if not 1 <= vnc_port <= 65535:
        raise ValueError("VNC port is outside the TCP port range")
    lines = (
        '.encoding = "UTF-8"',
        'config.version = "8"',
        'virtualHW.version = "22"',
        f'displayName = "{display_name}"',
        'guestOS = "other-64"',
        'firmware = "efi"',
        'uefi.secureBoot.enabled = "FALSE"',
        'efi.serialConsole.enabled = "FALSE"',
        'bios.bootOrder = "cdrom"',
        'memsize = "1024"',
        'numvcpus = "2"',
        'mks.enable3d = "FALSE"',
        'svga.present = "TRUE"',
        'pciBridge0.present = "TRUE"',
        'pciBridge4.present = "TRUE"',
        'pciBridge4.virtualDev = "pcieRootPort"',
        'pciBridge4.functions = "8"',
        'pciBridge5.present = "TRUE"',
        'pciBridge5.virtualDev = "pcieRootPort"',
        'pciBridge5.functions = "8"',
        'pciBridge6.present = "TRUE"',
        'pciBridge6.virtualDev = "pcieRootPort"',
        'pciBridge6.functions = "8"',
        'pciBridge7.present = "TRUE"',
        'pciBridge7.virtualDev = "pcieRootPort"',
        'pciBridge7.functions = "8"',
        'sata0.present = "TRUE"',
        'sata0:0.present = "TRUE"',
        f'sata0:0.fileName = "{target_descriptor}"',
        'sata0:0.redo = ""',
        'sata0:1.present = "TRUE"',
        f'sata0:1.fileName = "{guard_descriptor}"',
        'sata0:1.redo = ""',
        'sata0:2.present = "TRUE"',
        'sata0:2.deviceType = "cdrom-image"',
        f'sata0:2.fileName = "{iso_path}"',
        'sata0:2.startConnected = "TRUE"',
        'ethernet0.present = "FALSE"',
        'usb.present = "TRUE"',
        'ehci.present = "TRUE"',
        'usb_xhci.present = "TRUE"',
        f'RemoteDisplay.vnc.enabled = "TRUE"',
        f'RemoteDisplay.vnc.ip = "{LOOPBACK_ADDRESS}"',
        f'RemoteDisplay.vnc.port = "{vnc_port}"',
        'RemoteDisplay.vnc.keyMap = "us"',
        'sound.present = "FALSE"',
        'floppy0.present = "FALSE"',
        'tools.syncTime = "FALSE"',
    )
    text = "\n".join(lines) + "\n"
    if SERIAL_DEVICE_RE.search(text):
        raise ValueError("no-UART VMX unexpectedly contains a serial device")
    return text


def write_vmx(path: Path, text: str) -> None:
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(text, encoding="utf-8")
    os.replace(temporary, path)


def run_logged(command: list[str], log_path: Path, action: str) -> subprocess.CompletedProcess[str]:
    result = subprocess.run(command, capture_output=True, text=True, check=False)
    with log_path.open("a", encoding="utf-8") as stream:
        stream.write(f"action={action}\nexit_code={result.returncode}\n")
        stream.write(f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}\n")
    return result


def require_success(result: subprocess.CompletedProcess[str], action: str) -> None:
    if result.returncode != 0:
        detail = result.stderr.strip() or result.stdout.strip() or "command failed"
        raise RuntimeError(f"{action} failed: {detail}")


def parse_vmrun_version_output(text: str) -> str:
    match = re.search(
        r"(?m)^vmrun version ([0-9]+(?:\.[0-9]+){3})\s*$",
        text,
    )
    if match is None:
        raise RuntimeError("unable to determine an exact VMware vmrun version")
    return match.group(1)


def query_vmrun_version(vmrun: Path, host_log: Path) -> str:
    result = run_logged([str(vmrun)], host_log, "vmrun-version")
    if result.returncode != 0:
        raise RuntimeError("unable to query VMware vmrun version")
    return parse_vmrun_version_output(result.stdout)


def create_vmdk(
    vdiskmanager: Path,
    descriptor: Path,
    size: str,
    host_log: Path,
    run_root: Path,
) -> Path:
    if descriptor.exists():
        raise FileExistsError(descriptor)
    result = run_logged(
        [
            str(vdiskmanager),
            "-c",
            "-s",
            size,
            "-a",
            "lsilogic",
            "-t",
            "2",
            str(descriptor),
        ],
        host_log,
        f"create-{descriptor.stem}",
    )
    require_success(result, f"create {descriptor.name}")
    extent = resolve_under_safe_root(parse_flat_extent(descriptor), run_root)
    if extent == run_root or not extent.is_file():
        raise RuntimeError(f"unsafe or missing VMDK extent: {extent}")
    return extent


def seed_extent(path: Path, label: bytes) -> None:
    if len(label) > 256:
        raise ValueError("extent sentinel is too large")
    with path.open("r+b") as stream:
        stream.write(label)
        stream.seek(-4096, os.SEEK_END)
        stream.write(label)


class LoopbackPortReservation:
    def __init__(self) -> None:
        self.socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        if hasattr(socket, "SO_EXCLUSIVEADDRUSE"):
            self.socket.setsockopt(socket.SOL_SOCKET, socket.SO_EXCLUSIVEADDRUSE, 1)
        self.socket.bind((LOOPBACK_ADDRESS, 0))
        self.port = int(self.socket.getsockname()[1])

    def close(self) -> None:
        self.socket.close()


def parse_rfb_server_version(version: bytes) -> bytes:
    if version == RFB_VERSION_33:
        return RFB_VERSION_33
    if version == RFB_VERSION_38:
        return RFB_VERSION_38
    raise RuntimeError(f"unsupported RFB server version: {version!r}")


def key_packets(keysym: int) -> tuple[bytes, bytes]:
    def key_event(down: bool, keysym: int) -> bytes:
        return struct.pack("!BBHI", 4, 1 if down else 0, 0, keysym)

    return key_event(True, keysym), key_event(False, keysym)


def set_encodings_packet() -> bytes:
    return struct.pack(
        "!BBHii",
        2,
        0,
        2,
        RFB_ENCODING_RAW,
        RFB_ENCODING_DESKTOP_SIZE,
    )


class RfbConsole:
    def __init__(self, sock: socket.socket, version: bytes, expected_name: str) -> None:
        self.sock = sock
        self.version = version
        self.expected_name = expected_name
        self.width = 0
        self.height = 0
        self.name = ""
        self.framebuffer = bytearray()
        self.initialized = False
        self.resize_events = 0
        self.trace: list[str] = []
        self.stage = "RFB handshake"
        self.io_timeout = 15.0
        self.request_pending = False
        self.request_count = 0
        self.clean_idle_timeout = False

    @classmethod
    def connect(
        cls, port: int, timeout: float, expected_name: str
    ) -> "RfbConsole":
        deadline = time.monotonic() + timeout
        last_error: OSError | None = None
        sock: socket.socket | None = None
        while time.monotonic() < deadline:
            try:
                sock = socket.create_connection(
                    (LOOPBACK_ADDRESS, port), timeout=min(2.0, timeout)
                )
                break
            except OSError as exc:
                last_error = exc
                time.sleep(0.1)
        if sock is None:
            raise TimeoutError(
                f"timeout connecting to loopback VNC port {port}: {last_error}"
            )
        sock.settimeout(min(15.0, timeout))
        try:
            version = parse_rfb_server_version(cls._recv_exact_from(sock, 12))
            console = cls(sock, version, expected_name)
            console.io_timeout = min(15.0, timeout)
            console._handshake()
            return console
        except BaseException:
            sock.close()
            raise

    @staticmethod
    def _recv_exact_from(sock: socket.socket, size: int) -> bytes:
        data = bytearray()
        while len(data) < size:
            chunk = sock.recv(size - len(data))
            if not chunk:
                raise ConnectionError("RFB server closed the connection")
            data.extend(chunk)
        return bytes(data)

    def _recv_exact(self, size: int) -> bytes:
        data = bytearray()
        self.clean_idle_timeout = False
        while len(data) < size:
            try:
                chunk = self.sock.recv(size - len(data))
            except TimeoutError as exc:
                self.clean_idle_timeout = (
                    size == 1
                    and len(data) == 0
                    and self.stage.endswith("message type")
                )
                raise TimeoutError(
                    f"RFB timeout during {self.stage}; expected {size} bytes, "
                    f"received {len(data)}"
                ) from exc
            if not chunk:
                raise ConnectionError(
                    f"RFB server closed during {self.stage}; expected {size} bytes, "
                    f"received {len(data)}"
                )
            data.extend(chunk)
        return bytes(data)

    def _read_reason(self) -> str:
        length = struct.unpack("!I", self._recv_exact(4))[0]
        if length > 4096:
            raise RuntimeError("RFB failure reason is unreasonably large")
        return self._recv_exact(length).decode("utf-8", errors="replace")

    def _handshake(self) -> None:
        self.sock.sendall(self.version)
        if self.version == RFB_VERSION_33:
            security = struct.unpack("!I", self._recv_exact(4))[0]
            if security == 0:
                raise RuntimeError(f"RFB server rejected connection: {self._read_reason()}")
            if security != RFB_SECURITY_NONE:
                raise RuntimeError("RFB server requires authentication; refusing to weaken it")
        else:
            count = self._recv_exact(1)[0]
            if count == 0:
                raise RuntimeError(f"RFB server rejected connection: {self._read_reason()}")
            security_types = self._recv_exact(count)
            if RFB_SECURITY_NONE not in security_types:
                raise RuntimeError("RFB server does not offer loopback-only None security")
            self.sock.sendall(bytes((RFB_SECURITY_NONE,)))
            result = struct.unpack("!I", self._recv_exact(4))[0]
            if result != 0:
                raise RuntimeError(f"RFB None-security negotiation failed: {self._read_reason()}")

        self.sock.sendall(b"\x01")
        self.width, self.height = struct.unpack("!HH", self._recv_exact(4))
        if not (1 <= self.width <= 8192 and 1 <= self.height <= 8192):
            raise RuntimeError("RFB framebuffer dimensions are unsafe")
        self._recv_exact(16)  # Server pixel format; replaced below.
        name_length = struct.unpack("!I", self._recv_exact(4))[0]
        if name_length > 4096:
            raise RuntimeError("RFB desktop name is unreasonably large")
        self.name = self._recv_exact(name_length).decode("utf-8", errors="replace")
        if self.expected_name not in self.name:
            raise RuntimeError(
                f"RFB desktop identity mismatch: expected {self.expected_name!r}, got {self.name!r}"
            )

        pixel_format = struct.pack(
            "!BBBBHHHBBBxxx", 32, 24, 0, 1, 255, 255, 255, 16, 8, 0
        )
        self.sock.sendall(b"\x00\x00\x00\x00" + pixel_format)
        self.sock.sendall(set_encodings_packet())
        self.framebuffer = bytearray(self.width * self.height * 4)

    def _resize_framebuffer(self, width: int, height: int) -> None:
        if not (1 <= width <= 8192 and 1 <= height <= 8192):
            raise RuntimeError("RFB desktop resize dimensions are unsafe")
        self.width = width
        self.height = height
        self.framebuffer = bytearray(width * height * 4)
        self.initialized = False
        self.resize_events += 1

    def _skip_server_cut_text(self) -> None:
        header = self._recv_exact(7)
        length = struct.unpack("!I", header[3:])[0]
        if length > 1024 * 1024:
            raise RuntimeError("RFB cut text is unreasonably large")
        self._recv_exact(length)

    def request_frame(
        self, *, wait_timeout: float | None = None, incremental: bool = False
    ) -> bytes:
        self.sock.settimeout(
            self.io_timeout if wait_timeout is None else max(0.1, wait_timeout)
        )
        while True:
            if not self.request_pending:
                self.request_count += 1
                self.trace.append(
                    f"request={self.request_count} incremental={int(incremental)} "
                    f"framebuffer={self.width}x{self.height}"
                )
                self.sock.sendall(
                    struct.pack(
                        "!BBHHHH",
                        3,
                        1 if incremental else 0,
                        0,
                        0,
                        self.width,
                        self.height,
                    )
                )
                self.request_pending = True
            request_number = self.request_count
            while True:
                self.stage = f"framebuffer update {request_number} message type"
                message_type = self._recv_exact(1)[0]
                if message_type == 2:  # Bell.
                    continue
                if message_type == 3:  # ServerCutText.
                    self._skip_server_cut_text()
                    continue
                if message_type != 0:
                    raise RuntimeError(f"unsupported RFB server message: {message_type}")
                self.stage = f"framebuffer update {request_number} header"
                header = self._recv_exact(3)
                rectangles = struct.unpack("!H", header[1:])[0]
                self.trace.append(
                    f"request={request_number} rectangles={rectangles}"
                )
                break
            if rectangles == 0:
                self.request_pending = False
                continue
            coverage = (
                bytearray(self.width * self.height) if not self.initialized else None
            )
            for rectangle_index in range(rectangles):
                self.stage = (
                    f"framebuffer update {request_number} rectangle "
                    f"{rectangle_index + 1}/{rectangles} header"
                )
                x, y, width, height, encoding = struct.unpack(
                    "!HHHHi", self._recv_exact(12)
                )
                self.trace.append(
                    f"request={request_number} rectangle={rectangle_index + 1}/"
                    f"{rectangles} xy={x},{y} size={width}x{height} "
                    f"encoding={encoding}"
                )
                if encoding == RFB_ENCODING_DESKTOP_SIZE:
                    self._resize_framebuffer(width, height)
                    coverage = bytearray(self.width * self.height)
                    continue
                if encoding != RFB_ENCODING_RAW:
                    raise RuntimeError(f"RFB server ignored requested encodings: {encoding}")
                if (
                    width == 0
                    or height == 0
                    or x + width > self.width
                    or y + height > self.height
                ):
                    raise RuntimeError("RFB rectangle is outside the framebuffer")
                if coverage is None and not self.initialized:
                    coverage = bytearray(self.width * self.height)
                self.stage = (
                    f"framebuffer update {request_number} rectangle "
                    f"{rectangle_index + 1}/{rectangles} raw pixels"
                )
                raw = self._recv_exact(width * height * 4)
                for row in range(height):
                    source = row * width * 4
                    destination = ((y + row) * self.width + x) * 4
                    self.framebuffer[destination : destination + width * 4] = raw[
                        source : source + width * 4
                    ]
                    if coverage is not None:
                        covered = (y + row) * self.width + x
                        coverage[covered : covered + width] = b"\x01" * width
            self.request_pending = False
            if not self.initialized:
                if coverage is None or 0 in coverage:
                    incremental = False
                    continue
                self.initialized = True
            return bytes(self.framebuffer)

    def request_frame_or_none(
        self, timeout: float, *, incremental: bool = True
    ) -> bytes | None:
        try:
            return self.request_frame(
                wait_timeout=timeout,
                incremental=incremental,
            )
        except TimeoutError:
            if self.clean_idle_timeout:
                return None
            raise

    def send_key(self, keysym: int) -> None:
        for packet in key_packets(keysym):
            self.sock.sendall(packet)
            time.sleep(0.05)

    def close(self) -> None:
        self.sock.close()


def changed_pixel_metrics(before: bytes, after: bytes, width: int) -> tuple[int, int, int]:
    changed, min_x, min_y, max_x, max_y = changed_pixel_bounds(
        before, after, width
    )
    if changed == 0:
        return 0, 0, 0
    return changed, max_x - min_x + 1, max_y - min_y + 1


def changed_pixel_bounds(
    before: bytes, after: bytes, width: int
) -> tuple[int, int, int, int, int]:
    if len(before) != len(after) or len(before) % 4 != 0 or width <= 0:
        raise ValueError("incompatible framebuffers")
    changed = 0
    min_x = width
    min_y = len(before) // 4 // width
    max_x = -1
    max_y = -1
    for offset in range(0, len(before), 4):
        if before[offset : offset + 4] == after[offset : offset + 4]:
            continue
        pixel = offset // 4
        x = pixel % width
        y = pixel // width
        changed += 1
        min_x = min(min_x, x)
        min_y = min(min_y, y)
        max_x = max(max_x, x)
        max_y = max(max_y, y)
    if changed == 0:
        return 0, 0, 0, 0, 0
    return changed, min_x, min_y, max_x, max_y


def transition_is_material(
    before: bytes, after: bytes, width: int, height: int
) -> tuple[bool, tuple[int, int, int]]:
    metrics = changed_pixel_metrics(before, after, width)
    minimum_pixels = max(512, (width * height) // 1000)
    changed, box_width, box_height = metrics
    return (
        changed >= minimum_pixels and box_width >= 32 and box_height >= 16,
        metrics,
    )


def nonblack_pixel_count(frame: bytes) -> int:
    if len(frame) % 4 != 0:
        raise ValueError("invalid 32-bit framebuffer")
    return sum(frame[index : index + 3] != b"\x00\x00\x00" for index in range(0, len(frame), 4))


def wait_for_stable_console(
    console: RfbConsole, timeout: float, boot_grace: float
) -> bytes:
    time.sleep(max(0.0, boot_grace))
    deadline = time.monotonic() + timeout
    frame = console.request_frame()
    while time.monotonic() < deadline:
        if nonblack_pixel_count(frame) < 1000:
            next_frame = console.request_frame_or_none(1.0, incremental=True)
            if next_frame is not None:
                frame = next_frame
            continue
        next_frame = console.request_frame_or_none(1.0, incremental=True)
        if next_frame is None:
            return frame
        frame = next_frame
    raise TimeoutError("VMware console did not reach a stable non-empty installer screen")


def wait_for_unchanged_console(
    console: RfbConsole, expected: bytes, idle_seconds: float
) -> bytes:
    deadline = time.monotonic() + idle_seconds
    samples = 0
    while time.monotonic() < deadline:
        remaining = deadline - time.monotonic()
        frame = console.request_frame_or_none(
            min(0.5, max(0.1, remaining)), incremental=True
        )
        samples += 1
        if frame is None:
            continue
        if frame != expected:
            metrics = changed_pixel_metrics(expected, frame, console.width)
            raise RuntimeError(
                "installer prompt changed without input during the no-UART idle window "
                f"(metrics={metrics})"
            )
    if samples < 2:
        raise RuntimeError("no-UART idle window produced insufficient framebuffer samples")
    return expected


def zero_input_is_localized(
    before: bytes, after: bytes, width: int, height: int
) -> tuple[bool, tuple[int, int, int]]:
    changed, min_x, min_y, max_x, max_y = changed_pixel_bounds(
        before, after, width
    )
    if changed == 0:
        return False, (0, 0, 0)
    box_width = max_x - min_x + 1
    box_height = max_y - min_y + 1
    localized = (
        16 <= changed <= 512
        and 8 <= box_width <= 64
        and 8 <= box_height <= 32
        and min_x >= width // 4
        and max_x < (width * 3) // 4
        and min_y >= height // 2
        and max_y < (height * 3) // 4
    )
    return localized, (changed, box_width, box_height)


def wait_for_localized_zero(
    console: RfbConsole, prompt: bytes, timeout: float
) -> tuple[bytes, tuple[int, int, int]]:
    deadline = time.monotonic() + timeout
    last_metrics = (0, 0, 0)
    candidate: bytes | None = None
    stable_samples = 0
    while time.monotonic() < deadline:
        remaining = deadline - time.monotonic()
        frame = console.request_frame_or_none(
            min(0.5, max(0.1, remaining)), incremental=True
        )
        if frame is None:
            if candidate is not None:
                return candidate, last_metrics
            continue
        localized, metrics = zero_input_is_localized(
            prompt, frame, console.width, console.height
        )
        last_metrics = metrics
        if localized:
            stable_samples = stable_samples + 1 if frame == candidate else 0
            candidate = frame
            if stable_samples >= 1:
                return frame, metrics
        else:
            candidate = None
            stable_samples = 0
    raise TimeoutError(
        "exact zero input produced no stable localized framebuffer transition "
        f"(last metrics={last_metrics})"
    )


def dominant_nonblack_pixel_count(frame: bytes) -> int:
    counts: dict[bytes, int] = {}
    for index in range(0, len(frame), 4):
        pixel = frame[index : index + 3]
        if pixel == b"\x00\x00\x00":
            continue
        counts[pixel] = counts.get(pixel, 0) + 1
    return max(counts.values(), default=0)


def classify_cancel_transition(
    before: bytes, after: bytes, width: int, height: int
) -> tuple[str | None, tuple[int, int, int], int, int, int]:
    changed, box_width, box_height = changed_pixel_metrics(before, after, width)
    before_nonblack = nonblack_pixel_count(before)
    after_nonblack = nonblack_pixel_count(after)
    dominant_nonblack = dominant_nonblack_pixel_count(after)
    geometry_matches = (
        changed >= max(4096, (width * height) // 20)
        and box_width >= width // 2
        and box_height >= height // 3
    )
    mode: str | None = None
    if (
        geometry_matches
        and after_nonblack >= 256
        and after_nonblack * 4 <= before_nonblack * 3
    ):
        mode = "cleared-message"
    elif (
        geometry_matches
        and after_nonblack >= before_nonblack * 4
        and dominant_nonblack >= (width * height) // 5
    ):
        mode = "firmware-return"
    return (
        mode,
        (changed, box_width, box_height),
        before_nonblack,
        after_nonblack,
        dominant_nonblack,
    )


def wait_for_expected_cancel(
    console: RfbConsole, zero_frame: bytes, timeout: float
) -> tuple[bytes, str, tuple[int, int, int], int, int, int]:
    deadline = time.monotonic() + timeout
    last_metrics = (0, 0, 0)
    before_nonblack = nonblack_pixel_count(zero_frame)
    last_after_nonblack = before_nonblack
    last_dominant_nonblack = 0
    candidate_mode: str | None = None
    candidate: bytes | None = None
    while time.monotonic() < deadline:
        remaining = deadline - time.monotonic()
        frame = console.request_frame_or_none(
            min(0.5, max(0.1, remaining)), incremental=True
        )
        if frame is None:
            if candidate is not None and candidate_mode is not None:
                return (
                    candidate,
                    candidate_mode,
                    last_metrics,
                    before_nonblack,
                    last_after_nonblack,
                    last_dominant_nonblack,
                )
            continue
        mode, metrics, before_nonblack, after_nonblack, dominant_nonblack = (
            classify_cancel_transition(
                zero_frame, frame, console.width, console.height
            )
        )
        last_metrics = metrics
        last_after_nonblack = after_nonblack
        last_dominant_nonblack = dominant_nonblack
        if mode is not None:
            candidate = frame
            candidate_mode = mode
        else:
            candidate = None
            candidate_mode = None
    raise TimeoutError(
        "Return did not produce the expected stable cancellation screen "
        f"(last metrics={last_metrics}, nonblack="
        f"{before_nonblack}->{last_after_nonblack}, "
        f"dominant_nonblack={last_dominant_nonblack})"
    )


def listener_log_proves_loopback(text: str, port: int) -> bool:
    pattern = re.compile(
        rf"{re.escape(VNC_LISTENER_PREFIX)}\s+"
        rf"{re.escape(LOOPBACK_ADDRESS)}:{port}(?:\s|$)"
    )
    return any(pattern.search(line) is not None for line in text.splitlines())


def wait_for_loopback_listener_log(path: Path, port: int, timeout: float) -> None:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if path.is_file():
            text = path.read_text(encoding="utf-8", errors="replace")
            if listener_log_proves_loopback(text, port):
                return
        time.sleep(0.1)
    raise RuntimeError("VMware log did not prove a loopback-only VNC listener")


def capture_screenshot(vmcli: Path, vmx: Path, output: Path, host_log: Path) -> None:
    if output.exists():
        raise FileExistsError(output)
    result = run_logged(
        [str(vmcli), str(vmx), "MKS", "captureScreenshot", str(output)],
        host_log,
        f"capture-{output.stem}",
    )
    require_success(result, f"capture {output.name}")
    if not output.is_file() or output.stat().st_size == 0:
        raise RuntimeError(f"VMware MKS screenshot is empty: {output}")


def parse_serial_query_output(text: str) -> int:
    lines = tuple(line.strip() for line in text.splitlines() if line.strip())
    if lines != ("No serial ports configured in the guest.", "devices: []"):
        raise RuntimeError(f"ambiguous VMware serial query output: {lines!r}")
    return 0


def query_serial_devices(vmcli: Path, vmx: Path, host_log: Path) -> int:
    result = run_logged(
        [str(vmcli), str(vmx), "Serial", "query"],
        host_log,
        "query-serial",
    )
    require_success(result, "query VMware serial devices")
    return parse_serial_query_output(result.stdout)


def parse_vmrun_list(text: str) -> tuple[Path, ...]:
    lines = tuple(line.strip() for line in text.splitlines() if line.strip())
    if not lines:
        raise RuntimeError("vmrun list returned no output")
    match = re.fullmatch(r"Total running VMs:\s*(\d+)", lines[0])
    if match is None or int(match.group(1), 10) != len(lines) - 1:
        raise RuntimeError(f"ambiguous vmrun list output: {lines!r}")
    return tuple(Path(line).resolve() for line in lines[1:])


def paths_equal(left: Path, right: Path) -> bool:
    return os.path.normcase(str(left.resolve())) == os.path.normcase(str(right.resolve()))


def stop_disposable_vm(vmrun: Path, vmx: Path, host_log: Path) -> bool:
    list_result = run_logged(
        [str(vmrun), "-T", "ws", "list"], host_log, "list-before-stop"
    )
    if list_result.returncode != 0:
        stop_result = run_logged(
            [str(vmrun), "-T", "ws", "stop", str(vmx), "hard"],
            host_log,
            "stop-after-list-failure",
        )
        require_success(stop_result, "stop exact disposable VM after list failure")
        return True
    running = parse_vmrun_list(list_result.stdout)
    if not any(paths_equal(path, vmx) for path in running):
        return False
    stop_result = run_logged(
        [str(vmrun), "-T", "ws", "stop", str(vmx), "hard"],
        host_log,
        "stop",
    )
    require_success(stop_result, "stop disposable VMware VM")
    verify_result = run_logged(
        [str(vmrun), "-T", "ws", "list"], host_log, "list-after-stop"
    )
    require_success(verify_result, "verify disposable VMware VM stopped")
    if any(paths_equal(path, vmx) for path in parse_vmrun_list(verify_result.stdout)):
        raise RuntimeError("disposable VMware VM remained running after hard stop")
    return True


def frame_sha256(frame: bytes) -> str:
    return hashlib.sha256(frame).hexdigest()


def write_frame_bmp(path: Path, frame: bytes, width: int, height: int) -> None:
    expected = width * height * 4
    if width <= 0 or height <= 0 or len(frame) != expected:
        raise ValueError("invalid 32-bit framebuffer for BMP evidence")
    pixels = b"".join(
        frame[row * width * 4 : (row + 1) * width * 4]
        for row in range(height - 1, -1, -1)
    )
    offset = 14 + 40
    header = struct.pack("<2sIHHI", b"BM", offset + len(pixels), 0, 0, offset)
    dib = struct.pack(
        "<IiiHHIIiiII",
        40,
        width,
        height,
        1,
        32,
        0,
        len(pixels),
        2835,
        2835,
        0,
        0,
    )
    temporary = path.with_suffix(path.suffix + ".tmp")
    with temporary.open("wb") as stream:
        stream.write(header)
        stream.write(dib)
        stream.write(pixels)
    os.replace(temporary, path)


def render_evidence(fields: tuple[tuple[str, str], ...]) -> str:
    seen: set[str] = set()
    lines: list[str] = []
    for key, value in fields:
        if not SAFE_EVIDENCE_KEY_RE.fullmatch(key) or key in seen:
            raise ValueError(f"invalid or duplicate evidence key: {key}")
        if not value or "\n" in value or "\r" in value:
            raise ValueError(f"invalid evidence value: {key}")
        seen.add(key)
        lines.append(f"{key}={value}\n")
    return "".join(lines)


def main() -> int:
    args = parse_args()
    if os.name != "nt":
        print("[err] VMware no-UART gate must run with Windows Python", file=sys.stderr)
        return 2
    if args.step_timeout <= 0 or args.boot_grace < 0 or args.idle_seconds <= 0:
        print("[err] timeouts must be positive", file=sys.stderr)
        return 2

    repo_root = Path(__file__).resolve().parents[2]
    safe_root = (repo_root / "build/ci").resolve()
    iso = args.iso.resolve()
    vmrun = args.vmrun.resolve()
    vdiskmanager = args.vdiskmanager.resolve()
    vmcli = args.vmcli.resolve()
    try:
        work_root = resolve_under_safe_root(
            args.work_root if args.work_root.is_absolute() else repo_root / args.work_root,
            safe_root,
        )
        evidence_path = resolve_under_safe_root(
            args.evidence if args.evidence.is_absolute() else repo_root / args.evidence,
            safe_root,
        )
    except ValueError as exc:
        print(f"[err] {exc}", file=sys.stderr)
        return 2
    if not iso.is_file() or not all(path.is_file() for path in (vmrun, vdiskmanager, vmcli)):
        print("[err] ISO or VMware executable is missing", file=sys.stderr)
        return 2
    evidence_path.parent.mkdir(parents=True, exist_ok=True)
    evidence_path.unlink(missing_ok=True)

    run_id = uuid.uuid4().hex[:12]
    run_root = work_root / run_id
    run_root.mkdir(parents=True, exist_ok=False)
    display_name = f"CapyOS Installer No UART {run_id}"
    host_log = safe_root / f"smoke_x64_vmware_installer_no_uart_{run_id}.host.log"
    artifact_prefix = f"smoke_x64_vmware_installer_no_uart_{run_id}"
    prompt_bmp = safe_root / f"{artifact_prefix}.prompt.bmp"
    idle_bmp = safe_root / f"{artifact_prefix}.idle.bmp"
    zero_bmp = safe_root / f"{artifact_prefix}.zero.bmp"
    cancel_bmp = safe_root / f"{artifact_prefix}.cancel.bmp"
    failure_bmp = safe_root / f"{artifact_prefix}.failure.bmp"
    prompt_png = safe_root / f"{artifact_prefix}.prompt.png"
    idle_png = safe_root / f"{artifact_prefix}.idle.png"
    zero_png = safe_root / f"{artifact_prefix}.zero.png"
    cancel_png = safe_root / f"{artifact_prefix}.cancel.png"
    public_vmware_log = safe_root / f"smoke_x64_vmware_installer_no_uart_{run_id}.vmware.log"
    public_vmx = safe_root / f"smoke_x64_vmware_installer_no_uart_{run_id}.vmx"
    target_descriptor = run_root / "target.vmdk"
    guard_descriptor = run_root / "guard.vmdk"
    vmx = run_root / "CapyOS-Installer-No-UART.vmx"
    vmware_log = run_root / "vmware.log"
    reservation: LoopbackPortReservation | None = None
    console: RfbConsole | None = None
    start_attempted = False
    success = False
    primary_error: BaseException | None = None
    stop_error: BaseException | None = None

    try:
        iso_sha256_before = sha256_file(iso)
        vmrun_version = query_vmrun_version(vmrun, host_log)
        target_extent = create_vmdk(
            vdiskmanager, target_descriptor, args.target_size, host_log, run_root
        )
        guard_extent = create_vmdk(
            vdiskmanager, guard_descriptor, args.guard_size, host_log, run_root
        )
        if any(paths_equal(iso, path) for path in (target_extent, guard_extent)):
            raise RuntimeError("ISO and disposable VMDK extents must be distinct")
        seed_extent(target_extent, b"CAPYOS-VMWARE-NO-UART-TARGET-v1")
        seed_extent(guard_extent, b"CAPYOS-VMWARE-NO-UART-GUARD-v1")
        target_before = sha256_file(target_extent)
        guard_before = sha256_file(guard_extent)

        reservation = LoopbackPortReservation()
        vnc_port = reservation.port
        vmx_text = render_no_uart_vmx(
            display_name=display_name,
            iso_path=iso,
            target_descriptor=target_descriptor,
            guard_descriptor=guard_descriptor,
            vnc_port=vnc_port,
        )
        write_vmx(vmx, vmx_text)
        vmx_before = sha256_file(vmx)
        reservation.close()
        reservation = None

        start_attempted = True
        start_result = run_logged(
            [str(vmrun), "-T", "ws", "start", str(vmx), "nogui"],
            host_log,
            "start",
        )
        require_success(start_result, "vmrun start")
        console = RfbConsole.connect(vnc_port, args.step_timeout, display_name)
        wait_for_loopback_listener_log(vmware_log, vnc_port, min(10.0, args.step_timeout))
        runtime_serial_devices = query_serial_devices(vmcli, vmx, host_log)
        prompt_frame = wait_for_stable_console(
            console, args.step_timeout, args.boot_grace
        )
        frame_width = console.width
        frame_height = console.height
        write_frame_bmp(prompt_bmp, prompt_frame, frame_width, frame_height)
        capture_screenshot(vmcli, vmx, prompt_png, host_log)

        idle_frame = wait_for_unchanged_console(
            console, prompt_frame, args.idle_seconds
        )
        write_frame_bmp(idle_bmp, idle_frame, frame_width, frame_height)
        capture_screenshot(vmcli, vmx, idle_png, host_log)

        console.send_key(CANCEL_KEY_SEQUENCE[0])
        zero_frame, transition = wait_for_localized_zero(
            console, prompt_frame, args.step_timeout
        )
        write_frame_bmp(zero_bmp, zero_frame, frame_width, frame_height)
        capture_screenshot(vmcli, vmx, zero_png, host_log)

        zero_idle_seconds = min(2.0, args.idle_seconds)
        wait_for_unchanged_console(console, zero_frame, zero_idle_seconds)

        console.send_key(CANCEL_KEY_SEQUENCE[1])
        (
            cancel_frame,
            cancel_mode,
            cancel_transition,
            cancel_nonblack_before,
            cancel_nonblack_after,
            cancel_dominant_nonblack,
        ) = wait_for_expected_cancel(
            console, zero_frame, args.step_timeout
        )
        write_frame_bmp(cancel_bmp, cancel_frame, frame_width, frame_height)
        capture_screenshot(vmcli, vmx, cancel_png, host_log)
    except BaseException as exc:
        primary_error = exc
        if console is not None and console.initialized and not failure_bmp.exists():
            try:
                write_frame_bmp(
                    failure_bmp,
                    bytes(console.framebuffer),
                    console.width,
                    console.height,
                )
                print(
                    f"[info] exact failure framebuffer: {failure_bmp}",
                    file=sys.stderr,
                )
            except BaseException as frame_exc:
                with host_log.open("a", encoding="utf-8") as stream:
                    stream.write(
                        "failure_frame_capture_error="
                        f"{type(frame_exc).__name__}: {frame_exc}\n"
                    )
        if console is not None and console.trace:
            with host_log.open("a", encoding="utf-8") as stream:
                stream.write("rfb_trace:\n")
                stream.write("\n".join(console.trace) + "\n")
    finally:
        if reservation is not None:
            try:
                reservation.close()
            except BaseException as exc:
                if primary_error is None:
                    primary_error = exc
        if console is not None:
            try:
                console.close()
            except BaseException as exc:
                if primary_error is None:
                    primary_error = exc
        if start_attempted:
            try:
                stop_disposable_vm(vmrun, vmx, host_log)
            except BaseException as exc:
                stop_error = exc

    try:
        if primary_error is not None:
            raise primary_error
        if stop_error is not None:
            raise stop_error
        post_vmx_text = vmx.read_text(encoding="utf-8", errors="strict")
        if SERIAL_DEVICE_RE.search(post_vmx_text):
            raise RuntimeError("VMware added a serial device to the no-UART VMX")
        iso_sha256_after = sha256_file(iso)
        if iso_sha256_after != iso_sha256_before:
            raise RuntimeError("installer ISO changed during the VMware gate")
        target_after = sha256_file(target_extent)
        guard_after = sha256_file(guard_extent)
        if target_after != target_before:
            raise RuntimeError("target disk changed during safe cancellation gate")
        if guard_after != guard_before:
            raise RuntimeError("guard disk changed during safe cancellation gate")
        if not vmware_log.is_file():
            raise RuntimeError("VMware runtime log is missing")
        shutil.copyfile(vmware_log, public_vmware_log)
        shutil.copyfile(vmx, public_vmx)
        changed, box_width, box_height = transition
        cancel_changed, cancel_box_width, cancel_box_height = cancel_transition
        evidence = render_evidence(
            (
                ("format", "capyos-vmware-installer-no-uart-evidence-v2"),
                ("provider", "vmware-workstation"),
                ("track", "UEFI/GPT/x86_64"),
                ("iso_artifact", iso.name),
                ("iso_sha256", iso_sha256_before),
                ("iso_sha256_before", iso_sha256_before),
                ("iso_sha256_after", iso_sha256_after),
                ("iso_unchanged", "yes"),
                ("vmrun_version", vmrun_version),
                ("firmware", "uefi"),
                ("secure_boot", "disabled"),
                ("vcpu_count", "2"),
                ("memory_mib", "1024"),
                ("network", "disabled"),
                ("vmx_sha256_before", vmx_before),
                ("vmx_sha256_after", sha256_file(vmx)),
                ("serial_config_keys", "0"),
                ("serial_query_devices", str(runtime_serial_devices)),
                ("vnc_bind", LOOPBACK_ADDRESS),
                ("vnc_port", str(vnc_port)),
                ("vnc_security", "none"),
                ("rfb_version", console.version.decode("ascii").strip()),
                ("rfb_name", console.name),
                ("framebuffer", f"{console.width}x{console.height}"),
                ("rfb_desktop_resize_events", str(console.resize_events)),
                ("idle_seconds", f"{args.idle_seconds:g}"),
                ("prompt_nonempty", "yes"),
                ("input_zero_keysym", "0030"),
                ("input_return_keysym", "ff0d"),
                ("zero_changed_pixels", str(changed)),
                ("zero_change_box", f"{box_width}x{box_height}"),
                ("idle_matches_prompt", "yes"),
                ("zero_idle_seconds", f"{zero_idle_seconds:g}"),
                ("zero_stable_before_return", "yes"),
                ("cancel_transition", cancel_mode),
                ("cancel_changed_pixels", str(cancel_changed)),
                ("cancel_change_box", f"{cancel_box_width}x{cancel_box_height}"),
                ("cancel_nonblack_before", str(cancel_nonblack_before)),
                ("cancel_nonblack_after", str(cancel_nonblack_after)),
                ("cancel_dominant_nonblack", str(cancel_dominant_nonblack)),
                (
                    "cancel_nonblack_reduced",
                    "yes" if cancel_mode == "cleared-message" else "no",
                ),
                ("prompt_frame_sha256", frame_sha256(prompt_frame)),
                ("idle_frame_sha256", frame_sha256(idle_frame)),
                ("zero_frame_sha256", frame_sha256(zero_frame)),
                ("cancel_frame_sha256", frame_sha256(cancel_frame)),
                ("prompt_bmp", prompt_bmp.name),
                ("prompt_bmp_sha256", sha256_file(prompt_bmp)),
                ("idle_bmp", idle_bmp.name),
                ("idle_bmp_sha256", sha256_file(idle_bmp)),
                ("zero_bmp", zero_bmp.name),
                ("zero_bmp_sha256", sha256_file(zero_bmp)),
                ("cancel_bmp", cancel_bmp.name),
                ("cancel_bmp_sha256", sha256_file(cancel_bmp)),
                ("prompt_screenshot", prompt_png.name),
                ("prompt_screenshot_sha256", sha256_file(prompt_png)),
                ("idle_screenshot", idle_png.name),
                ("idle_screenshot_sha256", sha256_file(idle_png)),
                ("zero_screenshot", zero_png.name),
                ("zero_screenshot_sha256", sha256_file(zero_png)),
                ("cancel_screenshot", cancel_png.name),
                ("cancel_screenshot_sha256", sha256_file(cancel_png)),
                ("target_sha256_before", target_before),
                ("target_sha256_after", target_after),
                ("target_unchanged", "yes"),
                ("guard_sha256_before", guard_before),
                ("guard_sha256_after", guard_after),
                ("guard_unchanged", "yes"),
                ("host_log_sha256", sha256_file(host_log)),
                ("vmware_log_sha256", sha256_file(public_vmware_log)),
                ("host_log", host_log.name),
                ("vmware_log", public_vmware_log.name),
                ("vmx_artifact", public_vmx.name),
                ("vmx_artifact_sha256", sha256_file(public_vmx)),
                ("result", "pass"),
            )
        )
        temporary = evidence_path.with_suffix(evidence_path.suffix + ".tmp")
        temporary.write_text(evidence, encoding="utf-8")
        os.replace(temporary, evidence_path)
        success = True
        print(f"[ok] VMware no-UART installer input gate passed: {evidence_path}")
        return 0
    except BaseException as exc:
        print(
            f"[err] VMware no-UART installer input gate failed: "
            f"{type(exc).__name__}: {exc}",
            file=sys.stderr,
        )
        print(f"[info] preserving VMware scratch evidence: {run_root}", file=sys.stderr)
        if stop_error is not None and stop_error is not exc:
            print(f"[err] additional cleanup failure: {stop_error}", file=sys.stderr)
        return 1
    finally:
        if success and not args.keep_vm:
            checked_run_root = resolve_under_safe_root(run_root, work_root)
            if checked_run_root.parent != work_root or checked_run_root.name != run_id:
                raise RuntimeError("refusing unsafe VMware scratch cleanup")
            shutil.rmtree(checked_run_root, ignore_errors=False)


if __name__ == "__main__":
    raise SystemExit(main())
