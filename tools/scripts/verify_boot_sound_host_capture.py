#!/usr/bin/env python3
"""Match Windows loopback to the startup recording, allowing fixed host gain.

Unlike the QEMU bit-exact oracle, this checks waveform identity after one
constant output gain. It never normalizes individual windows or skips gaps.
Requires NumPy only on the validation host, never in the guest.
"""
import argparse
from pathlib import Path
import wave
import numpy as np


def read(path):
    with wave.open(str(path), 'rb') as stream:
        if (stream.getnchannels(), stream.getsampwidth(), stream.getframerate()) != (2, 2, 48000):
            raise ValueError('expected 48 kHz stereo S16')
        if stream.getnframes() > 48000 * 30:
            raise ValueError('capture exceeds boot budget')
        return np.frombuffer(stream.readframes(stream.getnframes()), dtype='<i2').astype(np.float64)


def verify(capture, reference, *, host_mono=False):
    actual, expected = read(capture), read(reference)
    if host_mono:
        # Only explicit, independently observed host accessibility policy may
        # select this transform. Never infer downmixing from a failed capture.
        expected = np.repeat(expected.reshape(-1, 2).mean(axis=1), 2)
    active = np.flatnonzero(np.abs(expected) > 100)
    if not active.size:
        raise ValueError('silent reference')
    first, end = int(active[0]) // 2 * 2, (int(active[-1]) // 2 + 1) * 2
    expected = expected[first:end]
    if actual.size < expected.size:
        raise ValueError('truncated startup capture')
    size = 1 << (actual.size + expected.size - 2).bit_length()
    correlation = np.fft.irfft(np.fft.rfft(actual, size) *
                              np.fft.rfft(expected[::-1], size), size)
    valid = correlation[expected.size - 1:actual.size:2]
    offset = int(np.argmax(valid)) * 2
    observed = actual[offset:offset + expected.size]
    energy = float(np.dot(expected, expected))
    gain = float(np.dot(observed, expected) / energy)
    if not 0.05 <= gain <= 4:
        raise ValueError('missing, inverted or unusable output gain')
    residual = observed - expected * gain
    relative_rms = float(np.sqrt(np.dot(residual, residual) / (energy * gain * gain)))
    if relative_rms > 0.02 or np.count_nonzero(np.abs(observed) >= 32767) > observed.size * 0.0001:
        raise ValueError(f'waveform mismatch or clipping: relative_rms={relative_rms:.6f}')
    # One global gain must explain every 100 ms, not just the loudest section.
    for start in range(0, expected.size, 9600):
        target = expected[start:start + 9600] * gain
        error = residual[start:start + 9600]
        scale = max(100.0, float(np.sqrt(np.mean(target * target))))
        if float(np.sqrt(np.mean(error * error))) > max(4.0, 0.02 * scale):
            raise ValueError(f'local waveform damage at {start // 2} frames')
    return {'audible_frames': expected.size // 2, 'host_gain': gain,
            'relative_rms_error': relative_rms, 'capture_offset_frames': offset // 2,
            'host_mode': 'windows-mono' if host_mono else 'stereo'}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('capture', type=Path)
    parser.add_argument('reference', type=Path)
    parser.add_argument('--host-mono', action='store_true',
                        help='Require the fixed L/R average selected by Windows accessibility mono')
    args = parser.parse_args()
    print(verify(args.capture, args.reference, host_mono=args.host_mono))
