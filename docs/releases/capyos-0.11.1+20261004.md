# CapyOS 0.11.1+20261004

Published `0.11.1+20261004`, stable Latest and immutable, with 12 verified
signed assets. Includes the audio, startup sound, preset music and FAT16 fixes
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
below 0.5 and peak error 1 PCM unit). Complete QEMU HDA playback passed with
zero PCM sample errors for all three tracks. WAV capture uses backend-driven
codec DMA (`use-timer=off`); ordinary HDA gates retain QEMU's default timer.
The traced default-timer run exposed four codec FIFO overruns and was retained
as a failed diagnostic, not counted as acceptance.

ABI, trust epoch, keys, CapyUI v2.27.0 and CapyCodecs v0.1.1 remain unchanged.
The signed immutable module snapshot `modules-capyos-base-v3-r2` was published
and its eight payloads verified from public URLs. The 0.11.0 local ISO also passed
Full installation, desktop, music files and reboot persistence in QEMU with the
new snapshot. The final CI ISO passed Full installation, desktop after login,
all preset music files and reboot persistence in VMware; the guard disk was
unchanged. Release Artifacts run `37182118304` and signed promotion run
`37225241587` passed, including all public downloads and the runtime Latest
route. Production VMware A/B acceptance FAILED on the released 0.10.0 client:
the signed manifest and full payload passed verification, but CapyFS could not
persist the payload cache. Its 12 direct blocks plus 1024 indirect blocks permit
only 4,243,456 bytes per file, below the 7,400,072-byte kernel. No update was
applied. Fresh ISO installation passed; upgrades from 0.10.0 are not accepted.
The immutable release is preserved, not retroactively patched. A compatible
migration/resource-packaging design is required before closing this gate.

Final ISO SHA-256:
`769d63d179bb5eaf71668963c23ef8a92d08931fa66c09cdbf0514eab35394a3`.
Final kernel: 7,400,072 bytes; SHA-256:
`d3786df855eb48cb8e5e35158d3cf602c658dbf042f22966850f096d92a0dc93`.

Evidence: `build/audio-budget-pull-clock.log`,
`build/ci/builtin-music-pull-clock/hda-overrun.log` (zero overruns),
`build/ci/release-0.11.1-vmware-installer.manifest` (run `9e7ac7f6ce08`).
Failed production evidence: `build/ci/smoke_x64_vmware_update_ab_6c062ca420e3.boot1.log`.
The earlier run `a05663b2018a` failed DNS resolution with 8.26.56.26; retrying
with the host's resolver 1.1.1.1 reached the verified-download/cache boundary.
