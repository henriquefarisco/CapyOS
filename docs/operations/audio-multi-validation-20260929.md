# Multi-source audio validation — 2026-09-29, unreleased

The runtime fixture uses two application IDs through the real service, backend
and DMA path. It verifies join without restarting the first source, application
gain, global gain, independent primary EOF, rejoin and independent stop. This
is a service-level multi-application test, not two GUI processes.

`make audio-multi-artifact` builds an isolated diagnostic ISO;
`make smoke-x64-qemu-audio-multi` additionally runs HDA with a PCM oracle.
`--audio-ac97 --audio-multi-fixture` covers AC97 with the same kernel.
Canonical installer validation rejects both the diagnostic macro and boot marker.

Results under `build/etapa10-completion/`:

- HDA QEMU: PASS, 324785 active stereo frames and all seven amplitude phases.
- AC97 QEMU: PASS on repeat, 326186 frames and all seven phases.
- VMware HDA: PASS, six ordered markers through independent EOF/stop and ready.
- Host audio suite, full `make test`, strict layout audit: PASS.
- Capture contracts: 13 tests, including incorrect mixing/gain/stop and stereo.
- Raw/VMDK comparison before VMware boot: identical. Same provisioned disk boot,
  not ISO installation; no physical-device or VMware PCM capture claim.

The first AC97 oracle run failed: the initial oracle assumed unity hardware
gain. QEMU 10.2.1 `get_volume()` instead maps PCM register 0x0808 to 190/255.
The captured 4000-level samples were 2980/-2981, matching that model. The driver
was not changed. The oracle now uses that fixed expected gain for AC97 only;
a negative test rejects a different gain. Source:
[QEMU AC97 implementation](https://github.com/qemu/qemu/blob/v10.2.1/hw/audio/ac97.c).
Failed evidence remains in `multi-ac97-result.log`; accepted repeat is
`multi-ac97-repeat-result.log`. No arbitrary normalization/tolerance increase.

Kernel SHA256: `f9724067dcd96c100c481a8788bc75013a0ac082700473811ba2087c4a79c3ac`.
Diagnostic ISO SHA256: `201abc35d45878873971e629c0ccd182412c865b372dc5eadf8be7a5c80fe2fd`.
Matching ELF: `multi.elf`; VMware config: `multi.vmx`, UEFI, SATA, HDA,
2 vCPU/1024 MiB, Secure Boot/network/USB off. Captures, logs and disks retained.

Etapa 10 is still open: bounded public Ogg/Vorbis decode and player integration,
USB Audio class, refreshed GUI playlist/acceptance gates. No public version,
ABI or release changed in this slice. The unrelated static-only `CapyOS/AGENTS.md`
was removed at the maintainer's request; a recovery copy is retained locally as
`build/etapa10-completion/removed-instructions.txt`. Root workspace rules remain.
