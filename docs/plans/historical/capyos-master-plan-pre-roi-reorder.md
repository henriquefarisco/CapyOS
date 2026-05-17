# CapyOS — Master Plan pre-ROI-reorder (snapshot 2026-05-15)

**Status:** Histórico. Este documento preserva a sequência das Etapas 3-15 do master plan **antes** da reorganização por ROI ao usuário desktop comum aplicada em `2026-05-15` sobre `0.8.0-alpha.237+20260514`.

Snapshot capturado quando a Etapa 2 (sessão gráfica operacional) estava em validação externa. Etapas 1-2 não foram alteradas pela reorganização; apenas as Etapas 3-15 mudaram de ordem/escopo conforme `docs/plans/active/capyos-master-plan.md` atual.

A versão ativa da história alpha por alpha permanece no master plan vigente em `active/capyos-master-plan.md` — este histórico foca somente na sequência abandonada de etapas 3-15 e nos critérios de aceite antigos.

## Sequência antiga das Etapas 3-15

| Etapa | Tema | Estado em 2026-05-15 | Dependia de | Saída para 100% |
|---|---|---|---|---|
| 3 | CapyDisplay 2D | Bloqueada até validação externa da Etapa 2 | Etapa 2 | camada 2D com damage, double buffer e primitives |
| 4 | Driver framework + VM drivers | Bloqueada | Etapa 3 | drivers previsíveis para VM, storage, USB e vídeo inicial |
| 5 | TLS userland real | Bloqueada | Etapa 4 | BearSSL userland com handshake real validado |
| 6 | Release/update gate oficial | Bloqueada | Etapa 5 | smoke VMware+E1000 e update HTTPS oficiais |
| 7 | Apps básicos do desktop | Bloqueada | Etapa 6 | apps essenciais ring-3 usáveis sem CLI |
| 8 | Package manager + SDK + ABI estável | Bloqueada | Etapa 7 | ecossistema instalável e ABI documentada |
| 9 | CapyLX L0-L2: CLI estático | Bloqueada | Etapa 8 | binários Linux estáticos simples rodam |
| 10 | CapyLX L3-L5: POSIX amplo | Bloqueada | Etapa 9 | threads, sinais, rede e bundles dinâmicos |
| 11 | Wayland bridge | Bloqueada | Etapa 10 | apps Linux GUI via Wayland mínimo |
| 12 | Mesa/Vulkan path | Bloqueada | Etapa 11 | lavapipe/virgl/Venus como rota gráfica aberta |
| 13 | JS engine sandboxed | Bloqueada | Etapa 12 | JavaScript isolado no browser/runtime web |
| 14 | CapyLang | Bloqueada | Etapa 13 | linguagem própria para automação e apps |
| 15 | Plataforma 1.0 hardening | Bloqueada | Etapa 14 | Secure Boot, SMP, firewall, multimídia e polish final |

## Etapa 3 antiga — CapyDisplay 2D

**Objetivo:** criar uma camada gráfica própria antes de drivers complexos.

### Entregáveis

- Abstração de displays, modes, framebuffers, planes e cursor.
- Double buffering, clipping, damage tracking e blits otimizados.
- Cache de glyph/fontes e primitives 2D estáveis.
- API interna para compositor não depender diretamente do framebuffer bruto.

### Critérios de aceite

- [ ] Compositor redesenha somente regiões danificadas quando possível.
- [ ] Cursor e texto não piscam sob resize/move de janela.
- [ ] Fallback framebuffer continua funcionando.

## Etapa 4 antiga — Driver framework + VM drivers

**Objetivo:** aumentar compatibilidade e preparar vídeo/storage/rede mais reais.

### Entregáveis

- Device manager com enumeração PCI/PCIe, IRQ, MSI/MSI-X, DMA API e logs.
- VirtIO-net, VirtIO-block e VirtIO-gpu como prioridade de VM.
- VMware SVGA II para desenvolvimento oficial.
- AHCI, NVMe básico, USB HID e USB mass storage.
- Política de recoverability para falhas de driver.

### Critérios de aceite

