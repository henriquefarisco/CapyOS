# NoirOS

NoirOS e um sistema operacional hobby, escrito em C e Assembly, com duas linhas
de evolucao em paralelo:

1. Fluxo BIOS/MBR 32-bit: caminho mais completo hoje (instalador, NoirFS,
   login, multiusuario basico e NoirCLI).
2. Fluxo UEFI/GPT 64-bit: bring-up em progresso (loader UEFI, kernel x86_64
   inicial, deteccao de NVMe e console framebuffer/serial).

Este README foi atualizado para refletir o estado atual do codigo no repositorio.

## O que o sistema oferece hoje

### 1. Boot e instalacao
- Instalador dedicado NGIS (`src/core/installer_main.c`) para BIOS/MBR.
- Escrita automatica de `stage1` + `stage2` + `manifest` + kernel no disco
  via `bootwriter_install_fresh`.
- Estrutura padrao de particoes no fluxo BIOS:
  - `sda1`: BOOT (tipo `0xDA`, contem payloads de boot)
  - `sda2`: DATA (NoirFS cifrado)
- Fluxo UEFI/GPT em paralelo com:
  - `BOOTX64.EFI` (`src/boot/uefi_loader.c`)
  - suporte a leitura de manifest em particao BOOT GPT
  - fallback para arquivos na ESP (`/boot/noiros64.bin`, `manifest.bin`)

### 2. Filesystem e armazenamento
- NoirFS nativo (`src/fs/noirfs/noirfs.c`) com:
  - superbloco, bitmap de blocos e inodes
  - inodes com ponteiros diretos + indireto simples
  - diretorios com entradas fixas (`NOIRFS_NAME_MAX = 32`)
- VFS com metadata de UID/GID/permissoes e resolucao de caminhos.
- Buffer cache de blocos e `do-sync` para flush explicito.
- Wrappers de dispositivo para offset e chunking (512B -> 4096B).

### 3. Criptografia e autenticacao
- Camada de bloco cifrada com AES-XTS 256 + PBKDF2-SHA256.
- Montagem do volume exige senha do NoirFS no boot 32-bit.
- Banco de usuarios em `/etc/users.db` com:
  - salt aleatorio por usuario (CSPRNG)
  - hash PBKDF2-SHA256 (`USER_ITERATIONS = 64000`)
- Fluxo de login em terminal com sessao e `cwd` isolado por usuario.

### 4. Multiusuario e permissao
- Sessao com `uid`, `gid`, `home`, `role`, `cwd`.
- Verificacao de permissao por owner/group/others no VFS.
- Setup inicial cria estrutura base (`/home`, `/etc`, `/var/log`, etc.),
  registra admin e gera `config.ini`.

### 5. CLI (NoirCLI)
- Arquitetura modular por conjuntos de comandos:
  - navegacao: `list`, `go`, `mypath`
  - conteudo: `print-file`, `page`, `print-file-begin`, `print-file-end`,
    `open`, `print-echo`
  - gerenciamento: `mk-file`, `mk-dir`, `kill-file`, `kill-dir`, `move`,
    `clone`, `stats-file`, `type`
  - busca: `hunt-file`, `hunt-dir`, `hunt-any`, `find`
  - sessao/ajuda/sistema: `help-any`, `help-docs`, `mess`, `bye`,
    `print-me`, `print-id`, `print-host`, `print-version`, `print-time`,
    `print-insomnia`, `print-envs`, `config-keyboard`, `shutdown-reboot`,
    `shutdown-off`, `do-sync`

### 6. Caminho x86_64 (estado atual)
- Kernel 64-bit inicial (`src/arch/x86_64/kernel_main.c`) com:
  - console framebuffer
  - fallback de input serial COM1
  - tentativas de input PS/2 e Hyper-V
  - scan PCI + inicializacao NVMe basica
  - deteccao XHCI inicial (enumeracao USB ainda pendente)
