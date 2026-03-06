# CapyOS

<p align="center">
  <img src="assets/branding/icon.svg" alt="CapyOS symbol" width="180" />
</p>

Ultima atualizacao: 2026-03-05
Versao de referencia: `0.8.0-alpha.0`

CapyOS e um sistema operacional hobby escrito em C/Assembly, com foco em:
- boot proprio UEFI/GPT
- filesystem proprio (CAPYFS)
- seguranca por criptografia no volume de dados
- shell modular (CapyCLI)
- consolidacao da trilha x86_64

Nota de compatibilidade de build:
- alguns artefatos de boot ainda usam identificadores tecnicos em caixa alta
  (por exemplo `CAPYOS64.BIN` na ESP e `CAPYOS.LOG` no diagnostico UEFI), para
  manter compatibilidade com FAT/EFI e com o fluxo de boot atual.

## Licenca, autoria e uso

Este repositorio usa a licenca Open Source `Apache-2.0`.

- texto integral da licenca: `LICENSE`
- atribuicao oficial e desenvolvedor principal: `NOTICE`
- politica de branding do projeto: `BRANDING.md`
- declaracao de uso licito: `LAWFUL_USE.md`

Desenvolvedor principal: `Henrique Schwarz Souza Farisco`.

Observacao importante:
- para manter o repositorio como Open Source, a licenca nao inclui uma
  clausula geral de proibicao de uso ilegal
- essa diretriz aparece como posicionamento do projeto em `LAWFUL_USE.md`,
  sem alterar os termos da licenca

## Documentacao

- indice principal: `docs/README.md`
- arquitetura atual: `docs/architecture/system-overview.md`
- setup Hyper-V: `docs/setup/hyper-v.md`
- validacao de boot/login/CLI: `docs/testing/boot-and-cli-validation.md`
- referencia de comandos: `docs/reference/cli-reference.md`
- roadmap e planos: `docs/plans/`
- release notes: `docs/releases/README.md`

## Visao geral do projeto

O repositorio segue uma unica trilha oficial:

1. `UEFI/GPT 64-bit`
- loader UEFI funcional (`BOOTX64.EFI`)
- kernel x86_64 com framebuffer, PCIe/NVMe e shell
- volume DATA cifrado com persistencia em disco
- login e CapyCLI persistentes no disco provisionado
- teclado em VM UEFI priorizando `EFI ConIn` enquanto o stack nativo de input e concluido

Codigo legado `BIOS/MBR 32-bit` ainda pode existir no repositorio como divida de migracao,
mas nao faz parte do pipeline suportado de build, boot, instalacao ou release.

## O que o sistema oferece hoje

### 1. Boot, instalacao e imagens

- Loader UEFI (`src/boot/uefi_loader.c`) com handoff para kernel x86_64.
- Provisionamento GPT/ESP/BOOT/DATA por script (`tools/scripts/provision_gpt.py`).
- Instalador UEFI em evolucao dentro do loader para provisionamento direto em disco.
- Build de artefatos oficiais UEFI (`make all64`, `make iso-uefi`, `make disk-gpt`).

### 2. Kernel e runtime

- Console framebuffer no x64 (`src/arch/x86_64/kernel_main.c`).
- Console/telemetria serial COM1 como fallback de depuracao.
- Deteccao de PCIe/NVMe e inicializacao de controladores suportados.
- Bootstrap inicial de rede no x64 com:
  - `e1000` funcional (RX/TX + ping/ICMP)
  - `tulip-2114x` em modo inicial/experimental (adaptador legado/generico)
- Parser/protocolo de ARP/IPv4/ICMP/UDP/TCP com diagnostico via CLI.
- Estado de sessao com usuario autenticado, `cwd`, prompt dinamico e logout.

### 3. Filesystem (CAPYFS) e VFS

- CAPYFS com superbloco, bitmap, inode e diretorios.
- VFS com resolucao de caminhos absolutos/relativos e metadados.
- Buffer cache com sincronizacao explicita (`do-sync`).
- Runtime x64 em disco provisionado:
  - volume `DATA` cifrado montado no boot
  - estrutura base persistente (`/bin`, `/docs`, `/etc`, `/home`, `/var/log`, etc.)
  - `users.db` reutilizado entre boots
