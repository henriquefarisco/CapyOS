# CapyOS — Status executivo

**Data:** 2026-05-21 · **Versão:** `0.8.0-alpha.255+20260521` · **Plataforma oficial:** VMware + UEFI + E1000 · **Público alvo:** usuário desktop comum

> **Fonte de verdade:** [`active/capyos-master-plan.md`](active/capyos-master-plan.md).
> **Implementação finalizada (alpha.93):**
> [`historical/implementation-delivered-through-alpha93.md`](historical/implementation-delivered-through-alpha93.md).
> **Snapshot da sequência antiga (pré-reordenação ROI):**
> [`historical/capyos-master-plan-pre-roi-reorder.md`](historical/capyos-master-plan-pre-roi-reorder.md).
> Este documento mostra apenas o plano ativo sequencial. Itens concluídos
> foram condensados aqui e ficam preservados em detalhe nos documentos
> históricos e em [`../../VERSION.yaml`](../../VERSION.yaml) (history por alpha).
>
> **Reorganização 2026-05-15:** Etapas 3-15 foram reordenadas por ROI ao
> usuário desktop comum e expandidas para 14 etapas (3-16) sem violar a
> regra sequencial estrita. Etapas 1-2 não foram afetadas.

---

## Progresso global

- **Base histórica:** 100% consolidada até `alpha.93`; Etapa 1 fechada em `alpha.100`.
- **Plano sequencial novo (pós-reordenação ROI):** Etapas 1-3 oficialmente fechadas; 3/16 etapas concluídas.
- **Etapa atual:** Etapa 4 — CapyDisplay 2D + scheduler/multithread runtime.
- **Slice 3D fechado em 2026-05-21 (alpha.245):** gate externo `make smoke-x64-vmware-usb-hid-keyboard` validado em VMware + UEFI + E1000 com teclado USB HID real, marker `[smoke] usb-hid-keyboard ready` observado no COM1, follow-ups §14.1-§14.3 entregues, audit fixes §15.1-§15.5 corrigidos e bug W (slot reuse collision) resolvido. 25 novos host tests cobrem smoke gate, event pump, release slot, port ack CSC, Ctrl combinations, LED dispatch e caps lock.
- **Slice 3E.1 entregue em 2026-05-21 (alpha.246):** extração host-testável dos AHCI/NVMe command builders.
- **Slice 3E.2.A entregue em 2026-05-21 (alpha.247):** unified block-I/O error classifier `block_io_classify_ahci`/`block_io_classify_nvme` com 5 classes. AHCI integrado em 3 sites de `ahci_exec`; NVMe em 4 sites. 15 novos host tests.
- **Slice 3E.2.B entregue em 2026-05-21 (alpha.248):** recoverable retry + reset escalation. `block_device_ops` ganha `read_block_ex`/`write_block_ex`/`reset` opcionais. Retry loop unificado aplica budget per-class. AHCI implementa COMRESET; NVMe implementa Controller Level Reset. 12 novos host tests.
- **Slice 3E.3 entregue em 2026-05-21 (alpha.249, escopo reduzido):** infraestrutura multi-slot AHCI via novo `ahci_slot_allocator`. NVMe queue depth 64 + CID rolling auditados. 11 novos host tests. Concurrent inflight real diferido para Slice 3F.
- **Slice 3E.4 entregue em 2026-05-21 (alpha.250):** storage stack smoke marker `[smoke] storage-stack ready`. 9 novos host tests. klog full migration deferida para sub-slice 3E.4.B.
- **Slice 3E.5 entregue em 2026-05-21 (alpha.251, scaffolding):** external validation gate `smoke-x64-vmware-storage-resilience` plumado.
- **Audit fix entregue em 2026-05-21 (alpha.252):** revisão crítica de Slices 3E.1–3E.5 identificou e corrigiu dois bugs críticos antes da execução externa: (1) double-emission do smoke marker em VMs dual-storage; (2) NVMe Controller Level Reset não reemitia Create I/O CQ/SQ após CC.EN=1. 4 novos host tests de regressão.
- **Sub-slice 3E.4.B entregue em 2026-05-21 (alpha.253):** migração mecânica de `dbg_puts`/`dbg_hex*`/`dbg_label_hex32` para `klog(KLOG_*, ...)` / `klog_hex(...)` em `ahci.c` e `nvme.c` (108 call sites em 2 arquivos). Helpers locais file-static removidos; output migra de port 0xE9 (QEMU-only) para o klog ring (recuperável em runtime). Como efeito colateral, **corrige bug latente**: 2 chamadas a `dbg_label_hex32` em `nvme_controller_reset` referenciavam o helper static de ahci.c (undefined-reference no escopo de TU). Outros 13 arquivos do projeto com ~126 sites `dbg_*` ficam como sub-slice 3E.4.C (follow-up).
- **Etapa 3 fechada formalmente em 2026-05-21 (alpha.253):** gate externo `make smoke-x64-vmware-storage-resilience` aprovado em VMware + UEFI + E1000 com marker `[smoke] storage-stack ready` no COM1. Encerrou os 8 sub-slices 3D + 3E.1-3E.5 + audit fix + 3E.4.B. Slices 3F-3J e sub-slices 3E.4.C/3E.5.B continuam como follow-ups não-bloqueantes.
- **Próximo bloco da Etapa 4:** scaffolding do contrato `capy-ui-widget` (widget/display-list ABI v1) entre core CapyOS e sister repo `CapyUI` + integração do scheduler cooperativo no runtime. Conforme `docs/operations/etapa-4-external-validation-playbook.md`.
- **Etapas bloqueadas:** Etapas 5-16 dependem do fechamento integral da etapa anterior.

