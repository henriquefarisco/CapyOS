# Guia Hyper-V para CapyOS (historico / nao suportado)

Status atual:

- `Hyper-V` nao faz parte da trilha suportada de validacao ou release
- o caminho oficial atual para VM e `VMware` em `UEFI` com `E1000`
- este documento permanece apenas como referencia historica de investigacao
- qualquer comportamento em `Hyper-V` deve ser tratado como experimental

O fluxo `BIOS/MBR` (Geracao 1) esta descontinuado para build, boot e release.

## 1) VM recomendada

### Criar VM
- Geracao: `2`
- Memoria: `512 MiB` ou `1 GiB` com memoria dinamica desabilitada
- Secure Boot: desabilitado (`BOOTX64.EFI` ainda nao e assinado)

### Rede
- `Geracao 2` usa somente `Network Adapter` sintetico (`NetVSC/VMBus`) na
  trilha suportada.
- `Legacy Network Adapter` nao entra no fluxo `Gen2`; se voce estiver usando
  `tulip-2114x`, isso ja e um laboratorio `Gen1`/legado fora do suporte atual.
- Para rede em `Hyper-V Gen2`, capture sempre:
  - `runtime-native show`
  - `net-status`
  - `net-dump-runtime`
  - log serial completo do boot ate o login

### Preflight do host
- A coleta do host Hyper-V exige PowerShell elevado.
- Script recomendado no host Windows:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\scripts\hyperv_host_preflight.ps1 -Name CapyOSGen2 -IncludeEvents
```

- O script exporta `build/hyperv-preflight/hyperv-preflight.json` e
  `build/hyperv-preflight/hyperv-preflight.txt` com:
  - estado do host/VM
  - geracao da VM
  - `Secure Boot`
  - memoria dinamica
  - NICs sinteticas configuradas
  - discos/DVD/COM
  - eventos `Hyper-V-Worker` e `VMMS` quando `-IncludeEvents` for usado

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
  --kernel build/capyos64.bin \
  --auto-manifest \
  --allow-existing \
  --confirm
```

Opcao PowerShell:

```powershell
wsl -e bash -lc "cd /mnt/d/Projetos/CapyOS && make iso-uefi && python3 tools/scripts/provision_gpt.py --img '/mnt/c/ProgramData/Microsoft/Windows/Virtual Hard Disks/CapyOSGen2.vhd' --bootx64 build/boot/uefi_loader.efi --kernel build/capyos64.bin --auto-manifest --allow-existing --confirm"
```

Notas:
- nao precisa `sudo`
- confirme que `--img` aponta para o mesmo disco anexado na VM
- se optar por `--volume-key`, o script agora exige
  `--allow-plain-volume-key` para deixar explicito que isso e apenas de
  laboratorio/smoke
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

Este e o artefato oficial para distribuicao e instalacao inicial:

```bash
cd /mnt/d/Projetos/CapyOS
make iso-uefi
```

- Anexe `build/CapyOS-Installer-UEFI.iso` como DVD SCSI na VM Gen2.
  Esse e o nome oficial atual do artefato ISO UEFI.
- A ISO UEFI usa El Torito apontando para `EFI/BOOT/efiboot.img`.
- O repositorio agora inclui smoke ponta a ponta da ISO oficial:

```bash
cd /mnt/d/Projetos/CapyOS
make smoke-x64-iso
```

- Esse smoke cobre instalacao, reboot pelo disco, login e persistencia.
- A trilha `Hyper-V Gen2` ja foi validada nesta etapa; a checagem segue manual
  quando o objetivo for repetir a certificacao nesse backend.
- Durante o boot hibrido em `Hyper-V Gen2`, o kernel mantem `EFI ConIn` como
  caminho principal. Depois do `ExitBootServices`, o `VMBus keyboard` e
  promovido de forma controlada, com retentativa limitada e fallback
  automatico se o canal degradar.

## 5) Troubleshooting rapido

- `The boot loader failed.`: geralmente `Secure Boot` habilitado.
- Sem video: confirmar VM `Geracao 2` e boot UEFI pelo disco provisionado.
- Sem persistencia:
  - rodar `make inspect-disk IMG=<seu-disco>`
  - rodar `make smoke-x64-cli` e comparar os logs de `boot1` e `boot2`
- Teclado inconsistente:
  - confirmar que a VM esta usando Geracao 2
  - validar layout no sistema com `config-keyboard show`
  - usar `info` para inspecionar `input.mode`, `ps2=` e `hyperv=`
- Rede sintetica parada:
  - confirmar que a VM esta em `Geracao 2` com `Network Adapter`
  - coletar `runtime-native show`, `net-status`, `net-dump-runtime`
  - registrar se `vmbus=` esta em `off|hypercall|synic|contact|offers`
  - registrar se `stage=` esta em `offers|channel|control|ready|failed`
  - exportar o log serial completo antes de repetir qualquer tentativa manual

### Mapa rapido de estagios Hyper-V

| Sinal | Leitura pratica | Proximo foco |
| --- | --- | --- |
| `vmbus=off` | guest nao entrou no trilho Hyper-V/VMBus | confirmar que a VM e `Gen2`, que o guest detectou Hyper-V e que o build atual realmente contem a trilha nativa |
| `vmbus=hypercall` | pagina de hypercall preparada, mas `SynIC` ainda nao estabilizou | revisar inicializacao minima do barramento e sinais de bootstrap hibrido |
| `vmbus=synic` | `SynIC` pronto, mas sem contato completo com o `VMBus` | olhar handshake de contato e telemetria serial do `VMBus` |
| `vmbus=contact` | tentativa de contato feita, offers ainda nao foram cacheadas | focar em `REQUESTOFFERS`, tempo de espera e mensagens de transporte |
| `vmbus=offers` + `stage=offers` | offer sintetica apareceu, mas o controlador ainda nao abriu canal | revisar gate: `StorVSC` antes de `ExitBootServices`, `NetVSC` so depois de storage nativo estavel |
| `stage=channel` | canal abriu, handshake de controle ainda nao fechou | focar em `NetVSP/RNDIS`, `relid`, `connection_id`, timeout e resultado final |
| `stage=control` | canal e controle ativos, mas o runtime ainda nao ficou pronto | revisar negociacao final do `NetVSC`, modo `ready`, DHCP e promocao do backend |
| `stage=ready` | backend sintetico pronto | validar `net-mode dhcp`, `net-resolve example.com`, `hey gateway` |
| `stage=failed` | runtime degradou para passivo seguro | usar `last_action`, `last_result`, `relid`, `connection_id`, log serial e eventos do host para localizar a falha exata |

## 6) Escopo descontinuado

- Hyper-V Geracao 1
- BIOS/MBR
- build/release x86_32

Esses fluxos nao fazem parte da trilha oficial atual.