- `ramdisk` permanece apenas como contingencia controlada quando nao existe
  caminho persistente valido

### 4. Criptografia e autenticacao

- Camada cifrada por bloco no fluxo x64:
  - AES-XTS 256
  - PBKDF2-SHA256
- Banco de usuarios em `/etc/users.db`:
  - salt por usuario
  - hash PBKDF2-SHA256 (`USER_ITERATIONS=64000`)
- Setup inicial de formatacao com `Usuario administrador [admin]` persistido no `users.db`.
- Login com validacao por `userdb_authenticate`.

### 5. CapyCLI (shell modular)

Conjuntos de comandos implementados:

- Navegacao: `list`, `go`, `mypath`
- Conteudo: `print-file`, `page`, `print-file-begin`, `print-file-end`, `open`, `print-echo`
- Gerenciamento: `mk-file`, `mk-dir`, `kill-file`, `kill-dir`, `move`, `clone`, `stats-file`, `type`
- Busca: `hunt-file`, `hunt-dir`, `hunt-any`, `find`
- Sessao/ajuda/sistema: `help-any`, `help-docs`, `mess`, `bye`, `print-*`, `config-keyboard`, `shutdown-reboot`, `shutdown-off`, `do-sync`
- Rede:
  - `net-status`
  - `net-ip`, `net-gw`, `net-dns`
  - `net-set <ip> <mask> <gw> <dns>`
  - `hey <ip|gateway|dns|self>` (ping/ICMP no x64)

Observacao sobre o x64:
- comandos antigos que estavam hardcoded no loop principal foram redirecionados para o modulo de shell.
- aliases de compatibilidade: `help -> help-any`, `clear -> mess`, `reboot -> shutdown-reboot`, `halt -> shutdown-off`.

### 6. Entrada de teclado e drivers em VMs

Prioridade atual de entrada no x64:

1. `EFI ConIn` (principal em VMs UEFI, incluindo Hyper-V Gen2)
2. PS/2
3. Hyper-V VMBus keyboard (experimental)
4. COM1 (ultimo fallback)

Impacto pratico:
- Hyper-V Gen2 com boot UEFI pode usar teclado sem Putty/porta COM.
- COM1 permanece para debug e contingencia.
- Para rede no Hyper-V:
  - `Network Adapter` (sintetico/netvsc) ainda esta em evolucao.
  - `Legacy Network Adapter` tem caminho inicial via driver `tulip-2114x` (em hardening).

## Estado atual por dominio (resumo)

| Dominio | Estado atual | Nivel |
|---|---|---|
| Boot BIOS/MBR | Descontinuado | Fora de suporte |
| Boot UEFI/GPT | Loader + kernel x64 com smoke em disco | Parcial |
| CAPYFS em disco cifrado | Ativo no x64 | Parcial |
| CAPYFS no x64 | Persistente em disco; RAM apenas como contingencia controlada | Parcial |
| Login e sessao | Funcional no x64 com persistencia em disco | Parcial |
| CLI modular | Comandos principais ativos | Estavel |
| Rede x64 | Stack com ARP/IPv4/ICMP ativo, `e1000` funcional e `tulip` em validacao | Parcial |
| Teclado em Hyper-V | Via EFI ConIn sem exigir COM (x64) | Parcial |
| USB HID teclado x64 | Enumeracao XHCI ainda incompleta | Em desenvolvimento |
| Multithread/scheduler | Ainda nao implantado | Pendente |

## Lacunas importantes

