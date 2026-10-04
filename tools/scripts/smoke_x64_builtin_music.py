#!/usr/bin/env python3
"""Run the complete preset playlist with native-local disk/PCM capture I/O."""
import argparse
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=ROOT / 'build/ci/builtin-music')
    parser.add_argument('--memory', type=int, choices=(512, 1024, 2048), default=1024)
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    # Keep real-time QEMU writes off a potentially slow shared WSL mount;
    # copy the exact captured bytes into durable evidence only after poweroff.
    with tempfile.TemporaryDirectory(prefix='capy-music-') as directory:
        work = Path(directory)
        capture = work / 'capture.wav'
        command = [sys.executable, str(ROOT / 'tools/scripts/smoke_x64_qemu_marker.py'),
            '--audio-hda', '--memory', str(args.memory), '--timeout', '900',
            '--marker', '[smoke] media-player-playlist ready',
            '--fail-marker', '[smoke] media-player-playlist FAIL',
            '--fail-marker', '[smoke] audio-playback-roundtrip FAIL',
            '--log', str(output / 'serial.log'), '--debugcon-log', str(output / 'debug.log'),
            '--disk', str(work / 'disk.img'), '--audio-capture', str(capture),
            '--audio-builtin-music']
        try:
            # The child owns the bounded VM wait and its shutdown in finally.
            # Do not kill that supervisor before it can stop the guest.
            return subprocess.run(command, cwd=ROOT, check=False).returncode
        finally:
            if capture.exists():
                shutil.copyfile(capture, output / 'capture.wav')


if __name__ == '__main__':
    raise SystemExit(main())
