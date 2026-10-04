#!/usr/bin/env python3
"""Require all three real tracks, in order, with unchanged audible PCM."""
import argparse
import array
from pathlib import Path
import sys
import wave

TRACKS = ('Capy Acoustic', 'Capy Opera', 'Capy Sound')


def verify(capture, references):
    with wave.open(str(capture), 'rb') as stream:
        if (stream.getnchannels(), stream.getsampwidth(), stream.getframerate()) != (2, 2, 48000):
            raise RuntimeError('expected stereo 48 kHz S16 capture')
        if stream.getnframes() > 48000 * 900:
            raise RuntimeError('capture exceeds the bounded playlist run')
        actual = stream.readframes(stream.getnframes())
    cursor, results = 0, []
    for track in TRACKS:
        path = references / (track + '.raw')
        if not 4 <= path.stat().st_size <= 40 * 1024 * 1024:
            raise RuntimeError('invalid bounded PCM reference')
        expected = path.read_bytes()
        samples = array.array('h', expected)
        if sys.byteorder != 'little':
            samples.byteswap()
        first = next((i // 2 for i, value in enumerate(samples) if abs(value) > 100), None)
        if first is None:
            raise RuntimeError('silent reference: ' + track)
        last = (len(samples) - 1 - next(i for i, value in enumerate(reversed(samples)) if abs(value) > 100)) // 2 + 1
        anchor = min(first + 48000, last - 32)
        at = actual.find(expected[anchor * 4:(anchor + 32) * 4], cursor)
        if at < 0 or at % 4:
            raise RuntimeError('track missing or changed PCM: ' + track)
        origin = at - anchor * 4
        begin, end = origin + first * 4, origin + last * 4
        if begin < cursor or end > len(actual):
            raise RuntimeError('track truncated or out of order: ' + track)
        if actual[begin:end] != expected[first * 4:last * 4]:
            raise RuntimeError('track PCM gap, repeat or corruption: ' + track)
        cursor = end
        results.append({'track': track, 'audible_frames': last - first, 'sample_errors': 0})
    return results


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('capture', type=Path)
    parser.add_argument('--references', type=Path, default=Path('build/generated/sounds'))
    args = parser.parse_args()
    print(verify(args.capture, args.references))
