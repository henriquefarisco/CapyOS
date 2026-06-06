# CapyOS — Master Plan sequencial

**Data de referência:** 2026-05-15
**Vers?o atual:** `0.8.0-alpha.263+20260606`
**Plataforma oficial atual de validação:** `VMware + UEFI + E1000`
**Compatibilidade oficial planejada:** `Hyper-V + UEFI + VMBus/synthetic devices`, promovida somente após gates dedicados de boot, input, storage e rede.
**Público alvo prioritário:** usuário desktop comum (não-técnico, experiência tipo Ubuntu/Win7 polida).
**Status:** Etapas 1-4 oficialmente fechadas; 4/16 etapas concluídas; Etapa 5 em andamento.

Este é o único plano ativo. Entregas concluídas foram removidas daqui e
consolidadas em
[`../historical/implementation-delivered-through-alpha93.md`](../historical/implementation-delivered-through-alpha93.md).

> **Reorganização em 2026-05-15:** as Etapas 3-15 foram **reordenadas por ROI ao usuário desktop comum** e expandidas para 14 etapas (3-16) sem violar a regra sequencial estrita. A sequência antiga foi preservada em
> [`../historical/capyos-master-plan-pre-roi-reorder.md`](../historical/capyos-master-plan-pre-roi-reorder.md).
> Etapas 1-2 não foram afetadas pela reorganização. Resumo das mudanças principais:
> drivers/USB HID antecipados; scheduler/multithread incorporado ao CapyDisplay 2D; apps básicos antecipados; navegador básico de texto incluído como ferramenta inicial de rede; browser gráfico usável explícito; áudio e WiFi/power management promovidos a etapas próprias; CapyLX unificado e adiado como rota futura para ports Linux/Mozilla; Mesa/Vulkan e CapyLang rebaixados para Etapa 15 nova.
>
> **Reestruturação de desacoplamento em 2026-05-18:** o plano ativo passa a
> comprometer o sistema base apenas com APIs, sandbox, adaptadores, gates e
> integração final. CapyLang, browser core, package format, widget model,
> codecs e harness de benchmark podem evoluir em projetos apartados, desde que
> sigam `docs/architecture/decoupled-development-contracts.md` e os contratos em
> `docs/reference/integration/`.

## 1. Regra de execução

A partir desta reorganização, o desenvolvimento volta a ser estritamente
sequencial:

1. Uma etapa só pode iniciar quando a etapa anterior estiver 100% concluída.
2. Cada etapa precisa fechar código, documentação, critérios de aceite e
   validação antes de liberar a próxima.
3. Etapas históricas não voltam ao plano ativo como checklist.
4. Nenhuma etapa deve introduzir dependência Linux no kernel base; compatibilidade
   Linux fica isolada no módulo CapyLX.
5. Segurança, privacidade, performance, estabilidade e UX continuam sendo os
   pilares de aceite.
6. Hyper-V é compatibilidade oficial planejada, mas não substitui a plataforma
   oficial atual (`VMware + UEFI + E1000`) até fechar seus gates dedicados com
   evidência externa.
7. Projetos desacoplados não contam como progresso oficial de etapa até serem
   integrados por contrato versionado, adaptador CapyOS pequeno e gate externo
   aprovado.

## 2. Estado base congelado

A base atual inclui tudo entregue até `0.8.0-alpha.93+20260510`, incluindo:

- fundamentos kernel/userland;
- release tooling e publicação pública;
- rede, DNS, HTTP e hardening de URL/headers;
- update agent com gates locais e remotos;
- fundação GUI/CapyUI;
- `libcapy-tls` metadata-only fail-closed até adaptador BearSSL;
- shims Linux ABI parciais existentes.

Detalhes vivem no documento histórico de implementação finalizada.

## 2.1 Política de projetos desacoplados

O sistema base não deve absorver engines, linguagens, parsers, codecs ou
formatos complexos antes de haver contrato explícito e host tests próprios. A
fronteira oficial é:

- **CapyOS base:** boot, kernel, drivers, storage, rede/TLS, compositor, input,
  timers, sandbox, package install real, APIs nativas e gates de release.
- **Projetos apartados:** CapyLang core, CapyBrowse/HTML core, package format,
  widget model, codecs puros e benchmark harness.
- **Integração:** adaptadores finos, versionados e testados por gates externos
  quando a etapa correspondente estiver ativa.

Referências obrigatórias:

- `docs/architecture/decoupled-development-contracts.md`.
- `docs/reference/integration/README.md`.

## 3. Sequência bloqueante de etapas

