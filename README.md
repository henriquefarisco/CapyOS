# CapyOS

<p align="center">
  <img src="assets/branding/icon.svg" alt="CapyOS symbol" width="180" />
</p>

Ultima atualizacao: 2026-05-01
Versao de referencia: `0.8.0-alpha.5`
Consolidacao atual de `develop`: M5 userland completo (fork/exec/wait/pipe + capysh ring 3 + isolamento de crash multi-processo) sobre a base de robustez ja entregue (DHCP automatico, gates endurecidos, metricas de performance, cache DNS/HTTP, journal CAPYFS autenticado, op_budget no navegador, API de privilegios, eventos de auditoria estruturados `[audit]`, SHA-256 de payload de update). Polish UX pos-M5: `clear` context-aware no terminal do desktop, task manager com auto-refresh e botao Kill funcional, browser com parser yield cooperativo e timeout duro de 30s. Roadmap consolidado em [`docs/plans/active/capyos-master-plan.md`](docs/plans/active/capyos-master-plan.md) (F1-F10 linear).

CapyOS e um sistema operacional hobby escrito em C/Assembly, com foco atual em:
- boot proprio `UEFI/GPT/x86_64`
- filesystem proprio (`CAPYFS`)
- volume persistente cifrado
- shell modular (`CapyCLI`)
- consolidacao da trilha x64 antes de GUI, apps e linguagem propria

## Compatibilidade atual

- caminho oficial de virtualizacao: `VMware` em modo `UEFI` com NIC `E1000`
- caminho de laboratorio adicional: `QEMU/OVMF` com `E1000`
- `VMXNET3` pode ser detectado, mas ainda nao e backend validado
- `Hyper-V` nao e trilha suportada neste momento

Diretriz pratica:
- para boot, instalacao, login, CLI e rede basica, use `VMware + E1000`
- nao trate `Hyper-V` como ambiente valido de release, smoke oficial ou promessa de compatibilidade

## Branches e canais

Modelo atual de entrega:
- `main`
  - trilha estavel do projeto
  - recebe apenas mudancas ja validadas em build, testes e smokes
- `develop`
  - trilha de integracao e desenvolvimento continuo
  - concentra o que esta pronto para consolidacao, mas ainda pode evoluir antes de release

Mapeamento com o sistema de updates:
- `stable` -> branch `main`
- `develop` -> branch `develop`

Diretriz pratica:
- trabalho novo entra por branch de feature
- consolidacao tecnica acontece primeiro em `develop`
- promocao para `main` acontece depois da validacao da trilha

## Licenca, autoria e uso

Este repositorio usa a licenca Open Source `Apache-2.0`.

- texto integral da licenca: `LICENSE`
- atribuicao oficial e desenvolvedor principal: `NOTICE`
- politica de branding do projeto: `BRANDING.md`
- declaracao de uso licito: `LAWFUL_USE.md`

Desenvolvedor principal: `Henrique Schwarz Souza Farisco`.

Observacao importante:
- para manter o repositorio como Open Source, a licenca nao inclui uma clausula geral de proibicao de uso ilegal
- essa diretriz aparece como posicionamento do projeto em `LAWFUL_USE.md`, sem alterar os termos da licenca

## Documentacao

- indice principal: `docs/README.md`
- indice de planos: `docs/plans/README.md`
- arquitetura atual: `docs/architecture/system-overview.md`
- plano-mestre do sistema: `docs/plans/active/system-master-plan.md`
- plano-mestre de consolidacao atual: `docs/plans/active/capyos-master-improvement-plan.md`
- plano de execucao atual: `docs/plans/active/system-execution-plan.md`
- roadmap tecnico por dominio: `docs/plans/active/system-roadmap.md`
- hardening de plataforma: `docs/plans/historical/platform-hardening-plan.md`
- validacao de boot/login/CLI: `docs/testing/boot-and-cli-validation.md`
- referencia de comandos: `docs/reference/cli-reference.md`
- guia Hyper-V historico/nao suportado: `docs/setup/hyper-v.md`
- release notes: `docs/releases/README.md`
- screenshots versionados: `docs/screenshots/README.md`

## Visao geral do projeto

O repositorio segue uma unica trilha oficial:

1. `UEFI/GPT/x86_64`
- loader UEFI funcional (`BOOTX64.EFI`)
- kernel x64 com framebuffer, shell e runtime de storage
- volume `DATA` cifrado com persistencia em disco
- login e `CapyCLI` persistentes no disco provisionado
- fluxo de instalacao por ISO UEFI e reboot para o disco provisionado

Codigo legado `BIOS/MBR 32-bit` pode ainda existir no repositorio como divida de migracao, mas nao faz parte do pipeline suportado de build, boot, instalacao ou release.

## O que o sistema oferece hoje

### 1. Boot, instalacao e imagens

- loader UEFI (`src/boot/uefi_loader.c`) com handoff para kernel x86_64
- provisionamento GPT/ESP/BOOT/DATA por script (`tools/scripts/provision_gpt.py`)
- instalador UEFI em evolucao dentro do loader para provisionamento direto em disco
- build de artefatos oficiais UEFI (`make all64`, `make iso-uefi`, `make disk-gpt`)

