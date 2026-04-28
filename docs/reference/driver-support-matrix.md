# CapyOS Driver Support Matrix

Last updated: 2026-04-28

## Status Definitions

| Status | Meaning |
|--------|---------|
| **Suportado** | Validado em smoke oficial; caminho primario de release |
| **Laboratorio** | Funcional em testes; nao e caminho de release obrigatorio |
| **Experimental** | Codigo presente; sem validacao sistematica |
| **Fora de suporte** | Codigo mantido por referencia tecnica; nao use em release |

---

## Virtualizadores e Plataformas

| Plataforma | Modo de boot | Status | Notas |
|------------|-------------|--------|-------|
| VMware Workstation/ESXi | UEFI | **Suportado** | Caminho oficial de release; `make release-check` valida neste ambiente |
| QEMU/OVMF | UEFI | **Laboratorio** | Usado em smoke CI (`make smoke-x64-*`); nao e caminho obrigatorio de release |
| Hyper-V (Generation 2) | UEFI | **Fora de suporte** | Backend VMBus presente para investigacao tecnica; nao use em smoke ou release oficial |

---

## Rede

| Driver | Hardware / Backend | Status | Notas |
|--------|--------------------|--------|-------|
| `e1000` | Intel E1000 / VMware E1000 | **Suportado** | RX/TX validado; ping, ICMP, TCP, HTTP/HTTPS funcionais |
| `tulip` | DEC 21143 / Tulip 2114x | **Experimental** | Modo inicial; RX/link precisam de hardening adicional |
| `rtl8139` | Realtek RTL8139 | **Experimental** | Codigo presente; sem validacao sistematica |
| `virtio_net` | VirtIO Net | **Experimental** | Codigo presente; testado apenas em QEMU basico |
| `vmxnet3` | VMware VMXNET3 | **Experimental** | Codigo presente; sem validacao sistematica |
| `netvsc` (VMBus) | Hyper-V NetVSC | **Fora de suporte** | Backend VMBus presente; segue trilha Hyper-V |

---

## Armazenamento

| Driver | Hardware / Backend | Status | Notas |
|--------|--------------------|--------|-------|
| `efi_block` | EFI Block I/O Protocol | **Suportado** | Interface primaria para boot UEFI em VMware e QEMU |
| `ahci` | SATA AHCI | **Laboratorio** | Funcional para acesso direto a disco apos boot EFI |
| `ramdisk` | Ramdisk em memoria | **Laboratorio** | Usado em testes e fallback de recovery RAM |
| `nvme` | NVMe | **Experimental** | Codigo presente; sem validacao em smoke |
| `storvsc` (VMBus) | Hyper-V StorVSC | **Fora de suporte** | Backend VMBus presente; segue trilha Hyper-V |
| `ata_pio` | ATA PIO legado | **Fora de suporte** | Mantido por referencia; nao use em novas plataformas |

---

## Input

| Driver | Hardware / Backend | Status | Notas |
|--------|--------------------|--------|-------|
| PS/2 Keyboard (i8042) | PS/2 nativo | **Suportado** | Teclado primario em VMware UEFI; layouts PT-BR e US validados |
| `vmbus_keyboard` | Hyper-V VMBus HID | **Fora de suporte** | Segue trilha Hyper-V |
| USB HID (`xhci` + `usb_hid`) | USB HID via xHCI | **Experimental** | Codigo presente; sem validacao sistematica |
| Mouse (PS/2 / USB) | Mouse | **Experimental** | Codigo presente; integrado ao compositor GUI |

---

## Sistema / Plataforma

| Driver | Funcao | Status | Notas |
|--------|--------|--------|-------|
| `apic` | APIC / interruptores | **Suportado** | Necessario para operacao normal em x86_64 |
| `pit` | Timer PIT | **Suportado** | Timer de sistema; usado para timebase e esperas |
| `rtc` | RTC (relogio em tempo real) | **Laboratorio** | Leitura de hora funcional |
| `com1` | Serial COM1 | **Laboratorio** | Usado para debug serial; nao obrigatorio em release |
| `acpi` | ACPI (power/tables) | **Experimental** | Parsing basico presente; shutdown/sleep nao completos |
| `pcie` | PCIe enumeration | **Experimental** | Enumeracao basica; sem suporte a hot-plug |
| `gpu_core` | GPU framebuffer | **Laboratorio** | Framebuffer UEFI GOP; sem aceleracao 2D/3D |

---

## Notas Gerais

- O caminho oficial de release e **VMware + UEFI + E1000**. Qualquer mudanca nessa trilha requer re-validacao em `make release-check`.
- Drivers marcados como **Experimental** podem ser promovidos a **Laboratorio** ou **Suportado** mediante validacao em smoke oficial com evidencia documentada.
- Drivers **Fora de suporte** nao devem ser usados em releases; o codigo e mantido para referencia tecnica e investigacao futura.
- Atualizar esta tabela sempre que o status de um driver mudar ou um novo driver for adicionado.