| Etapa | Tema | Estado | Depende de | Saída para 100% |
|---|---|---|---|---|
| 1 | CapyUI Shell Polish v1 | Concluída | base alpha.93 | desktop familiar Ubuntu/Windows 7-like |
| 2 | Sessão gráfica operacional | Concluída | Etapa 1 | login GUI real e smokes `gui-session`/`mouse-events` |
| 3 | Driver framework + entrada USB HID + storage estável | Concluída (alpha.253) | Etapa 2 | XHCI/USB HID maduro, AHCI/NVMe estáveis, VirtIO opcional, política fail-safe de driver |
| 4 | CapyDisplay 2D + scheduler/multithread runtime | Concluída (alpha.262) | Etapa 3 | camada 2D com damage/double buffer, scheduler cooperativo, multithread runtime e contrato widget/display-list |
| 5 | TLS userland real | Em andamento | Etapa 4 | BearSSL userland com handshake real validado |
| 6 | Apps básicos do desktop maduros | Bloqueada | Etapa 5 | apps essenciais, `CapyBrowse Text` para sites de texto/diagnóstico de rede, libcapy-ui inicial e localização PT-BR/ES |
| 7 | Browser usável com web estática moderna | Bloqueada | Etapa 6 | HTTPS real, decode JPEG/PNG/WebP, streaming render, HTTP cache, forms, sem JavaScript |
| 8 | Release/update gate oficial + instalador polido | Bloqueada | Etapa 7 | smoke VMware+E1000 oficial, update HTTPS, instalador wizard amigável |
| 9 | Package manager + SDK + ABI estável | Bloqueada | Etapa 8 | ecossistema instalável, ABI documentada e integração de package format desacoplado |
| 10 | Áudio + multimídia básica | Bloqueada | Etapa 9 | Intel HDA/AC97/USB Audio, mixer de sistema, media player com playlist e codecs por contrato |
| 11 | WiFi + power management + suspend/resume | Bloqueada | Etapa 10 | driver WiFi popular, WPA2/WPA3, ACPI battery, suspend S3 inicial |
| 12 | JS engine sandboxed | Bloqueada | Etapa 11 | JavaScript isolado no browser com bridge DOM controlada e engine sem syscalls diretas |
| 13 | CapyLX L0-L5 unificado | Bloqueada | Etapa 12 | binários Linux estáticos + POSIX amplo + threads/futex/sockets, base futura para ports Linux de browsers grandes |
| 14 | Wayland bridge + apps Linux GUI | Bloqueada | Etapa 13 | apps Linux GUI via Wayland mínimo integrados ao compositor CapyOS |
| 15 | Mesa/Vulkan path + CapyLang | Bloqueada | Etapa 14 | lavapipe/virgl/Venus + linguagem própria com VM bytecode, bindings seguros e benchmarks Snake/Asteroids |
| 16 | Plataforma 1.0 hardening | Bloqueada | Etapa 15 | Secure Boot, SMP, firewall, compatibilidade oficial Hyper-V, polish final |


## 4. Etapa 1 — CapyUI Shell Polish v1 (concluída)

**Objetivo:** desktop familiar Ubuntu/Win7-like sem GPU 3D.

**Status:** concluída em `0.8.0-alpha.100+20260510`.

**Owner autoritativo pós-`alpha.241`:** repositório
[`CapyUI`](../../../../CapyUI) via módulo capypkg
`org.capyos.ui.desktop-session` + `org.capyos.ui.widget-core`. O
CapyOS mantém in-tree um fallback de build (`src/gui/desktop/`,
`src/gui/window/`, `src/apps/`) que é compilado quando o sibling
`../CapyUI` não está presente — owner de feature continua o repo
`CapyUI`.

**Resumo dos entregáveis (preservado por contrato):**

- Tema `classic-modern` com tokens de cor, spacing, radius, borda, sombra e estados.
- Taskbar inferior com botão Capy, apps fixados, apps abertos e relógio em pill.
- Launcher com busca textual, categorias visuais, apps recentes/fixados e ações de sessão.
- Decoração de janela com estados ativo/inativo, minimizar, maximizar e fechar.
- Wallpaper padrão e desktop com grid de ícones refinado.
- Toasts/notificações simples alinhadas à paleta ativa.
- System tray NET/SND/SYS/USR.

**Critérios de aceite (fechados):**

- [x] Desktop abre com visual consistente em resolução oficial.
- [x] Launcher abre/fecha por botão Capy e tecla Super.
- [x] Taskbar diferencia app focado, aberto e fixado.
- [x] Janela ativa/inativa tem contraste claro.
- [x] Notificação aparece, expira e não bloqueia input.
- [x] Tudo funciona sem GPU 3D e sem dependência Linux.

Detalhe histórico por alpha (94 → 100) está em
[`../historical/implementation-delivered-through-alpha93.md`](../historical/implementation-delivered-through-alpha93.md)
e em [`../../../VERSION.yaml`](../../../VERSION.yaml).

## 5. Etapa 2 — Sessão gráfica operacional (concluída)

**Objetivo:** transformar a fundação GUI existente em sessão gráfica
completa: login GUI real, dispatcher de input central, frame pacing,
fallback TTY, terminal gráfico, gates externos `gui-session` +
`mouse-events`.

**Status:** concluída em `0.8.0-alpha.237+20260514`; validação externa
final informada como aprovada pelo operador em 2026-05-18.

**Owner autoritativo pós-`alpha.241`:** desktop/window/apps migrados
para [`CapyUI`](../../../../CapyUI) (`org.capyos.ui.desktop-session`).
Auth, criptografia, volume header, runtime de input, dispatcher
central, login window, recovery, session handoff e fallback textual
permanecem no CapyOS core. CapyOS continua dono do compositor,
fontes, surfaces, framebuffer e do `kernel/module_gate.c` que decide
se o desktop é ativado por consulta a
`/var/capypkg/<canonical-name>/installed`.

**Resumo dos entregáveis (preservado por contrato):**

- Runtime gráfico operacional: frame pacing ocioso, tick gráfico
  evita composição/cursor quando cena e cursor estão estáveis,
  taskbar/launcher/desktop icons/File Manager polish, terminal
  gráfico consumindo shell real (prompt/cwd/logout via contrato), fallback `CTRL+ALT+F1` retorna ao TTY nos backends PS/2 e Hyper-V.
- Dispatcher central de input: teclado, scroll, hover/mouse move,
  click, drag, right-click/context menu, todos roteados pelo
  `gui_window_dispatch_event()` com snapshots de saúde e diagnóstico
  determinístico de rotas.
- Login window GUI: view model fail-closed, preview passivo, política
  de credenciais, buffer mascarado com limite e wipe obrigatório,
  redutor puro de input, snapshot de prontidão, evento auditável
  redigido, pipeline completo (UI plan → action plan → event plan →
  route plan → controller plan → presenter plan → binding plan →
  mount/commit/handoff/dispatch/queue/activation/frame/surface/
  compositor/damage/present/schedule/vsync/scanout/display/output/
  blit/framebuffer/flush/barrier/fence/timeline/sync/deadline/
  completion/ack/retire/cleanup/seal/audit/record/receipt/ledger/
  journal/archive/retention/expiry/purge/tombstone/compaction/
  reclaim/release/window plans).
