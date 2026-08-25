from __future__ import annotations

import struct
import tempfile
from pathlib import Path

from smoke_x64_vmware_installer_no_uart import (
    CANCEL_KEY_SEQUENCE,
    KEYSYM_RETURN,
    KEYSYM_ZERO,
    RFB_ENCODING_DESKTOP_SIZE,
    RFB_VERSION_33,
    RFB_VERSION_38,
    RfbConsole,
    SERIAL_DEVICE_RE,
    classify_cancel_transition,
    changed_pixel_metrics,
    listener_log_proves_loopback,
    key_packets,
    parse_serial_query_output,
    parse_rfb_server_version,
    parse_vmrun_version_output,
    parse_vmrun_list,
    render_evidence,
    render_no_uart_vmx,
    resolve_under_safe_root,
    set_encodings_packet,
    transition_is_material,
    write_frame_bmp,
    zero_input_is_localized,
)


def fail(message: str) -> int:
    print(f"[FAIL] {message}")
    return 1


def main() -> int:
    vmx = render_no_uart_vmx(
        display_name="CapyOS Installer No UART abc123",
        iso_path=Path(r"C:\build\CapyOS.iso"),
        target_descriptor=Path(r"C:\scratch\target.vmdk"),
        guard_descriptor=Path(r"C:\scratch\guard.vmdk"),
        vnc_port=59123,
    )
    required = (
        'firmware = "efi"',
        'uefi.secureBoot.enabled = "FALSE"',
        'efi.serialConsole.enabled = "FALSE"',
        'ethernet0.present = "FALSE"',
        'RemoteDisplay.vnc.enabled = "TRUE"',
        'RemoteDisplay.vnc.ip = "127.0.0.1"',
        'RemoteDisplay.vnc.port = "59123"',
        'RemoteDisplay.vnc.keyMap = "us"',
        'pciBridge0.present = "TRUE"',
        'pciBridge4.virtualDev = "pcieRootPort"',
        'pciBridge4.functions = "8"',
        'pciBridge5.virtualDev = "pcieRootPort"',
        'pciBridge5.functions = "8"',
        'pciBridge6.virtualDev = "pcieRootPort"',
        'pciBridge6.functions = "8"',
        'pciBridge7.virtualDev = "pcieRootPort"',
        'pciBridge7.functions = "8"',
        'sata0:2.deviceType = "cdrom-image"',
        'usb_xhci.present = "TRUE"',
    )
    if any(value not in vmx for value in required):
        return fail("no-UART VMX lost a required VMware setting")
    if SERIAL_DEVICE_RE.search(vmx) or "RemoteDisplay.vnc.password" in vmx:
        return fail("no-UART VMX contains serial hardware or remote password")
    try:
        render_no_uart_vmx(
            display_name='bad"name',
            iso_path=Path("iso"),
            target_descriptor=Path("target"),
            guard_descriptor=Path("guard"),
            vnc_port=1,
        )
    except ValueError:
        pass
    else:
        return fail("VMX renderer accepted an injected display name")

    packets = tuple(
        packet for keysym in CANCEL_KEY_SEQUENCE for packet in key_packets(keysym)
    )
    decoded = tuple(struct.unpack("!BBHI", packet) for packet in packets)
    expected = (
        (4, 1, 0, KEYSYM_ZERO),
        (4, 0, 0, KEYSYM_ZERO),
        (4, 1, 0, KEYSYM_RETURN),
        (4, 0, 0, KEYSYM_RETURN),
    )
    if decoded != expected:
        return fail("cancel input is not exactly 0 down/up plus Return down/up")
    if (
        parse_vmrun_version_output("vmrun version 1.17.0.25388281\n\nUsage:\n")
        != "1.17.0.25388281"
    ):
        return fail("exact VMware vmrun version was not parsed")
    for invalid_version in ("", "vmrun version unknown\n", "1.17.0.25388281\n"):
        try:
            parse_vmrun_version_output(invalid_version)
        except RuntimeError:
            pass
        else:
            return fail("ambiguous VMware vmrun version was accepted")
    encodings = struct.unpack("!BBHii", set_encodings_packet())
    if encodings != (2, 0, 2, 0, RFB_ENCODING_DESKTOP_SIZE):
        return fail("RFB client did not advertise raw plus DesktopSize")

    if parse_rfb_server_version(RFB_VERSION_33) != RFB_VERSION_33:
        return fail("RFB 3.3 was rejected")
    if parse_rfb_server_version(RFB_VERSION_38) != RFB_VERSION_38:
        return fail("RFB 3.8 was rejected")
    try:
        parse_rfb_server_version(b"RFB 003.007\n")
    except RuntimeError:
        pass
    else:
        return fail("unsupported RFB version was accepted")

    width = 64
    height = 32
    before = bytes(width * height * 4)
    after = bytearray(before)
    for y in range(8, 24):
        for x in range(8, 48):
            offset = (y * width + x) * 4
            after[offset : offset + 4] = b"\xff\xff\xff\x00"
    metrics = changed_pixel_metrics(before, bytes(after), width)
    material, returned_metrics = transition_is_material(
        before, bytes(after), width, height
    )
    if metrics != (640, 40, 16) or returned_metrics != metrics or not material:
        return fail("material framebuffer transition was rejected")
    cursor = bytearray(before)
    cursor[0 : 64 * 4] = b"\xff\xff\xff\x00" * 64
    if transition_is_material(before, bytes(cursor), width, height)[0]:
        return fail("single-row cursor-like change was accepted")

    cancel_width = 128
    cancel_height = 96
    cancel_before = bytearray(cancel_width * cancel_height * 4)
    for y in range(16, 80):
        for x in range(16, 112):
            offset = (y * cancel_width + x) * 4
            cancel_before[offset : offset + 4] = b"\xff\xff\xff\x00"
    cancel_after = bytearray(cancel_width * cancel_height * 4)
    for y in range(64, 72):
        for x in range(24, 104):
            offset = (y * cancel_width + x) * 4
            cancel_after[offset : offset + 4] = b"\xff\xff\xff\x00"
    mode, cancel_metrics, before_nonblack, after_nonblack, dominant_nonblack = (
        classify_cancel_transition(
            bytes(cancel_before), bytes(cancel_after), cancel_width, cancel_height
        )
    )
    if (
        mode != "cleared-message"
        or cancel_metrics[0] < 4096
        or before_nonblack != 6144
        or after_nonblack != 640
        or dominant_nonblack != 640
    ):
        return fail("expected cleared cancellation screen was rejected")
    invalid_selection = bytearray(cancel_before)
    for y in range(80, 88):
        for x in range(24, 104):
            offset = (y * cancel_width + x) * 4
            invalid_selection[offset : offset + 4] = b"\xff\xff\xff\x00"
    if classify_cancel_transition(
        bytes(cancel_before),
        bytes(invalid_selection),
        cancel_width,
        cancel_height,
    )[0] is not None:
        return fail("appended invalid-selection line was accepted as cancellation")
    phantom_fill = bytearray(cancel_before)
    for y in range(80, 96):
        for x in range(cancel_width):
            offset = (y * cancel_width + x) * 4
            phantom_fill[offset : offset + 4] = b"\xff\xff\xff\x00"
    if classify_cancel_transition(
        bytes(cancel_before), bytes(phantom_fill), cancel_width, cancel_height
    )[0] is not None:
        return fail("growing phantom-input screen was accepted as cancellation")

    firmware_before = bytearray(cancel_width * cancel_height * 4)
    for y in range(44, 48):
        for x in range(48, 80):
            offset = (y * cancel_width + x) * 4
            firmware_before[offset : offset + 4] = b"\xff\xff\xff\x00"
    firmware_after = bytearray(cancel_width * cancel_height * 4)
    for y in range(16, 80):
        for x in range(16, 112):
            offset = (y * cancel_width + x) * 4
            firmware_after[offset : offset + 4] = b"\x80\x00\x00\x00"
    if classify_cancel_transition(
        bytes(firmware_before),
        bytes(firmware_after),
        cancel_width,
        cancel_height,
    )[0] != "firmware-return":
        return fail("structured firmware return screen was rejected")

    localized_width = 1024
    localized_height = 768
    localized_before = bytes(localized_width * localized_height * 4)
    localized_after = bytearray(localized_before)
    for y in range(480, 496):
        for x in range(520, 528):
            offset = (y * localized_width + x) * 4
            localized_after[offset : offset + 4] = b"\xff\xff\xff\x00"
    localized, localized_metrics = zero_input_is_localized(
        localized_before,
        bytes(localized_after),
        localized_width,
        localized_height,
    )
    if not localized or localized_metrics != (128, 8, 16):
        return fail("localized zero-input change was rejected")
    corrupted = bytearray(localized_before)
    for y in range(480, 496):
        for x in range(300, 700):
            offset = (y * localized_width + x) * 4
            corrupted[offset : offset + 4] = b"\xff\xff\xff\x00"
    if zero_input_is_localized(
        localized_before, bytes(corrupted), localized_width, localized_height
    )[0]:
        return fail("wide phantom-input-like change was accepted as exact zero")

    class FakeSocket:
        def __init__(self, received: bytes) -> None:
            self.received = bytearray(received)
            self.sent = bytearray()

        def recv(self, size: int) -> bytes:
            result = bytes(self.received[:size])
            del self.received[:size]
            return result

        def sendall(self, data: bytes) -> None:
            self.sent.extend(data)

        def settimeout(self, timeout: float) -> None:
            del timeout

    raw = bytes(range(16))
    server_messages = (
        b"\x00\x00\x00\x01"
        + struct.pack("!HHHHi", 0, 0, 2, 2, RFB_ENCODING_DESKTOP_SIZE)
        + b"\x00\x00\x00\x01"
        + struct.pack("!HHHHi", 0, 0, 2, 2, 0)
        + raw
    )
    fake = FakeSocket(server_messages)
    resized_console = RfbConsole(fake, RFB_VERSION_38, "contract")  # type: ignore[arg-type]
    resized_console.width = 640
    resized_console.height = 480
    resized_console.framebuffer = bytearray(640 * 480 * 4)
    resized_console.initialized = True
    if resized_console.request_frame() != raw:
        return fail("RFB DesktopSize did not lead to a complete resized frame")
    if (
        resized_console.width,
        resized_console.height,
        resized_console.resize_events,
        len(fake.sent),
    ) != (2, 2, 1, 20):
        return fail("RFB DesktopSize state or follow-up request is invalid")

    valid_log = (
        "MKSRemoteMgr: loading\n"
        "MKSSocketListener: Started VNC server, listening at 127.0.0.1:59123\n"
    )
    if not listener_log_proves_loopback(valid_log, 59123):
        return fail("loopback VNC listener evidence was rejected")
    invalid_logs = (
        valid_log.replace("127.0.0.1", "0.0.0.0"),
        valid_log.replace("127.0.0.1", "127.0.0.10"),
        valid_log.replace("59123", "59124"),
        valid_log.replace("59123", "591230"),
        "",
    )
    if any(listener_log_proves_loopback(text, 59123) for text in invalid_logs):
        return fail("non-loopback or wrong-port VNC evidence was accepted")

    serial_none = "No serial ports configured in the guest.\r\ndevices: []\r\n"
    if parse_serial_query_output(serial_none) != 0:
        return fail("empty VMware runtime serial query was rejected")
    for ambiguous in ("devices: []\n", serial_none + "warning\n", "devices: [serial0]\n"):
        try:
            parse_serial_query_output(ambiguous)
        except RuntimeError:
            pass
        else:
            return fail("ambiguous VMware runtime serial query was accepted")

    vmrun_paths = parse_vmrun_list(
        "Total running VMs: 2\nC:\\scratch\\one.vmx\nC:\\scratch\\two.vmx\n"
    )
    if len(vmrun_paths) != 2:
        return fail("vmrun list parser lost an exact VM path")
    try:
        parse_vmrun_list("Total running VMs: 1\n")
    except RuntimeError:
        pass
    else:
        return fail("ambiguous vmrun list count was accepted")

    with tempfile.TemporaryDirectory() as temp:
        safe = Path(temp) / "build" / "ci"
        safe.mkdir(parents=True)
        inside = safe / "vmware" / "run"
        if resolve_under_safe_root(inside, safe) != inside.resolve():
            return fail("safe scratch path was not resolved")
        try:
            resolve_under_safe_root(Path(temp) / "outside", safe)
        except ValueError:
            pass
        else:
            return fail("path outside build/ci was accepted")

        bmp = safe / "frame.bmp"
        write_frame_bmp(bmp, bytes(2 * 2 * 4), 2, 2)
        bmp_data = bmp.read_bytes()
        if bmp_data[:2] != b"BM" or len(bmp_data) != 70:
            return fail("exact framebuffer BMP evidence is malformed")

    evidence = render_evidence(
        (("format", "test-v1"), ("serial_devices", "0"), ("guard_unchanged", "yes"))
    )
    if evidence != "format=test-v1\nserial_devices=0\nguard_unchanged=yes\n":
        return fail("evidence rendering is not deterministic")
    try:
        render_evidence((("format", "test-v1"), ("format", "duplicate")))
    except ValueError:
        pass
    else:
        return fail("duplicate evidence field was accepted")

    print("[OK] VMware installer no-UART contract")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