- Este caminho ainda e experimental e nao substitui o fluxo 32-bit completo.

## Estado atual por trilha

### Trilha A: BIOS/MBR 32-bit (mais completa)
- Boot stage1/stage2 funcionando.
- Instalacao e particionamento via NGIS.
- NoirFS cifrado montado em runtime.
- Login + sessao + NoirCLI funcional.

### Trilha B: UEFI/GPT 64-bit (em evolucao)
- ISO UEFI e loader (`make iso-uefi`) funcionais para bring-up.
- Instalador UEFI no loader (modo detectado por marcador/read-only).
- Kernel 64-bit ainda em fase de consolidacao de drivers, storage e auth.

## Build e execucao

### Dependencias
- Linux/WSL com `make`, `nasm`, `xorriso`, `grub-mkrescue`
- Toolchains:
  - 32-bit: `i686-elf-gcc` (ou fallback `gcc -m32`)
  - 64-bit: `x86_64-elf-*` (ou fallback `x86_64-linux-gnu-*`)
- UEFI: `gnu-efi` (headers e linker script)

Checagem rapida:

```bash
python3 tools/scripts/check_deps.py
```

### Build 32-bit (fluxo BIOS/MBR)

```bash
make clean
make
make run
```

Com disco persistente:

```bash
make disk-img
make run-disk
```

ISO do instalador BIOS:

```bash
make iso
make run-installer-iso
```

### Build 64-bit (fluxo UEFI/GPT)

```bash
make all64
make iso-uefi
make disk-gpt
```

Provisionamento de VHD existente:

```bash
make provision-vhd IMG=/caminho/para/NoirOSGenII.vhd
```

### Testes

```bash
make test
```

Os testes de host cobrem wrappers de bloco, parser MBR, boot writer, manifest,
layout de teclado, CSPRNG e gerador de `grub.cfg`.

## Estrutura do repositorio

- `boot/`: stage1/stage2 e bootstrap legado.
- `src/core/`: kernel 32-bit principal, instalador, sessao, init.
- `src/arch/x86/`: caminho 32-bit (GDT/IDT/ISR, linker, entry).
- `src/arch/x86_64/`: caminho 64-bit (entry, kernel bring-up, stubs).
- `src/boot/`: loader UEFI, manifest e escrita de payloads.
- `src/fs/`: NoirFS, VFS, cache, block wrappers.
- `src/security/`: crypto, KDF e CSPRNG.
- `src/shell/`: core do NoirCLI e comandos.
- `src/drivers/`: VGA, teclado, ATA, PCIe, NVMe, USB, Hyper-V.
- `tools/scripts/`: provisionamento GPT, manifest, inspeccao de disco.
- `docs/`: arquitetura, setup Hyper-V, referencia CLI e releases.

## Limitacoes atuais importantes

- O caminho 64-bit ainda nao reproduz todo o fluxo de login/NoirFS do 32-bit.
- Driver USB XHCI ainda nao enumera teclado HID fim-a-fim.
- Teclado sintetico Hyper-V (VMBus) esta com partes desativadas por watchdog.
- Criptografia de bloco atual protege confidencialidade, mas ainda sem
  autenticacao forte por bloco/metadata (roadmap abaixo).
- `include/core/version.h` e `VERSION.yaml` podem divergir em algumas branches;
  para release, usar `VERSION.yaml` como referencia oficial do canal.

## Documentacao complementar

- `docs/architecture.md`: mapa tecnico atualizado do boot/runtime.
- `docs/noiros-cli-reference.md`: referencia de comandos da CLI.
- `docs/HYPERV_SETUP.md`: fluxo recomendado para Hyper-V.
- `docs/system-roadmap.md`: roadmap detalhado de melhorias (NFS, crypto,
  performance, seguranca, multiusuario, CLI, multithread).
- `docs/releases/`: notas por versao.

## Proximos passos

O planejamento detalhado esta em `docs/system-roadmap.md`, com prioridades
separadas por dominio tecnico.