- Login real com submit/autenticação: `userdb_authenticate_with_policy`
  composto com `auth_policy_check_allowed/record_*` (PBKDF2/Argon2id
  constant-time, lockout timing-equalised, dummy salt para
  non-existent users), recovery decision com rotas redigidas
  stay/recovery/resume/text-login, handoff seguro para
  `session_begin`/ativação/shell context/desktop autostart.
- Segurança/auth/storage para sessão persistente:
  header-managed encrypted volumes em produção via novo
  `volume_provider` (`include/security/volume_provider.h` +
  `src/security/volume_provider.c`) — header v1 `CAPYVHDR`
  Argon2id (t=3, m=8192 KiB) com salt CSPRNG per-install,
  HMAC-SHA256 check tag, CRC32 fast bit-rot gate, downgrade
  protection (não cai para legacy quando header está presente).
- Migração transacional legacy → header-managed: planner read-only,
  executor guardado/dry-run, checkpoint persistente auditado, cópia
  reversa segura um bloco por chamada, commit LBA0 por último com
  read-back e abertura final verificada, rollback/abort/cleanup
  operacional, orquestrador automático de passo único.
- Fundação criptográfica CapyOS canônica completa (11 primitivas
  auditadas): SHA-256, SHA-512, HMAC, PBKDF2, HKDF, CSPRNG, AES-XTS,
  ChaCha20-Poly1305 AEAD, X25519 ECDH, Ed25519 signatures, Argon2id
  memory-hard hashing, BLAKE2b, constant-time compare. CSPRNG
  hardening (rdtsc constraint, sha256_clear hygiene, snapshot fresh,
  no leak na stack). Update verifier `ed25519_verify` real (não mais
  esqueleto fail-closed) — `update_agent` operacional pela primeira
  vez. Privacy hardening em `auth_policy_status` (counts agregados;
  zero username leak) e `priv_log_emit` (`actor_role` em vez de
  `actor=<username>`).
- Hardening cross-module printable-ASCII em
  `update_agent_parse.c::parse_buffer_line`, `http_parse_url` (fecha
  CRLF injection) e `http_store_headers` (fecha ANSI escape em
  response headers).
- Smokes reais: gates determinísticos
  `desktop_gui_session_smoke_gate_from_readiness()` e
  `desktop_mouse_events_smoke_gate_from_readiness()`, markers seriais
  públicos `[smoke] gui-session ready` e `[smoke] mouse-events ready`,
  alvos `make smoke-x64-vmware-gui-session` e
  `make smoke-x64-vmware-mouse-events` com DHCP + gui-session +
  mouse-events como gate final.

**Critérios de aceite (fechados):**

- [x] Usuário consegue logar, abrir terminal gráfico e voltar ao TTY.
- [x] Mouse e teclado passam pelo dispatcher sem perda crítica.
- [x] Frame pacing reduz uso de CPU quando o desktop está ocioso.
- [x] Login GUI autentica via PBKDF2/Argon2id constant-time com lockout
      timing-equalised; falha não vaza existência nem timing de conta.
- [x] Volume cifrado boota via header-managed path com Argon2id;
      legacy mounts continuam funcionando; downgrade attack é rejeitado.
- [x] Migração legacy → header-managed tem checkpoint persistente,
      rollback/abort e cleanup verificados.
- [x] Gates externos `gui-session` e `mouse-events` aprovados em
      VMware + UEFI + E1000 com `RELEASE_TAG=0.8.0-alpha.237+20260514`.

**Runbook único do gate externo:**
[`../../operations/etapa-2-external-validation-playbook.md`](../../operations/etapa-2-external-validation-playbook.md).

Detalhe histórico por alpha (101 → 237) está em
[`../../../VERSION.yaml`](../../../VERSION.yaml) (history) e na
[`STATUS.md`](../STATUS.md).

## 6. Etapa 3 — Driver framework + entrada USB HID + storage estável (concluída em alpha.253, 2026-05-21)

**Objetivo:** garantir que o hardware básico de um desktop comum funcione de forma previsível antes de qualquer trilha gráfica avançada ou de apps maduros. Hardware funcionar é pré-requisito de UX.

**Status:** **CONCLUÍDA** em 2026-05-21 com a build `alpha.253`. Slices 3A-3D (XHCI + USB HID) fecharam em `alpha.245` via gate `make smoke-x64-vmware-usb-hid-keyboard`; Slices 3E.1-3E.5 (storage hardening) fecharam em `alpha.251` via scaffolding completo; audit fix em `alpha.252` corrigiu BUG #1 (smoke marker double-emission) e BUG #2 (NVMe CLR queue recreation); sub-slice 3E.4.B em `alpha.253` migrou `dbg_*` → `klog`/`klog_hex` em ahci.c e nvme.c. Gate externo `make smoke-x64-vmware-storage-resilience` validado em VMware + UEFI + E1000 com marker `[smoke] storage-stack ready` observado no COM1 exatamente uma vez. Slices 3F (multi-table AHCI), 3G (fallback policy), 3H (VirtIO-net/block prioritization), 3I (VMware SVGA II) e 3J (USB mass storage), assim como sub-slice 3E.4.C (klog migration nos outros 13 arquivos) e sub-slice 3E.5.B (nvme reset testability), permanecem como follow-ups oportunísticos não-bloqueantes — serão tratados como bug fixes da Etapa 3 quando regressões aparecerem.

**ROI:** alto — teclado USB e storage confiável são base; sem isso o usuário não usa o sistema.

### Entregáveis

- Device manager com enumeração PCI/PCIe, IRQ, MSI/MSI-X, DMA API e logs.
- XHCI enumeration + USB HID class completo (fecha lacuna em `system-overview.md §9`).
- AHCI maduro + NVMe básico estável + tratamento de erros de I/O recuperável.
- VirtIO-net e VirtIO-block como prioridade VM (preserva foco VMware).
- VMware SVGA II como backend secundário para resoluções estáveis.
- Política de fallback: falha de driver não derruba kernel, registra diagnóstico.