## Repositórios apartados (estado em alpha.255, Etapa 4 ativa; ver pendência de sincronização)

Os contratos de integração cross-repo são autoritativos em
[`docs/reference/integration/`](../reference/integration/README.md). A
matriz pinada está em
[`compatibility-matrix.md`](../reference/integration/compatibility-matrix.md)
e o snapshot técnico atual está em
[`compatibility-audit-2026-05-21.md`](../reference/integration/compatibility-audit-2026-05-21.md).

| Repo apartado | Versão atual | Owner autoritativo | Gate de integração CapyOS |
|---|---|---|---|
| [`CapyUI`](../../../CapyUI) | `0.7.3` | widget model (`capy-ui-widget` v0.6) **e** desktop session (`capy-ui-desktop-session` v1, publicado em `alpha.241`) | Etapas 4 e 6 |
| [`CapyAgent`](../../../CapyAgent) | `0.0.4` | formato `.capypkg`, component-index, resolver, futuro signer Ed25519 (`capy-agent-component-index` v1) | Etapas 8-9 |
| [`CapyBrowser`](../../../CapyBrowser) | `0.0.4` | browser-core text/HTML estático (`capy-browser-core` v1 planejado) | Etapas 6-7 |
| [`CapyCodecs`](../../../CapyCodecs) | `0.0.4` | image codecs portáveis (`capy-codec-image` v1) | Etapas 6-7 (imagem); Etapa 10 (áudio/vídeo) |
| [`CapyLang`](../../../CapyLang) | `0.1.3` | lexer S1 entregue; parser/bytecode/VM no roadmap (`capy-lang-host` v0 parcial) | Etapa 15 |
| [`CapyBenchmark`](../../../CapyBenchmark) | `0.0.4` | harness + baseline (`capy-benchmark-report` v1 planejado) | Etapas 15-16 |

Regras gerais (válidas mesmo antes da etapa abrir):

- Repositório externo **não conta como progresso oficial de etapa** até
  ser integrado por contrato versionado, adaptador CapyOS pequeno e gate
  externo aprovado.
- O adapter in-tree `services/capypkg` é a fronteira de recepção alpha
  para módulos remotos; signature verifier do `CapyAgent` permanece
  intencionalmente NULL até o signer Ed25519 ser publicado e plugado
  via `capypkg_set_signature_verifier`.
- Cada repo apartado mantém `docs/compatibility.md` próprio com a
  versão pinada do CapyOS, ABI declarada, limites e gate de integração.

## Higiene do core (concluída)

Snapshots seguros foram registrados em
[`external-core-repositories.md`](../reference/integration/external-core-repositories.md).
A higiene total do core foi concluída
([`core-migration-quarantine.md`](../reference/integration/core-migration-quarantine.md)):
os fontes e headers legados sem callers ativos foram **removidos do tree**
e o flag `CAPYOS_ENABLE_LEGACY_MIGRATED` foi aposentado. O adaptador
in-tree `services/capypkg` recebe pacotes Capy remotos via `capysh`.