- [ ] VM oficial sobe com storage/rede/vídeo previsíveis.
- [ ] Falha de driver não derruba o kernel sem diagnóstico.
- [ ] Driver framework documenta ownership, DMA e teardown.

## Etapa 5 antiga — TLS userland real

**Objetivo:** avançar `libcapy-tls` de metadata-only para handshake real.

### Entregáveis

- Adaptador BearSSL inicializa engine somente após todos os gates passarem.
- Trust anchors deixam de ser somente metadata e passam por parse seguro.
- Buffers/contexto BearSSL têm ownership e zeroização explícitos.
- Handshake TLS cliente com SNI, hostname verification e timeout.
- Smoke local `tls-handshake` contra servidor controlado.

### Critérios de aceite

- [ ] Erro em qualquer gate mantém fail-closed.
- [ ] HTTPS em `libcapy-net` deixa de retornar unsupported para caso válido.
- [ ] Certificado inválido falha fechado.

## Etapa 6 antiga — Release/update gate oficial

**Objetivo:** fechar a release operacional com CI/smoke oficial e update real.

### Entregáveis

- Chave Ed25519 offline oficial publicada como chave esperada.
- CI executa smoke VMware+E1000 com DHCP/DNS/HTTP/HTTPS.
- `update-fetch` e payload HTTPS passam em ambiente controlado.
- Release gate promove artefatos somente com evidência pública válida.

### Critérios de aceite

- [ ] Smoke VMware+E1000 real passa em CI provisionada.
- [ ] Update HTTPS baixa, valida, prepara e aplica payload assinado.
- [ ] Evidência pública permite auditoria sem chave privada.

## Etapa 7 antiga — Apps básicos do desktop

**Objetivo:** tornar o sistema utilizável sem CLI.

### Entregáveis

- File Manager, Text Editor, Settings, Image Viewer, Calculator e Log Viewer.
- Toolkit `libcapy-ui` inicial com button, list, textbox e dialog.
- Ícones oficiais e integração com launcher/taskbar.

### Critérios de aceite

- [ ] Cada app abre, executa função primária e fecha sem crash.
- [ ] Falha de um app não derruba desktop.
- [ ] Apps usam o tema da Etapa 1.

## Etapa 8 antiga — Package manager + SDK + ABI estável

**Objetivo:** permitir apps fora da imagem base.

### Entregáveis

- Formato `.capypkg` com manifest, assinatura e rollback.
- `pkgd`, CLI `pkg` e app Software Center.
- ABI estável documentada.
- SDK headers, samples e guia de build.

### Critérios de aceite

- [ ] Instalar, listar, atualizar e remover pacote sobrevive reboot.
- [ ] ABI pública tem versionamento e política de compatibilidade.

## Etapa 9 antiga — CapyLX L0-L2: Linux ABI CLI estático

**Objetivo:** iniciar compatibilidade Linux estilo WSL1, sem kernel Linux.

### Entregáveis

- `linux_personality` por processo.
- Loader ELF64 Linux com stack `argc/argv/envp/auxv`.
- Dispatcher Linux syscall auditável.
- Syscalls mínimas: read/write/openat/close/fstat/lseek/mmap/munmap/brk, exit/exit_group/getpid/clock_gettime/uname/getrandom.
- `/dev`, `/proc`, `/tmp` e `/etc` mínimos.

### Critérios de aceite

- [ ] Binário Linux estático simples executa e retorna código correto.
- [ ] Syscall desconhecida retorna `-ENOSYS` de forma previsível.
- [ ] Processo CapyLX não acessa APIs internas fora da tradução.

## Etapa 10 antiga — CapyLX L3-L5: POSIX amplo

**Objetivo:** ampliar CapyLX para ferramentas Linux reais.

### Entregáveis

- `clone`, `futex`, sinais, `wait4`, `execve`, `pipe2`, `dup3`, poll/epoll.
- Sockets Linux ABI traduzidos para a rede CapyOS.
- App bundles com bibliotecas empacotadas.
- Rootfs Linux-like opcional e isolado.

### Critérios de aceite

- [ ] Ferramentas CLI Linux dinâmicas simples rodam em app bundle.
- [ ] Threads/futex funcionam para libc/pthread comum.
- [ ] CapyLX permanece módulo de compatibilidade, não base do sistema.