### Critérios de aceite (fechados)

- [x] VM oficial sobe com storage/rede/vídeo previsíveis.
- [x] Teclado USB funcional fora do `EFI ConIn` em VMware + UEFI.
- [x] Falha de driver não derruba o kernel sem diagnóstico.
- [x] Driver framework documenta ownership, DMA e teardown.

### Gates externos validados

- `make smoke-x64-vmware-usb-hid-keyboard` — aprovado em alpha.245.
- `make smoke-x64-vmware-storage-resilience` — aprovado em alpha.253.
- `make smoke-x64-iso TOOLCHAIN64=host` continua passando.
- `make all64` + `make release-check` continuam passando.

Evidência externa registrada em `docs/operations/etapa-3-external-validation-playbook.md` (Slice 3D) e `docs/operations/etapa-3-slice-3e-validation-playbook.md` (Slice 3E).

## 7. Etapa 4 — CapyDisplay 2D + scheduler/multithread runtime (concluída em alpha.262)

**Objetivo:** criar uma camada gráfica 2D sólida e introduzir scheduler/multithread cooperativo no runtime — pré-requisito para apps que precisam de paralelismo previsível e UI fluida.

**Status:** **CONCLUÍDA** em `alpha.262+20260602` via **Fase F validada externamente** em VMware + UEFI + E1000 (gate agregado `make smoke-x64-vmware-etapa-4`, 5 markers em ordem). Fases A-E fechadas em código + host tests em alpha.260 (empacotadas em alpha.261); a Fase F externa foi aprovada e a etapa fechada na release alpha.262. Histórico: a **Fase A foi revertida em alpha.255** (alpha.254 rolled back) porque o scaffolding criou ABI paralela em vez de consumir a ABI real do sister `CapyUI`; a correção consome `CapyUI/src/widget/capy_display_list.h` do `CapyUI` `2.22.0` (`capy-ui-widget` v2.22, display-list schema v7 inalterado) via Makefile sibling detection (`CAPYOS_HAVE_CAPYUI_WIDGET`) e adapter CapyOS-side.

**Estado por fase** (fonte única de detalhe: [`etapa-4-closure-tracker.md`](etapa-4-closure-tracker.md)):

- Fase A (adapter) — ✅ código + host tests.
- Fase B (produtor real CapyUI) — 🟡 capability entregue e exercitada por fluxos reais (Terminal, Context menu, Inline prompt no core; Calculator/Text Editor/Settings/File Manager/Task Manager/Taskbar/Notification/Desktop icons via `capy_widget_emit` do sibling). A migração dos demais fluxos de produção é polish **não-bloqueante** — o critério de aceite de capability (render via adapter sem acesso direto ao compositor) já está atendido.
- Fases C (scheduler cooperativo), D (damage tracking + double buffering) e E (thread-crash survives) — ✅ código + host tests, cada uma com seu latch de smoke.

**Fechamento:** a **Fase F** foi **validada externamente** em VMware oficial (`make smoke-x64-vmware-etapa-4`, 5 markers em ordem + regressões da Etapa 3 + `release-check`) e a Etapa 4 foi **fechada na release `alpha.262+20260602`**. A etapa seguinte (**Etapa 5 — TLS userland real**) está desbloqueada e ativa.

**ROI:** médio-alto — UI fluida sem travar é base de qualquer experiência polida; scheduler fecha uma lacuna conhecida em `project-overview.md`.

### Entregáveis

- Abstração de displays, modes, framebuffers, planes e cursor.
- Double buffering, clipping, damage tracking e blits otimizados.
- Cache de glyph/fontes e primitives 2D estáveis.
- API interna para compositor não depender diretamente do framebuffer bruto.
- Contrato CapyUI widget/display-list para permitir que layout de widgets evolua
  desacoplado de compositor, input real e janelas.
- Scheduler cooperativo + multithread runtime (incorporação do gap listado em `project-overview.md`).
- Política de panic/oops controlada quando thread de app falha.

### Critérios de aceite

> Os seis critérios abaixo foram **confirmados externamente na Fase F**
> (VMware + UEFI + E1000, `make smoke-x64-vmware-etapa-4`, 5 markers em
> ordem) e **fechados na release `alpha.262+20260602`**. Rastreabilidade
> critério → fase → evidência → gate em
> [`etapa-4-closure-tracker.md`](etapa-4-closure-tracker.md) §3.

- [x] Compositor redesenha somente regiões danificadas quando possível.
- [x] Cursor e texto não piscam sob resize/move de janela.
- [x] Fallback framebuffer continua funcionando.
- [x] Apps single-threaded existentes continuam funcionais como regressão.
- [x] Thread de app crashando não derruba kernel nem desktop.
- [x] Widget model desacoplado consegue renderizar display list por adaptador
      CapyOS sem acessar compositor diretamente.

### Gates externos recomendados

- `make smoke-x64-vmware-compositor-damage-track` (novo).
- `make smoke-x64-vmware-scheduler-fairness` (novo).

## 8. Etapa 5 — TLS userland real (em andamento)

**Objetivo:** avançar `libcapy-tls` de metadata-only para handshake real. Pré-requisito direto para browser HTTPS (Etapa 7) e release/update HTTPS (Etapa 8).

**ROI:** alto — sem HTTPS real, nada moderno funciona (web, update, sync, qualquer serviço).

