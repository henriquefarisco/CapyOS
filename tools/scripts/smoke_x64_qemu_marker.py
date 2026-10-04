#!/usr/bin/env python3
"""
Generic CapyOS x64 QEMU+OVMF runtime smoke: boot a built kernel and assert a
COM1 marker. Development feedback / CI pre-flight only; VMware + UEFI + E1000
remains the official release-acceptance gate.

Unlike smoke_x64_qemu_capybrowse.py (which adds a hermetic HTTP server + an
E1000 NIC for the browser path), this driver is marker-only: it provisions the
GPT disk from the already-built artifacts, boots it, and waits for a `--marker`
string on the serial log. It suits in-kernel boot markers that fire before login
and need no network -- e.g. the Etapa 6 apps-basic-roundtrip orchestrator
(`[smoke] apps-basic-roundtrip ready`).

The kernel must already be built with the relevant gate flags (the Makefile
target does that) and `make iso-uefi manifest64` run, so the installed-disk
artifacts exist.

Pass criterion: `--marker` observed on the COM1 serial log within `--timeout`.
Failure: timeout / early QEMU exit / any `--fail-marker` present -> exit 1 +
serial-log tail to stderr. Missing build artifacts -> exit 2.
"""

from __future__ import annotations

import argparse
import array
import sys
import wave
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO_ROOT / "tools" / "scripts"))

from smoke_x64_common import (  # noqa: E402  (sys.path tweak above)
    boot_with_session,
    cleanup_file,
    create_runtime_ovmf_vars,
    print_log_tail,
    provision_disk,
    resolve_ovmf_or_raise,
    resolve_qemu_binary,
    validate_installed_disk_artifacts,
)

DEFAULT_FAIL_MARKERS = ("panic", "KERNEL PANIC", "#PF", "#GP")


def wait_for_clean_marker(session, marker, fail_markers, timeout):
    observed = session.wait_for_any(list(fail_markers) + [marker], timeout=timeout)
    captured = session.text()
    failure = next((value for value in fail_markers if value in captured), None)
    if observed != marker or failure is not None:
        raise RuntimeError(f"failure marker observed: {failure or observed!r}")


def stop_and_check_session(session, fail_markers):
    """Drain the final capture before accepting a successful guest marker."""
    session.stop()
    captured = session.text()
    failure = next((value for value in fail_markers if value in captured), None)
    if failure is not None:
        raise RuntimeError(f"failure marker in final capture: {failure!r}")


def verify_audio_capture(path):
    """Require sustained non-silent PCM and reject gaps inside the tone."""
    with wave.open(str(path), "rb") as capture:
        if (capture.getnchannels(), capture.getsampwidth(), capture.getframerate()) != (2, 2, 48000):
            raise RuntimeError("audio capture must be 48 kHz stereo S16")
        if capture.getnframes() > 48000 * 30:
            raise RuntimeError("audio capture exceeds the 30-second test budget")
        samples = array.array("h", capture.readframes(capture.getnframes()))
    if sys.byteorder != "little":
        samples.byteswap()
    active = [i for i, sample in enumerate(samples) if abs(sample) > 100]
    if len(active) < 48000 * 2:
        raise RuntimeError("less than one second of audible PCM captured")
    longest_gap = max((b - a - 1 for a, b in zip(active, active[1:])), default=0)
    if longest_gap > 480 * 2:
        raise RuntimeError("audio contains a silent gap longer than 10 ms")
    return len(active) // 2


def verify_audio_stream_capture(path):
    """Distinguish 240/300/400 Hz fixture sections from a repeated DMA ring."""
    if verify_audio_capture(path) < 130000:
        raise RuntimeError("stream capture is shorter than the three-section fixture")
    with wave.open(str(path), "rb") as capture:
        samples = array.array("h", capture.readframes(capture.getnframes()))
    if sys.byteorder != "little":
        samples.byteswap()
    left, right = samples[::2], samples[1::2]
    if left != right:
        raise RuntimeError("fixture stereo channels differ")
    start = next(i for i, sample in enumerate(left) if abs(sample) > 100)
    counts = []
    for second, expected in enumerate((240, 300, 400)):
        window = left[start + second * 48000 + 9600:
                      start + second * 48000 + 33600]
        changes = sum((a < 0) != (b < 0) for a, b in zip(window, window[1:]))
        if len(window) != 24000 or abs(changes - expected) > 12:
            raise RuntimeError(f"stream section {second} has {changes} transitions, expected {expected}")
        counts.append(changes)
    return counts


