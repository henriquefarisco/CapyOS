#!/usr/bin/env python3
"""Fail-closed asset preparation, without modifying the supplied recordings."""
from pathlib import Path
import shutil
import tempfile
import unittest
from unittest.mock import patch
import wave

from prepare_builtin_sounds import MUSIC_VORBIS_QUALITY, SOUNDS, prepare


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

    def test_music_quality_budget_preserves_originals_and_boot_pcm(self):
        boot = self.source_wav()
        original = boot.read_bytes()
        for name in SOUNDS:
            if name != boot.name:
                shutil.copyfile(boot, self.source / name)

        def encoder(command, **kwargs):
            source = Path(command[command.index('-i') + 1])
            target = Path(command[-1])
            if '-ac' in command:
                self.assertEqual(command[command.index('-ac') + 1], '2')
            if target.suffix == '.ogg':
                self.assertEqual(command[command.index('-q:a') + 1], '0')
                target.write_bytes(b'Ogg fixture')
            else:
                shutil.copyfile(source, target)

        with patch('prepare_builtin_sounds.subprocess.run', side_effect=encoder) as run:
            records = prepare(self.source, self.output)
        self.assertEqual(MUSIC_VORBIS_QUALITY, '0')
        self.assertEqual(run.call_count, 7)
        self.assertEqual(len(records), 4)
        self.assertTrue(all(record['frames'] == 48 for record in records))
        for name in SOUNDS:
            self.assertEqual((self.source / name).read_bytes(), original)
        self.assertEqual((self.output / 'boot.wav').read_bytes(), original)


if __name__ == '__main__':
    unittest.main()