> **ATIVA desde `alpha.262`** (Etapa 4 fechada). Auditoria + plano por slice em [`../../architecture/etapa-5-tls-userland-readiness.md`](../../architecture/etapa-5-tls-userland-readiness.md). Achados-chave: o TLS BearSSL **kernel-side já é real e em produção** (`src/security/tls.c`); a Etapa 5 torna real a `libcapy-tls` **userland** (hoje stub fail-closed, `capy_tls_is_supported()=0`). O gap mais fundamental é a **ausência de syscall de entropia userland** (`getrandom`) para semear o DRBG do BearSSL.
>
> **Slice 5.1 (em andamento):** syscall de entropia userland (`SYS_GETRANDOM`) backed pela CSPRNG do kernel, com stub capylibc e assert de ABI; TLS permanece intocado (fail-closed) nesta fatia. Próximas: BearSSL no build userland → trust anchors reais → handshake real → HTTPS userland deixa de retornar unsupported.

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

### Gates externos recomendados

- `make smoke-x64-vmware-tls-handshake` (novo).
- `make release-check` continua passando.

## 9. Etapa 6 — Apps básicos do desktop maduros

**Objetivo:** entregar o primeiro conjunto de apps verdadeiramente usáveis sem CLI, com toolkit estável, ícones oficiais, localização nativa e um navegador textual inicial para validar rede/HTTPS em páginas simples. Esta é a etapa onde o usuário comum começa a perceber valor real.

**ROI:** muito alto — primeiro valor visível ao usuário final.

### Entregáveis

- File Manager, Text Editor, Settings, Image Viewer, Calculator, Log Viewer, Notes/Calendar simples, Media Player de áudio e imagem (sem vídeo ainda — vídeo entra na Etapa 10).
- Adaptadores CapyOS para apps básicos: janela, input, FS permitido, tema, strings localizadas e lifecycle.
- `CapyBrowse Text`: integrar no CapyOS um core HTML-to-text desacoplado, sem JavaScript/CSS avançado, para abrir sites de texto via HTTP/HTTPS, extrair texto/links de HTML simples, fazer word wrap/scroll e servir como diagnóstico amigável de DNS/TCP/TLS/HTTP.
- Contenção do browser/html_viewer histórico: não ampliar parser/render acoplado antes de extrair lógica pura conforme `docs/reference/integration/browser-core-integration-contract.md`.
- Toolkit `libcapy-ui` inicial: button, list, textbox, dialog, menu.
- Ícones oficiais e integração com launcher/taskbar.
- Acessibilidade básica: atalhos de teclado consistentes, contraste mínimo.
- Localização nativa: PT-BR e ES como targets de release; EN continua default obrigatório.

### Critérios de aceite

- [ ] Cada app abre, executa função primária e fecha sem crash.
- [ ] Falha de um app não derruba desktop.
- [ ] `CapyBrowse Text` abre páginas alvo de texto/HTML simples, mostra erros claros de DNS/TLS/HTTP e não executa JavaScript.
- [ ] HTML-to-text e widget layout entram por contratos versionados, com adaptador
      CapyOS pequeno e testável.
- [ ] Apps usam o tema da Etapa 1.
- [ ] Strings de UI dos apps estão localizadas em PT-BR e ES com fallback EN.

### Gates externos recomendados

- `make smoke-x64-vmware-apps-basic-roundtrip` (novo): abre cada app, executa função primária, fecha sem leak/crash.
- `make smoke-x64-vmware-capybrowse-text` (novo): abre página HTTP/HTTPS controlada, valida texto, links numerados, scroll e erro TLS fail-closed.

## 10. Etapa 7 — Browser usável com web estática moderna

**Objetivo:** evoluir o `CapyBrowse Text`/`html_viewer` para browser gráfico usável em sites HTTPS estáticos modernos. JavaScript fica fora desta etapa (entra na Etapa 12); o foco é HTTPS, decode robusto, streaming render, cache e formulários.

**ROI:** muito alto — abre acesso à internet real para o usuário.

### Entregáveis

- HTTPS funcional via TLS Etapa 5 e política de certificados do sistema base.
- Migração incremental a partir do `CapyBrowse Text`, preservando modo texto como fallback/diagnóstico de rede.
- Integração de core HTML/CSS/display-list desacoplado; compositor e input continuam no CapyOS base.
- Streaming render para páginas grandes (fecha gap `feature/browser-internet-improvements` em `system-overview.md §10`).
- Image decode JPEG/PNG/WebP em produção via contrato de codecs puros e backend CapyOS de render.
- HTTP cache em memória + persistência simples em disco sob política do sistema base.
- Cookies básicos com escopo por domínio sob storage/sandbox do sistema base.
- Formulários simples (login, busca).
- Limites de memória/tempo por página, mensagens de erro úteis e bloqueio explícito de scripts nesta fase.

### Critérios de aceite

- [ ] Páginas alvo (wikipedia, blog estático, docs, search engine simples, news estático) carregam e renderizam sem travar a UI.
- [ ] HTTPS válido carrega; HTTPS inválido falha fechado com mensagem clara.
- [ ] Imagens JPEG/PNG aparecem inline.
- [ ] Cache acelera segunda visita observavelmente.
- [ ] Modo texto continua disponível quando render gráfico, imagem ou CSS falha.
- [ ] Parser/layout/display-list permanecem substituíveis sem acoplar core puro ao compositor.

### Gates externos recomendados

- `make smoke-x64-vmware-browser-https-static` (novo) com lista alvo de 5 sites.
- `make smoke-x64-vmware-browser-text-fallback` (novo).

## 11. Etapa 8 — Release/update gate oficial + instalador polido

**Objetivo:** fechar a release operacional com CI/smoke oficial, update HTTPS real e wizard de instalação amigável. Os blocos cripto (Ed25519 real em `alpha.217`) já estão prontos; falta wiring operacional.

**ROI:** médio-alto — confiança e manutenção contínua para o usuário final.

### Entregáveis

