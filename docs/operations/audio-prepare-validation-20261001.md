# Etapa 10: preparation without starving active audio

Local checkpoint on `feature/etapa-10-audio-multimedia`. No release or stage
completion is claimed. This slice changes CapyOS; existing sibling changes
are preserved.

## Implementation

Previously the runtime held the mixer gate throughout synchronous Vorbis decode
and file reads. Timer service skipped that gate, so adding a compressed source
could leave the active DMA ring unserviced.

Preparation now owns request-local PCM outside the engine gate. A short commit
revalidates the ring, transfers ownership and joins the source. Invalid
replacement data leaves the previous source intact. Successful file playback
remains exclusive, but only at commit. Peak memory can include the old source
and its prepared replacement; allocation failure rejects the new request.

Only one preparation is admitted. Timer/worker IRQ-phase progress continues;
worker memory reclamation is deferred during preparation because the legacy
heap/VFS is not reentrant. Request allocation/free guards BSP task dispatch
without disabling interrupts. Control commands retain the documented single
foreground owner; this does not claim general SMP command concurrency.

The VM guarded-playback smoke decodes and joins a second real Vorbis source
through the public runtime, checks its 144000 frames, and stops only that source.
The diagnostic source is muted to preserve an independent PCM oracle for the
original track. Its playback ID and progress must survive, with task dispatch
guarded for at least 500 ms and IRQ service enabled.

## Evidence

Logs in `build/etapa10-completion/`:

- `audio-prepare-focused.log`: `make audio-selftest`, exit 0. Real Vorbis
  preparation interrupted for 24 DMA fragments (> one ring), isolated rejection,
  reentrant-play rejection, EOF during preparation, slow reads and no leaked PCM.
- `audio-prepare-sanitize-final.log`: same gate with ASan/UBSan, exit 0.
  Retained earlier failures exposed outer BUILD overrides inherited by two
  Makefile contract tests. Their subprocess environments now exclude inherited
  make flags; missing-source rejection and ISO isolation remain asserted.
- `audio-prepare-full-tests.log`: `make -j8 test TOOLCHAIN64=elf`, exit 0.
- `audio-prepare-layout.log`: layout audit, exit 0, no warnings.
- `audio-join-artifact.log`: kernel, diagnostic ISO and manifest, exit 0.
- `audio-join-cross.log`: exact cross-compiled codec objects, bounded decode,
  allocation failures and 256 residue cascade masks passed; reference peak error 1.
- `audio-join-qemu-result.log`: final artifact, captured QEMU/HDA PCM passed.
  Section transitions `[[239,299,399],[239,300,400]]`; duration, channel and gap
  assertions passed. This includes the compressed-source join.
- `audio-join-vmware-result.log`: final artifact, ordered preemption-SIMD,
  guarded-DMA (compressed join), rendered-two-tracks and ready passed.
  VMware evidence is serial/runtime, not PCM capture or listening.
- `audio-join-vmware-compare.log`: fresh RAW/VMDK identical before boot.

QEMU 10.2.1, q35/TCG, OVMF 4M, SATA/HDA. VMware virtual hardware 22, UEFI,
Secure Boot off, 2 vCPU, 1024 MiB, SATA/HDA, network/USB off.
VMX: `build/etapa10-completion/audio-prepare.vmx`, using the new
`audio-join-vmware.vmdk`. No physical disk was used.

Kernel SHA-256:
`6f16d1c334f7824e19ff2089c888ebee9f1514e8302d366a3e703834f139ce96`.
Saved ELF: `build/etapa10-completion/audio-join-final.elf`.
Diagnostic ISO SHA-256:
`1f47261c805e126ceffcee899d01b1bf59e627a1d9d7bd24f6f49fd7c240e4db`.
ISO: `build/ci/CapyOS-Smoke-Media-Player-UEFI.iso`.

## USB progress and remaining stage gates

UAC1 selection is integrated into enumeration. EP0 fetches the first 8 device
descriptor bytes and updates its maximum packet size through Evaluate Context.
The persistent context is quarantined on timeout. Tests cover 32/64-byte contexts,
speed-specific packet sizes, malformed audio and composite HID preservation.

`usb-uac-enumeration-focused.log`, `usb-uac-enumeration-sanitize.log` and QEMU
`usb-uac-enumeration-result.log` passed; `audio-prepare-uac-hid-result.log` also
passed speaker discovery and actual injected USB keyboard input together.
These prove discovery, NOT USB PCM playback.

Still required: UAC1 streaming-interface/isochronous DMA transport, backend
connection, captured USB PCM, driver-failure runtime cases and final integrated
stage acceptance. Versions/public audio structure ABI unchanged.
Nested `AGENTS.md` removal remains in effect; backup:
`build/etapa10-completion/removed-instructions.txt`.