### 2. Kernel e runtime

- console framebuffer no x64 (`src/arch/x86_64/kernel_main.c`)
- console serial COM1 como fallback de depuracao
- deteccao de PCIe/NVMe e inicializacao de controladores suportados
- bootstrap inicial de rede no x64 com:
  - `e1000` funcional (RX/TX + ping/ICMP/TCP)
  - `tulip-2114x` em modo inicial/experimental
- pilha TCP/IP completa: ARP/IPv4/ICMP/UDP/TCP com checksum correto, RST handling, retransmissao SYN
- HTTP/HTTPS funcional via `net-fetch` (BearSSL TLS 1.2, segue redirects, exibe TLS e body preview)
- navegador HTML interno com barra de carregamento e indicador de progresso
- diagnostico de rede ampliado: `diag: arp=N syn-out=N syn-ack=N` em falhas
- estado de sessao com usuario autenticado, `cwd`, prompt dinamico e logout

### 3. Filesystem (CAPYFS) e VFS

- `CAPYFS` com superbloco, bitmap, inode e diretorios
- `VFS` com resolucao de caminhos absolutos/relativos e metadados
- buffer cache com sincronizacao explicita (`do-sync`)
- runtime x64 em disco provisionado:
  - volume `DATA` cifrado montado no boot
  - estrutura base persistente (`/bin`, `/docs`, `/etc`, `/home`, `/var/log`, etc.)
  - `users.db` reutilizado entre boots
- `ramdisk` permanece apenas como contingencia controlada quando nao existe caminho persistente valido

### 4. Criptografia e autenticacao

- camada cifrada por bloco no fluxo x64:
  - AES-XTS 256
  - PBKDF2-SHA256
- banco de usuarios em `/etc/users.db`:
  - salt por usuario
  - hash PBKDF2-SHA256 (`USER_ITERATIONS=64000`)
- setup inicial de formatacao com `Usuario administrador [admin]` persistido no `users.db`
- login com validacao por `userdb_authenticate`

### 5. CapyCLI (shell modular)

Conjuntos de comandos implementados:

- navegacao: `list`, `go`, `mypath`
- conteudo: `print-file`, `page`, `print-file-begin`, `print-file-end`, `open`, `print-echo`
- gerenciamento: `mk-file`, `mk-dir`, `kill-file`, `kill-dir`, `move`, `clone`, `stats-file`, `type`
- busca: `hunt-file`, `hunt-dir`, `hunt-any`, `find`
- sessao/ajuda/sistema: `help-any`, `help-docs`, `mess`, `bye`, `print-*`, `config-keyboard`, `shutdown-reboot`, `shutdown-off`, `do-sync`
- rede:
  - `net-status`, `net-refresh`, `net-dump-runtime`
  - `net-ip`, `net-gw`, `net-dns`
  - `net-set <ip> <mask> <gw> <dns>`, `net-mode [static|dhcp]`
  - `net-resolve <hostname>` (DNS lookup)
  - `hey <ip|hostname|gateway|dns|self>` (ICMP ping)
  - `net-fetch <url>` (HTTP/HTTPS GET, segue ate 5 redirects, exibe TLS, body preview)

Observacao sobre o x64:
- comandos antigos que estavam hardcoded no loop principal foram redirecionados para o modulo de shell
- aliases de compatibilidade: `help -> help-any`, `clear -> mess`, `reboot -> shutdown-reboot`, `halt -> shutdown-off`

### 6. Entrada e virtualizacao

Prioridade atual de entrada no x64:

1. `EFI ConIn` (principal durante boot hibrido em VMs UEFI)
2. PS/2
3. COM1 (ultimo fallback)

Impacto pratico:
- `VMware` e a trilha principal para validacao do sistema
- `E1000` e o backend recomendado de rede
- `VMXNET3` ainda nao deve ser usado como caminho principal
- `Hyper-V NetVSC/VMBus` nao e caminho suportado nesta fase
- COM1 permanece para debug e contingencia

## Screenshots da versao atual

Os prints oficiais agora ficam versionados em `docs/screenshots/<versao>/` para
acompanhar a evolucao visual do sistema por release alpha.

### Boot e login

![Login do sistema](docs/screenshots/0.8.0-alpha.5/login-system.png)

![Provisionamento/boot da ISO](docs/screenshots/0.8.0-alpha.5/bootstage1-iso.png)

### Desktop, navegador e apps

![Desktop com navegador](docs/screenshots/0.8.0-alpha.5/desktop-browser1.png)

![Desktop com apps](docs/screenshots/0.8.0-alpha.5/desktop-apps.png)

## Estado atual por dominio