def verify_audio_multi_capture(path, *, ac97=False, usb=False):
    """Check audible mixing, independent gain, EOF and stop after ring latency."""
    frames = verify_audio_capture(path)
    if not 310000 <= frames <= 340000:
        raise RuntimeError("multi-app fixture duration outside bounds")
    with wave.open(str(path), "rb") as capture:
        samples = array.array("h", capture.readframes(capture.getnframes()))
    if sys.byteorder != "little": samples.byteswap()
    left, right = samples[::2], samples[1::2]
    if left != right:
        raise RuntimeError("multi-app stereo channels differ")
    start = next(i for i, value in enumerate(left) if abs(value) > 100)
    # QEMU 10.2 AC97 get_volume() maps PCM register 0x0808 linearly:
    # 255 - floor(255*8/31) = 190, unlike the real codec's 0 dB setting.
    # Fixed model expectation, never normalize to arbitrary captured gain.
    if ac97 and usb:
        raise ValueError("one audio backend per capture")
    # QEMU 10.2.1 hw/usb/dev-audio.c initializes UAC1 volume to 240 (0 dB),
    # passed directly to AUD_set_volume_out. Fixed model gain, not a fit to PCM.
    gain = 240 / 255 if usb else (190 / 255 if ac97 else 1.0)
    phases = ((0.5, (4000,)), (1.7, (2000, 6000)),
              (2.7, (3000, 5000)), (3.7, (1500, 2500)),
              (4.7, (2000,)), (5.5, (1500, 2500)), (6.5, (2000,)))
    for second, levels in phases:
        offset = start + int(second * 48000)
        window = left[offset:offset + 4800]
        matches = sum(any(abs(abs(value) - level * gain) <= 2 for level in levels)
                      for value in window)
        if len(window) != 4800 or matches < 4752:
            raise RuntimeError(f"multi-app phase {second}s: incorrect mix/gain/stop")
    return frames


def verify_audio_playlist_capture(path):
    """Two 3-second fixtures; permit loading silence only between the tracks.

    This gate does not claim gapless playlist transitions. Each audible run
    must independently contain the full 240/300/400 Hz source progression.
    """
    with wave.open(str(path), "rb") as capture:
        if (capture.getnchannels(), capture.getsampwidth(), capture.getframerate()) != (2, 2, 48000):
            raise RuntimeError("playlist capture must be 48 kHz stereo S16")
        if capture.getnframes() > 48000 * 30:
            raise RuntimeError("playlist capture exceeds budget")
        samples = array.array("h", capture.readframes(capture.getnframes()))
    if sys.byteorder != "little": samples.byteswap()
    left, right = samples[::2], samples[1::2]
    if left != right:
        raise RuntimeError("fixture stereo channels differ")
    active = [i for i, value in enumerate(left) if abs(value) > 100]
    if not active: raise RuntimeError("silent playlist")
    boundaries = [i for i in range(1, len(active)) if active[i] - active[i-1] > 481]
    if len(boundaries) > 1:
        raise RuntimeError("extra silent gap inside playlist tracks")
    # QEMU's WAV backend can omit time while a stream is stopped, yielding
    # concatenated PCM rather than a loading gap. Both representations must
    # still contain two complete 240/300/400 sequences of bounded duration.
    split = boundaries[0] if boundaries else len(active) // 2
    groups = (active[:split], active[split:])
    counts = []
    for group in groups:
        if not 130000 <= len(group) <= 155000:
            raise RuntimeError("playlist track duration outside fixture bounds")
        start = group[0]
        transitions = []
        for second, expected in enumerate((240, 300, 400)):
            window = left[start + second * 48000 + 9600:start + second * 48000 + 33600]
            changes = sum((a < 0) != (b < 0) for a, b in zip(window, window[1:]))
            if len(window) != 24000 or abs(changes - expected) > 12:
                raise RuntimeError("playlist fixture section missing/replayed")
            transitions.append(changes)
        counts.append(transitions)
    return counts


