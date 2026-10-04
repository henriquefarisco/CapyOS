#!/usr/bin/env python3
"""Real xHCI control/interrupt regression using a QEMU USB keyboard, not UART."""
import argparse
import json
from pathlib import Path
import socket
import tempfile

from smoke_x64_common import (provision_disk, validate_installed_disk_artifacts,
    resolve_ovmf_or_raise, resolve_qemu_binary, print_log_tail)
from smoke_x64_session import SmokeSession, make_qemu_cmd, choose_free_port
from smoke_x64_qemu_marker import (wait_for_clean_marker, stop_and_check_session,
                                    verify_audio_playlist_capture)

ROOT = Path(__file__).resolve().parents[2]
FAILURES = ("KERNEL PANIC", "#PF", "#GP", "descriptor read failed",
            "HID interrupt endpoint config failed", "address device failed")


def qmp_execute(stream, command, arguments=None):
    request = {"execute": command, "id": command}
    if arguments is not None:
        request["arguments"] = arguments
    stream.write(json.dumps(request).encode() + b"\r\n")
    stream.flush()
    for _ in range(64):
        line = stream.readline(65537)
        if not line or len(line) > 65536:
            raise RuntimeError("invalid/closed QMP response")
        response = json.loads(line)
        if response.get("id") != command:
            continue
        if "error" in response or "return" not in response:
            raise RuntimeError(f"QMP {command}: {response}")
        return response["return"]
    raise RuntimeError("QMP response budget exhausted")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--timeout", type=float, default=180)
    parser.add_argument("--ovmf")
    parser.add_argument("--usb-audio-probe", action="store_true",
                        help="Also require UAC1 discovery; does not assert USB playback")
    parser.add_argument("--usb-audio-playback", action="store_true",
                        help="Require captured USB PCM playlist alongside keyboard input")
    parser.add_argument("--disconnect-audio", action="store_true",
                        help="Unplug the active speaker and require error containment plus live keyboard")
    parser.add_argument("--log", default="build/ci/smoke_x64_qemu_usb_hid.log")
    args = parser.parse_args()
    if args.disconnect_audio and not args.usb_audio_playback:
        parser.error("--disconnect-audio requires --usb-audio-playback")
    failures = FAILURES + (("[smoke] media-player-playlist FAIL",)
                           if args.usb_audio_playback and not args.disconnect_audio else ())
    log = (ROOT / args.log).resolve()
    log.parent.mkdir(parents=True, exist_ok=True)
    boot, kernel, manifest = validate_installed_disk_artifacts(ROOT)
    code, template = resolve_ovmf_or_raise(args.ovmf)
    qemu = resolve_qemu_binary("qemu-system-x86_64")
    # Short private /tmp paths also respect Unix socket pathname limits.
    with tempfile.TemporaryDirectory(prefix="capy-usb-") as temporary:
        work = Path(temporary)
        disk, variables, qmp = work / "disk.img", work / "vars.fd", work / "qmp"
        variables.write_bytes(Path(template).read_bytes())
        provision_disk(repo_root=ROOT, disk_path=disk, disk_size="2G",
                       bootx64=boot, kernel=kernel, manifest=manifest,
                       keyboard_layout="us", language="en",
                       volume_key="CAPYOS-SMOKE-KEY-2026-0001")
        port = choose_free_port()
        debug = log.with_suffix(".debugcon.log")
        capture = log.with_suffix(".wav")
        command = make_qemu_cmd(qemu, code, variables, disk, port, 512,
                                debugcon_log=debug, audio_hda=not args.usb_audio_playback)
        command += ["-device", "qemu-xhci,id=usb", "-device",
                    "usb-kbd,bus=usb.0,id=test-keyboard", "-qmp",
                    f"unix:{qmp},server=on,wait=off"]
        if args.usb_audio_probe or args.usb_audio_playback:
            backend = "driver=none,id=usbaudio"
            if args.usb_audio_playback:
                backend = (f"driver=wav,id=usbaudio,path={str(capture).replace(',', ',,')},"
                           "out.frequency=48000,out.channels=2,out.format=s16")
            command += ["-audiodev", backend, "-device",
                        "usb-audio,bus=usb.0,audiodev=usbaudio,id=test-audio"]
        session = SmokeSession(command, port, log, debugcon_log_path=debug)
        result = 0
        try:
            session.start()
            wait_for_clean_marker(session, "[smoke] usb-hid-keyboard configured",
                                  FAILURES, args.timeout)
            if args.usb_audio_probe or args.usb_audio_playback:
                wait_for_clean_marker(session, "[usb-audio] UAC1 48k stereo S16 discovered",
                                      FAILURES, args.timeout)
            with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as client:
                client.settimeout(5)
                client.connect(str(qmp))
                with client.makefile("rwb") as stream:
                    greeting = json.loads(stream.readline(65537))
                    if "QMP" not in greeting:
                        raise RuntimeError("missing QMP greeting")
                    qmp_execute(stream, "qmp_capabilities")
                    if args.usb_audio_playback:
                        wait_for_clean_marker(session, "[smoke] media-player-playlist guarded-DMA ready",
                                              FAILURES, args.timeout)
                    if args.disconnect_audio:
                        unplug_cursor = session.marker()
                        qmp_execute(stream, "device_del", {"id": "test-audio"})
                        for expected in ("[smoke] media-player-playlist FAIL playback-or-progress",
                                         "[lr] shell runtime ready"):
                            observed = session.wait_for_any(list(FAILURES) + [expected],
                                timeout=args.timeout, start_at=unplug_cursor)
                            if observed != expected:
                                raise RuntimeError(f"unplug containment failed: {observed}")
                    qmp_execute(stream, "send-key", {
                        "keys": [{"type": "qcode", "data": "a"}], "hold-time": 100})
            wait_for_clean_marker(session, "[smoke] usb-hid-keyboard ready",
                                  FAILURES, args.timeout)
            if args.usb_audio_playback and not args.disconnect_audio:
                wait_for_clean_marker(session, "[smoke] media-player-playlist ready",
                    FAILURES + ("[smoke] media-player-playlist FAIL",), args.timeout)
        except (OSError, ValueError, RuntimeError, TimeoutError) as error:
            print(f"[err] USB HID runtime: {error}")
            result = 1
        finally:
            try:
                stop_and_check_session(session, failures)
            except RuntimeError as error:
                print(f"[err] final USB HID capture: {error}")
                result = 1
        if result:
            print_log_tail(log)
            print_log_tail(debug)
            return result
        print("[ok] USB enumeration, EP0 configuration and injected HID report passed")
        if args.usb_audio_probe:
            print("[ok] UAC1 speaker discovery coexists with HID; USB playback not tested")
        if args.usb_audio_playback:
            if args.disconnect_audio:
                print("[ok] active USB speaker unplug isolated; shell and actual USB keyboard remain live")
            else:
                print(f"[ok] USB PCM with HID: {verify_audio_playlist_capture(capture)}")
        return 0


if __name__ == "__main__":
    raise SystemExit(main())
