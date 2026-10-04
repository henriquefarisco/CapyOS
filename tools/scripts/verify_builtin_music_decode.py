#!/usr/bin/env python3
"""Compare the real bounded CapyCodecs output with independent FFmpeg PCM."""
import argparse
import array
from pathlib import Path
import subprocess
import sys

from verify_builtin_music_capture import TRACKS


def samples(data):
    result = array.array('h', data)
    if sys.byteorder != 'little':
        result.byteswap()
    return result


def verify(root):
    for track in TRACKS:
        ours = samples((root / (track + '.raw')).read_bytes())
        completed = subprocess.run(['ffmpeg', '-nostdin', '-v', 'error', '-i',
            str(root / (track + '.ogg')), '-f', 's16le', '-'],
            check=True, stdout=subprocess.PIPE, timeout=60)
        reference = samples(completed.stdout)
        count = min(len(ours), len(reference))
        # Same independent-reference limits as the owning CapyCodecs
        # tests/audio/test_vorbis_decode_reference.py, including end trimming.
        if not count or abs(len(ours) - len(reference)) > 256 * 2:
            raise RuntimeError('decoded duration mismatch: ' + track)
        total = peak = 0
        for a, b in zip(ours, reference):
            error = abs(a - b)
            total += error
            peak = max(peak, error)
        mean = total / count
        if mean >= 9 or peak >= 100:
            raise RuntimeError(f'{track}: decoder mismatch mean={mean} peak={peak}')
        print(f'[ok] {track}: frames={len(ours)//2}, independent_samples={count}, '
              f'mean_error={mean:.6f}, peak_error={peak}', flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--references', type=Path, default=Path('build/generated/sounds'))
    verify(parser.parse_args().references)
