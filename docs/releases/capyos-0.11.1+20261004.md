# CapyOS 0.11.1+20261004

Candidate `0.11.1+20261004`; signed publication and final-artifact acceptance
remain pending. Includes the audio, startup sound, preset music and FAT16 fixes
described in [0.11.0](capyos-0.11.0+20261003.md).

The 0.11.0 tag is retained: its release workflow correctly rejected the
9,734,792-byte kernel before creating a draft because legacy update clients
accept at most 8 MiB. No OS release was published and Latest did not change.

The three music copies now use Vorbis quality 0 at the same 48 kHz stereo rate
and full duration; original WAVs and the lossless boot sound remain unchanged.
Local generated OGG sizes total 3,023,397 bytes (previously 5,359,235).
The kernel budget is checked immediately after compilation in PR CI, release CI
and local release-check, using the same bound as the update manifest signer.
Tests cover empty, one-byte, exact-limit and one-byte-over-limit payloads.

Local cross build: 7,412,336 bytes out of 8,388,608 allowed. All three complete
tracks passed CapyCodecs decoding and independent FFmpeg comparison (mean error
below 0.5 and peak error 1 PCM unit). Captured playback and final CI artifact
acceptance remain separate gates.

ABI, trust epoch, keys, CapyUI v2.27.0 and CapyCodecs v0.1.1 remain unchanged.
The signed immutable module snapshot `modules-capyos-base-v3-r2` was published
and its eight payloads verified from public URLs. The 0.11.0 local ISO also passed
Full installation, desktop, music files and reboot persistence in QEMU with the
new snapshot; final 0.11.1 runtime and signed-publication gates are still required.
