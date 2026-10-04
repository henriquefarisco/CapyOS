#!/usr/bin/env python3
"""Negative controls for the real startup PCM capture oracle."""
import array
from pathlib import Path
import random
import sys
import tempfile
import unittest
import wave

from verify_boot_sound_capture import verify


class BootCaptureTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        rng = random.Random(472)
        values = array.array('h', (rng.randint(-12000, 12000) for _ in range(120000)))
        if sys.byteorder != 'little':
            values.byteswap()
        self.pcm = values.tobytes()
        self.reference = self.write('reference.wav', self.pcm)

    def write(self, name, pcm):
        path = self.root / name
        with wave.open(str(path), 'wb') as output:
            output.setparams((2, 2, 48000, 0, 'NONE', 'not compressed'))
            output.writeframes(pcm)
        return path

    def test_exact_with_device_padding(self):
        capture = self.write('capture.wav', bytes(400) + self.pcm + bytes(800))
        self.assertEqual(verify(capture, self.reference)['sample_errors'], 0)

    def test_rejects_damage(self):
        candidates = {
            'empty': b'',
            'silence': bytes(len(self.pcm)),
            'truncated': self.pcm[:-4000],
            'missing_start': self.pcm[4000:],
            'gap': self.pcm[:4000] + bytes(400) + self.pcm[4000:],
            'repeat': self.pcm[:8000] + self.pcm[4000:8000] + self.pcm[8000:],
            'corruption': self.pcm[:4000] + bytes(400) + self.pcm[4400:],
        }
        for name, pcm in candidates.items():
            with self.subTest(name=name), self.assertRaises(ValueError):
                verify(self.write(name + '.wav', pcm), self.reference)


if __name__ == '__main__':
    unittest.main()
