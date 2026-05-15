# Visao tecnica do projeto

Este documento concentra as informacoes tecnicas que antes ficavam no README
principal. A capa do repositorio deve permanecer curta; detalhes de estado,
compatibilidade, validacao e roadmap ficam aqui e nos documentos especializados
em `docs/`.

## Escopo atual

O CapyOS segue uma trilha oficial:

- `UEFI/GPT/x86_64`
- loader UEFI funcional (`BOOTX64.EFI`)
- kernel x64 com framebuffer, GUI, shell e runtime de storage
- volume `DATA` cifrado com persistencia em disco
- login e `CapyCLI` persistentes no disco provisionado
- instalacao por ISO UEFI e reboot para o disco provisionado

Codigo legado `BIOS/MBR 32-bit` pode existir no repositorio como divida de
migracao, mas nao faz parte do pipeline suportado de build, boot, instalacao ou
release.

## Compatibilidade

| Ambiente | Estado |
|---|---|
| VMware UEFI + E1000 | trilha principal de validacao |
| QEMU/OVMF + E1000 | laboratorio e smoke automatizado |
| VMXNET3 | detectavel, ainda nao validado como backend principal |
| Hyper-V | investigacao, fora de suporte oficial |

Diretriz pratica: para boot, instalacao, login, CLI e rede basica, use
`VMware + UEFI + E1000`. Nao trate Hyper-V como ambiente valido de release,
smoke oficial ou promessa de compatibilidade.

## Funcionalidades atuais

### Boot e instalacao

- loader UEFI com handoff para kernel x86_64
- provisionamento GPT/ESP/BOOT/DATA por scripts
- build de artefatos oficiais UEFI por `make all64`, `make iso-uefi` e
  `make disk-gpt`
- instalador UEFI em evolucao para provisionamento direto em disco

### Kernel e runtime

- console framebuffer x64 e serial COM1 para debug
- deteccao de PCIe/NVMe e controladores suportados
- taskbar, desktop, compositor, janelas e apps basicos
- estado de sessao com usuario autenticado, `cwd`, prompt dinamico e logout

### Filesystem e persistencia

- `CAPYFS` com superbloco, bitmap, inode e diretorios
- `VFS` com resolucao de caminhos e metadados
- buffer cache com sincronizacao explicita por `do-sync`
- volume persistente montado no boot com estrutura base em `/bin`, `/docs`,
  `/etc`, `/home` e `/var/log`

### Criptografia e autenticacao

- AES-XTS para volume cifrado
- PBKDF2-SHA256 e Argon2id em componentes de seguranca
- banco de usuarios em `/etc/users.db`
- salts por usuario e politicas de autenticacao em evolucao
- componentes adicionais: SHA-256/SHA-512, BLAKE2b, ChaCha20-Poly1305,
  X25519, Ed25519 e infraestrutura de release signing

### Rede

- driver `E1000` validado na trilha principal
- ARP, IPv4, ICMP, UDP, TCP e DNS
- HTTP/HTTPS via `net-fetch`
- diagnosticos por `net-status`, `net-refresh`, `net-dump-runtime`,
  `net-ip`, `net-gw`, `net-dns`, `net-resolve` e `hey`

### CapyCLI

Conjuntos principais:

- navegacao: `list`, `go`, `mypath`
- conteudo: `print-file`, `page`, `open`, `print-echo`
- gerenciamento: `mk-file`, `mk-dir`, `kill-file`, `kill-dir`, `move`,
  `clone`, `stats-file`, `type`
- busca: `hunt-file`, `hunt-dir`, `hunt-any`, `find`
- sessao e sistema: `help-any`, `help-docs`, `mess`, `bye`,
  `shutdown-reboot`, `shutdown-off`, `do-sync`
- rede: `net-status`, `net-ip`, `net-gw`, `net-dns`, `net-set`, `net-mode`,
  `net-resolve`, `hey`, `net-fetch`

Aliases de compatibilidade:

- `help` -> `help-any`
- `clear` -> `mess`
- `reboot` -> `shutdown-reboot`
- `halt` -> `shutdown-off`

## Estado por dominio

| Dominio | Estado atual | Nivel |
|---|---|---|
| Boot UEFI/GPT | loader + kernel x64 com smoke em disco | parcial |
| CAPYFS em disco cifrado | ativo no x64 | parcial |
| Login e sessao | funcional no x64 com persistencia | parcial |
| CLI modular | comandos principais ativos | estavel |
| Rede x64 | `E1000`, TCP/IP, DNS e HTTP/HTTPS | parcial |
| VMware | caminho principal de validacao | parcial |
| Hyper-V | investigacao historica | fora de suporte |
| USB HID teclado x64 | enumeracao XHCI incompleta | em desenvolvimento |
| Multithread/scheduler | ainda nao implantado | pendente |

## Lacunas conhecidas

- validar continuamente o caminho oficial ISO UEFI em VMware com `E1000`
- finalizar driver USB HID completo
- endurecer `VMXNET3` antes de promove-lo a backend suportado
- manter Hyper-V fora da promessa de release ate estabilizacao real
- melhorar driver `tulip-2114x`
- implantar scheduler/multithread no runtime de kernel
- evoluir integridade autenticada por bloco/metadata
- reduzir dependencia de `EFI ConIn` em cenarios UEFI

## Validacao

Gates locais recomendados:

```bash
make test
make layout-audit
make version-audit
make boot-perf-baseline-selftest
make all64
make iso-uefi
make verify-release-checksums TOOLCHAIN64=host
make smoke-x64-iso TOOLCHAIN64=host
```

Smokes VMware oficiais exigem argumentos do ambiente real:

```bash
make smoke-x64-vmware-mouse-events SMOKE_X64_VMWARE_ARGS="..."
```

## Roadmap macro

A fonte de verdade do roadmap fica em
`docs/plans/active/capyos-master-plan.md` e o status executivo em
`docs/plans/STATUS.md`.

Eixos principais:

- CAPYFS: journal, recuperacao, fsck e escalabilidade
- Rede: drivers, sockets, DNS, TLS e utilitarios
- Criptografia: integridade autenticada, rotacao e hierarquia de chaves
- Performance: cache, I/O, NVMe tuning e operacoes em lote
- Seguranca: auditoria, ACL, parser hardening e metadata integrity
- Multiusuario: usuarios, grupos e isolamento de sessao
- CLI: historico, autocomplete, pipes e jobs
- Multithread: scheduler, workers e sincronizacao
- GUI: mouse, compositor, janelas e toolkit
- Distribuicao: pacote, update assinado e rollback
- Plataforma: ABI, userland, SDK e linguagem propria
