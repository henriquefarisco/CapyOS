#!/usr/bin/env python3
"""Prepare deterministic 48 kHz stereo PCM copies; never modify supplied WAVs."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import wave

SOUNDS = {
    "Capy Boot.wav": "boot.wav",
    "Capy Acoustic.wav": "Capy Acoustic.ogg",
    "Capy Opera.wav": "Capy Opera.ogg",
    "Capy Sound.wav": "Capy Sound.ogg",
}


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def prepare(source, output, ffmpeg="ffmpeg"):
    if source.resolve() == output.resolve():
        raise ValueError("output must not overwrite the supplied source directory")
    output.mkdir(parents=True, exist_ok=True)
    records = []
    for original, name in SOUNDS.items():
        path = source / original
        with wave.open(str(path), "rb") as wav:
            if (wav.getnchannels(), wav.getsampwidth(), wav.getcomptype()) != (2, 2, "NONE"):
                raise ValueError(f"unsupported source format: {original}")
            frames, rate = wav.getnframes(), wav.getframerate()
            if not 8000 <= rate <= 192000 or not 0 < frames <= rate * 300:
                raise ValueError(f"source duration/rate exceeds bounds: {original}")
            if original == "Capy Boot.wav" and frames > rate * 12:
                raise ValueError("boot sound exceeds 12 seconds")
        target = output / name
        pcm_target = target if target.suffix == ".wav" else target.with_suffix(".wav")
        subprocess.run([ffmpeg, "-nostdin", "-v", "error", "-y", "-i", str(path),
                        "-map_metadata", "-1", "-fflags", "+bitexact", "-flags:a", "+bitexact",
                        "-ac", "2", "-ar", "48000", "-c:a", "pcm_s16le", str(pcm_target)],
                       check=True, timeout=60)
        with wave.open(str(pcm_target), "rb") as wav:
            if (wav.getnchannels(), wav.getsampwidth(), wav.getframerate()) != (2, 2, 48000):
                raise ValueError("invalid generated PCM format")
            if abs(wav.getnframes() - frames * 48000 / rate) > 1:
                raise ValueError("resampling changed source duration")
            output_frames = wav.getnframes()
        if target.suffix == ".ogg":
            subprocess.run([ffmpeg, "-nostdin", "-v", "error", "-y", "-i", str(pcm_target),
                            "-map_metadata", "-1", "-fflags", "+bitexact", "-flags:a", "+bitexact",
                            "-c:a", "libvorbis", "-q:a", "3", str(target)],
                           check=True, timeout=120)
            if target.stat().st_size > 4 * 1024 * 1024:
                raise ValueError(f"music exceeds CapyFS seed limit: {name}")
        records.append({"source": original, "file": name, "frames": output_frames,
                        "source_sha256": digest(path), "sha256": digest(target)})
    (output / "manifest.json").write_text(json.dumps(records, indent=2) + "\n", encoding="utf-8")
    return records


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=Path("assets/sounds"))
    parser.add_argument("--output", type=Path, default=Path("build/generated/sounds"))
    args = parser.parse_args()
    for record in prepare(args.source, args.output):
        print(f"[sounds] {record['file']}: {record['frames']} frames, {record['sha256']}")
