# Plano de Refatoração NoirOS → UEFI/GPT x86_64

## Metas (caixas de controle)
- [x] **Fase 0** Preparação: definir alvo UEFI x86_64, esquema GPT, toolchain e estratégia de boot.
- [x] **Fase 1** Build x86_64: ajustar Makefile/linker, toolchain cross, payloads 64-bit.
- [x] **Fase 2** Kernel x86_64: entrada em long mode, GDT/IDT 64-bit, paging, tipos 64-bit.
- [x] **Fase 3** Bootloader UEFI: BOOTX64.EFI lendo manifest/kernel e passando handoff.
- [x] **Fase 4** GPT/Instalador: particionamento GPT (ESP/BOOT/DATA), ESP FAT32, escrita de payloads. **Validado no Hyper-V Gen 2 (2026-01-15)**.
- [ ] **Fase 5** Drivers: NVMe (PCIe), AHCI legado, bloco de disco no kernel 64-bit.
- [ ] **Fase 6** Filesystem: port NoirFS/VFS, alocador de memória 64-bit.
- [ ] **Fase 7** Shell/Auth: port NoirCLI, autenticação de usuários, sistema de configuração.
- [ ] **Fase 8** Segurança: TPM2 medições, plano de Secure Boot/assinatura.
- [ ] **Fase 9** Testes/CI: smoke test UEFI (OVMF + NVMe), testes de manifest/GPT.

## Fases detalhadas

### Fase 0 – Preparação e estratégia
- Alvo primário: UEFI x86_64 (BIOS/MBR como legado opcional).
- Layout GPT: ESP (FAT32 300–512 MiB), BOOT (manifest + kernels raw), DATA (NoirFS cifrado). ESP montada em `/boot/efi`.
- Toolchain: cross x86_64-elf, nasm, ld/objcopy 64-bit; atualizar `tools/scripts/check_deps.py`.
- Boot path: loader UEFI próprio (pequeno) lendo manifest + kernel ELF64; manter manifest NIBT.
- Testes alvo: QEMU/OVMF com NVMe (`-device nvme`), e Hyper-V Gen2 (UEFI + TPM em roadmap).

### Fase 1 – Build system 64-bit
- Ajustar Makefile: CFLAGS/LDFLAGS x86_64, `linker64.ld`, artefatos `noiros64.efi`/`noiros64.bin`.
- Gerar payloads 64-bit em `boot_payloads.h` (loader UEFI + kernel x64).
- Alvos: `make iso-uefi` e, se necessário, `make iso-bios-legacy`.

### Fase 2 – Kernel para x86_64
- Entry longo: GDT/IDT 64-bit, paging (4K/2M/1G), stack alta.
- Revisar tipos (uintptr_t/size_t) e estruturas dependentes de 32 bits.
- ISR/APIC/HPET: portar para x64, checar contexto e máscaras.
- LBA 64-bit e limites de memória ampliados.
**Estado atual:** `entry64.S` preserva o ponteiro de handoff (RDI), desabilita interrupções, configura uma IDT 64-bit (256 vetores com handler padrão) e entra em `kernel_main64`. Para compatibilidade com firmware (Hyper-V), **ainda não troca as page tables do UEFI** (paging próprio será fase posterior). O `kernel_main64` consome o handoff e desenha um status básico via framebuffer GOP.

### Fase 3 – Bootloader UEFI
- Loader UEFI (C) que:
  - Usa serviços de boot para achar ESP/BOOT e ler manifest.
  - Carrega kernel ELF64, passa handoff (memória, ACPI RSDP, framebuffer GOP).
  - Log em console e opcional `efi/noiros.log`.
- Gerar `EFI/BOOT/BOOTX64.EFI` na ISO/ESP.
**Estado atual:** BOOTX64.EFI está em **PE/COFF (EFI app)** e o `iso-uefi` gera uma entrada El Torito UEFI apontando para uma **imagem FAT** (`EFI/BOOT/efiboot.img`), compatível com Hyper-V Gen2. O loader:
- localiza o volume, tenta ler manifest via **GPT/BOOT (BlockIO)** e faz fallback para arquivos na FAT;
- carrega o kernel ELF64, aplicando relocação em um bloco contíguo **abaixo de 4GiB**;
- coleta RSDP, framebuffer GOP e memory map, chama `ExitBootServices` e passa o handoff ao kernel.
Validado em QEMU/OVMF e Hyper-V Gen2 (barras de status no framebuffer).
O loader tambem tenta gravar log em `\\EFI\\NOIROS.LOG` na ESP (quando o volume e gravavel).
Diagnóstico ACPI: o loader tenta obter o RSDP via *configuration table*; se inválido, faz fallback via *MemoryMap* (regiões `EfiACPIReclaimMemory`/`EfiACPIMemoryNVS`) e, por último, via varredura legada (EBDA/0xE0000..0xFFFFF). Se o RSDP não for válido, o handoff passa `rsdp=0` para evitar o kernel dereferenciar lixo.

### Fase 4 – GPT/Instalador
- Atualizar `boot_writer`/installer para GPT: cabeçalhos primário/backup, entradas padrão.
- ESP FAT32 com `BOOTX64.EFI`; BOOT raw com manifest+kernels; DATA NoirFS cifrado.
- Suporte LBA 64-bit em wrappers/blocos; NVMe no instalador para detectar discos.
**Estado atual:** Boot validado no Hyper-V Gen 2. O `BOOTX64.EFI` carrega kernel, exibe UI framebuffer, aceita comandos básicos. O modo instalador existe mas ainda não foi testado (pressionar `I` na ISO). Pendente: integrar formatação da partição DATA (NoirFS) no kernel 64-bit.

### Fase 5 – TPM2/Secure Boot
- TSS/TPM2 básico: medir manifest/kernel e estender PCRs.
- Plano de Secure Boot: assinatura de BOOTX64.EFI/kernels (modo teste com PK/KEK próprios).

### Fase 6 – Drivers
- NVMe (PCIe) admin/IO queues; AHCI legado.
- Vídeo GOP (framebuffer) inicial.
- Teclado básico via UEFI simples ou PS/2 no kernel.

### Fase 7 — ISO e layout
- ISO híbrida: EFI e opcional BIOS legado.
- Estrutura: `/EFI/BOOT/BOOTX64.EFI`, `/boot/loader` (manifest+kernels), opcional stage2 BIOS.
- Para Hyper-V Gen2: El Torito UEFI deve apontar para uma **imagem FAT (ex.: FAT16)** (ex.: `EFI/BOOT/efiboot.img`), não para o `.EFI` diretamente.
- Atualizar scripts (`gen_efi_boot`/`gen_grub_cfg`) e `inspect_disk` para GPT/ESP.

### Fase 8 – Testes e CI
- QEMU UEFI (OVMF) com NVMe (`-device nvme`) para smoke test.
- Testes de manifest/GPT/parsers; automação de build x86_64 e ISO UEFI.

### Fase 9 – Migração/compatibilidade
- Documentar caminho preferencial (UEFI/GPT) e legado BIOS.
- Ferramenta para detectar ambiente (UEFI vs BIOS) e seguir fluxo correto.
- Atualização in-place: criar ESP, reescrever GPT, migrar manifest/kernels.

## Próximos passos imediatos
- Fase 5: implementar driver NVMe básico para acesso a disco no kernel 64-bit.
- Fase 6: portar alocador de memória e NoirFS para 64-bit.
- Fase 7: portar shell e autenticação de usuários.
