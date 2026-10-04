# Etapa 10 — integrated acceptance, 2026-10-02

Development branch: `feature/etapa-10-audio-multimedia`.
Acceptance status: local Etapa 10 engineering complete. On resuming this task,
the `build/etapa10-completion/` evidence directory was absent, and the previous
process handle no longer existed. The results and hashes below are historical
checkpoint records, not independently reverified current evidence. The latest
selector input-amplifier fix passed the refreshed gates listed immediately below;
stage closure relies on those new logs and artifacts, not the missing originals.

This record concerns the local engineering stage, not a public release. No push,
tag, package publication or stable/Latest promotion was performed. Existing
unrelated changes were preserved. The nested, untracked `CapyOS/AGENTS.md` was
removed as requested and its absence was reverified. The previously recorded
recovery copy, `build/etapa10-completion/removed-instructions.txt`, is also absent
on this machine now; recovery from that path cannot be promised. Workspace
instructions remain.

## Fresh verification after evidence-directory loss

The current build includes the selector fix: selectors use input amplifier
index zero; only mixers address one amplifier per input connection. The added
fixture selects connection one while asserting amplifier index zero.

Successfully rerun (exit 0, logs under `build/etapa10-completion/`):

- `hda-route-selector-tests.log`: focused route test with ASan/UBSan.
- `etapa10-selector-artifact.log`: full kernel, diagnostic ISO and manifest.
- `codecs-refreshed.log`, `ui-refreshed.log`: both owners' `make validate`.
- `refreshed-cross.log`: codec and USB freestanding-object replay.
- `refreshed-sanitize.log`: complete audio selftest with ASan/UBSan.
- `refreshed-layout.log`: layout audit, exit 0.
- `refreshed-qemu-result.log`: HDA playlist PCM passed.
- `refreshed-ac97-result.log`: AC97 playlist PCM passed.
- `refreshed-usb-result.log`: USB playlist PCM and injected keyboard passed.
- `refreshed-unplug-result.log`: active speaker removal contained; shell and
  injected keyboard remained responsive.

Fresh artifact: `etapa10-selector.elf`, SHA-256
`1e8d0ccdce81973530acbe73c70f9f202b06902083c205a55a48ad898eab4a2e`.
Diagnostic ISO SHA-256:
`e472ac74f351dc5b424916fbd7dcbc9ecdb6c24962a39808b3ff4eaeb0805e7e`.
The newly provisioned raw disk and VMDK were compared byte-for-byte successfully.
`etapa10-refreshed-tests.log`: full `make -j8 test TOOLCHAIN64=elf`, exit 0.
`etapa10-selector-vmware.log` and `etapa10-selector-repeat-vmware.log`: two
separate VMware boots passed all ordered markers and the unchanged strict PCM
oracle. Transitions were `[[239,299,399],[239,299,399]]` and
`[[239,299,399],[240,299,400]]`. Both validate actual stereo output, track duration,
frequency and no internal silence exceeding 10 ms, not marker progress alone.
The final output files are `etapa10-selector.wav` and
`etapa10-selector-repeat.wav`. Both runs used the fresh `etapa10-selector.vmdk`
and `audio-levels.vmx`, with no concurrent builds or other VMs.

The refreshed source-level mixer tests exercise actual ring samples after both
global/per-app gain changes, source replacement, stop/EOF and concurrent Vorbis
preparation. Player tests assert meter pixels and ownership. Codec owner tests
cover resource limits and allocation failures; cross-object replay also checks
independent reference PCM. These, plus HDA/AC97/USB captured playback and live
USB failure containment, prove the four master-plan acceptance criteria without
relying on the unavailable older multi-source capture files. Optional video,
public release and physical-device certification are not part of this closure.

## Historical requirement-by-requirement evidence

