#!/usr/bin/env python3
"""Windows speaker-loopback PCM gate around the maintained VMware playlist smoke.

Requires PyAudioWPatch in a local Python environment. Never records a microphone.
Other host playback can contaminate the capture and must not be active during
this lab test. Does not change host volume, mute, or default audio endpoint.
"""
import argparse
import array
from pathlib import Path
import subprocess
import sys
import wave
import re

from smoke_x64_qemu_marker import verify_audio_playlist_capture


def windows_mono_enabled():
    import winreg
    try:
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, r'Software\Microsoft\Multimedia\Audio') as key:
            value, _ = winreg.QueryValueEx(key, 'AccessibilityMonoMixState')
            return value == 1
    except FileNotFoundError:
        return False  # Absent override never relaxes the stereo oracle.


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--vmx', type=Path, required=True)
    parser.add_argument('--disk', type=Path, required=True)
    parser.add_argument('--iso', type=Path, required=True)
    parser.add_argument('--serial-log', type=Path, required=True)
    parser.add_argument('--summary-log', type=Path, required=True)
    parser.add_argument('--capture', type=Path, required=True)
    parser.add_argument('--loopback-name', help='Exact output-loopback name; defaults to Windows default output')
    parser.add_argument('--timeout', type=float, default=150)
    parser.add_argument('--boot-reference', type=Path,
                        help='Validate the real startup sound instead of the synthetic playlist')
    parser.add_argument('--reference-python', default=sys.executable,
                        help='Python with NumPy for the optional Windows startup waveform oracle')
    args = parser.parse_args()
    if sys.platform != 'win32':
        parser.error('WASAPI capture requires Windows')
    host_mono = windows_mono_enabled()
    print('[info] Windows accessibility mono:', host_mono, flush=True)
    if args.boot_reference:
        if not args.boot_reference.is_file():
            parser.error('startup reference WAV does not exist')
        subprocess.run([args.reference_python,
            str(Path(__file__).with_name('verify_boot_sound_host_capture.py')), '--help'],
            check=True, stdout=subprocess.DEVNULL)
    configured = re.search(r'^serial0\.fileName\s*=\s*"([^"]+)"', args.vmx.read_text(encoding='utf-8'), re.M)
    if not configured:
        parser.error('VMX must configure a serial0 file')
    serial = Path(configured.group(1))
    if not serial.is_absolute():
        serial = args.vmx.parent / serial
    if serial.resolve() != args.serial_log.resolve():
        parser.error('--serial-log must match serial0.fileName in the VMX')
    import pyaudiowpatch as pa

    cmd = [sys.executable, '-u', str(Path(__file__).with_name('smoke_x64_vmware.py'))]
    for name in ('vmx', 'disk', 'iso', 'serial_log', 'summary_log', 'timeout'):
        cmd += ['--' + name.replace('_', '-'), str(getattr(args, name))]
    if args.boot_reference:
        cmd += ['--marker', '[boot-audio] starting', '--marker', '[boot-audio] completed',
                '--fail-marker', '[boot-audio] unavailable', '--fail-marker', '[music] preset failed']
    else:
        for marker in ('preemption-SIMD ready', 'guarded-DMA ready', 'rendered-two-tracks', 'ready'):
            cmd += ['--marker', '[smoke] media-player-playlist ' + marker]
        cmd += ['--fail-marker', '[smoke] media-player-playlist FAIL']
    chunks = []
    capture_errors = []
    frames_captured = 0
    def collect(data, frames, timing, status):
        nonlocal frames_captured
        del timing
        frames_captured += frames
        if status:
            capture_errors.append(status)
        if frames_captured > (args.timeout + 120) * 48000:
            capture_errors.append('capture budget exceeded')
            return None, pa.paAbort
        chunks.append(data)
        return None, pa.paContinue
    rc = 1
    try:
        with pa.PyAudio() as audio:
            device = audio.get_default_wasapi_loopback()
            if args.loopback_name:
                matches = [d for d in audio.get_loopback_device_info_generator() if d['name'] == args.loopback_name]
                if len(matches) != 1:
                    raise RuntimeError('requested output-loopback name is missing or ambiguous')
                device = matches[0]
            if not device['isLoopbackDevice'] or device['maxInputChannels'] != 2:
                raise RuntimeError('requires a stereo output loopback, never a microphone')
            print('[info] loopback:', device['name'], flush=True)
            # Shared-mode Windows mixes in float. Request it directly to avoid
            # the capture library's per-channel integer conversion/dither.
            with audio.open(format=pa.paFloat32, channels=2, rate=48000, input=True,
                            input_device_index=device['index'], frames_per_buffer=480,
                            stream_callback=collect):
                # The child owns bounded VM startup, marker timeout and poweroff.
                with subprocess.Popen(cmd) as proc:
                    # A blocking capture read can wait forever when an idle
                    # endpoint stops producing packets after VM poweroff.
                    rc = proc.wait(timeout=args.timeout + 120)
    finally:
        # Keep even a failed/partial capture for diagnosis, without normalization.
        floats = array.array('f', b''.join(chunks))
        pcm = array.array('h', (max(-32768, min(32767, round(value * 32768))) for value in floats))
        active = [i // 2 for i, value in enumerate(pcm) if abs(value) > 100]
        if active:
            pcm = pcm[max(0, active[0] - 4800) * 2:(active[-1] + 4801) * 2]
        args.capture.parent.mkdir(parents=True, exist_ok=True)
        with wave.open(str(args.capture), 'wb') as wav:
            wav.setnchannels(2)
            wav.setsampwidth(2)
            wav.setframerate(48000)
            wav.writeframes(pcm.tobytes())
    if rc:
        return rc
    if capture_errors:
        raise RuntimeError(f'WASAPI capture errors: {capture_errors}')
    if args.boot_reference:
        if windows_mono_enabled() != host_mono:
            raise RuntimeError('host mono configuration changed during capture')
        verifier = [args.reference_python,
            str(Path(__file__).with_name('verify_boot_sound_host_capture.py')),
            str(args.capture), str(args.boot_reference)]
        if host_mono:
            verifier.append('--host-mono')
        subprocess.run(verifier, check=True)
        print('[ok] VMware captured real startup waveform')
    else:
        print('[ok] VMware captured playlist:', verify_audio_playlist_capture(args.capture))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
