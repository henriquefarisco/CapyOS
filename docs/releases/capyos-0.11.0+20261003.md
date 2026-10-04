# CapyOS 0.11.0+20261003

Superseded candidate: workflow 37167270217 rejected the 9,734,792-byte kernel
against the updater's 8 MiB limit before draft creation. The tag is retained;
no OS release was published. Continue with [0.11.1](capyos-0.11.1+20261004.md).

Candidate `0.11.0+20261003`; signed publication and final-artifact acceptance
remain pending. The currently published stable release is not changed by this
document.

## Changes

- HDA, AC97 and USB audio output with bounded WAV and Ogg/Vorbis decoding,
  per-application playback and PCM level meters in the Media Player.
- Startup plays the supplied Capy Boot recording while the capybara splash stays
  visible; startup continues on unavailable audio or a bounded playback failure.
  Progress rendering updates only changed pixels to avoid audio starvation.
- Three supplied recordings are installed under `/Music` as OGG/Vorbis. Original
  WAV assets are preserved. Opening an empty player discovers the presets without
  autoplay; existing queues and user files are preserved.
- Decoded PCM budget is 40 MiB, kernel heap 96 MiB, UEFI reservation 128 MiB.
  Audio/package ABIs remain unchanged; CapyUI 2.27.0 and CapyCodecs 0.1.1 are the
  coordinated build-time producer versions.
- EFI ISO boot image grows from 8 to 16 MiB. The FAT16 writer rejects payloads
  exceeding its actual cluster capacity before opening the destination, rejects
  invalid geometry/overflow, checks write failures and emits contiguous optional
  directory entries. Oversized kernels can no longer silently extend a corrupt
  image beyond its declared volume.

## Evidence and remaining gates

The development artifact passed complete host tests, producer validation and
packaging, real-song decode/reference comparison, captured QEMU playback and
VMware startup audio. Fresh ISO installation with the Full profile, remote
module bootstrap, desktop, preset files and reboot persistence passed on QEMU
and VMware; both tests preserved the non-target disk. These results precede the
release version bump and do not replace acceptance of the final signed bytes.

The Basic installation profile intentionally provides a shell without a desktop.
Desktop acceptance requires Full or Custom with the desktop module installed.
The smoke harness now rejects contradictory Basic/desktop requirements before
creating disks.

CapyUI v2.27.0 and CapyCodecs v0.1.1 are published. The candidate module snapshot
`modules-capyos-base-v3-r2` resolves eight pinned packages, adding the audio codec;
publication revision 2 does not change ABI 3 or signature epoch 1. Release staging
verifies exact published payload bytes instead of re-archiving source checkouts.

Pending: signed module snapshot publication, refreshed release-check and final ISO runtime
gates, reviewed main integration, draft generation, offline production signing,
verified immutable Latest promotion and public update/deployment validation.