| Master-plan requirement | Implementation and evidence |
|---|---|
| Intel HDA, AC97 and USB Audio; at least one in VMware | HDA codec-route/DMA backend, AC97 fallback and UAC1 isochronous backend. HDA passed actual Windows-loopback PCM capture twice in VMware; HDA/USB captured playlist passed on the final QEMU artifact. AC97 single/multi runtime and gain evidence: [audio](audio-validation-20260929.md), [mixing](audio-multi-validation-20260929.md). USB bounds and failure evidence: [USB output](usb-output-validation-20261001.md). |
| System mixer and per-app volume | Up to four concurrent sources, independent gain/stop/EOF, global gain and isolated rejected requests. Native real-service tests and captured multi-source phase oracles passed on HDA/AC97/USB. USB final multi capture: `usb-output-multi-retry-result.log`, 326400 frames. |
| WAV and Ogg/Vorbis, pure bounded codec | CapyCodecs 0.1.1 audio ABI v1; real independently generated Vorbis fixtures, reference PCM, allocation failures, malformed input and work limits. Final owner validation and exact cross-compiled object replay passed. [Codec evidence](audio-ogg-validation-20260929.md). |
| Playlist and basic visualization | CapyUI 2.26.0: WAV/OGG routing, playlist, auto-advance, progress, stereo source peak meters. Actual player callbacks/pixels tested; VM gate requires meters on both tracks and zero at completion. |
| Playback without perceptible stutter on official VM | Actual 48 kHz stereo PCM, not just serial progress. Both official VMware runs passed the maintained duration/channel/frequency and maximum-10-ms-internal-silence oracle. Measurements below quantify the result rather than claiming perfect sample timing on all hardware. |
| Audio driver failure does not take down OS | Final QEMU speaker removal produced the expected player error, followed by fresh shell readiness and actual USB keyboard input. Host timeout, stale completion and DMA lifetime tests also passed. |
| Pure codec has no FS/network and bounded memory/time | Owner contract caps input/packet/scratch/output and decoded-vector work; kernel applies its tighter 8 MiB/48 kHz policy. Reference, resource rejection, 21 allocation failures and cross-object residue tests passed. VFS/decode preparation runs outside the mixer gate; [concurrent preparation evidence](audio-prepare-validation-20261001.md). |
| Optional software video | Not implemented; explicitly optional in the master plan. |

## HDA defects exposed by real output capture

The old VMware serial gates advanced DMA and completed the playlist, but the
host output was almost silent (peak 6). The codec exposes the route
pin 0x14 -> mixer 0x0c -> DAC 0x02. The DAC has no output amp; the intervening
mixer's output gain was left at zero instead of its advertised unity offset 64.

The driver now walks a bounded analog connection route, sets selectors and
capability-derived unity gains, and mutes unrelated mixer inputs. The walk is
limited to eight widgets, sixteen short connections per widget and 256 discovery
commands. Long/range lists, unsupported routes, cycles and command failures
fail closed. No allocation, vendor codec node hardcoding or blanket microphone
unmute was introduced. Tests cover the VMware-shaped mixer route, a direct
QEMU-shaped route, malformed lists, cycles, invalid gain and command failure.

After restoring gain, capture exposed ~19 ms startup starvation in VMware's
WAVE backend and premature loss of its queued tail. The VMware PCI model
15ad:1977 now requests three silent startup fragments (64 ms) and nine drain
fragments (192 ms), matching the observed nine-buffer host queue. The service
does not count this silence as source frames or advance the playlist before
draining. The additional lifecycle latency is 256 ms, without spinning or
extending failure timeouts. Other backends retain zero padding. Unit tests check
the silence, source cursor, unchanged frame count, delayed EOF and reclamation.

## Historical gates (original local files unavailable)

All log paths below are relative to `build/etapa10-completion/`.

