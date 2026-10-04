# USB DMA/MMIO validation — 2026-09-30

Branch: `feature/etapa-10-audio-multimedia`. Development checkpoint, not USB Audio
playback acceptance or an installer release.

## Changes and reproduced failure

The isochronous queue is now linked into the kernel. A nonblocking event gate
serializes consumption; each completion reaches its queue rather than replacing
a single pending latch. Tests cover duplicate/error completions, wrap and bounds.

EP0 DMA now uses a persistent page-aligned buffer (maximum 4096 bytes), never the
caller's stack. Failure quarantines it until successful Disable Slot. Control
requests and slot release share an ownership gate; failed disable retains DMA
allocations and DCBAA. Other command input-context lifetime paths still need
review when integrating the audio endpoint.

QEMU initially faulted at RIP `0x1007e214` in `xhci_init`, reading physical BAR
`0xc000000000`, outside the 16-GiB identity map. The failure is preserved in
`usb-dma-hid-runtime.debugcon.log`. `vmm_map_device` now provides permanent,
supervisor-only UC/NX mappings through a high-half entry shared before user
address-space creation. Firmware entries are preserved. The window is 64 MiB,
individual mappings at most 16 MiB; overflow and allocation failures fail closed.
xHCI maps the register span derived from capability offsets, not a raw pointer.

## Evidence

All logs below are in `build/etapa10-completion/`.

- `usb-mmio-focused.log`: descriptor, queue, event, EP0 lifetime, MMIO and four
  QMP harness tests passed.
- `usb-dma-sanitize.log`, `usb-mmio-sanitize.log`, `usb-events-sanitize.log`,
  `usb-iso-sanitize.log`: ASan/UBSan passed.
- `usb-events-cross.log`, `usb-mmio-cross.log`: hosted replay of actual kernel
  objects passed, cross compiler GCC 13.2.0.
- `usb-mmio-full-tests.log`: `make -j8 test TOOLCHAIN64=elf`, exit 0.
- `usb-mmio-layout.log`: layout audit, exit 0.
- `usb-mmio-final-artifact.log`: full-profile diagnostic kernel/ISO/manifest,
  consistent mixed-player flags throughout, exit 0.
- `usb-mmio-final-hid-result.log`, `usb-mmio-repeat-hid-result.log`: two cold
  boots with fresh disks/OVMF variables passed enumeration, EP0 configuration
  and interrupt input. QMP injects `a` after the configured-keyboard marker;
  `[smoke] usb-hid-keyboard ready` requires an actual USB character, not UART.
- `usb-mmio-player-qemu-result.log`: captured OGG/WAV playlist passed; transitions
  `[[239, 299, 399], [240, 300, 400]]` and PCM gap checks passed.
- `usb-mmio-vmware-result.log`: ordered preemption-SIMD, guarded-DMA,
  rendered-two-tracks and ready markers passed. This is HDA regression, not
  VMware USB Audio evidence.
- `usb-mmio-vmware-compare.log`: RAW/VMDK identical before boot. Final inspection:
  zero VMware VMs and no QEMU process. No physical disks were used.

QEMU 10.2.1: q35/TCG, 512 MiB, OVMF 4M, SATA, qemu-xhci + usb-kbd for input.
VMware via vmrun: virtual hardware 22, UEFI, Secure Boot off, 2 vCPU, 1024 MiB,
SATA/HDA, network and USB disabled for the player regression.

Kernel SHA-256: `fe027158ebc7c7c974190001f2d9c02d9be4bc33f3c36e0633d64f2880a8c47f`.
Saved ELF: `build/etapa10-completion/usb-mmio-final.elf`.
Diagnostic ISO SHA-256: `fdea6cbfde919ed4dd398734b7b517db850062bbeb6b7acc256f90dfc0c81976`.
ISO: `build/ci/CapyOS-Smoke-Media-Player-UEFI.iso`.

## Remaining Etapa 10 work

Integrate UAC1 enumeration/configuration, including EP0 packet-size update where
needed; configure/stop real isoch DMA; connect the PCM backend and capture USB
audio. Compressed-source concurrency, driver-failure runtime cases and final
integrated gates also remain. No release version or public ABI was changed.

References: [xHCI specification](https://www.intel.com/content/dam/www/public/us/en/documents/technical-specifications/extensible-host-controler-interface-usb-xhci.pdf),
[QEMU QMP reference](https://www.qemu.org/docs/master/interop/qemu-qmp-ref.html).