- Chave Ed25519 offline oficial publicada como chave esperada.
- CI executa smoke VMware+E1000 com DHCP/DNS/HTTP/HTTPS.
- `update-fetch` e payload HTTPS passam em ambiente controlado.
- Release gate promove artefatos somente com evidência pública válida.
- Instalador wizard amigável: seleção de disco, criação de usuário, idioma, fuso, política de senha.
- Migration de volume legacy → header-managed transparente (orquestrador já entregue em `alpha.232`).

### Critérios de aceite

- [ ] Smoke VMware+E1000 real passa em CI provisionada.
- [ ] Update HTTPS baixa, valida, prepara e aplica payload assinado.
- [ ] Evidência pública permite auditoria sem chave privada.
- [ ] Instalador wizard completa fresh install + reboot + login + persistência.

### Gates externos recomendados

- `make smoke-x64-vmware-installer-wizard` (novo).
- `make release-check` com payload assinado.

## 12. Etapa 9 — Package manager + SDK + ABI estável

**Objetivo:** permitir apps fora da imagem base e que terceiros publiquem software para CapyOS.

**ROI:** alto (médio-prazo) — ecossistema cresce sem releases monolíticas.

**Status:** bloqueada por Etapas 3-8. A **fronteira de recepção alpha**
já está in-tree, validada e auditável (veja "Entrega antecipatória"
abaixo), mas isso **não fecha a etapa** — os critérios de aceite
permanecem abertos até o resolver desacoplado, o Software Center
gráfico e o gate VMware oficial entrarem.

### Entrega antecipatória (capypkg adapter alpha)

Publicado antes do gate oficial da Etapa 9 para reduzir risco de
integração futura. Não conta como progresso desta etapa; serve apenas
de fronteira estável para o resolver externo plugar quando a etapa
abrir.

- **Código:** `src/services/capypkg/` (4 TUs runtime, todos < 900 LOC),
  `include/services/capypkg.h` (API pública), adapter de kernel em
  `src/arch/x86_64/kernel_services.c` (binding VFS + HTTPS).
- **Supervisor:** `SYSTEM_SERVICE_CAPYPKG` enumerado em
  `service_manager` e registrado no target `FULL`.
- **CLI:** 9 comandos tri-língua em `src/shell/commands/system_control/capypkg_commands.c`
  (`pkg-list`, `pkg-info`, `pkg-fetch`, `pkg-install`, `pkg-remove`,
  `pkg-update`, `pkg-source-list`, `pkg-source-add`, `pkg-source-remove`).
- **Testes:** 28 casos host-side em `tests/services/test_capypkg.c`,
  rodáveis via `make test-capypkg` (focado) ou `make test` (agregado).
  Cobertura inclui regressões para prefix-bypass de install_root,
  segmentos `..`, alfabeto restrito do `name` e dos `depends`,
  overflow de `payload_size`, reset de `any_repo_signed` em
  transição signed→unsigned, skip de entry malformada em manifests
  multi-entry, rejeição de ANSI escape (control bytes) em fields de
  manifest e em `pkg-source-add`, e dois testes diretos de klog
  audit messages.
- **Audit trail:** klog `[audit] [capypkg] …` em todas as mutações
  de pacote/repo. Install: success (`payload-sha256 verified;
  package installed`) e 8 variantes WARN distintas (dependência
  ausente/ciclo, dependência falhou downstream, fetch falhou,
  sha256 mismatch, assinatura falhou, write falhou, quota
  exhausted, db persistence falhou). Remove: success, payload
  removal falhou (entry ainda droppada), persistência falhou.
  Repository: add/update/remove com variante de persistência
  falhou em cada. INFO emitido apenas quando a mutação
  in-memory também persistiu; WARN em toda branch de falha.
- **Política de segurança:** HTTPS-only no transporte, SHA-256
  obrigatório, signature gate fail-closed (verificador Ed25519
  externo via `capypkg_set_signature_verifier`), `install_root`
  restrito a `/var/capypkg` ou `/opt/`, zero execução de payload
  pelo adapter.
- **Doc canônico:** `docs/architecture/capypkg-adapter.md` registra
  fronteiras, seams pluggáveis, lifecycle, formato de manifest e
  roadmap futuro (streaming download, ledger persistente, sandbox
  loader).

### Entregáveis

- Integração do formato `.capypkg`/manifest/resolver desenvolvido por contrato desacoplado.
- `pkgd`, CLI `pkg` e app Software Center gráfico.
- ABI estável documentada em `include/`.
- SDK headers, samples e guia de build.
- Aplicador CapyOS de plano de instalação/rollback sobre filesystem real.

### Critérios de aceite

- [ ] Instalar, listar, atualizar e remover pacote sobrevive reboot.
- [ ] ABI pública tem versionamento e política de compatibilidade.
- [ ] App Software Center instala um pacote via UI sem CLI.
- [ ] Resolver/manifest ficam host-testáveis fora do sistema; CapyOS só aplica plano validado.
- [ ] Verificador Ed25519 do `CapyAgent` plugado no adapter via
      `capypkg_set_signature_verifier` antes do primeiro `pkg-install`
      real; até lá, repos `signed` permanecem fail-closed.

### Gates externos recomendados

- `make smoke-x64-vmware-pkg-install` (novo).

## 13. Etapa 10 — Áudio + multimídia básica

**Objetivo:** habilitar áudio de sistema e ampliar Media Player para playlist e reprodução real. Vídeo software simples entra aqui como bônus opcional.

**ROI:** alto — multimídia é uso diário do desktop comum (música, calls leves, vídeos curtos).

### Entregáveis

- Driver Intel HDA + AC97 + USB Audio class (ao menos um deles validado em VMware).
- Mixer de sistema + controle por app.
- Decoders de áudio: WAV nativo, OGG/Vorbis ou MP3 por contrato de codec puro ou library vendorizada aprovada.
- App Media Player evolui para suportar playlist e visualização básica.
- Vídeo software simples (decode 1 codec leve em resolução baixa) opcional, somente se o codec tiver limites/fuzz/golden tests.

### Critérios de aceite

