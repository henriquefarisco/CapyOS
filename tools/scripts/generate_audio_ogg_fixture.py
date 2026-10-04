"""Build-only reproducible three-section Ogg fixture, using independent oggenc."""
import pathlib
import struct
import subprocess
import sys
import tempfile
import wave

output = pathlib.Path(sys.argv[1])
with tempfile.TemporaryDirectory() as tmp:
    wav, ogg = pathlib.Path(tmp) / "fixture.wav", pathlib.Path(tmp) / "fixture.ogg"
    with wave.open(str(wav), "wb") as f:
        f.setnchannels(2); f.setsampwidth(2); f.setframerate(48000)
        pcm = bytearray()
        for i in range(144000):
            period = (200, 160, 120)[i // 48000]
            sample = 6000 if i % period < period // 2 else -6000
            pcm.extend(struct.pack("<hh", sample, sample))
        f.writeframes(pcm)
    subprocess.run(["oggenc", "-Q", "-s", "1", "-q", "4", "-o", str(ogg), str(wav)], check=True)
    data = ogg.read_bytes()
    if len(data) > 65536: raise RuntimeError("fixture exceeds build budget")
    lines = ["/* Generated lab fixture; never used by the normal kernel. */",
             "static const unsigned char audio_ogg_fixture[] = {"]
    for offset in range(0, len(data), 16):
        lines.append("  " + ",".join(str(v) for v in data[offset:offset+16]) + ",")
    lines.append("};\n")
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text("\n".join(lines), encoding="ascii")
    output.with_suffix(".ogg").write_bytes(data)
    print(f"[ogg-fixture] {len(data)} bytes -> {output}")
