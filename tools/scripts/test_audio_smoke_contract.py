"""Host regression tests for HDA device setup and fail-closed smoke results."""
import unittest
import array
import sys
import tempfile
import wave
from pathlib import Path
from unittest.mock import Mock, patch
from smoke_x64_vmware import wait_for_markers, wait_for_govc_markers

from smoke_x64_qemu_marker import wait_for_clean_marker, verify_audio_capture, verify_audio_stream_capture
from smoke_x64_qemu_marker import verify_audio_playlist_capture
from smoke_x64_session import make_qemu_cmd


class AudioSmokeContract(unittest.TestCase):
    def test_audio_smokes_use_noncanonical_iso_names(self):
        makefile = (Path(__file__).resolve().parents[2] / "Makefile").read_text(
            encoding="utf-8", errors="strict"
        )
        self.assertIn("CapyOS-Smoke-Audio-UEFI.iso", makefile)
        self.assertIn("CapyOS-Smoke-Media-Player-UEFI.iso", makefile)
        self.assertIn(
            "canonical installer ISO cannot contain Etapa 10 smoke boot hooks",
            makefile,
        )

    def test_playlist_capture_requires_two_complete_tracks(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "playlist.wav"
            for variant in ("valid", "contiguous", "replay", "gap", "missing", "stereo"):
                pcm = array.array("h")
                for track in range(1 if variant == "missing" else 2):
                    if track and variant != "contiguous": pcm.extend([0] * 48000)
                    for second, period in enumerate((200, 160, 120)):
                        if variant == "replay": period = 200
                        for i in range(48000):
                            value = 6000 if i % period < period // 2 else -6000
                            if variant == "gap" and second == 1 and 12000 < i < 14000: value = 0
                            pcm.extend((value, -value if variant == "stereo" else value))
                if sys.byteorder != "little": pcm.byteswap()
                with wave.open(str(path), "wb") as out:
                    out.setnchannels(2); out.setsampwidth(2); out.setframerate(48000)
                    out.writeframes(pcm.tobytes())
                if variant in ("valid", "contiguous"):
                    self.assertEqual(verify_audio_playlist_capture(path), [[239, 299, 399]] * 2)
                else:
                    with self.assertRaises(RuntimeError): verify_audio_playlist_capture(path)

    def test_stream_capture_rejects_replayed_ring(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "stream.wav"
            for periods, valid in (((200, 160, 120), True), ((200, 200, 200), False)):
                pcm = array.array("h")
                for period in periods:
                    for i in range(48000):
                        value = 6000 if i % period < period // 2 else -6000
                        pcm.extend((value, value))
                if sys.byteorder != "little": pcm.byteswap()
                with wave.open(str(path), "wb") as out:
                    out.setnchannels(2); out.setsampwidth(2); out.setframerate(48000)
                    out.writeframes(pcm.tobytes())
                if valid:
                    self.assertEqual(verify_audio_stream_capture(path), [239, 299, 399])
                else:
                    with self.assertRaises(RuntimeError): verify_audio_stream_capture(path)

    def test_vmware_failure_wins_for_both_providers(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "serial.log"
            path.write_text("audio FAIL\nready\n")
            self.assertEqual(wait_for_markers(path, ("ready",), 1, 0,
                                             ("audio FAIL",)),
                             (False, "audio FAIL\nready\n", "audio FAIL"))
            with patch("smoke_x64_vmware.run_command", return_value=Mock(returncode=0)):
                self.assertFalse(wait_for_govc_markers(
                    "govc", "remote", path, ("ready",), 1, 0, ("audio FAIL",))[0])
            path.write_text("ready\n")
            self.assertTrue(wait_for_markers(path, ("ready",), 1, 0, ("audio FAIL",))[0])

    def test_capture_requires_audio_and_rejects_gaps(self):
        def write_capture(path, samples):
            pcm = array.array("h", samples)
            if sys.byteorder != "little":
                pcm.byteswap()
            with wave.open(str(path), "wb") as output:
                output.setnchannels(2)
                output.setsampwidth(2)
                output.setframerate(48000)
                output.writeframes(pcm.tobytes())
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "tone.wav"
            write_capture(path, [6000, -6000] * 48000)
            self.assertEqual(verify_audio_capture(path), 48000)
            write_capture(path, [0, 0] * 48000)
            with self.assertRaises(RuntimeError):
                verify_audio_capture(path)
            write_capture(path, [6000] * 48000 + [0] * 1920 + [6000] * 48000)
            with self.assertRaises(RuntimeError):
                verify_audio_capture(path)

    def test_hda_is_explicit_and_hermetic(self):
        args = dict(qemu_bin="qemu", ovmf_code="code.fd",
                    ovmf_vars_runtime=Path("vars.fd"), disk_path=Path("disk.img"),
                    serial_port=1234, memory_mb=512)
        normal = make_qemu_cmd(**args)
        audio = make_qemu_cmd(**args, audio_hda=True)
        self.assertNotIn("intel-hda", normal)
        self.assertIn("intel-hda", audio)
        self.assertIn("hda-duplex,audiodev=audio0", audio)
        self.assertIn("driver=none,id=audio0", audio)

    def test_clean_success(self):
        session = Mock()
        session.wait_for_any.return_value = "ready"
        session.text.return_value = "boot\nready\n"
        wait_for_clean_marker(session, "ready", ("FAIL",), 2)
        session.wait_for_any.assert_called_once_with(["FAIL", "ready"], timeout=2)

    def test_failure_wins_over_success_in_same_capture(self):
        session = Mock()
        session.wait_for_any.return_value = "ready"
        session.text.return_value = "FAIL\nready\n"
        with self.assertRaises(RuntimeError):
            wait_for_clean_marker(session, "ready", ("FAIL",), 2)

    def test_early_failure_and_timeout_propagate(self):
        session = Mock()
        session.wait_for_any.return_value = "FAIL"
        session.text.return_value = "FAIL"
        with self.assertRaises(RuntimeError):
            wait_for_clean_marker(session, "ready", ("FAIL",), 2)
        session.wait_for_any.side_effect = TimeoutError("no DMA progress")
        with self.assertRaises(TimeoutError):
            wait_for_clean_marker(session, "ready", ("FAIL",), 2)


if __name__ == "__main__":
    unittest.main()