- [ ] Reprodução de WAV/OGG sem stutter perceptível em VM oficial.
- [ ] Mixer permite ajuste de volume global e por app.
- [ ] Falha de driver de áudio não derruba o sistema.
- [ ] Codec puro não acessa FS/rede diretamente e respeita limites de memória/tempo.

### Gates externos recomendados

- `make smoke-x64-vmware-audio-playback-roundtrip` (novo).

## 14. Etapa 11 — WiFi + power management + suspend/resume

**Objetivo:** habilitar uso real fora da VM oficial: WiFi, ACPI battery, suspend/resume. Sem isso o sistema não roda em laptops/desktops modernos.

**ROI:** muito alto — exigência para uso fora do laboratório.

### Entregáveis

- Stack 802.11 mínimo: ao menos um driver WiFi popular (RTL8821CE ou Intel iwlwifi se viável).
- WPA2/WPA3 supplicant userland.
- ACPI battery + thermal monitoring básico.
- Suspend-to-RAM (S3) inicial em VMware e máquina real onde viável.

### Critérios de aceite

- [ ] WiFi conecta a rede WPA2 com DHCP funcional.
- [ ] ACPI battery aparece no system tray com nível atualizando.
- [ ] Suspend/resume preserva sessão em VMware.

### Gates externos recomendados

- `make smoke-x64-vmware-wifi-dhcp-roundtrip` (novo) com WiFi via passthrough quando disponível.
- `make smoke-x64-vmware-acpi-battery-readout` (novo).

## 15. Etapa 12 — JS engine sandboxed

**Objetivo:** habilitar web dinâmica sem comprometer isolamento. Browser da Etapa 7 ganha JavaScript com bridge DOM controlada e budget de execução.

**ROI:** alto — abre a web realmente moderna; sem JS muitos sites úteis não funcionam.

### Entregáveis

- Decisão QuickJS vs CapyJS subset como projeto desacoplado ou engine integrada por contrato.
- Bridge DOM controlada e budget de execução no sistema base.
- Sem syscalls diretas a partir do script.

### Critérios de aceite

- [ ] Script básico altera título/DOM permitido.
- [ ] Loop infinito é interrompido por budget.
- [ ] Página com JS hostil não escapa do sandbox.
- [ ] Engine JS não acessa syscalls diretamente; toda capacidade passa por bridge versionada.

### Gates externos recomendados

- `make smoke-x64-vmware-browser-js-dom` (novo) em página de teste com DOM mutável.

## 16. Etapa 13 — CapyLX L0-L5 unificado

**Objetivo:** iniciar e expandir compatibilidade Linux estilo WSL1, sem kernel Linux. Une os antigos níveis L0-L2 (CLI estático) e L3-L5 (POSIX amplo) em uma etapa única, agora rebaixada para depois das prioridades do desktop comum. Esta etapa também cria a base técnica para, no futuro, avaliar ports grandes de browsers Linux/Mozilla sem transformar a ABI Linux na base do CapyOS.

**ROI:** médio — público power user e ferramentas Linux; não é exigência do desktop comum.

### Entregáveis

- `linux_personality` por processo.
- Loader ELF64 Linux com stack `argc/argv/envp/auxv`.
- Dispatcher Linux syscall auditável.
- Syscalls mínimas: read/write/openat/close/fstat/lseek/mmap/munmap/brk, exit/exit_group/getpid/clock_gettime/uname/getrandom.
- `clone`, `futex`, sinais, `wait4`, `execve`, `pipe2`, `dup3`, poll/epoll.
- Sockets Linux ABI traduzidos para a rede CapyOS.
- App bundles com bibliotecas empacotadas.
- Perfil de compatibilidade para browsers Linux grandes: `mmap`/`mprotect`, pthread/futex, sockets, epoll, `/proc`, `/dev`, dynamic loader e limites de sandbox documentados como pré-requisitos antes de qualquer tentativa de Firefox/Mozilla.
- `/dev`, `/proc`, `/tmp` e `/etc` mínimos.
- Rootfs Linux-like opcional e isolado.

### Critérios de aceite

- [ ] Binário Linux estático simples executa e retorna código correto.
- [ ] Ferramentas CLI Linux dinâmicas simples rodam em app bundle.
- [ ] Threads/futex funcionam para libc/pthread comum.
- [ ] Syscall desconhecida retorna `-ENOSYS` de forma previsível.
- [ ] CapyLX permanece módulo de compatibilidade, não base do sistema.
- [ ] Roadmap de port Mozilla/Firefox via ABI Linux fica explicitamente bloqueado até CapyLX, Wayland bridge, multimídia e hardening de sandbox terem gates próprios.

### Gates externos recomendados

- `make smoke-x64-vmware-capylx-binary-static` (novo).
- `make smoke-x64-vmware-capylx-pthread` (novo).
- `make smoke-x64-vmware-capylx-browser-primitives` (novo): valida primitives POSIX mínimas exigidas por browsers grandes sem ainda portar Firefox.

## 17. Etapa 14 — Wayland bridge + apps Linux GUI

**Objetivo:** rodar apps gráficos Linux modernos sem X11 inicial, integrados ao compositor CapyOS.

**ROI:** médio-alto — expande ecossistema gráfico para milhares de apps Linux já existentes.

### Entregáveis

- Servidor/proxy Wayland mínimo: `wl_compositor`, `wl_shm`, input e `xdg_shell` básico.
- Ponte entre Wayland surfaces e compositor CapyOS.
- Clipboard e resize/focus básicos.

### Critérios de aceite

- [ ] App Wayland simples abre janela, recebe input e fecha.
- [ ] Falha do app Linux não derruba compositor.

### Gates externos recomendados

- `make smoke-x64-vmware-wayland-roundtrip` (novo).

## 18. Etapa 15 — Mesa/Vulkan path + CapyLang