def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--marker", required=True,
                   help="COM1 success marker string to wait for")
    p.add_argument("--fail-marker", action="append", default=[],
                   help="Extra marker that means failure (repeatable; the "
                        "common panic/fault markers are always checked)")
    p.add_argument("--timeout", type=float, default=180.0,
                   help="Seconds to wait for the marker (TCG boot is slow)")
    p.add_argument("--qemu", default="qemu-system-x86_64")
    p.add_argument("--ovmf", default=None,
                   help="Path to OVMF_CODE.fd (auto-detected if omitted)")
    p.add_argument("--memory", type=int, default=512)
    p.add_argument("--storage-bus", choices=("sata", "nvme"), default="sata")
    p.add_argument("--networking", action="store_true",
                   help="Attach an E1000 user-net NIC (off by default; the "
                        "in-kernel boot markers need no network)")
    p.add_argument("--audio-hda", action="store_true",
                   help="Attach Intel HDA with a hermetic null audio backend")
    p.add_argument("--audio-ac97", action="store_true",
                   help="Attach an ICH AC'97 controller instead of HDA so the "
                        "guest service exercises its AC'97 fallback")
    p.add_argument("--audio-capture", type=Path,
                   help="Capture the audio device output to WAV and verify sustained PCM")
    p.add_argument("--audio-usb", action="store_true",
                   help="Attach only xHCI/UAC1 speaker for real USB PCM validation")
    p.add_argument("--audio-stream-fixture", action="store_true",
                   help="Require the three-section streamed audio fixture (WAV or Ogg)")
    p.add_argument("--audio-playlist-fixture", action="store_true",
                   help="Require two three-section WAV tracks; loading gap optional")
    p.add_argument("--audio-multi-fixture", action="store_true",
                   help="Verify two-source PCM mixing, gains, EOF and stop")
    p.add_argument("--audio-builtin-music", action="store_true",
                   help="Compare the complete three-song preset playlist against decoded PCM")
    p.add_argument("--log", default="build/ci/smoke_x64_qemu_marker.log",
                   help="Combined QEMU stdout + COM1 serial log")
    p.add_argument("--debugcon-log",
                   default="build/ci/smoke_x64_qemu_marker.debugcon.log")
    p.add_argument("--disk", default="build/ci/smoke_x64_qemu_marker.img")
    p.add_argument("--artifact-dir", type=Path,
                   help="Explicit loader/kernel/manifest bundle; default is build/")
    p.add_argument("--disk-size", default="2G")
    p.add_argument("--keep-disk", action="store_true")
    p.add_argument("--volume-key", default="CAPYOS-SMOKE-KEY-2026-0001")
    p.add_argument("--keyboard-layout", default="us")
    p.add_argument("--language", default="en")
    p.add_argument("--verbose", action="store_true")
    return p.parse_args()


