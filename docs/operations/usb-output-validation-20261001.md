# USB audio output: local runtime checkpoint, 2026-10-01

Branch: `feature/etapa-10-audio-multimedia`. This is development evidence,
not a published release or universal USB hardware certification.

## Implemented transport

`usb_audio_output` is the fallback after HDA/AC97. It configures a UAC1 alternate
and 48 kHz frequency through EP0, publishes the xHCI isochronous context and
feeds 192-byte stereo S16 packets (one millisecond each). The supported subset
is full-speed adaptive/synchronous OUT without feedback; UAC2, asynchronous
feedback, resampling and recovery after failed/disconnected hardware are not
implemented. Unsupported descriptors fail closed.

The queue keeps up to 32 packets ahead, each in its own persistent DMA payload.
It copies from the mixer ring rather than exposing mutable fragments directly
to the controller. Completion advances the position; bounded servicing refills
the queue. Stop stops replenishment, confirms Stop Endpoint and Set TR Dequeue,
and only then permits reuse. Failure quarantines published DMA allocations.

Command completion matching now checks the submitted TRB address, including
ring wrap. Late Address/Evaluate/Configure completions cannot authorize a
different command's teardown. Address and interrupt Configure input contexts
remain allocated until matching completion or successful Disable Slot.
The event gate protects command snapshots and isochronous event routing.
USB enumeration preserves unchanged devices and acknowledges port changes
after stale-slot handling. Existing USB keyboard input remains functional.

## Executed evidence

All logs are under `build/etapa10-completion/`.

- `usb-output-focused.log`, `usb-output-sanitize.log`: backend normal playback,
  four-plus mixer ring wraps, timeout, stale stop completion, disconnect and
  transfer errors; native and ASan/UBSan passed.
- `usb-output-command-focused.log`, `usb-output-lifetime-sanitize.log`: delayed
  command completions and Address/Configure/Disable allocation lifetime passed.
- `usb-output-final-tests.log`: full `make -j8 test TOOLCHAIN64=elf`, exit 0.
- `usb-output-audio-tests.log`: `make audio-selftest`, exit 0.
- `usb-output-cross.log`: exact freestanding xHCI/iso objects passed host replay.
- `usb-output-final-artifact.log`: full player kernel, ISO and manifest, exit 0.
- `usb-output-final-coexist-result.log`: real QEMU USB audio output, captured
  Ogg/WAV playlist and actual QMP-injected USB keyboard input passed. Frequency
  transitions were `[[239,299,399],[239,299,399]]`; duration/gap/channel checks
  passed. No HDA or AC97 device was used in this test.
- `usb-output-final-disconnect-result.log`: speaker removal during playback
  produced the expected player error; fresh shell-ready and subsequent real
  USB keyboard markers proved the OS survived. No panic or CPU fault accepted.
- `usb-output-multi-retry-result.log`: multi-application output and volume phase
  oracle passed across 326400 captured PCM frames.
- `usb-output-vmware-result.log`: HDA regression with ordered preemption-SIMD,
  guarded-DMA/compressed-source join, rendered-two-tracks and final ready passed.
  This is serial/runtime evidence, not VMware PCM capture or listening.
- `usb-output-vmware-compare.log`: new RAW/VMDK images identical before boot.

The first multi-source capture failed the unattenuated amplitude oracle. The
fixture amplitudes were 240/255 of input because QEMU initializes its UAC1
device gain to 240. The verifier now uses this **fixed model constant**, never a
gain fitted to captured data, and rejects a synthetic wrong-gain capture.
The failed capture/log are retained. Source:
[QEMU 10.2.1 USB audio model](https://github.com/qemu/qemu/blob/v10.2.1/hw/usb/dev-audio.c)
(`usb_audio_init`, `usb_audio_reinit`).

## Artifact and environments

Saved playlist kernel: `build/etapa10-completion/usb-output-final.elf`, SHA-256
`9b5c6c439150f570aebdeb873512b524a299c33862ebf8e39df3e617cc1e71fd`.
Playlist diagnostic ISO at this checkpoint: SHA-256
`91d55726a401e4c214827a63608d5fba39678198bfb6d9626864001fe47a92e2`.
The multi-source test uses its own separately rebuilt diagnostic variant.

QEMU 10.2.1, q35/TCG, OVMF 4M, SATA, qemu-xhci with usb-audio and USB keyboard.
VMware Workstation: virtual hardware 22, UEFI, Secure Boot off, two vCPUs,
1024 MiB, SATA, HDA, host-only E1000, USB off; disposable `usb-output.vmx`.
No physical disks were used. Both harnesses stop their VM after each run.

This supersedes the transport-pending portion of the preparation checkpoint.
The Etapa 10 final player visualization/integrated acceptance remains a
separate checkpoint; parser or transport success alone does not close it.