**Objetivo:** abrir rota gráfica software via Mesa/lavapipe e introduzir CapyLang como linguagem própria de automação e apps. CapyLang foi rebaixada nesta reorganização por ter baixo ROI direto ao desktop comum, ainda que seja identidade de longo prazo do projeto.

**ROI:** médio — gráficos software permitem demos; CapyLang é identidade do projeto.

### Entregáveis

- Mesa software/lavapipe para Vulkan software inicial.
- VirGL/Venus sobre VirtIO-gpu quando disponível.
- Política clara: Vulkan real exige driver/memory manager/sync antes.
- Integração do CapyLang core desenvolvido fora do sistema conforme contrato de host ABI.
- Loader CapyOS para bytecode `.capyscript`/artefato equivalente, com versão e checksum.
- Bindings FS/config/shell seguros.
- Bindings gráficos 2D/input/timer mínimos para apps CapyLang sandboxed.
- Apps benchmark/demo em CapyLang: `Snake` e `Asteroids`, com modo interativo e modo determinístico para medir FPS, input latency, uso de CPU e throughput da VM bytecode.
- Módulos, FFI controlada, formatter e LSP em ondas posteriores (não bloqueiam a etapa).

### Critérios de aceite

- [ ] Demo gráfica software roda via caminho Mesa controlado.
- [ ] Fallback 2D continua estável quando aceleração indisponível.
- [ ] Script CapyLang de automação roda em ring 3.
- [ ] Bindings respeitam permissões do usuário.
- [ ] `Snake` e `Asteroids` em CapyLang rodam sem acesso privilegiado, reportam métricas determinísticas e não derrubam desktop em erro de script.
- [ ] CapyLang core permanece portátil e testável fora do CapyOS; integração oficial usa apenas host ABI versionada.

### Gates externos recomendados

- `make smoke-x64-vmware-mesa-software-demo` (novo).
- `make smoke-x64-vmware-capylang-automation` (novo).
- `make smoke-x64-vmware-capylang-bench-games` (novo): executa `Snake` e `Asteroids` em modo benchmark determinístico e coleta FPS/latência/CPU sem promover regressão como sucesso silencioso.

## 19. Etapa 16 — Plataforma 1.0 hardening

**Objetivo:** consolidar CapyOS como sistema sólido para uso contínuo. É o fechamento de release 1.0.

**ROI:** alto (qualidade de produção) — auditável e estável para uso real prolongado.

### Entregáveis

- Secure Boot e measured boot.
- SMP/multicore.
- Firewall mínimo.
- Compatibilidade oficial Hyper-V: boot UEFI, VMBus/synthetic input, storage e
  rede com fallback diagnosticável, sem regredir VMware.
- Hardening do navegador nativo: limites por origem/página, isolamento de cache/cookies, política de certificados, auditoria de parser HTML/CSS e regressões contra páginas hostis.
- Decisão formal de produto sobre continuar evoluindo browser nativo leve, portar engine intermediária (ex.: NetSurf/WebKit) ou tentar Mozilla/Firefox via CapyLX, baseada em gates e não em promessa antecipada.
- Baseline regressivo dos benchmarks CapyLang (`Snake`/`Asteroids`) para detectar regressões de render, input, scheduler e VM bytecode antes da promoção 1.0.
- USB completo, polish final.
- Suíte Office/IDE opcional via package manager.

### Critérios de aceite

- [ ] Plataforma tem boot/update/rollback/GUI/apps/compatibilidade auditáveis.
- [ ] Segurança e performance têm gates regressivos documentados.
- [ ] SMP roda sob workload sintético sem regressão observável.
- [ ] Hyper-V sobe como plataforma oficialmente compatível com boot, input,
      storage e rede validados por smoke dedicado, preservando VMware como
      plataforma de validação primária até a promoção explícita.
- [ ] Benchmarks CapyLang têm baseline registrada, limites de variação definidos
      e falham de forma explícita quando render/input/scheduler regredirem.

### Gates externos recomendados

- `make release-check` em pipeline CI oficial.
- `make smoke-x64-vmware-smp-stress` (novo).
- `make smoke-x64-vmware-firewall-block` (novo).
- `make smoke-x64-hyperv-boot` (novo).
- `make smoke-x64-hyperv-input-storage-net` (novo).
- `make smoke-x64-vmware-capylang-benchmark-regression` (novo).

## 20. Próximo comando esperado

A Etapa 3 fechou formalmente em 2026-05-21 (alpha.253) após validação externa do gate `make smoke-x64-vmware-storage-resilience` em VMware oficial. A Etapa 4 abriu em sequência mas o scaffolding entregue em alpha.254 foi rolled back em **alpha.255** após descoberta de que a ABI real do sister `CapyUI` já estava além do contrato paralelo criado. A matriz agora pina `CapyUI` `2.22.0` / `capy-ui-widget` v2.22 (display-list schema v7), e a Fase A correta consome `CapyUI/src/widget/capy_display_list.h` via adapter CapyOS-side em vez de inventar schema paralelo. A **Etapa 4 foi fechada na release `alpha.262+20260602`** após a Fase F validada externamente (`make smoke-x64-vmware-etapa-4`, 5 markers em ordem + regressões `usb-hid-keyboard`/`storage-resilience` + `release-check`). **Próxima ação: Etapa 5 (TLS userland real) — Slice 5.1**, a syscall de entropia userland (`SYS_GETRANDOM`) backed pela CSPRNG do kernel. Estado por fase da Etapa 4 em [`etapa-4-closure-tracker.md`](etapa-4-closure-tracker.md); plano da Etapa 5 em [`../../architecture/etapa-5-tls-userland-readiness.md`](../../architecture/etapa-5-tls-userland-readiness.md). Slices 3F-3J e sub-slices 3E.4.C/3E.5.B continuam como follow-ups não-bloqueantes da Etapa 3. Runbook completo da Etapa 4: `docs/operations/etapa-4-external-validation-playbook.md`.
