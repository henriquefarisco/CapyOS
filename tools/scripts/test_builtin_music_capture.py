#!/usr/bin/env python3
import array
from pathlib import Path
import random
import sys
import tempfile
import unittest
import wave

from verify_builtin_music_capture import TRACKS, verify


class MusicCaptureTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.tracks = []
        for i, name in enumerate(TRACKS):
            rng = random.Random(i)
            samples = array.array('h', (rng.randint(-8000, 8000) for _ in range(100000)))
            if sys.byteorder != 'little':
                samples.byteswap()
            pcm = samples.tobytes()
            (self.root / (name + '.raw')).write_bytes(pcm)
            self.tracks.append(pcm)

    def capture(self, tracks):
        path = self.root / 'capture.wav'
        with wave.open(str(path), 'wb') as stream:
            stream.setparams((2, 2, 48000, 0, 'NONE', 'not compressed'))
            stream.writeframes(bytes(400) + bytes(800).join(tracks) + bytes(400))
        return path

    def test_ordered_complete_tracks_with_loading_gaps(self):
        results = verify(self.capture(self.tracks), self.root)
        self.assertEqual([r['track'] for r in results], list(TRACKS))
        self.assertTrue(all(r['sample_errors'] == 0 for r in results))

    def test_rejects_incomplete_repeated_reordered_and_damaged_tracks(self):
        a, b, c = self.tracks
        for tracks in ((a, b), (a, a, c), (b, a, c), (a, b[:-400], c),
                       (a, b[:4000] + bytes(400) + b[4400:], c)):
            with self.subTest(lengths=list(map(len, tracks))), self.assertRaises(RuntimeError):
                verify(self.capture(tracks), self.root)


if __name__ == '__main__':
    unittest.main()
