#!/usr/bin/env python3
"""Fail-closed asset preparation, without modifying the supplied recordings."""
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import wave

from prepare_builtin_sounds import prepare


class PrepareSoundsTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.source = Path(self.directory.name) / 'source'
        self.output = Path(self.directory.name) / 'generated'
        self.source.mkdir()

    def source_wav(self, channels=2, rate=48000, frames=48):
        path = self.source / 'Capy Boot.wav'
        with wave.open(str(path), 'wb') as wav:
            wav.setparams((channels, 2, rate, 0, 'NONE', 'not compressed'))
            wav.writeframes(bytes(frames * channels * 2))
        return path

    def test_same_directory_rejected(self):
        path = self.source_wav()
        original = path.read_bytes()
        with self.assertRaises(ValueError):
            prepare(self.source, self.source)
        self.assertEqual(path.read_bytes(), original)

    def test_invalid_sources_never_start_encoder(self):
        for params in ({'channels': 1}, {'rate': 4000}, {'frames': 0},
                       {'frames': 48000 * 13}):
            with self.subTest(params=params):
                path = self.source_wav(**params)
                original = path.read_bytes()
                with patch('prepare_builtin_sounds.subprocess.run') as run:
                    with self.assertRaises(ValueError):
                        prepare(self.source, self.output)
                    run.assert_not_called()
                self.assertEqual(path.read_bytes(), original)
                self.assertFalse((self.output / 'manifest.json').exists())


if __name__ == '__main__':
    unittest.main()