Fluxo modular alpha: tags de release GitHub + sha256 + índice de ABI
mínima conforme
[`tag-release-component-index.md`](../reference/integration/tag-release-component-index.md);
assinatura e certificados ficam diferidos para hardening antes de
qualquer release oficial.

## Entrega antecipatória vigente: `services/capypkg` (alpha.239+)

Infra de recepção de pacotes Capy publicada in-tree em `services/capypkg`
(4 TUs runtime + 1 header público + 1 header interno, todas < 900 LOC),
com 9 comandos CLI tri-língua (`pkg-list`, `pkg-info`, `pkg-fetch`,
`pkg-install`, `pkg-remove`, `pkg-update`, `pkg-source-list`,
`pkg-source-add`, `pkg-source-remove`), supervisor de serviço
`SYSTEM_SERVICE_CAPYPKG` integrado ao target `FULL`, 28 testes host-side
passando (`make test-capypkg`) e trilha auditável via klog
(`[audit] [capypkg] …`) em todas as mutações de pacote/repo, com
variantes WARN distintas para falhas de digest/signature/dependency/
fetch/write/quota/persistence (forensicamente reconstruíveis).

Política de segurança documentada em
[`../architecture/capypkg-adapter.md`](../architecture/capypkg-adapter.md):
HTTPS-only no transporte, SHA-256 obrigatório, signature gate
fail-closed (Ed25519 só é aceito quando `CapyAgent` plugar o verificador
externamente), escopo de filesystem restrito a `/var/capypkg` ou `/opt/`,
e zero execução de payload pelo adapter.

**Não fecha a Etapa 9:** o gate oficial continua bloqueado por Etapas
3-8 conforme tabela vigente abaixo; este entregável apenas garante que,
quando a Etapa 9 abrir, a fronteira de recepção já estará verificada
e estável.

Extensões posteriores:

- `alpha.240` — install profile (`/system/install/profile.ini`), comando
  `pkg-bootstrap`, auto-bootstrap em kernel poll, `make package` em cada
  repo apartado e aggregator `make modules-index`.
- `alpha.241` — higienização end-to-end + wizard de primeiro boot
  interativo TUI (idioma, teclado, hostname, tema, splash, usuário,
  senha, **seleção de módulos**) + comando `capy` unificado +
  **migração da desktop session para `CapyUI`** (sources `gui/desktop/`,
  `gui/window/` e `apps/` agora têm o `CapyUI` como owner autoritativo;
  in-tree permanece como fallback de build) + activation gate em
  `kernel/module_gate.c` que consulta `/var/capypkg/<name>/installed`.
- `alpha.242` — hardening de redirect HTTP e staging persistente
  (`HTTP_MAX_URL`/`HTTP_MAX_PATH` para 2048; payloads rejeitados ficam
  em `/var/capypkg/updates` para diagnóstico).
- `alpha.243` — correção de HTTP redirect/bodyless no bootstrap remoto;
  validação real de ISO instalada com persistência.
- `alpha.244` — instalação remota completa de módulos via GitHub
  Release: download HTTPS de payload grande, staging dividido no CAPYFS,
  marker de ativação e smoke ISO com desktop ativado no reboot.

## Hardening cross-module ativo

Gate de printable-ASCII propagado a todos os módulos que ecoam dados
externos via `shell_print` → `vga_write` → COM1:

- `src/services/update_agent_parse.c::parse_buffer_line` — manifests,
  `state.ini` e `repository.ini` que carreguem control bytes em
  qualquer value são silenciosamente descartados na ingestão (sem
  alterar contrato externo).
- `src/net/services/http/url_request_builder.c::http_parse_url` —
  fechado vetor de HTTP request smuggling: o parser rejeita
  0x00-0x20 e 0x7F antes de qualquer parsing.
- `src/net/services/http/prelude_headers_encoding.c::http_store_headers`
  — response headers de servidores hostis com bytes não-printáveis
  são substituídos por `?` em parse time, sem afetar
  Content-Length / chunked / Content-Encoding.

