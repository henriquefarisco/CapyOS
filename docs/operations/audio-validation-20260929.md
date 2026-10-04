# Audio runtime validation — 2026-09-29 (unreleased)

Owner: CapyOS, branch `feature/etapa-10-audio-multimedia`, dirty development
tree based on `5e35f1fe128e00ebb5d1095fe6caa3b987693ae6`.
Existing backend/mixing and FP changes were preserved. CapyUI/CapyCodecs were
not edited. No public version, ABI, dependency pin or release changed.

## Changes and regression results

- Stream capture now rejects unequal stereo channels, not just incorrect
  left-channel frequencies. Negative tests cover silent, inverted and replayed
  right-channel audio.
- QEMU success is decided after stopping the session and checking its final
  capture for failure markers, then validating requested PCM. A negative test
  injects a panic during session stop.
- Independent read-only review confirmed both fixes.
- `test_audio_smoke_contract.py`: 12 tests passed.
- `make audio-selftest TOOLCHAIN64=elf`: passed, including HDA/AC97 cores,
  backend selection, multi-source service and Media Player host contracts.
- `make test TOOLCHAIN64=elf`: passed through the final browser pipeline target.
- `make layout-audit`: passed without warnings; `git diff --check`: passed.

## Exact artifact and runtime results

Built once with cross-ELF toolchain, FULL profile and
`EXTRA_CFLAGS64=-DCAPYOS_AUDIO_PLAYBACK_SMOKE`; diagnostic ISO uses explicit
variant reuse. No global clean or canonical ISO rebuild was performed.

Kernel SHA-256:
`9709501b53ab00800d9044361e69c46920dec2e64d077f7c56bbd999b6e24534`.
Diagnostic ISO `build/ci/CapyOS-Smoke-Audio-20260929.iso` SHA-256:
`0a1ed6e246ac45826976b08c9ea59d1da631d0be41a43b37517ca762e15ec291`.

| Gate | Result | Evidence |
| --- | --- | --- |
| QEMU HDA, captured PCM | PASS | 142460 active stereo frames; transitions 239/299/399 |
| QEMU AC97 only, captured PCM | PASS | 143687 active stereo frames; transitions 239/299/399 |
| VMware HDA | PASS | ordered file-loaded, EOF 144000 frames, ready markers |

QEMU captures are stereo S16/48 kHz, channels identical, without silent gaps
over 10 ms within playback. Counts are fixture/liveness evidence, not a
performance benchmark. QEMU used the maintained OVMF harness, 512 MiB and SATA.
VMware used UEFI, hardware version 22, 2 vCPU, 1024 MiB, SATA and `hdaudio`;
Secure Boot/network/USB disabled, serial log enabled. It booted the provisioned
VMDK, not the diagnostic ISO. Raw and VMDK compared identical before VMware boot.
VMware host logs show the Wave playback stream; no VMware PCM capture or human
listening test was performed. Both hypervisors were stopped after the tests.

## Reproduction evidence and remaining scope

`build/audio-validation-20260929/` retains `build.log`, `contracts.log`,
`audio-selftest.log`, `test.log`, `layout.log`, `hda-result2.log`,
`ac97-result.log`, both WAV captures, serial/debugcon logs, `kernel.elf`,
`vmware-result.log`, `vmware-summary.log`, `hda.vmx` and `disk-compare.log`.
The first `hda-result.log` is a shell-argument quoting failure before VM launch,
not a runtime pass; the corrected invocation is `hda-result2.log`.

These tests close current single-source HDA/AC97 playback regressions. They do
not establish simultaneous multi-application playback in a VM, SMP correctness,
physical-device support, playlist GUI acceptance or public release readiness.
Next slice: a bounded two-source runtime fixture with PCM verification of
mixing, per-app gain and independent EOF/stop behavior. Etapa 10 remains open.