| Dominio | Estado atual | Nivel |
|---|---|---|
| Boot BIOS/MBR | Descontinuado | Fora de suporte |
| Boot UEFI/GPT | Loader + kernel x64 com smoke em disco | Parcial |
| CAPYFS em disco cifrado | Ativo no x64 | Parcial |
| Login e sessao | Funcional no x64 com persistencia em disco | Parcial |
| CLI modular | Comandos principais ativos | Estavel |
| Rede x64 | TCP/IP corrigido; HTTP/HTTPS funcional (`net-fetch`); `e1000` validado | Parcial |
| VMware | Caminho principal atual para boot, setup, login e CLI | Parcial |
| Hyper-V | Backend experimental, sem suporte oficial de release | Fora de suporte |
| USB HID teclado x64 | Enumeracao XHCI ainda incompleta | Em desenvolvimento |
| Multithread/scheduler | Ainda nao implantado | Pendente |

## Lacunas importantes

- o caminho oficial via ISO UEFI deve ser validado em `VMware` com `E1000`
- driver USB HID completo (enumeracao + input) ainda nao finalizado
- `VMXNET3` ainda nao esta validado como backend de rede principal
- `Hyper-V` segue no repositorio apenas como trilha tecnica em investigacao
- driver `tulip-2114x` precisa de hardening adicional de RX/link
- scheduler/multithread ainda nao entrou no kernel runtime
- hardening criptografico de integridade por bloco/metadata ainda pendente
- o kernel x64 ainda depende de `EFI ConIn` em parte dos cenarios UEFI
- navegador HTML:
  - Fase 1 de estabilizacao fechada
  - ainda sem isolamento por processo, JS robusto ou render moderno amplo
  - paginas pesadas nao devem congelar o sistema inteiro, mas a compatibilidade web moderna continua parcial

## Build e testes

### Dependencias

- WSL/Linux com: `make`, `nasm`, `xorriso`, `grub-mkrescue`
- toolchains:
  - `x86_64-elf-*` para build oficial endurecido
  - `x86_64-linux-gnu-*` apenas como build rapido de desenvolvimento
- UEFI: `gnu-efi`

Checagem rapida:

```bash
python3 tools/scripts/check_deps.py
```

Ou pelo alvo do projeto:

```bash
make check-toolchain
```

Observacao de seguranca:
- `CAPYCFG.BIN` com chave de volume em claro e permitido apenas em laboratorio e nos smokes automatizados; o provisionamento exige `--allow-plain-volume-key`

### Fluxo UEFI/GPT (64-bit)

Build oficial endurecido:

```bash
make all64 TOOLCHAIN64=elf
```

Build rapido de desenvolvimento:

```bash
make all64
```

Imagem UEFI:

```bash
make iso-uefi
```

Gate local de release robusta:

```bash
make release-check
```

Esse alvo roda `check-toolchain` com `TOOLCHAIN64=elf`, testes de host,
auditoria estrita de layout, auditoria de versao, build x64 endurecido,
geracao da ISO UEFI e verificacao de checksums em
`build/release-artifacts.sha256`.

### Testes de host

```bash
make test
```

### Smokes automatizados x64

```bash
make smoke-x64-cli
make smoke-x64-iso
```

Eles validam, em fluxo automatizado:
- boot pelo HDD provisionado
- instalacao pela ISO oficial, reboot pelo HDD, login e persistencia
- comandos principais do CapyCLI
- persistencia do arquivo criado entre boots

Scripts base:
- `tools/scripts/smoke_x64_cli.py`
- `tools/scripts/smoke_x64_iso_install.py`

### Auditoria de disco provisionado

Para validar GPT, ESP, particao BOOT raw e manifest de um disco instalado:

```bash
make inspect-disk IMG=build/disk-gpt.img
```

## Validacao recomendada apos mudancas

1. Build 64-bit e ISO UEFI.
2. Rodar `make smoke-x64-iso` para validar o artefato oficial de instalacao.
3. Boot em `VMware` UEFI com `E1000` e validar:
- login
- comandos `list/go/mk-dir/mk-file/open/find`
- logout (`bye`)
- aliases (`help`, `clear`, `reboot`, `halt`)
4. Auditar a imagem GPT/ESP/BOOT e validar boot por HDD provisionado.

## Roadmap tecnico (macro)

A evolucao detalhada esta em:
- `docs/plans/historical/mvp-implementation-plan.md`
- `docs/plans/active/system-roadmap.md`
- `docs/plans/active/system-master-plan.md`

Eixos principais:
- CAPYFS: journal, recuperacao, fsck, escalabilidade
- Rede: driver NIC, sockets, DNS, TLS e utilitarios CLI
- Criptografia: integridade autenticada, rotacao e hierarquia de chaves
- Performance: cache, I/O, NVMe tuning, operacoes em lote
- Seguranca: auditoria, ACL, parser hardening, metadata integrity
- Multiusuario: gestao de usuarios/grupos e isolamento de sessao
- CLI: historico, autocomplete, pipes e jobs
- Multithread: scheduler, workers e sincronizacao
- GUI: mouse, compositor, janelas e toolkit
- Distribuicao: pacote, atualizacao automatica assinada e rollback
- Plataforma: ABI, userland, SDK e linguagem propria
