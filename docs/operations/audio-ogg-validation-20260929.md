# Ogg/Vorbis: integration checkpoint, 2026-09-29

Development branch: `feature/etapa-10-audio-multimedia`. This is local evidence,
not a published release and not completion of Etapa 10.

## Delivered surface

CapyCodecs 0.1.1 adds bounded Ogg/Vorbis decoding through audio ABI v1's generic
memory query/decode API and additive feature bit. The kernel links the producer's
source inventory and consumes decoded 48 kHz mono/stereo S16 PCM. CapyUI 2.25.1
routes `.ogg` alongside `.wav`; the refreshed GUI playlist gate passed below.
The codec has no filesystem/network dependency. Input, output, scratch memory,
packets and entropy work are bounded; chained/multiplexed streams fail closed.

## Root cause found by runtime validation

The native GCC 15.2 tests passed, but the x86_64-elf GCC 13.2 optimized objects
rejected valid residue configurations. Replaying those exact objects in a hosted
test reproduced `CAPY_AUDIO_ERR_INVALID_ARGUMENT` without a VM. For cascade 4,
pass 2, book 28, the original combined signed-range/boolean expression compiled
to a bit-test rejection without the corresponding book-sign comparison.

The check now branches explicitly on the cascade bit: an enabled pass requires
an in-range nonnegative book; a disabled pass requires exactly -1. Validation is
not weakened. Regression tests cover all 256 cascade masks and missing, negative
and out-of-range book references. Temporary tracing was removed from production
sources. `make test-audio-cross-objects` replays the actual kernel codec objects,
including full decode/reference and residue tests, instead of rebuilding the
codec with the host compiler.

An independent build issue was repaired: all phases of `audio-ogg-artifact` and
`audio-multi-artifact` retain identical profile/flags, including ISO and manifest
generation. The smoke contract suite verifies this invariant.

## Evidence

All evidence below is in `build/etapa10-completion/`:

- `codec-fixed-validate.log`: CapyCodecs validate and independent reference decode
  passed; FFmpeg differences mean 0.497/0.501, peak 1 S16 unit.
- `ogg-fixed-sanitize.log`: ASan/UBSan reference decode passed, including each of
  21 allocation failures, quotas, truncation and CRC corruption.
- `ogg-cross-gate.log`: actual cross-compiled objects passed both reference
  fixtures and 256-mask residue validation.
- `audio-contracts-variant.log`: 14 harness tests passed.
- `ogg-kernel-full-tests.log`: `make -j8 test audio-selftest TOOLCHAIN64=elf`
  completed successfully after the fix (exit 0).
- `ogg-hda-fixed-result.log`: QEMU 10.2.1/OVMF, HDA, 512 MiB; real Ogg file decode
  reached EOF at 144000 frames and READY. Captured 142362 active stereo frames,
  no silent gap over 10 ms, equal channels and section transitions 239/299/399.
- `ogg-vmware-result2.log`: VMware Workstation, UEFI, HDA, 2 vCPUs, 1024 MiB;
  ordered file-loaded, EOF 144000 and READY markers passed. This verifies the
  driver lifecycle, not host PCM capture or subjective listening.
- `ogg-vmware-compare.log`: fresh provisioned raw disk and VMDK were identical
  before boot. The test powered off its VM; vmrun subsequently reported zero VMs.

Kernel SHA-256: `261953e762315bd45a79cb9438542bc4cbfc3eeb22c7d6639d18238806d5aca3`.
Diagnostic ISO SHA-256: `4fc6560e65650f65c06088c52049ce31ce36e28300973797528c523ed8c5d063`.
This diagnostic ISO is not an installer release.

## Remaining stage acceptance

USB Audio class is still missing. Compressed-source concurrency,
driver-failure runtime checks and final integrated
stage gates remain necessary. Global/per-app mixing has separate WAV evidence in
`audio-multi-validation-20260929.md`; it is not evidence for concurrent Ogg decode.

## Mixed-format desktop playlist follow-up

`media-player-artifact` now builds a diagnostic desktop with an Ogg first track
and a WAV second track. All artifact phases use the same flags, and the target
does not clear existing evidence with `make clean`.

- `player-ui-validate.log`: CapyUI `make validate` passed.
- `player-contracts.log`: 14 harness tests passed, including artifact consistency.
- `player-ogg-result.log`: QEMU HDA passed with captured playlist transitions
  `[[239, 299, 399], [240, 300, 400]]`; two full tracks and no extra internal gaps.
- `player-vmware-result.log`: VMware HDA passed ordered preemption-SIMD,
  guarded-DMA, rendered-two-tracks and ready markers. The guarded test suspends
  task dispatch for 500 ms with IRQs enabled and checks continuing DMA progress.
  Both tracks must show intermediate UI progress before completion.
- `player-vmware-compare.log`: fresh raw/VMDK images identical before boot.
- Final cleanup check: zero VMware VMs and no QEMU process remained.

Player kernel SHA-256: `0b58737f27a3231d779d5c7771a4cf6f7e0ca0b3992104d82a73e5e29ca983a1`.
Player diagnostic ISO SHA-256: `ce56a599b97617842de22e3d486baa55ca2223373a39980d7aff4f5c39010002`.
The saved kernel is `build/etapa10-completion/player-ogg.elf`. This gate covers
the rendered player loop, not a complete installer/first-login workflow or
subjective visual/audio review.