## Visão executiva das etapas concluídas

### Etapa 1 — CapyUI Shell Polish v1 (concluída em `alpha.100`)

Entregou o desktop visual familiar Ubuntu/Win7-like sem GPU 3D:
tema `classic-modern`, taskbar inferior com botão Capy + relógio,
launcher com busca textual/categorias/ações de sessão, decoração
de janelas com estados ativo/inativo/minimizar/maximizar/fechar,
wallpaper 2D + grid de ícones, toasts/notificações e system tray
NET/SND/SYS/USR.

**Owner autoritativo pós-alpha.241:** repositório [`CapyUI`](../../../CapyUI)
via módulo capypkg `org.capyos.ui.desktop-session` (compositor session,
window manager, apps). O CapyOS mantém in-tree um fallback de build em
`src/gui/desktop/`, `src/gui/window/` e `src/apps/` para sustentar o
caminho `make all64` quando o sibling `../CapyUI` não está presente,
mas o owner de feature é o repo `CapyUI` (versão `0.7.3`+).

### Etapa 2 — Sessão gráfica operacional (concluída em `alpha.237`)

Sessão gráfica completa com login GUI real, dispatcher central de
input, frame pacing ocioso, fallback `CTRL+ALT+F1` para TTY, terminal
gráfico consumindo shell real e gates de evidência externa
`gui-session` + `mouse-events`. Em paralelo, segurança/auth/storage
para sessão persistente: header-managed encrypted volumes em produção
via `volume_provider`, migração legacy → header-managed transacional
com checkpoint persistente + rollback/abort/cleanup, login com
constant-time PBKDF2/Argon2id + lockout timing-equalised, CSPRNG
hardenado, fundação cripto canônica completa (SHA-256, SHA-512, HMAC,
PBKDF2, HKDF, CSPRNG, AES-XTS, ChaCha20-Poly1305 AEAD, X25519 ECDH,
Ed25519 signatures, Argon2id, BLAKE2b — 11 primitivas auditadas).

**Owner autoritativo pós-alpha.241:** a parte de desktop/window/apps
foi migrada para o repositório [`CapyUI`](../../../CapyUI) como
`org.capyos.ui.desktop-session`. A parte de auth, criptografia,
volume header, runtime de input, dispatcher e fallback textual
permanece no CapyOS core.

**Aceite externo:** em 2026-05-18 o operador informou execução
bem-sucedida fora desta máquina de `make test`, `make layout-audit`,
`make all64`, `make release-check`, `make smoke-x64-vmware-mouse-events`
e dos gates de readiness/evidência/aceitação/promoção com
`RELEASE_TAG=0.8.0-alpha.237+20260514` na plataforma oficial
VMware + UEFI + E1000.

**Runbook único para o operador externo / CI privada:**
[`docs/operations/etapa-2-external-validation-playbook.md`](../operations/etapa-2-external-validation-playbook.md)
orquestra build gates + provisionamento + smoke real + evidência/aceitação + promoção pública.

## Sequência ativa

> **Nota:** após a reordenação por ROI em 2026-05-15, a numeração das
> Etapas 3-16 mudou. A sequência antiga está em
> [`historical/capyos-master-plan-pre-roi-reorder.md`](historical/capyos-master-plan-pre-roi-reorder.md).

Resumo executivo vigente:

