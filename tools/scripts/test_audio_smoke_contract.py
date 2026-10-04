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
from smoke_x64_qemu_marker import stop_and_check_session
from smoke_x64_qemu_marker import verify_audio_multi_capture
from smoke_x64_session import make_qemu_cmd


class AudioSmokeContract(unittest.TestCase):
    def test_artifact_phases_preserve_kernel_variant(self):
        makefile = (Path(__file__).resolve().parents[2] / "Makefile").read_text(encoding="utf-8")
        for name, flag in (("ogg", "OGG"), ("multi", "MULTI")):
            body = makefile.split(f"audio-{name}-artifact:", 1)[1].split("\n\n", 1)[0]
            phases = [line for line in body.splitlines() if "$(MAKE)" in line]
            self.assertEqual(len(phases), 3)
            for phase in phases:
                self.assertIn("PROFILE=full", phase)
                self.assertIn(f"EXTRA_CFLAGS64='-DCAPYOS_AUDIO_PLAYBACK_SMOKE -DCAPYOS_AUDIO_{flag}_SMOKE'", phase)
        body = makefile.split("media-player-artifact:", 1)[1].split("\n\n", 1)[0]
        phases = [line for line in body.splitlines() if "$(MAKE)" in line]
        self.assertEqual(len(phases), 3)
        for phase in phases:
            self.assertIn("PROFILE=full", phase)
            self.assertIn("EXTRA_CFLAGS64='$(MEDIA_PLAYER_SMOKE_FLAGS)'", phase)
        self.assertNotIn("$(MAKE) clean", body)

    def test_multi_capture_requires_mix_gains_and_independent_stop(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "multi.wav"
            for variant in ("valid", "ac97-valid", "ac97-wrong-gain", "usb-valid", "usb-wrong-gain", "single", "no-app-gain", "no-global-gain",
                            "no-stop", "stereo", "short"):
                pcm = array.array("h")
                count = 100000 if variant == "short" else 326400
                for i in range(count):
                    t = i / 48000
                    level = (4000 if t < 1.34 else 6000 if t < 2.34 else
                             5000 if t < 3.34 else 2500 if t < 4.34 else
                             2000 if t < 5.24 else 2500 if t < 6.14 else 2000)
                    if variant == "single": level = 4000
                    if variant == "no-app-gain" and 2.34 <= t < 3.34: level = 6000
                    if variant == "no-global-gain" and 3.34 <= t < 4.34: level = 5000
                    if variant == "no-stop" and t >= 6.14: level = 2500
                    value = level if i % 200 < 100 else -level
                    if variant.startswith("ac97-"):
                        value = int(value * (190 / 255 if variant == "ac97-valid" else 0.5))
                    if variant.startswith("usb-"):
                        value = int(value * (240 / 255 if variant == "usb-valid" else 0.5))
                    pcm.extend((value, -value if variant == "stereo" else value))
                if sys.byteorder != "little": pcm.byteswap()
                with wave.open(str(path), "wb") as out:
                    out.setnchannels(2); out.setsampwidth(2); out.setframerate(48000)
                    out.writeframes(pcm.tobytes())
                with self.subTest(variant=variant):
                    if variant in ("valid", "ac97-valid", "usb-valid"):
                        self.assertEqual(verify_audio_multi_capture(path, ac97=variant.startswith("ac97-"), usb=variant.startswith("usb-")), count)
                    else:
                        with self.assertRaises(RuntimeError):
                            verify_audio_multi_capture(path, ac97=variant.startswith("ac97-"), usb=variant.startswith("usb-"))

    def test_audio_smokes_use_noncanonical_iso_names(self):
        makefile = (Path(__file__).resolve().parents[2] / "Makefile").read_text(
            encoding="utf-8", errors="strict"
        )
        self.assertIn("CapyOS-Smoke-Audio-UEFI.iso", makefile)
        self.assertIn("CapyOS-Smoke-Audio-AC97-UEFI.iso", makefile)
        self.assertIn("CapyOS-Smoke-Media-Player-UEFI.iso", makefile)
        self.assertIn("verify_installer_variant.py", makefile)

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

    def test_stream_capture_rejects_corrupt_right_channel(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "stereo.wav"
            for variant in ("inverted", "silent", "replayed"):
                pcm = array.array("h")
                for period in (200, 160, 120):
                    for i in range(48000):
                        left = 6000 if i % period < period // 2 else -6000
                        right = -left if variant == "inverted" else (
                            0 if variant == "silent" else
                            (6000 if i % 200 < 100 else -6000))
                        pcm.extend((left, right))
                if sys.byteorder != "little": pcm.byteswap()
                with wave.open(str(path), "wb") as out:
                    out.setnchannels(2); out.setsampwidth(2); out.setframerate(48000)
                    out.writeframes(pcm.tobytes())
                with self.subTest(variant=variant):
                    with self.assertRaises(RuntimeError):
                        verify_audio_stream_capture(path)

    def test_final_capture_rejects_failure_arriving_during_stop(self):
        session = Mock()
        session.text.return_value = "ready\n"
        def drain():
            session.text.return_value = "ready\npanic late\n"
        session.stop.side_effect = drain
        with self.assertRaises(RuntimeError):
            stop_and_check_session(session, ("panic",))
        session.stop.assert_called_once()
        session.stop.side_effect = None
        session.text.return_value = "ready\n"
        stop_and_check_session(session, ("panic",))

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
        captured = make_qemu_cmd(**args, audio_hda=True,
                                 audio_capture=Path("out.wav"))
        self.assertIn("hda-duplex,audiodev=audio0,use-timer=off", captured)
        self.assertNotIn("hda-duplex,audiodev=audio0", captured)
        self.assertTrue(any(a.startswith("driver=wav,id=audio0,path=")
                            for a in captured))

    def test_ac97_is_explicit_hermetic_and_exclusive(self):
        args = dict(qemu_bin="qemu", ovmf_code="code.fd",
                    ovmf_vars_runtime=Path("vars.fd"), disk_path=Path("disk.img"),
                    serial_port=1234, memory_mb=512)
        normal = make_qemu_cmd(**args)
        audio = make_qemu_cmd(**args, audio_ac97=True)
        self.assertNotIn("AC97,audiodev=audio0", normal)
        self.assertIn("AC97,audiodev=audio0", audio)
        self.assertNotIn("intel-hda", audio)
        self.assertIn("driver=none,id=audio0", audio)
        captured = make_qemu_cmd(**args, audio_ac97=True,
                                 audio_capture=Path("out.wav"))
        self.assertTrue(any(a.startswith("driver=wav,id=audio0,path=") for a in captured))
        with self.assertRaises(ValueError):
            make_qemu_cmd(**args, audio_hda=True, audio_ac97=True)
        with self.assertRaises(ValueError):
            make_qemu_cmd(**args, audio_capture=Path("out.wav"))

    def test_usb_audio_cannot_silently_test_another_backend(self):
        args = dict(qemu_bin="qemu", ovmf_code="code.fd",
                    ovmf_vars_runtime=Path("vars.fd"), disk_path=Path("disk.img"),
                    serial_port=1234, memory_mb=512)
        command = make_qemu_cmd(**args, audio_usb=True, audio_capture=Path("out.wav"))
        self.assertIn("qemu-xhci,id=audio-usb", command)
        self.assertIn("usb-audio,bus=audio-usb.0,audiodev=audio0", command)
        self.assertNotIn("intel-hda", command)
        self.assertNotIn("AC97,audiodev=audio0", command)
        for other in ("audio_hda", "audio_ac97"):
            with self.assertRaises(ValueError):
                make_qemu_cmd(**args, audio_usb=True, **{other: True})

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