def main() -> int:
    args = parse_args()
    if args.audio_builtin_music and (not args.audio_capture or not args.audio_hda or
            args.audio_multi_fixture or args.audio_playlist_fixture or args.audio_stream_fixture):
        print("[err] builtin music requires HDA capture and excludes synthetic fixtures", file=sys.stderr)
        return 2
    if args.audio_multi_fixture and (not args.audio_capture or
            args.audio_stream_fixture or args.audio_playlist_fixture):
        print("[err] multi fixture requires capture and excludes other fixtures", file=sys.stderr)
        return 2
    if args.audio_playlist_fixture and (not args.audio_capture or args.audio_stream_fixture):
        print("[err] playlist fixture requires capture and excludes stream fixture", file=sys.stderr)
        return 2
    if args.audio_stream_fixture and not args.audio_capture:
        print("[err] --audio-stream-fixture requires --audio-capture", file=sys.stderr)
        return 2
    if sum((args.audio_hda, args.audio_ac97, args.audio_usb)) > 1:
        print("[err] audio backends are mutually exclusive", file=sys.stderr)
        return 2
    if args.audio_capture and not (args.audio_hda or args.audio_ac97 or args.audio_usb):
        print("[err] --audio-capture requires an audio backend", file=sys.stderr)
        return 2
    fail_markers = tuple(DEFAULT_FAIL_MARKERS) + tuple(args.fail_marker)

    log_path = (REPO_ROOT / args.log).resolve()
    debugcon_log = (REPO_ROOT / args.debugcon_log).resolve()
    disk_path = (REPO_ROOT / args.disk).resolve()
    audio_capture = (REPO_ROOT / args.audio_capture).resolve() if args.audio_capture else None
    if audio_capture:
        audio_capture.parent.mkdir(parents=True, exist_ok=True)
    for target in (log_path, debugcon_log, disk_path):
        target.parent.mkdir(parents=True, exist_ok=True)
    debugcon_log.write_bytes(b"")

    try:
        qemu_bin = resolve_qemu_binary(args.qemu)
        ovmf_code, ovmf_vars_template = resolve_ovmf_or_raise(args.ovmf)
        bootx64, kernel, manifest = validate_installed_disk_artifacts(
            REPO_ROOT, artifact_dir=args.artifact_dir)
    except FileNotFoundError as exc:
        print(f"[err] {exc}", file=sys.stderr)
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

    print(f"[info] launching QEMU (networking={args.networking}); "
          f"serial+stdout -> {log_path}")
    session = boot_with_session(
        qemu_bin=qemu_bin,
        ovmf_code=ovmf_code,
        ovmf_vars_runtime=ovmf_vars_runtime,
        disk_path=disk_path,
        log_path=log_path,
        debugcon_log=debugcon_log,
        memory_mb=args.memory,
        storage_bus=args.storage_bus,
        verbose=args.verbose,
        networking=args.networking,
        audio_hda=args.audio_hda,
        audio_capture=audio_capture,
        audio_ac97=args.audio_ac97,
        audio_usb=args.audio_usb,
    )

    rc = 1
    try:
        print(f"[info] waiting for {args.marker!r} (<= {args.timeout:.0f}s)")
        wait_for_clean_marker(session, args.marker, fail_markers, args.timeout)
        print(f"[ok]   + {args.marker!r}")
        rc = 0
    except (TimeoutError, RuntimeError) as exc:
        print(f"[err] qemu-marker smoke failed: {exc}", file=sys.stderr)
        captured = session.text()
        for marker in fail_markers:
            if marker in captured:
                print(f"      failure marker present: {marker!r}",
                      file=sys.stderr)
        print_log_tail(log_path)
    finally:
        try:
            stop_and_check_session(session, fail_markers)
        except RuntimeError as exc:
            print(f"[err] final guest capture failed: {exc}", file=sys.stderr)
            rc = 1
        cleanup_file(ovmf_vars_runtime)
        if not args.keep_disk:
            cleanup_file(disk_path)

    if rc == 0 and audio_capture:
        try:
            if args.audio_builtin_music:
                from verify_builtin_music_capture import verify
                print(f"[ok] complete preset PCM: {verify(audio_capture, REPO_ROOT / 'build/generated/sounds')}")
            elif args.audio_multi_fixture:
                print(f"[ok] multi-app PCM phases: {verify_audio_multi_capture(audio_capture, ac97=args.audio_ac97, usb=args.audio_usb)} frames")
            elif args.audio_playlist_fixture:
                print(f"[ok] captured playlist sections: {verify_audio_playlist_capture(audio_capture)}")
            else:
                frames = verify_audio_capture(audio_capture)
                print(f"[ok] captured {frames} non-silent stereo frames without long gaps")
            if args.audio_stream_fixture:
                print(f"[ok] streamed audio section transitions: {verify_audio_stream_capture(audio_capture)}")
        except (OSError, EOFError, wave.Error, RuntimeError) as exc:
            print(f"[err] audio capture failed: {exc}", file=sys.stderr)
            rc = 1

    if rc == 0:
        print("[ok] qemu-marker smoke passed (final log and requested PCM checks)")
    return rc


if __name__ == "__main__":
    sys.exit(main())
