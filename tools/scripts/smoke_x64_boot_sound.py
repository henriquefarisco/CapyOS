#!/usr/bin/env python3
"""Two cold boots of one disposable disk; capture the real startup sound/logo."""
import argparse
import json
from pathlib import Path
import socket
import shutil
import tempfile

from smoke_x64_common import provision_disk, validate_installed_disk_artifacts, resolve_ovmf_or_raise, resolve_qemu_binary
from smoke_x64_session import SmokeSession, make_qemu_cmd, choose_free_port
from smoke_x64_qemu_marker import wait_for_clean_marker, stop_and_check_session, verify_audio_capture
from smoke_x64_qemu_usb_hid import qmp_execute
from verify_boot_sound_capture import verify

ROOT = Path(__file__).resolve().parents[2]
FAILURES = ("KERNEL PANIC", "#PF", "#GP", "[boot-audio] unavailable", "[music] preset failed")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=ROOT / "build/boot-sound-validation")
    parser.add_argument("--artifact-dir", type=Path,
                        help="Explicit loader/kernel/manifest bundle; default is build/")
    parser.add_argument("--timeout", type=float, default=300)
    parser.add_argument("--boots", type=int, choices=range(1, 11), default=2)
    parser.add_argument("--no-screenshot", action="store_true",
                        help="isolate capture overhead while diagnosing playback")
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    boot, kernel, manifest = validate_installed_disk_artifacts(ROOT, artifact_dir=args.artifact_dir)
    code, template = resolve_ovmf_or_raise(None)
    qemu = resolve_qemu_binary("qemu-system-x86_64")
    with tempfile.TemporaryDirectory(prefix="capy-boot-sound-") as temp:
        work = Path(temp)
        disk = work / "disk.img"
        provision_disk(repo_root=ROOT, disk_path=disk, disk_size="2G", bootx64=boot,
                       kernel=kernel, manifest=manifest, keyboard_layout="us", language="en",
                       volume_key="CAPYOS-SMOKE-KEY-2026-0001")
        for run in range(1, args.boots + 1):
            variables = work / f"vars{run}.fd"
            variables.write_bytes(Path(template).read_bytes())
            qmp = work / f"qmp{run}"
            log, debug = output / f"boot{run}.log", output / f"boot{run}.debug.log"
            capture, screenshot = output / f"boot{run}.wav", output / f"boot{run}.ppm"
            live_capture = work / f"boot{run}.wav"
            live_screenshot = work / f"boot{run}.ppm"
            port = choose_free_port()
            command = make_qemu_cmd(qemu, code, variables, disk, port, 1024,
                                   debugcon_log=debug, audio_hda=True, audio_capture=live_capture)
            command += ["-qmp", f"unix:{qmp},server=on,wait=off"]
            session = SmokeSession(command, port, log, debugcon_log_path=debug)
            session.start()
            try:
                wait_for_clean_marker(session, "[music] preset ready", FAILURES, args.timeout)
                wait_for_clean_marker(session, "[boot-audio] starting", FAILURES, args.timeout)
                with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as client:
                    client.settimeout(5)
                    client.connect(str(qmp))
                    with client.makefile("rwb") as stream:
                        if "QMP" not in json.loads(stream.readline(65537)):
                            raise RuntimeError("invalid QMP greeting")
                        qmp_execute(stream, "qmp_capabilities")
                        if not args.no_screenshot:
                            qmp_execute(stream, "screendump", {"filename": str(live_screenshot)})
                wait_for_clean_marker(session, "[boot-audio] completed", FAILURES, args.timeout)
            finally:
                try:
                    stop_and_check_session(session, FAILURES)
                finally:
                    if live_screenshot.exists():
                        shutil.copyfile(live_screenshot, screenshot)
                    if live_capture.exists():
                        shutil.copyfile(live_capture, capture)
            print(f"[ok] cold boot {run}: preset files, startup EOF; screenshot={not args.no_screenshot}; "
                  f"{verify_audio_capture(capture)} active PCM frames", flush=True)
            print(f"[ok] real boot PCM: {verify(capture, ROOT / 'build/generated/sounds/boot.wav')}", flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
