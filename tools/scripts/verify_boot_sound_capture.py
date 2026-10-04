#!/usr/bin/env python3
"""Compare QEMU HDA startup output to the real supplied PCM, without gain fitting."""
import argparse
import array
from pathlib import Path
import sys
import wave


def read(path):
    with wave.open(str(path), "rb") as stream:
        if (stream.getnchannels(), stream.getsampwidth(), stream.getframerate()) != (2, 2, 48000):
            raise ValueError("expected stereo 48 kHz S16 PCM")
        if stream.getnframes() > 48000 * 30:
            raise ValueError("boot capture exceeds 30-second budget")
        return stream.readframes(stream.getnframes())


def verify(capture, reference):
    actual, expected = read(capture), read(reference)
    samples = array.array("h", expected)
    if sys.byteorder != "little": samples.byteswap()
    active = [i // 2 for i, v in enumerate(samples) if abs(v) > 100]
    if not active: raise ValueError("reference is silent")
    first, last = active[0], active[-1] + 1
    anchor = min(first + 48000, last - 32)
    at = actual.find(expected[anchor * 4:(anchor + 32) * 4])
    if at < 0 or at % 4:
        raise ValueError("source waveform absent or wrong output gain")
    start = at - anchor * 4
    begin, end = start + first * 4, start + last * 4
    if begin < 0 or end > len(actual):
        raise ValueError("audible source was truncated")
    if actual[begin:end] != expected[first * 4:last * 4]:
        raise ValueError("source PCM differs: gap, repeat, gain or corruption")
    return {"audible_span_frames": last - first, "sample_errors": 0,
            "source_frames": len(expected) // 4}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("capture", type=Path)
    parser.add_argument("reference", type=Path)
    args = parser.parse_args()
    print(verify(args.capture, args.reference))