| Etapa | Tema | Status | Bloqueio / Repo apartado relacionado |
|---|---|---|---|
| 1 | CapyUI Shell Polish v1 | Concluída | owner pós-alpha.241: `CapyUI` |
| 2 | Sessão gráfica operacional | Concluída | desktop session: `CapyUI`; auth/crypto/runtime: core |
| 3 | Driver framework + entrada USB HID + storage estável | Concluída | fechada em alpha.253 (2026-05-21); follow-ups 3F-3J não-bloqueantes |
| 4 | CapyDisplay 2D + scheduler/multithread runtime | **Em andamento** | etapa atual; abre contrato `capy-ui-widget` v1 com sister repo `CapyUI` |
| 5 | TLS userland real | Bloqueada | depende da Etapa 4; sem repo apartado |
| 6 | Apps básicos do desktop maduros | Bloqueada | inclui integração de `CapyBrowser` (HTML-to-text core) e `CapyCodecs` (image) por contrato |
| 7 | Browser usável com web estática moderna | Bloqueada | integra `CapyBrowser` core + `CapyCodecs` imagem |
| 8 | Release/update gate oficial + instalador polido | Bloqueada | hardening do canal de release; sem repo apartado |
| 9 | Package manager + SDK + ABI estável | Bloqueada | integra `CapyAgent` (signer Ed25519 + component index oficial) |
| 10 | Áudio + multimídia básica | Bloqueada | integra `CapyCodecs` áudio por contrato |
| 11 | WiFi + power management + suspend/resume | Bloqueada | sem repo apartado |
| 12 | JS engine sandboxed | Bloqueada | engine pode ser apartada por contrato |
| 13 | CapyLX L0-L5 unificado | Bloqueada | sem repo apartado |
| 14 | Wayland bridge + apps Linux GUI | Bloqueada | sem repo apartado |
| 15 | Mesa/Vulkan path + CapyLang | Bloqueada | integra `CapyLang` (VM bytecode via host ABI versionada) |
| 16 | Plataforma 1.0 hardening | Bloqueada | inclui baseline regressivo de `CapyBenchmark` + compatibilidade oficial Hyper-V planejada |

---

## Etapa 3 (concluída em alpha.253) — resumo

Etapa fechou formalmente em 2026-05-21 (build `0.8.0-alpha.253+20260521`)
após aprovação externa do gate `make smoke-x64-vmware-storage-resilience`
em VMware + UEFI + E1000. Marker `[smoke] storage-stack ready` observado
no COM1 exatamente uma vez; regressão de Slice 3D (`[smoke] usb-hid-keyboard
ready`, aprovado em alpha.245) manteve-se verde.

Sub-slices entregues (alpha.245 → alpha.253):

- **3A-3D** — Device manager + XHCI + USB HID (boot protocol completo);
  gate `make smoke-x64-vmware-usb-hid-keyboard` aprovado em alpha.245.
- **3E.1** — AHCI/NVMe command builders host-testable (alpha.246).
- **3E.2.A** — Classifier de erro de bloco unificado em 5 classes (alpha.247).
- **3E.2.B** — Retry budget per-class + reset escalation
  (COMRESET para AHCI, Controller Level Reset para NVMe) (alpha.248).
- **3E.3** — AHCI slot allocator infraestrutura (alpha.249).
- **3E.4** — Storage smoke marker `[smoke] storage-stack ready` (alpha.250).
- **3E.5** — External validation gate scaffolding (alpha.251).
- **audit fix** — BUG #1 (smoke marker double-emission cross-driver)
  + BUG #2 (NVMe CLR missing queue recreation) (alpha.252).
- **3E.4.B** — Migração mecânica `dbg_*` → `klog`/`klog_hex` em ahci.c
  e nvme.c, com efeito colateral de fechar undefined-reference latente
  em `nvme_controller_reset` (alpha.253).

Detalhe técnico por sub-slice e implementação log em
[`../architecture/etapa-3-driver-foundation-plan.md`](../architecture/etapa-3-driver-foundation-plan.md)
e [`../architecture/etapa-3-slice-3e-plan.md`](../architecture/etapa-3-slice-3e-plan.md).

Runbooks operacionais do fechamento:

- [`../operations/etapa-3-external-validation-playbook.md`](../operations/etapa-3-external-validation-playbook.md) (Slice 3D).
- [`../operations/etapa-3-slice-3e-validation-playbook.md`](../operations/etapa-3-slice-3e-validation-playbook.md) (Slice 3E).

