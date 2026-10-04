# Stereo player meters and final acceptance investigation

Local development checkpoint, not a release. CapyOS and CapyUI are affected;
CapyUI is now 2.26.0, CapyCodecs remains 0.1.1. Published package pins unchanged.

## Implemented

`audio_service_get_app_levels` is an additive, serialized, no-allocation query
over at most 256 decoded frames at the played cursor. It reports stereo peak
magnitudes 0..32768 with current global/app gains, duplicates mono, and zeros
idle/completed/not-yet-queued sources. It is a source meter, not hardware RMS;
queued gain changes can reach hardware later. Existing status layout unchanged.

Media Player draws two themed bars, invalidates on level changes, and clears
them when stopped or playback ownership changes. The real desktop smoke now
requires nonzero stereo meters on both Ogg/WAV tracks and zero at completion.

## Passed gates

Logs under `build/etapa10-completion/`:

- `audio-levels-tests.log`: audio-selftest including bounded windows, signed
  minimum, unequal stereo, mono, gains, stop and actual player pixel assertions.
- `audio-levels-sanitize.log`: same tests with ASan/UBSan.
- `audio-levels-ui.log`: CapyUI validate, including 348 widget contracts.
- `audio-levels-full-tests.log`: full `make -j8 test TOOLCHAIN64=elf`, exit 0.
- `audio-levels-layout.log`: layout audit, no warnings.
- `audio-levels-artifact.log`: real kernel, diagnostic ISO and manifest.
- `audio-levels-qemu-result.log`: captured HDA playlist passed, sections
  `[[239,299,399],[240,300,400]]`, with the new meter assertions.
- `audio-levels-usb-result.log`: USB captured playlist + actual HID input passed,
  sections `[[239,299,399],[239,299,399]]`.
- `audio-levels-usb-disconnect-result.log`: speaker unplug contained; subsequent
  shell and actual keyboard input passed.
- `audio-levels-vmware-summary.log`: ordered SIMD, guarded DMA, rendered tracks
  (including meters) and ready passed on HDA/UEFI/SATA/host-only E1000.

Saved ELF `audio-levels-final.elf`, SHA-256
`8e19b5b8af8b19c0a9d4159600d550fb953cf21ba7a1c2465a702e6f05abccf3`.
Diagnostic playlist ISO at that checkpoint, SHA-256
`99f8351a624a93c6887dab9e0ed570fe94ae67987eb3b6a201ac100ee1df4710`.

## Newly exposed blocker: VMware host output is silent

Serial progress is NOT proof of audible playback. Windows speaker-loopback
capture was added locally to check the official VM's actual output. The first
SoundCard attempt lost the NVIDIA endpoint; a retry on Realtek returned silence.
An independent PyAudioWPatch WASAPI capture also returned silence while the VM
passed all serial markers. Endpoint volume was nonzero and VMware's session
volume was 1.0, unmuted. The same loopback passed a 440 Hz host selfcheck with
peak 4001 (`capture-selfcheck.log`). Thus no audible/no-stutter acceptance is
claimed. The HDA codec route/configuration is being diagnosed before stage close.

Failed evidence: `audio-levels-vmware-result.log`,
`audio-levels-vmware-capture-retry.log`, `audio-levels-vmware-portaudio.log`,
`audio-levels-vmware-silent.wav`, `audio-levels-vmware.wav`.
Capture dependencies are isolated in a build-local venv; no microphone is used.
No public release, push, tag or stable promotion was performed.
