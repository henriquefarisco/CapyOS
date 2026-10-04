#!/usr/bin/env python3
from pathlib import Path
import tempfile
import unittest
import wave
import numpy as np

from verify_boot_sound_host_capture import verify


class HostCaptureTests(unittest.TestCase):
    def test_fixed_gain_and_negative_controls(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            pcm = np.random.default_rng(7).integers(-10000, 10000, 96000, dtype=np.int16)
            def write(name, values):
                path = root / name
                with wave.open(str(path), 'wb') as out:
                    out.setparams((2, 2, 48000, 0, 'NONE', 'not compressed'))
                    out.writeframes(values.astype('<i2').tobytes())
                return path
            reference = write('reference.wav', pcm)
            amplified = np.rint(pcm.astype(np.float64) * 1.3)
            actual = write('valid.wav', np.concatenate((np.zeros(400), amplified, np.zeros(400))))
            self.assertAlmostEqual(verify(actual, reference)['host_gain'], 1.3, places=5)
            mono = np.repeat(pcm.reshape(-1, 2).astype(np.float64).mean(axis=1), 2)
            mono_capture = write('mono.wav', np.concatenate((np.zeros(400), np.rint(mono), np.zeros(400))))
            self.assertEqual(verify(mono_capture, reference, host_mono=True)['host_mode'], 'windows-mono')
            with self.assertRaises(ValueError):
                verify(mono_capture, reference)
            damaged = amplified.copy()
            damaged[40000:41000] = 0
            changed_gain = amplified.copy()
            changed_gain[48000:] *= 0.5
            swapped = amplified.reshape(-1, 2)[:, ::-1].reshape(-1)
            for name, values in (
                ('silence', np.zeros(96000)), ('cut', amplified[:-480]),
                ('gap', np.concatenate((amplified[:40000], np.zeros(960), amplified[40000:]))),
                ('damage', damaged), ('changed-gain', changed_gain), ('swapped', swapped)):
                with self.subTest(name=name), self.assertRaises(ValueError):
                    verify(write(name + '.wav', values), reference)


if __name__ == '__main__':
    unittest.main()