Documento arquitetural novo: `docs/architecture/smoke-marker-pattern.md`
canoniza o pattern de smoke markers (resultado do BUG #1 audit) para
prevenir reincidência em Etapas futuras.

**Follow-ups não-bloqueantes** (ficam como bug fixes oportunísticos):

- Slice 3F — Multi-table AHCI dispatch concorrente + remoção de spin-wait.
- Slice 3G — Política de fallback de driver no nível do kernel.
- Slice 3H — VirtIO-net/block prioritização VM.
- Slice 3I — VMware SVGA II como backend secundário.
- Slice 3J — USB mass storage.
- Sub-slice 3E.4.C — Migração `dbg_*` → `klog` nos 13 arquivos restantes.
- Sub-slice 3E.5.B — Extração de `nvme_controller_reset` em passos puros
  para unit test do BUG #2 fix.

## Etapa 4 (em andamento) — detalhes operacionais

A etapa ativa entrega CapyDisplay 2D + scheduler/multithread runtime e
**abre o primeiro gate cross-repo com sister** depois do fechamento da
Etapa 3: o contrato `capy-ui-widget` v1 com o repo `CapyUI`.

**Runbook autoritativo:**
[`../operations/etapa-4-external-validation-playbook.md`](../operations/etapa-4-external-validation-playbook.md).

**Fases planejadas** (extraídas do runbook):

| Fase | Sub-gate | Owner |
|---|---|---|
| A | Scaffolding do contrato `capy-ui-widget` v1 no core | CapyOS core |
| B | Sister `CapyUI` publica `capy-ui-widget` v1 | CapyUI |
| C | Scheduler cooperativo + multithread runtime + smoke `scheduler-fairness` | CapyOS core |
| D | Damage tracking + double buffering + smoke `compositor-damage-track` | CapyOS core |
| E | Política de panic/oops para thread de app + smoke `thread-crash-survives` | CapyOS core |
| F | Aprovação externa final + fechamento da Etapa 4 | operador |

**Fase A revertida em alpha.255 (2026-05-21):** o scaffolding entregue
em alpha.254 era um contrato paralelo + incompatível com a ABI real
do sister `CapyUI`. Descoberta por inspeção do sister sibling em
`/Users/t808981/Desktop/PR/CapyUI/`: `capy-ui-widget` já está em v2.7
com display-list schema v7, struct nativo `capy_dl_cmd` (não byte-tagged),
12 opcodes (RECT, BORDER, TEXT, CLIP_PUSH, CLIP_POP, IMAGE_REF,
FOCUS_RING, DIRTY_HINT, DPI_SCOPE, TRANSFORM_PUSH, TRANSFORM_POP,
PLUGIN_OP). Header autoritativo é `CapyUI/src/widget/capy_display_list.h`,
não um header novo no CapyOS. Arquivos do alpha.254 foram removidos.

**Pendência crítica identificada (não resolvida nesta sessão):** a
matriz cross-repo no CapyOS continua **stale**: pina CapyUI em 0.7.3
enquanto o sister real está em **2.7.0** com `capy-ui-widget` **v2.7**.
Sincronizar isso requer workflow `cross-repo-contract-sync` completo +
novo audit em `docs/reference/integration/compatibility-audit-<date>.md`.

**Próximo passo bloqueador (Fase A correta):** implementar adapter
CapyOS-side que **CONSUMA** `CapyUI/src/widget/capy_display_list.h`
(struct nativo, schema v7) via Makefile sibling detection, não
invente um schema paralelo. Combinado com sincronização cross-repo
completa (pin de CapyUI atualizado).

**Critérios de aceite (a fechar):**

- [ ] Compositor redesenha somente regiões danificadas quando possível.
- [ ] Cursor e texto não piscam sob resize/move de janela.
- [ ] Fallback framebuffer continua funcionando.
- [ ] Apps single-threaded existentes continuam funcionais como regressão.
- [ ] Thread de app crashando não derruba kernel nem desktop.
- [ ] Widget model desacoplado consegue renderizar display list por
      adaptador CapyOS sem acessar compositor diretamente.

**Cross-repo handshake esperado:** quando Fase A fechar, invocar
workflow `cross-repo-contract-sync` para coordenar a publicação do
`capy-ui-widget` v1 no sister `../CapyUI`.

## Bloqueio das etapas 5-16

Todas dependem do fechamento integral da etapa anterior conforme
[`active/capyos-master-plan.md`](active/capyos-master-plan.md). Repositórios
apartados podem evoluir em paralelo (CapyUI já entregou v0.7.3 com
desktop session; CapyLang continua em S1 lexer; demais permanecem em
ABI host-only ou planejada) — mas só contam como progresso oficial
quando a etapa correspondente abrir e o adapter + gate externo aceitarem
a integração.

Novas regressões em código já entregue são bugs da etapa ativa
correspondente, salvo mudança explícita deste plano.
