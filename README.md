# NoirOS

Ultima atualizacao: 2026-02-10

NoirOS e um sistema operacional hobby escrito em C/Assembly, com foco em:
- boot proprio (BIOS e UEFI)
- filesystem proprio (NFS/NoirFS)
- seguranca por criptografia no volume de dados
- shell modular (NoirCLI)
- evolucao gradual de 32-bit para 64-bit

## Visao geral do projeto

O repositorio mantem duas trilhas em paralelo:

1. `BIOS/MBR 32-bit` (trilha mais madura)
- instalador funcional
- particionamento e bootstrap completos
- NoirFS cifrado em disco
- login, sessao e NoirCLI em uso real

2. `UEFI/GPT 64-bit` (trilha em consolidacao)
- loader UEFI funcional (`BOOTX64.EFI`)
- kernel x86_64 com framebuffer, PCIe/NVMe e shell
- bootstrap de NoirFS em RAM para fluxo de login + CLI
- teclado em VM UEFI priorizando `EFI ConIn` (sem exigir COM)

## O que o sistema oferece hoje

### 1. Boot, instalacao e imagens

- Instalador NGIS para fluxo BIOS/MBR (`src/core/installer_main.c`).
- Escrita de boot payloads (`stage1`, `stage2`, manifest, kernel) via boot writer.
- Loader UEFI (`src/boot/uefi_loader.c`) com handoff para kernel x86_64.
- Build de ISO BIOS (`make iso`) e ISO UEFI (`make iso-uefi`).

### 2. Kernel e runtime

- Console framebuffer no x64 (`src/arch/x86_64/kernel_main.c`).
- Console/telemetria serial COM1 como fallback de depuracao.
- Deteccao de PCIe/NVMe e inicializacao de controladores suportados.
- Estado de sessao com usuario autenticado, `cwd`, prompt dinamico e logout.

### 3. Filesystem (NFS/NoirFS) e VFS

- NoirFS com superbloco, bitmap, inode e diretorios.
- VFS com resolucao de caminhos absolutos/relativos e metadados.
- Buffer cache com sincronizacao explicita (`do-sync`).
- Bootstrap x64 em RAM:
  - `ramdisk` + formatacao NoirFS
  - estrutura base (`/bin`, `/docs`, `/etc`, `/home`, `/var/log`, etc.)
  - `users.db` inicializado com usuario padrao quando necessario

### 4. Criptografia e autenticacao

- Camada cifrada por bloco no fluxo 32-bit:
  - AES-XTS 256
  - PBKDF2-SHA256
- Banco de usuarios em `/etc/users.db`:
  - salt por usuario
  - hash PBKDF2-SHA256 (`USER_ITERATIONS=64000`)
- Login com validacao por `userdb_authenticate`.

### 5. NoirCLI (shell modular)

Conjuntos de comandos implementados:

- Navegacao: `list`, `go`, `mypath`
- Conteudo: `print-file`, `page`, `print-file-begin`, `print-file-end`, `open`, `print-echo`
- Gerenciamento: `mk-file`, `mk-dir`, `kill-file`, `kill-dir`, `move`, `clone`, `stats-file`, `type`
- Busca: `hunt-file`, `hunt-dir`, `hunt-any`, `find`
- Sessao/ajuda/sistema: `help-any`, `help-docs`, `mess`, `bye`, `print-*`, `config-keyboard`, `shutdown-reboot`, `shutdown-off`, `do-sync`

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

## Estado atual por dominio (resumo)

| Dominio | Estado atual | Nivel |
|---|---|---|
| Boot BIOS/MBR | Instalador e fluxo completo | Estavel |
| Boot UEFI/GPT | Loader + kernel x64 em evolucao | Parcial |
| NoirFS em disco cifrado | Pronto no 32-bit | Estavel |
| NoirFS no x64 | Bootstrap em RAM (sem persistencia final) | Parcial |
| Login e sessao | Funcional em 32-bit e x64 (x64 usa base em RAM) | Parcial |
| CLI modular | Comandos principais ativos | Estavel |
| Teclado em Hyper-V | Via EFI ConIn sem exigir COM (x64) | Parcial |
| USB HID teclado x64 | Enumeracao XHCI ainda incompleta | Em desenvolvimento |
| Multithread/scheduler | Ainda nao implantado | Pendente |

## Lacunas importantes

- x64 ainda nao concluiu paridade total com fluxo 32-bit em persistencia real de disco cifrado.
- driver USB HID completo (enumeracao + input) ainda nao finalizado.
- caminho VMBus keyboard continua experimental e depende de hardening.
- scheduler/multithread ainda nao entrou no kernel runtime.
- hardening criptografico de integridade por bloco/metadata ainda pendente.

## Build e testes

### Dependencias

- WSL/Linux com: `make`, `nasm`, `xorriso`, `grub-mkrescue`
- Toolchains:
  - `i686-elf-gcc` (ou fallback host `gcc -m32`)
  - `x86_64-elf-*` (ou fallback `x86_64-linux-gnu-*`)
- UEFI: `gnu-efi`

Checagem rapida:

```bash
python3 tools/scripts/check_deps.py
```

### Fluxo BIOS/MBR (32-bit)

```bash
make clean
make
make iso
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
- login (`admin/admin`)
- comandos `help-any -help`, `mk-dir`, `go`, `mk-file`, `open`, `print-file`, `find`, `list`
- logout (`bye`) retornando para o prompt de login

Script base:
- `tools/scripts/smoke_x64_cli.py`

## Validacao recomendada apos mudancas

1. Build 64-bit e ISO UEFI.
2. Boot em VM UEFI (QEMU/Hyper-V Gen2) e validar:
- login
- comandos `list/go/mk-dir/mk-file/open/find`
- logout (`bye`)
- aliases (`help`, `clear`, `reboot`, `halt`)
3. Build 32-bit e ISO BIOS para garantir ausencia de regressao cruzada.

## Roadmap tecnico (macro)

A evolucao detalhada esta em:
- `docs/mvp-implantation-plan.md`
- `docs/system-roadmap.md`

Eixos principais:
- NFS/NoirFS: journal, recuperacao, fsck, escalabilidade
- Criptografia: integridade autenticada, rotacao e hierarquia de chaves
- Performance: cache, I/O, NVMe tuning, operacoes em lote
- Seguranca: auditoria, ACL, hardening de parser e metadata
- Multiusuario: gestao de usuarios/grupos e isolamento de sessao
- CLI: historico, autocomplete, pipes e jobs
- Multithread: scheduler, workers e sincronizacao

## Estrutura do repositorio

- `boot/`: stage1/stage2 e bootstrap legado
- `src/boot/`: loader UEFI e handoff
- `src/arch/x86/`: kernel path 32-bit
- `src/arch/x86_64/`: kernel path 64-bit
- `src/core/`: kernel core, instalador, sessao, init
- `src/fs/`: NoirFS, VFS, cache e wrappers de bloco
- `src/security/`: crypto/KDF/CSPRNG
- `src/shell/`: core do NoirCLI e comandos
- `src/drivers/`: video/input/storage/pci/usb/hyper-v
- `docs/`: arquitetura, setup, roadmap e referencia de comandos
- `tools/scripts/`: utilitarios de build/provisionamento/inspecao

## Gitflow sugerido

- Desenvolvimento em feature branches
- Merge para `develop`
- Promocao para `main` apos validacao de release
- Commits devem incluir:
  - alteracoes de codigo
  - atualizacao de docs afetadas
  - evidencias de build/teste