- o caminho de instalacao via ISO UEFI ainda precisa de matriz de validacao dedicada; o smoke oficial atual cobre provisionamento GPT direto + boot por HDD.
- driver USB HID completo (enumeracao + input) ainda nao finalizado.
- caminho VMBus keyboard continua experimental e depende de hardening.
- netvsc (adaptador sintetico Hyper-V) ainda nao esta pronto; usar legado para validacao de rede no Hyper-V.
- driver `tulip-2114x` precisa de hardening adicional de RX/link para cobertura total de adaptadores genericos.
- scheduler/multithread ainda nao entrou no kernel runtime.
- hardening criptografico de integridade por bloco/metadata ainda pendente.
- o kernel x64 ainda depende de `EFI ConIn` em parte dos cenarios UEFI, o que mantem o loader em modo hibrido e aumenta o acoplamento com firmware.

## Build e testes

### Dependencias

- WSL/Linux com: `make`, `nasm`, `xorriso`, `grub-mkrescue`
- Toolchains:
  - `x86_64-elf-*` (ou fallback `x86_64-linux-gnu-*`)
- UEFI: `gnu-efi`

Checagem rapida:

```bash
python3 tools/scripts/check_deps.py
```

### Fluxo UEFI/GPT (64-bit)

```bash
make all64
make iso-uefi
```

### Testes de host

```bash
make test
```

### Smoke automatizado x64 (QEMU + UEFI)

O repositorio inclui um smoke test de login + CLI para o kernel x64:

```bash
make smoke-x64-cli
```

Ele valida, em fluxo automatizado:
- boot pelo HDD provisionado
- login no boot 1
- comandos `help-any -help`, `mk-dir`, `go`, `mk-file`, `open`, `print-file`, `find`, `list`
- reboot, login no boot 2 e persistencia do arquivo criado

Script base:
- `tools/scripts/smoke_x64_cli.py`

### Auditoria de disco provisionado

Para validar GPT, ESP, particao BOOT raw e manifest de um disco instalado:

```bash
make inspect-disk IMG=build/disk-gpt.img
```

## Validacao recomendada apos mudancas

1. Build 64-bit e ISO UEFI.
2. Boot em VM UEFI (QEMU/Hyper-V Gen2) e validar:
- login
- comandos `list/go/mk-dir/mk-file/open/find`
- logout (`bye`)
- aliases (`help`, `clear`, `reboot`, `halt`)
3. Auditar a imagem GPT/ESP/BOOT e validar boot por HDD provisionado.

## Roadmap tecnico (macro)

A evolucao detalhada esta em:
- `docs/plans/mvp-implementation-plan.md`
- `docs/plans/system-roadmap.md`

Eixos principais:
- CAPYFS: journal, recuperacao, fsck, escalabilidade
- Rede: driver NIC, ARP/IPv4/ICMP/UDP/TCP e utilitarios CLI (`hey`)
- Criptografia: integridade autenticada, rotacao e hierarquia de chaves
- Performance: cache, I/O, NVMe tuning, operacoes em lote
- Seguranca: auditoria, ACL, hardening de parser e metadata
- Multiusuario: gestao de usuarios/grupos e isolamento de sessao
- CLI: historico, autocomplete, pipes e jobs
- Multithread: scheduler, workers e sincronizacao
- Futuro grafico: base de userspace + compositor para viabilizar navegador open source (Chromium/Servo/WebKit)

## Estrutura do repositorio

- `boot/`: artefatos e bootstrap legados em desuso
- `src/boot/`: loader UEFI e handoff
- `src/arch/x86/`: codigo legado 32-bit em desuso
- `src/arch/x86_64/`: kernel path 64-bit
- `src/core/`: kernel core, instalador, sessao, init
- `src/fs/`: CAPYFS, VFS, cache e wrappers de bloco
- `src/security/`: crypto/KDF/CSPRNG
- `src/shell/`: core do CapyCLI e comandos
- `src/drivers/`: video/input/storage/pci/usb/hyper-v
- `docs/`: documentacao organizada por dominio (`architecture/`, `setup/`,
  `testing/`, `reference/`, `plans/`, `releases/`, `archive/`)
- `tools/scripts/`: utilitarios de build/provisionamento/inspecao

## Gitflow sugerido

- Desenvolvimento em feature branches
- Merge para `develop`
- Promocao para `main` apos validacao de release
- Commits devem incluir:
  - alteracoes de codigo
  - atualizacao de docs afetadas
  - evidencias de build/teste
