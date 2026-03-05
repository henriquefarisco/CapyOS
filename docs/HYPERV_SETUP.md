# Guia Hyper-V para CapyOS (UEFI/GPT x86_64)

Este guia cobre o caminho oficial de validacao e release:

- Hyper-V Geracao 2
- trilha unica `UEFI/GPT/x86_64`
- boot pelo disco provisionado

O fluxo `BIOS/MBR` (Geracao 1) esta descontinuado para build, boot e release.

## 1) VM recomendada

### Criar VM
- Geracao: `2`
- Memoria: `512 MiB` ou `1 GiB` com memoria dinamica desabilitada
- Secure Boot: desabilitado (`BOOTX64.EFI` ainda nao e assinado)

### Rede
- Adapter padrao: `Network Adapter` apenas quando o objetivo for validar boot
  puro
- Para validacao de rede atual, prefira laboratorio separado com `Legacy
  Network Adapter` ate o caminho `netvsc` estar pronto

### Disco
- Use VHD fixo (`.vhd`) conectado em SCSI.
- O fluxo oficial usa provisionamento GPT/ESP/BOOT/DATA via script.

## 2) Provisionar disco GPT no WSL

No WSL, dentro do repositorio:

```bash
cd /mnt/d/Projetos/CapyOS
make iso-uefi
python3 tools/scripts/provision_gpt.py \
  --img '/mnt/c/ProgramData/Microsoft/Windows/Virtual Hard Disks/CapyOSGen2.vhd' \
  --bootx64 build/boot/uefi_loader.efi \
  --kernel build/noiros64.bin \
  --auto-manifest \
  --allow-existing \
  --confirm
```

Opcao PowerShell:

```powershell
wsl -e bash -lc "cd /mnt/d/Projetos/CapyOS && make iso-uefi && python3 tools/scripts/provision_gpt.py --img '/mnt/c/ProgramData/Microsoft/Windows/Virtual Hard Disks/CapyOSGen2.vhd' --bootx64 build/boot/uefi_loader.efi --kernel build/noiros64.bin --auto-manifest --allow-existing --confirm"
```

Notas:
- nao precisa `sudo`
- confirme que `--img` aponta para o mesmo disco anexado na VM
- depois do provisionamento, valide a imagem:

```bash
cd /mnt/d/Projetos/CapyOS
make inspect-disk IMG='/mnt/c/ProgramData/Microsoft/Windows/Virtual Hard Disks/CapyOSGen2.vhd'
```

## 3) Boot esperado

- Configure a VM para iniciar pelo `Hard Drive`.
- Comportamento esperado:
  - loader UEFI transfere para o kernel x64
  - o runtime detecta e monta o volume `DATA`
  - first boot, login e shell aparecem sem depender de COM
  - arquivos e usuarios persistem apos reboot

Para smoke automatizado da trilha oficial:

```bash
cd /mnt/d/Projetos/CapyOS
make smoke-x64-cli
```

## 4) Uso da ISO

Uso recomendado apenas para instalacao/provisionamento inicial:

```bash
cd /mnt/d/Projetos/CapyOS
make iso-uefi
```

- Anexe `build/NoirOS-Installer-UEFI.iso` como DVD SCSI na VM Gen2.
- A ISO UEFI usa El Torito apontando para `EFI/BOOT/efiboot.img`.
- O instalador por ISO ainda nao possui smoke ponta a ponta dedicado; o fluxo
  oficialmente validado hoje continua sendo `provision_gpt.py` + boot por HDD.

## 5) Troubleshooting rapido

- `The boot loader failed.`: geralmente `Secure Boot` habilitado.
- Sem video: confirmar VM `Geracao 2` e boot UEFI pelo disco provisionado.
- Sem persistencia:
  - rodar `make inspect-disk IMG=<seu-disco>`
  - rodar `make smoke-x64-cli` e comparar os logs de `boot1` e `boot2`
- Teclado inconsistente:
  - confirmar que a VM esta usando Geracao 2
  - validar layout no sistema com `config-keyboard show`
  - lembrar que o runtime ainda depende de `EFI ConIn` em parte dos cenarios

## 6) Escopo descontinuado

- Hyper-V Geracao 1
- BIOS/MBR
- build/release x86_32

Esses fluxos nao fazem parte da trilha oficial atual.