- `hda-padding-tests.log`: `make audio-selftest`, exit 0.
- `hda-padding-sanitize.log`: same with ASan/UBSan, exit 0.
- `hda-route-tests.log`: route parser/setup ASan/UBSan, exit 0.
- `etapa10-final-tests.log`: `make -j8 test TOOLCHAIN64=elf`, exit 0.
- `etapa10-final-layout.log`: layout audit, no warnings.
- `etapa10-final-cross.log`: real freestanding codec/USB objects passed replay.
- `etapa10-codecs-validate.log`: CapyCodecs `make validate`, exit 0.
- `audio-levels-ui.log`: CapyUI `make validate`, exit 0 (348 widget contracts).
- `hda-padding-artifact.log`: full diagnostic player kernel/ISO/manifest, exit 0.
- `etapa10-final-qemu-result.log`: HDA captured playlist passed,
  transitions `[[239,300,400],[240,300,400]]`.
- `etapa10-final-usb-result.log`: USB captured playlist and real HID input passed,
  transitions `[[239,299,399],[239,299,399]]`.
- `etapa10-final-unplug-result.log`: live speaker removal contained, shell/HID pass.
- `hda-padding-vmware.log`, `hda-padding-repeat.log`: ordered runtime markers and
  actual captured PCM passed on two separate boots, without concurrent builds/VMs.

VMware PCM measurements (`etapa10-pcm-metrics.log`):

| Run | Active frames | Channel difference | Track spans | Maximum internal silence |
|---|---:|---:|---|---|
| First | 288000 | 0 | 3006 / 3000 ms | 6 / 0 ms |
| Repeat | 287616 | 0 | 2996.67 / 3000 ms | 4.67 / 0 ms |

Peak was 8575 in both runs. The oracle retains its existing 10 ms gap limit;
it was not relaxed to accept the failing capture. Playlist loading silence
between tracks is allowed; gapless transitions are not claimed.

## Historical artifact identity and configuration

Previously saved diagnostic ELF (superseded by the fresh artifact above):
`build/etapa10-completion/etapa10-final.elf`.
SHA-256: `68560f91d4219bdef75380621d466ba9f9a31916b9e6c388a59fd725b0392add`.
ISO: `build/ci/CapyOS-Smoke-Media-Player-UEFI.iso`.
SHA-256: `0fd99ee565aea6870dd6b3de210687bd7a2fc166a32343cca19bf06499f5be3a`.

QEMU 10.2.1, q35/TCG, OVMF 4M, SATA; independent HDA or xHCI/UAC1 configurations.
VMware virtual hardware 22, UEFI, Secure Boot off, 2 vCPU, 1024 MiB, SATA, HDA,
host-only E1000; `audio-levels.vmx` with freshly provisioned `hda-padding.vmdk`.
No physical disk used. VM output was pinned to the existing Realtek speaker;
the user's Windows default device/volume/mute were not changed.

The maintained Windows helper `tools/scripts/smoke_x64_vmware_audio_capture.py`
wraps the normal VMware smoke, records only an output loopback, captures native
float before deterministic S16 conversion, validates serial-path matching and
applies the same strict PCM oracle. PyAudioWPatch is installed only in the local
`audio-capture-venv`. Callback capture avoids blocking forever on an idle output.
The original integer captures, endpoint-switch failures and failed waveform
checks are retained; no failed run is counted as a pass.

## Compatibility and remaining scope

CapyUI 2.26.0 requires the coordinated kernel's additive level getter;
CapyCodecs 0.1.1 provides public bounded Vorbis decode. Desktop/widget and codec
package ABI layouts are unchanged. The output latency fields are kernel-internal,
not a syscall or persisted package ABI. Published known-good package pins and
the release version in `VERSION.yaml` remain unchanged until a release workflow.

USB support is UAC1 full-speed adaptive/synchronous stereo S16 at 48 kHz, without
feedback. UAC2, resampling, physical-device certification, recovery after failed
hardware and general SMP command concurrency are not claimed. No installer,
Secure Boot, public deployment or universal-VM certification is inferred from
this diagnostic ISO. Those are separate release/platform gates, not omitted
mandatory requirements of Etapa 10. Next sequential stage: WiFi/power management.
