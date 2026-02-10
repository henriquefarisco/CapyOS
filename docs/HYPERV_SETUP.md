# Guia Hyper-V para NoirOS (UEFI/GPT x86_64)

Este documento cobre dois cenários:

- **Recomendado (atual)**: UEFI/GPT em **Geração 2** (64-bit).
- **Legado**: BIOS/MBR em **Geração 1** (mantido apenas para compatibilidade).

## 1) Recomendado: Geração 2 (UEFI/GPT, 64-bit)

### Criar VM
- **Geração**: **2**
- **Memória**: 512 MiB ou 1 GiB (**memória dinâmica desabilitada**)
- **Secure Boot**: **Desabilitado** (não assinamos `BOOTX64.EFI`)
- **Rede (estado atual)**:
  - para validar o caminho de driver legado no CapyOS atual, prefira adicionar **Legacy Network Adapter** (driver `tulip-2114x`, em evolucao)
  - o adaptador sintético padrão (`Network Adapter`/netvsc) segue em desenvolvimento

### Disco
- Crie um disco **VHD fixo** (`.vhd`) e conecte como **SCSI** (padrão da Geração 2).
- O NoirOS ainda não tem drivers de disco no kernel; o loader UEFI carrega o kernel e passa o handoff. O acesso ao disco pós-boot é parte das próximas fases.

### Provisionar GPT/ESP/BOOT (a partir do WSL)
No WSL, dentro do repo:

```bash
cd /mnt/d/Projetos/NoirOS
make iso-uefi
python3 tools/scripts/provision_gpt.py \
  --img '/mnt/c/ProgramData/Microsoft/Windows/Virtual Hard Disks/NoirOSGenII.vhd' \
  --bootx64 build/boot/uefi_loader.efi \
  --kernel build/noiros64.bin \
  --auto-manifest \
  --allow-existing \
  --confirm
```

Se preferir rodar direto do PowerShell (Windows), um comando que funciona bem com paths com espacos:

```powershell
wsl -e bash -lc "cd /mnt/d/Projetos/NoirOS && make iso-uefi && python3 tools/scripts/provision_gpt.py --img '/mnt/c/ProgramData/Microsoft/Windows/Virtual Hard Disks/NoirOSGenII.vhd' --bootx64 build/boot/uefi_loader.efi --kernel build/noiros64.bin --auto-manifest --allow-existing --confirm"
```

Notas:
- Não precisa de `sudo` (o script opera em arquivo `.vhd`/`.img`).
- Garanta que `--img` aponta para o **mesmo disco** anexado na VM; `build/disk-gpt.img` é apenas uma imagem de teste no repositório.

Isso cria GPT com **ESP (FAT32)** e **BOOT (raw)**:
- ESP: `\EFI\BOOT\BOOTX64.EFI`, `\boot\noiros64.bin`, `\boot\manifest.bin`
- BOOT: `manifest.bin` no início + kernel ELF logo após (LBA relativo 1)

### Boot
- Configure a VM para bootar do **Hard Drive** (disco provisionado).
- Comportamento esperado: tela escura + barras (verde = boot ok, azul/vermelho = RSDP) e texto básico (quando GOP disponível).

- Para debug, o loader tenta gravar `\\EFI\\NOIROS.LOG` na ESP (quando o volume de boot é gravável; tipicamente ao bootar do disco).
- O loader também imprime na tela (antes do kernel) uma linha do tipo: `RSDP src=cfg|memmap|legacy copied=0|1 addr=... valid=0|1`.
- Para ler o log do ESP (com a VM desligada): `py -3 tools/scripts/inspect_disk.py "C:\ProgramData\Microsoft\Windows\Virtual Hard Disks\NoirOSGenII.vhd" --cat-esp EFI/NOIROS.LOG`

### Boot via ISO (DVD) (opcional)
**Modo instalador (novo):** ao bootar pela ISO, o `BOOTX64.EFI` detecta o ISO de instalação pelo marcador `\\NOIROS.INI` (e/ou volume **read-only**) e entra em modo instalador.
- Ele pede confirmacao (tecla `I`) antes de apagar o disco.
- Ele cria GPT (ESP/BOOT/DATA), cria FAT32 na ESP e copia `BOOTX64.EFI` + `NOIROS64.BIN` + `MANIFEST.BIN`.
- Ele grava `manifest+kernel` na particao BOOT (raw) e reinicia a VM; depois remova a ISO e deixe bootar pelo disco.

Se quiser bootar primeiro pela ISO (ex.: modo "installer"):

```bash
cd /mnt/d/Projetos/NoirOS
make iso-uefi
```

- Anexe `build/NoirOS-Installer-UEFI.iso` como **SCSI DVD** na VM Gen2.
- O `make iso-uefi` agora gera uma entrada **El Torito UEFI** apontando para uma **imagem FAT16** (`EFI/BOOT/efiboot.img`). Hyper‑V Gen2 normalmente **não boota** quando o El Torito aponta diretamente para `BOOTX64.EFI`.

## 2) Legado: Geração 1 (BIOS/MBR)
- Use **Geração 1** se precisar testar o bootloader Stage1/Stage2 BIOS (MBR).
- Memória dinâmica desabilitada.
- Disco em IDE (Geração 1).

## 3) Troubleshooting rápido
- Se não aparece vídeo: verifique Secure Boot desabilitado e se a VM é Geração 2.
- Se aparece **"The boot loader failed."**: quase sempre é **Secure Boot ligado** (nossos binários não são assinados).
- Se o kernel não desenha nada: o framebuffer pode estar fora do identity-map atual (primeiro 1 GiB). Isso será resolvido ao ampliar o mapeamento no kernel.