## Etapa 11 antiga — Wayland bridge

**Objetivo:** rodar apps gráficos Linux modernos sem X11 inicial.

### Entregáveis

- Servidor/proxy Wayland mínimo: `wl_compositor`, `wl_shm`, input e `xdg_shell` básico.
- Ponte entre Wayland surfaces e compositor CapyOS.
- Clipboard e resize/focus básicos.

### Critérios de aceite

- [ ] App Wayland simples abre janela, recebe input e fecha.
- [ ] Falha do app Linux não derruba compositor.

## Etapa 12 antiga — Mesa/Vulkan path

**Objetivo:** criar rota gráfica open source sem prometer Vulkan nativo cedo.

### Entregáveis

- Mesa software/lavapipe para Vulkan software inicial.
- VirGL/Venus sobre VirtIO-gpu quando disponível.
- Política clara: Vulkan real exige driver/memory manager/sync antes.

### Critérios de aceite

- [ ] Demo gráfica software roda via caminho Mesa controlado.
- [ ] Fallback 2D continua estável quando aceleração indisponível.

## Etapa 13 antiga — JS engine sandboxed

**Objetivo:** habilitar web dinâmica sem comprometer isolamento.

### Entregáveis

- Decisão QuickJS vs CapyJS subset.
- Bridge DOM controlada e budget de execução.
- Sem syscalls diretas a partir do script.

### Critérios de aceite

- [ ] Script básico altera título/DOM permitido.
- [ ] Loop infinito é interrompido por budget.

## Etapa 14 antiga — CapyLang

**Objetivo:** linguagem própria para automação e apps CapyOS.

### Entregáveis

- Parser, VM bytecode e `.capyscript`.
- Bindings FS/config/shell seguros.
- Módulos, FFI controlada, formatter e LSP em ondas posteriores.

### Critérios de aceite

- [ ] Script de automação roda em ring 3.
- [ ] Bindings respeitam permissões do usuário.

## Etapa 15 antiga — Plataforma 1.0 hardening

**Objetivo:** consolidar CapyOS como sistema sólido para uso contínuo.

### Entregáveis

- Secure Boot e measured boot.
- SMP/multicore.
- Firewall mínimo.
- USB completo, áudio, multimídia, suíte Office/IDE e polish final.

### Critérios de aceite

- [ ] Plataforma tem boot/update/rollback/GUI/apps/compatibilidade auditáveis.
- [ ] Segurança e performance têm gates regressivos documentados.

## Motivos da substituição

A reorganização aplicada em `2026-05-15` reordenou e expandiu as Etapas 3-15 conforme o plano `docs/plans/historical/` ou a entrada original em `~/.windsurf/plans/capyos-roi-reorder-desktop-49ac80.md`. Resumo das mudanças principais:

- Drivers/USB HID antecipados (eram Etapa 4, viraram Etapa 3).
- Scheduler/multithread incorporado à Etapa 4 nova (junto com CapyDisplay 2D), pois estava listado como lacuna em `project-overview.md` mas fora do plano.
- Apps básicos antecipados para Etapa 6 nova (eram 7) com Image Viewer, Notes/Calendar e Media Player simples adicionados, e localização PT-BR/ES como meta.
- Browser usável virou Etapa 7 nova explícita (antes só implícito via Etapa 5 + Etapa 13).
- Release/update + instalador wizard fundidos na Etapa 8 nova (era 6).
- Áudio antecipado para Etapa 10 nova (era polish da Etapa 15).
- WiFi + power management viraram Etapa 11 nova explícita (antes ausente).
- CapyLX L0-L5 unificado na Etapa 13 nova (eram 9 e 10).
- Wayland virou Etapa 14 nova (era 11).
- Mesa/Vulkan + CapyLang fundidos na Etapa 15 nova rebaixada (eram 12 e 14).
- 1.0 hardening manteve-se como fechamento (Etapa 16 nova, era 15).

A regra sequencial estrita foi preservada: cada etapa só inicia quando a anterior fecha 100%. O snapshot completo da história alpha-por-alpha permanece em `active/capyos-master-plan.md`.
