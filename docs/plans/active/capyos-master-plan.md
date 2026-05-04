# CapyOS — Master Plan único e linear

**Data de referência:** 2026-05-03
**Versão atual:** `0.8.0-alpha.6+20260503`
**Plataforma oficial:** `VMware + UEFI + E1000`
**Branch ativa:** `feature/dev-bugfixes`
**Substitui:** `capyos-robustness-master-plan.md`, `system-master-plan.md`, `system-roadmap.md`, `system-execution-plan.md`, `capyos-master-improvement-plan.md`, `browser-status-roadmap.md`, `source-organization-roadmap.md`, `m5-userland-progress.md`, `post-m5-ux-followups.md` (todos arquivados em `historical/` em 2026-05-01).

---

## 0. Convenção de nomenclatura única (2026-05-02)

A partir desta data, **toda documentação ativa** (`docs/plans/active/*.md`,
release notes em `docs/releases/`, e `STATUS.md`) usa o vocabulário
abaixo de forma consistente:

| Termo | Significado | Onde aparece |
|---|---|---|
| **Etapa N** (N = 1, 2, 3, …) | Unidade linear de trabalho com escopo finito, dependência declarada e critério de aceite verificável. Substitui `Fase F1`, `Slice 5d`, `Phase 7b`, `Sessão 3`. | Toda nova subdivisão de plano. |
| **Seção a, b, c, d, e** | Sub-divisão dentro de uma Etapa. Letra minúscula, sequencial. Substitui `E1.1`, `E1.2`, `slice 4-final`, `M5.G.4`. | Toda nova decomposição interna de uma Etapa. |
| **Marco** (M0…M8) | **Tag arqueológico** de blocos históricos já entregues (M0 governança, M4 processos, M5 userland, M6 segurança, M7 WAL, M8 browser kernel-side). Imutável. Apenas leitura. | Tabela de "Entregue" em §2.1 deste master plan. |
| **F1…F10** | **Tag arqueológico** das fases originais deste master plan. Identificadores estáveis para evitar quebra de referências cruzadas, mas todo conteúdo NOVO usa Etapa N + Seção a-e. | §4 (renomeada para "Etapas linearizadas"). |
| **Sessão** | Janela temporal de trabalho de um operador (não unidade de plano). Forma livre. Em release notes, vira "Etapa N seção L (sessão YYYY-MM-DD)". | Logs de sessão, conversation summaries. |

**Regra de ouro:** Qualquer plano novo ou refactor de plano existente
**deve** usar `Etapa N` + `Seção a/b/c…`. Identificadores legados (F3.3f,
slice 5d, M5.G.4, etc.) podem aparecer entre crases como tag arqueológico
mas nunca como cabeçalho de uma tarefa nova.

---

## 1. Propósito deste documento

Este é o **único plano vivo** do CapyOS. Ele consolida:

1. O que já foi entregue (M0–M8 do plano antigo de robustez, M4 finalization, M5 userland, W1–W3 UX).
2. O que falta — em forma de **fases lineares numeradas (F1…F10)**, cada uma com escopo finito, dependências explícitas, critério de aceite verificável e plano de execução.
3. A visão de longo prazo (pós-F10) sem prometer datas.

Regras de manutenção:

- **Toda mudança relevante atualiza este documento e o `STATUS.md`.**
- Fases só promovem para `Implementado` com **evidência** (host test + smoke + commit).
- Nenhuma feature de produto começa sem fechar a fase anterior, exceto quando explicitamente marcada como `paralelo possível`.
- Nenhum plano novo pode ser criado em `active/` sem reconciliar com este. `historical/` recebe planos finalizados ou substituídos.

---

## 2. Estado atual consolidado

### 2.1 Entregue (não volta a este plano como tarefa)

| Bloco | Status | Evidência principal |
|---|---|---|
| **M0** Governança / matriz de suporte / gates de release | ✅ 100% | `make release-check`; `docs/reference/driver-support-matrix.md` |
| **M1** Build `TOOLCHAIN64=elf` + stack-protector + checksums + version-audit | ✅ 100% | `make all64 TOOLCHAIN64=elf`; `make iso-uefi`; `make version-audit` |
| **M3** Layout estrito (≤ 900 linhas, headers `internal/`, audit cruzado) | ✅ 100% | `make layout-audit --strict` |
| **M4** Processos + scheduler preemptivo + CoW + TSS/RSP0 + ring-3 preemption (31/31 fases) | ✅ 100% | `historical/m4-finalization-progress.md`; suítes `test_vmm_anon_regions`, `test_vmm_cow`, `test_pmm_refcount`, `test_user_task_init`, `test_context_switch`; smokes `smoke-x64-preemptive-all` |
| **M5-perf** Métricas de boot, baseline, `perf-*` commands, op_budget genérico | ✅ 100% | `docs/performance/boot-baseline.json`; `make boot-perf-baseline-selftest` |
| **M5-userland** fork/exec/wait/pipe + capysh ring 3 + isolamento de crash | ✅ 95% (CI pendente) | A.1–F.4 entregues; smokes `fork-cow`, `exec`, `fork-wait`, `pipe`, `fork-crash`, `capysh` (aguardam CI) |
| **M6.1** Política de senha + lockout | ✅ 100% | `tests/test_auth_policy.c` |
| **M6.2** Auditoria persistente (`[auth]`/`[net]`/`[update]`/`[recovery]`/`[priv]`/`[browser]`/`[capyfs-journal]`) | ✅ 100% | `tests/test_audit_events.c` |
| **M6.3** API centralizada de privilégios | ✅ 100% | `src/auth/privilege.c`; `tests/test_privilege.c` |
| **M6.5** CAPYFS journal v2 autenticado (HMAC-SHA256) | ✅ 100% | `tests/test_journal.c` |
| **M7** WAL + replay + fsck + recovery cause + update transacional | ✅ 100% | `tests/test_journal`, `test_capyfs_check`, `test_capyfs_journal_cause`, `test_update_transact` |
| **M8.1** Browser com estado formal + strict mode + isolation hooks | ✅ 100% | `tests/html_viewer/navigation_cases.inc` |
| **M8.3** Budgets parse/render/external + `nav_op_budget` cooperativo | ✅ 100% | `tests/html_viewer/resource_cases.inc` |
| **M8.4** DNS cache TTL + HTTP cache com budget + `no-store` | ✅ 100% | `tests/test_dns_cache`; `tests/html_viewer/resource_cases.inc` |
| **M8.5** `about:network` / `about:memory` | ✅ 100% | `tests/html_viewer/resource_cases.inc` |
| **W1** TTY polish (`clear` context-aware via callback) | ✅ 100% (2026-04-30) | `src/shell/core/shell_main/output_files.c`; `userland/bin/capysh/main.c` |
| **W2** Task manager auto-refresh + Kill button | ✅ 100% (2026-04-30) | `src/apps/task_manager.c`; `src/gui/desktop/desktop.c` |
| **W3 core** Browser parser yield + timeout 30s + per-frame async drain | ✅ 100% (2026-04-30) | `src/apps/html_viewer/html_parser.c`; `src/apps/html_viewer/async_runtime.c` |

### 2.2 Pendente (entra como fase neste plano)

| Origem | Item | Vira fase |
|---|---|---|
| M2.1–M2.5 | DHCP no smoke oficial VMware+E1000 | **F2** |
| M5.G.4 / G.5 | Release notes + tag `0.8.0-alpha.6` + master-plan promote | **F1** |
| M6.4 | Assinatura ponta-a-ponta dos checksums + smoke VMware | **F2** (acopla) |
| M8.2 + W3.4 | Browser em processo userland isolado + watchdog | **F3** |
| Sistema | Stack sockets userland + TLS p/ apps | **F4** |
| Sistema | Update real via GitHub Releases (fetch + Ed25519) | **F5** |
| Sistema | Sessão gráfica completa (mouse fim-a-fim, login GUI, dispatcher) | **F6** |
| Sistema | Apps básicos (file_manager, text_editor, settings, image_viewer) | **F7** |
| Sistema | Package manager + SDK + ABI estável | **F8** |
| M8.6 | JS engine sandboxed | **F9** |
| Sistema | CapyLang (linguagem própria) | **F10** |

### 2.3 Métricas globais

- **Progresso rumo a "SO sólido de uso geral":** ~30% (era 18% no plano antigo; M4+M5+M7+W1–W3 fecharam grandes blocos).
- **Cobertura de testes host:** 7/7 suites principais + op_budget + privilege + buffer_cache_pacing + 110+ asserts M4 + 84 asserts M5.
- **Smokes QEMU em CI:** 4 preemptivos + 6 M5 (aguardando push) + boot + ISO + CLI + capysh.
- **Lacunas estruturais críticas:** rede sockets userland, TLS de apps, package manager, GUI fim-a-fim, JS engine.

---

## 3. Princípios arquiteturais (inalteráveis)

1. **Não expandir feature de produto sem fronteira clara entre kernel e userland.** M5 entregou a fronteira — toda nova feature deve respeitá-la.
2. **Toda capacidade nova nasce observável, testável e com critério de rollback.**
3. **Update automático faz parte da arquitetura, não é script tardio.**
4. **GUI não acopla ao boot path.** Sessão gráfica é serviço, não inicialização.
5. **Linguagem própria só após ABI, package manager, toolkit gráfico e SDK estáveis.**
6. **Plataforma oficial é `VMware + UEFI + E1000`.** Hyper-V e QEMU são laboratório.
7. **Branches:** `main` = canal `stable`; `develop` = canal `develop`. Update agent trata como trilhas distintas.
8. **Layout estrito:** ≤ 900 linhas por arquivo C/teste, headers `internal/` para fronteiras privadas, `make layout-audit --strict` em CI.

---

## 4. Plano linear de entregas (F1 → F10)

Cada fase tem o mesmo formato:

```
### Fx — Nome
**Criticidade:** alta / média / baixa
**Depende de:** lista
**Paralelo possível com:** lista
**Risco:** texto
**Esforço estimado:** sessões/sprints
**Branch sugerida:** nome

#### Objetivo
#### Entregáveis (E1, E2, …)
#### Critérios de aceite (checkbox)
#### Como será feito (passos técnicos)
#### Validação (commands, suites, smokes)
#### Saída esperada / DoD
```

---

### F1 — Release `0.8.0-alpha.6` (consolidação M5 + W1/W2/W3 + F3 browser)

**Status:** 🟡 Em andamento (2026-05-03) — bump local validado; falta push/CI + tag.
**Criticidade:** alta — destrava M8.2 e o ciclo seguinte.
**Depende de:** branch `feature/continue-development` (M5+W1+W2+W3 já merge no HEAD).
**Paralelo possível com:** F2 (smoke VMware) — diferentes superfícies.
**Risco:** baixo — código já entregue, falta apenas validação CI + bookkeeping de release.
**Esforço:** 1 sessão.
**Branch sugerida:** `release/0.8.0-alpha.6`.

#### Progresso 2026-05-01

- ✅ E1.4 — `VERSION.yaml`, `include/core/version.h` e `README.md` bumped para `0.8.0-alpha.6+20260503`.
- ✅ E1.5 — Release note `docs/releases/capyos-0.8.0-alpha.6+20260503.md` escrita.
- ✅ E1.6 — Diretório `docs/screenshots/0.8.0-alpha.6/` criado com `README.md` placeholder (PNGs aguardam captura pós-CI verde).
- ✅ E1.8 — Master plan atualizado (este bloco).
- ✅ Validações locais passaram: `make test`, `make layout-audit`, `make version-audit`, `make boot-perf-baseline-selftest`.
- ⏳ E1.1 — Push do branch para o GitHub (pendente — usuário decide quando).
- ⏳ E1.2 — CI roda os 6 smokes M5 (pendente — depende de E1.1 + cross-toolchain `x86_64-elf-*`).
- ⏳ E1.3 — `make release-check` em CI (host local não tem cross-compiler).
- ⏳ E1.7 — Tag `0.8.0-alpha.6+20260503` (pendente — após E1.2 + E1.3 verdes).

#### Objetivo

Fechar o release alpha.6 consolidando:

- M5 userland (fork/exec/wait/pipe + capysh + isolamento de crash) — já 95%.
- W1+W2+W3 UX (clear, task manager, browser responsiveness) — já 100%.
- Promover M4.1–M4.5 e marcar M8.2 como "pronto para iniciar" no master plan.

#### Entregáveis

- **E1.1** — Push de `feature/dev-bugfixes` ao GitHub, abertura de PR contra `develop`.
- **E1.2** — CI executa **6 smokes M5**: `fork-cow`, `exec`, `fork-wait`, `pipe`, `fork-crash`, `capysh`. Todos verdes.
- **E1.3** — CI executa **release-check**: `make test`, `make layout-audit`, `make version-audit`, `make boot-perf-baseline-selftest`, `make all64 TOOLCHAIN64=elf`, `make iso-uefi TOOLCHAIN64=elf`, `make release-checksums`, `make verify-release-checksums`.
- **E1.4** — Bump em `VERSION.yaml`, `include/core/version.h`, `README.md` para `0.8.0-alpha.6+<YYYYMMDD>`.
- **E1.5** — Release notes em `docs/releases/capyos-0.8.0-alpha.6+<date>.md` cobrindo: SYS_FORK/EXEC/WAIT/PIPE, capysh ring 3, isolamento de crash, W1/W2/W3, parser yield + timeout do browser.
- **E1.6** — Screenshots `docs/screenshots/0.8.0-alpha.6/` (capysh prompt, task manager Kill button, browser carregando com cancel).
- **E1.7** — Tag `0.8.0-alpha.6+<date>`.
- **E1.8** — Atualização deste master plan: F1 → ✅, M8.2 entra em F3.

#### Critérios de aceite

- [ ] PR `feature/dev-bugfixes` → `develop` mergeado.
- [ ] Os 6 smokes M5 passam em CI.
- [ ] `make release-check` passa em CI.
- [ ] `make version-audit` valida `VERSION.yaml` ↔ headers ↔ README ↔ screenshots ↔ release note.
- [ ] Release note publicado em `docs/releases/`.
- [ ] Tag git criada e pushed.
- [ ] STATUS.md atualizado com `0.8.0-alpha.6+<date>`.

#### Como será feito

1. `git push origin feature/dev-bugfixes`.
2. Abrir PR descrevendo W1+W2+W3.
3. Aguardar CI verde (host tests + 6 smokes M5 + release-check).
4. Mergear em `develop`.
5. Em `develop`, rodar `tools/scripts/audit_version_manifest.py` localmente; bumpar versões.
6. Escrever release note seguindo o template de `docs/releases/capyos-0.8.0-alpha.4+20260429.md`.
7. Capturar screenshots no QEMU (`make all64 && qemu-system-x86_64 ... -display gtk`).
8. Tag + push.
9. Mover `m5-userland-progress.md` e `post-m5-ux-followups.md` para `historical/` (já feito em 2026-05-01 como parte deste plano consolidado).

#### Validação

```bash
make release-check
make smoke-x64-fork-cow
make smoke-x64-exec
make smoke-x64-fork-wait
make smoke-x64-pipe
make smoke-x64-fork-crash
make smoke-x64-capysh
make version-audit
```

#### Saída esperada

`0.8.0-alpha.6` publicado. M4.1–M4.5 oficialmente "Implementado". M8.2 (browser isolado) entra como F3.

---

### F2 — DHCP no smoke oficial VMware+E1000 + assinatura de checksums

**Criticidade:** alta — fecha M2 (75% → 100%) e M6.4.
**Depende de:** F1 (release base limpo); harness VMware acessível.
**Paralelo possível com:** F3 (browser isolado) — superfícies independentes.
**Risco:** médio — depende de infraestrutura VMware externa que pode não estar disponível em CI público.
**Esforço:** 2–3 sessões.
**Branch sugerida:** `feature/m2-vmware-smoke`.

#### Objetivo

Validar que uma instalação nova em VMware+E1000:

- Boota via UEFI sem intervenção.
- Obtém DHCP automaticamente.
- Resolve DNS e faz fetch HTTP/HTTPS no `net-fetch`.
- Diagnostica falha de lease em `net-status`.

E publicar release com **checksums assinados** (Ed25519) — não só hash.

#### Entregáveis

- **E2.1** — Harness `tools/scripts/smoke_x64_vmware.py` que orquestra `vmrun`/`govc` para subir VM, capturar serial console, validar markers de boot.
- **E2.2** — Variante do `smoke_x64_cli.py` parametrizável para o harness VMware (driver=`e1000`, network=`bridged`, BIOS=`uefi`).
- **E2.3** — `make smoke-x64-vmware-dhcp` — alvo agregador.
- **E2.4** — Documentação `docs/testing/vmware-e1000-smoke.md` cobrindo setup, expectativas, troubleshooting.
- **E2.5** — Chave Ed25519 offline gerada + `tools/scripts/sign_release.py` que assina `build/release-artifacts.sha256` e emite `.sig`.
- **E2.6** — `tools/scripts/verify_release_signature.py` consumido por `verify-release-checksums`.
- **E2.7** — Documento `docs/security/release-signing.md` com procedimento de chave + rotação.

#### Critérios de aceite

- [ ] Em VMware+E1000, boot novo obtém DHCP em ≤ 10s sem comando manual.
- [ ] `net-status` mostra IP, gateway, DNS, sem `dhcp_last_error`.
- [ ] `net-fetch http://example.org` retorna 200 OK.
- [ ] `net-fetch https://example.org` valida TLS e retorna 200 OK (somente após F4 entregar TLS userland; aceite parcial: HTTP funciona, HTTPS reservado para F4).
- [ ] `make smoke-x64-vmware-dhcp` passa em CI.
- [ ] `release-check` produz `release-artifacts.sha256` + `.sig` válidos.
- [ ] `verify-release-signature.py` rejeita assinatura mutilada (teste negativo).
- [ ] M2.1–M2.5 promovidos para ✅ no master plan.
- [ ] M6.4 promovido para ✅.

#### Como será feito

1. Provisionar template VMware (kernel + ISO atual).
2. Escrever harness Python usando `govc` (free, headless).
3. Capturar log serial via `Serial.fileType=file`.
4. Adicionar markers explícitos no kernel: `[dhcp] lease ack ip=...` e `[net] ready` para o harness verificar.
5. Gerar chave Ed25519 com `openssl genpkey` ou `ssh-keygen -t ed25519`. Documentar onde fica armazenada (offline).
6. Adaptar `release-checksums` para emitir `.sig` no fim.
7. CI roda `verify-release-signature` antes de aceitar a tag.

#### Validação

```bash
make smoke-x64-vmware-dhcp
make release-check                   # agora inclui assinatura
python3 tools/scripts/verify_release_signature.py build/release-artifacts.sha256 build/release-artifacts.sha256.sig
```

#### Saída esperada

M2 fecha; M6.4 fecha. Todos os 9 critérios de aceite da release α passam.

---

### F3 — Browser em processo userland isolado + watchdog (M8.2 + W3.4)

**Status:** 🟡 **99%** — ver [`historical/f3-browser-delivered.md`](../historical/f3-browser-delivered.md)
para detalhe completo das entregas (F3.3a..F3.3h + Etapas 1/2/3 b+e
+ b-polish + b-polish++ + 3a placeholder + 3c forms MVP+polish+textarea+select +
3d tables MVP+colspan + Etapa 4 a+b + Etapa 5 rate limiter+URL policy+bytes obs+audit log,
total **~1389 asserts host** novos de F3 + integracao com a suite base).

**Entregas consolidadas** (nao voltam como tarefa):

- Protocolo IPC binario (F3.3a), stub ring-3 + embedded_progs (F3.3b).
- Chrome scaffolding logico (F3.3d) — watchdog, dispatcher, runtime,
  spawn helper, e2e integration.
- Migracao `html_viewer` → `libcapyhtml` userland (F3.3c), slices 1..6.
- Desktop wiring + URL bar editavel (F3.3f), respawn anti-storm,
  13+ markers debugcon.
- **Etapa 2** — estabilidade pre-JS (7 secoes: process_kill fecha FDs,
  deteccao defensiva de morte, FB 480×360, pipe 64 KiB, LOG forward).
- **Etapa 3 b+e** — click→navigate com hit-test + scroll vertical.
- **Etapa 3 b-polish** — EVENT_TITLE, history BACK/FORWARD com ring
  32×1024, hotkeys F5/F6/F7.
- **Etapa 3 b-polish++** — pagina de erro HTML nativa (`<h1>+3×<p>`)
  em 4 caminhos de falha, sanitizacao `<`/`>`, scroll/BACK preservam
  contexto.
- **Etapa 3 seção a (placeholder MVP)** — parser reconhece `<img>` como
  void tag (top-level e inline dentro de blocks); extrai `src` → node
  `href`, `alt` → node `text`. Layout emite `CMD_IMAGE` com dims
  default 100×80 (clampadas por viewport) + margens 4/4. Raster
  desenha placeholder: fill MUTED + borda 1px LINK + marcador 3×3 no
  canto + alt text centralizado (se couber). `+33 asserts host` (7
  parser + 16 render + 10 raster).
- **Etapa 3 seção c (forms MVP)** — parser: `<form action>` empurra
  TAG_FORM com action no `href`; `<input type/name/value/placeholder>`
  empurra TAG_INPUT com `name` em campo proprio + `text=value` +
  `bold=subtype` (text/submit/password; hidden silenciosamente
  descartado). Layout emite `CMD_INPUT` carregando subtype em
  `reserved[0]` e `node_idx` empacotado em `reserved[1..2]` para o
  engine localizar o node sem re-walk. Raster desenha caixa estilizada
  por subtipo (text=fundo MUTED, submit=fundo LINK, password=mascara
  glifos com `*`). Engine: `g_focused_input_idx` rastreia foco;
  `hit_test_doc` retorna kind (link/input_text/input_submit);
  `run_click` dispatcha NAVIGATE/foco/submit; `run_key` recebe
  `BROWSER_IPC_KEY` (codigo + mods 5 B BE) e roteia caracteres
  printable, BS, TAB (next input), Enter (submit); `run_submit`
  constroi query string `?k1=v1&k2=v2`. Chrome runtime:
  `chrome_runtime_send_key` novo. Browser_app: `focus_target`
  URLBAR vs PAGE; CLICK no frame vira PAGE, CLICK na URL bar vira
  URLBAR; Esc desfoca em PAGE antes de fechar; F5/F6/F7 sempre rodam.
  `+50 asserts host` (7 parser + 16 render + 10 raster + 4 chrome
  runtime + 13 ipc).
- **Etapa 3 seção c polish (forms refinements)** — bit
  `CAPYHTML_INPUT_FLAG_FOCUSED` no nibble alto de `reserved[0]`
  (subtype mascara low nibble); engine pos-processa cmd list
  setando esse bit no input cujo `node_idx == g_focused_input_idx`;
  raster usa HEADING color em borda 2 px e desenha caret 1×GLYPH_H
  apos os chars. Encoding RFC 3986: `build_form_query` reescrita
  com `form_query_is_unreserved` + `form_query_emit_pct` (saida
  `%XX` uppercase para qualquer byte fora de `[A-Za-z0-9-._~]`,
  espaco vira `+`). Click fora de input limpa foco e re-emite
  frame para descomissionar a borda. `+8 asserts host`
  (5 raster: focus-border, no-ring-when-unfocused, caret, no-caret-on-submit, subtype-mask-isolates-flag).
- **Etapa 3 seção d (tables MVP)** — parser reconhece `<table>`/
  `<tr>`/`<td>`/`<th>` (TH com bold=1). Layout: state machine
  com `table_col_count` derivado de TDs/THs da primeira TR;
  `cell_w = avail_w / cols` (com fallback se < TABLE_MIN_CELL_W);
  cada TD/TH emite `CMD_CELL` (kind=6) em `(col_index*cell_w,
  row_y)`; TR avanca y para a proxima linha. Defesas: tabela vazia
  emite 0 cells, excess cells em uma linha sao dropados, tabela
  aninhada fecha a anterior. Raster: `draw_cell_cmd` pinta borda
  1 px LINK em todo perimetro + bg MUTED para TH; texto em
  HEADING/TEXT color via `color_role`. `+41 asserts host`
  (11 parser + 22 render + 8 raster).
- **Etapa 3 seção d refinement (colspan + auto-fit)** — `colspan`
  atributo armazenado em `node->reserved[0]` (clamp 1..255); render
  soma colspans para derivar col_count e emite `CMD_CELL` com
  `w = colspan * cell_w`, avancando col_index por colspan. Auto-fit
  em viewport estreito: `cell_w` clampa para `TABLE_MIN_CELL_W`
  (cells overflow tolerated, raster put_pixel clipa) ao inves de
  dropar a tabela inteira. `+22 asserts` (7 parser: colspan parsed,
  default 0, oversize→255, zero→1, th colspan; 15 render: colspan
  layout, abut, clamp-to-remaining, narrow viewport).
- **Etapa 3 seção c refinement (textarea)** — parser detecta
  `<textarea name="X">body</textarea>` e empurra como TAG_INPUT
  com subtype TEXTAREA (4). Render emite CMD_INPUT com
  `INPUT_TEXTAREA_W=280, INPUT_TEXTAREA_H=72`. Raster posiciona
  texto no topo (padding 6 px) ao inves de centro vertical (que
  fica esquisito em altura grande). `+13 asserts` (4 parser: subtype,
  body capture, name, empty case; 6 render: cmd shape, h>=64;
  3 raster implicit via cell tests reuse).
- **Etapa 3 seção c refinement (select)** — parser detecta
  `<select name="X">` (subtype SELECT=5) e cada `<option value="V">L</option>`
  vira um TAG_OPTION sibling (`name=value, text=label`). O primeiro
  option label e copiado para o text do select como valor default.
  Sem `value`, o label vira o value. Render reusa as dimensoes do
  text input. Raster desenha um marcador "▼" 5 px na direita em
  HEADING color (caret e padding na esquerda preservados). `+13
  asserts` (6 parser: subtype, name, default, count, value+label,
  fallback; 5 render: cmd kind, subtype, default text, action; 2
  raster: marker presente, marker isolado).
- **Etapa 5 hardening (rate limit + URL policy + bytes obs +
  audit log)** — chrome_runtime ganha:
    1. **Rate limiter incoming**: contadores `incoming_in_window` +
       `total_incoming_drops`, novo status `POLL_RATE_LIMITED`,
       `chrome_runtime_poll_event` recusa quando excede
       `CHROME_RUNTIME_INCOMING_RATE_MAX = 64/tick`;
       `chrome_runtime_tick` reseta. browser_app trata RATE_LIMITED
       como break do loop.
    2. **URL whitelist policy (opt-in)**: novo setter
       `chrome_runtime_set_url_policy(fn)`. Quando instalado,
       `chrome_runtime_send_navigate` chama o callback antes de
       escrever no pipe; deny incrementa `total_url_blocked` e
       retorna -1 sem tocar o I/O.
    3. **Bytes observability**: contador `total_event_bytes_received`
       (u64) acumulado de header+payload de cada evento admitido,
       util para detectar engine vazando memoria via spam.
    4. **Audit log subsystem**: modulo dedicado
       `src/apps/browser_chrome/audit_log.{c,h}` com ring buffer de
       32 entradas (categoria + code + seq monotonico). Categorias:
       NAV, RATE_DROP, POLICY_DENY, ENGINE_EOF, PROTOCOL, FETCH.
       API: `capyc_audit_init/record/count/visible/at`. Hooks em
       `chrome_runtime_send_navigate` (NAV / POLICY_DENY),
       `chrome_runtime_poll_event` (RATE_DROP / ENGINE_EOF /
       PROTOCOL), `chrome_runtime_dispatch_pending_fetch` (FETCH).
       Ring envolve para sobrescrever entrada mais antiga em O(1);
       sem alocacao dinamica. 4 testes unitarios diretos do ring +
       3 de integracao com runtime; total **+45 asserts** distribuidos
       em `test_browser_chrome_runtime_rate.c` (3 audit + 3 rate +
       3 policy + 1 bytes), `test_browser_chrome_audit.c` (4 testes:
       init zera, record em ordem, wraparound, null safety).
- **Etapa 4 a+b** — HTTP/HTTPS reais via bridge kernel-side
  (`net/http.h` + BearSSL TLS 1.2), URL auto-prefix `http://`.

**Criticidade:** alta. **Depende de:** F1 (M5 mergeado). **Paralelo
possivel com:** F2, F4. **Branch sugerida:** `feature/f3-browser-isolation`.

#### Restante para fechar F3 a 100%

Cada item abaixo e um incremento independente; nao ha dependencia
sequencial forte entre eles (pode ser qualquer ordem).

##### Etapa 3 seção a — Imagens inline (MVP placeholder ✅; fetch/decode pendente)

**Progresso 2026-05-03:** parser/render/raster agora suportam `<img>`
com placeholder visivel (ver "Entregas consolidadas" acima). Pagina
com `<img>` renderiza sem swallow de nodes vizinhos; o retangulo
indicativo serve de affordance visual ate a decode real chegar.

**Gap remanescente:** placeholder nao baixa nem decodifica a imagem
real. Para fechar 100% da seção, falta o pipeline de fetch+decode.

**Entregaveis:**

- Parser: ~~IMG node~~ ✅; opcional: parsear `width`/`height` attrs
  para sobreescrever dims default 100×80.
- Render: ~~CMD_IMAGE emitido~~ ✅.
- Raster: ~~placeholder visivel~~ ✅; upgrade: substituir fill por
  pixels BGRA decodificados quando disponiveis.
- **Engine**: adicionar `BROWSER_IPC_EVENT_IMAGE_REQUEST` (url, img_id)
  e `BROWSER_IPC_IMAGE_RESPONSE` (img_id, width, height, pixels BGRA).
  Engine emite REQUEST apos parse; armazena pixels em cache
  `.bss` indexado por URL hash; raster blitta do cache em vez do
  placeholder quando disponivel.
- **Chrome**: reaproveitar `png_loader.c`/`jpeg_loader.c` no
  runtime para decode; cache `.bss` 1-2 MiB para evitar re-fetch.
- Alternativa *simpler*: chrome compoe sobre o frame do engine no
  compositor (requer saber as coordenadas das IMAGEs; engine pode
  incluir uma tabela `img_rects[]` no EVENT_FRAME extra-payload).

**Criterios de aceite (remanescentes):**

- `<img src="http://.../image.png">` em pagina real renderiza
  pixels decodificados (PNG/JPEG) no lugar do placeholder.
- Falha de decode mantem o placeholder com alt text.
- Largura/altura vem de atributos HTML quando presentes.
- Cache evita re-fetch na mesma navegacao (F5/scroll/navigate-back).

##### Etapa 3 seção c — Form inputs (MVP+polish ✅; refinamentos avancados pendentes)

**Progresso 2026-05-03:** parser/render/raster/engine/runtime/browser_app
suportam `<form>` + `<input type="text/submit/password">` end-to-end
incluindo indicador visual de foco (borda 2 px HEADING + caret) e
percent-encoding RFC 3986 (`%XX` para chars nao-unreserved). Ver
"Entregas consolidadas" acima.

**Gap remanescente (impacto medio):**

- **`<textarea>` e `<select>/<option>`**: nao parseados ainda;
  textarea pode reusar TAG_INPUT com flag de multilinha; select
  precisa de novo node + listbox dropdown.
- **Submit por POST**: hoje so GET (query string em URL); para POST,
  precisa-se de body separado no NAVIGATE + `method` no FORM node.
- **Validacao client-side basica**: `required`, `minlength`,
  `pattern` ignorados; aceitar tudo no submit.
- **Cursor blink**: caret atualmente solido; visualmente parece
  fixo. Idealmente alterna 500 ms via timer.

**Criterios de aceite (remanescentes):**

- `<textarea>` aparece como caixa multi-linha editavel.
- Form com `method="post"` envia body separado.

##### Etapa 3 seção d — Tabelas (MVP ✅; refinamentos pendentes)

**Progresso 2026-05-03:** parser/render/raster suportam tabelas
2D simples com `<table>/<tr>/<td>/<th>`. Layout em grade fixa
(largura igual por coluna), borda 1 px LINK ao redor de cada
celula, header (TH) com bg MUTED + texto HEADING. Ver
"Entregas consolidadas" acima.

**Gap remanescente:**

- **Box model CSS inline `style=`**: hoje sem padding/margin/border
  customizaveis; reativar parser minimalista de `style="padding:4px;
  border: 1px solid #x"` (reusa logica do
  `src/apps/css_parser/` removido na slice 6 — portar o minimo para
  userland).
- **`colspan` / `rowspan`**: hoje cada celula ocupa exatamente 1
  posicao de grade; tabelas reais usam spans para "merge" de celulas.
- **Larguras variaveis por coluna**: hoje `cell_w = avail_w / cols`
  igual; sites reais dependem de auto-fit por conteudo.
- **Scroll horizontal** quando `cols * TABLE_MIN_CELL_W > viewport_w`:
  hoje a tabela inteira e descartada (`table_col_count = 0`); melhor
  emitir mesmo assim e deixar o usuario scrollar.

**Criterios de aceite (remanescentes):**

- `<div style="padding: 8px; border: 1px solid #333">` mostra borda.
- `<td colspan="2">` ocupa duas colunas.
- Tabela com 8 colunas em viewport 480 px ainda eh visivel
  (scroll horizontal ou auto-fit text).

##### Etapa 3 seção f — Fonte real (não 8×8)

**Gap:** Fonte embutida e 8×8 monospace. Pagina moderna fica ilegivel.

**Entregaveis:**

- Parser TTF minimalista ou bundle de bitmap 16×16 de open-source
  font (`userland/assets/fonts/capy-16.bmp`).
- `capyhtml_font_ops_default` passa a apontar para a nova fonte.
- Glyph cache em `.bss` (256 glifos × 16×16 = 64 KiB).

**Criterios de aceite:**

- Texto renderizado tem serifas/proporcoes razoaveis (nao mais
  pixel-blocky 8×8).
- `capyhtml_raster` zero-copy continua funcionando.

##### Etapa 5 — Hardening pré-JS

**Gap:** Browser ainda confia no engine ring-3; sem rate limiting,
budget de memoria, audit log, whitelist.

**Entregaveis:**

- **Seccomp-like syscall filter** aplicado ao engine em
  `browser_engine_spawn`: bloqueia `open/exec/socket/etc`. So permite
  `read/write/exit/yield/mmap limitado`.
- **Memory budget per nav**: chrome contabiliza bytes alocados
  pelo engine (via stat de paginas mapeadas); mata em excesso.
- **IPC rate limiting**: chrome limita frames/sec emitidos pelo
  engine (ex. 60 fps).
- **URL whitelist/blacklist** opcional (feature flag):
  `/etc/browser/policy.list` com dominios permitidos.
- **Audit log**: emit `[audit] [browser] nav_started url=X`,
  `[browser] nav_failed reason=...` via klog.

**Criterios de aceite:**

- Engine com loop infinito e morto por rate limit antes de travar
  o chrome.
- Engine tentando abrir um arquivo e bloqueado por syscall filter
  (audit log evidencia).
- Tests: novo `test_browser_hardening` cobre os gates.

##### Validacao visual em QEMU (bloqueio transversal)

- **Depende de:** cross-toolchain `x86_64-elf-*` em CI (nao disponivel
  no host macOS de desenvolvimento).
- **Alvo:** `make smoke-x64-browser-spawn` passa com 9 markers
  debugcon. Harness ja pronto em
  `tools/scripts/smoke_x64_browser_spawn.py`.

#### Saida esperada (quando 100%)

M8.2 fecha; browser e o primeiro app real em ring-3 com HTML/CSS
maturity suficiente para paginas web comuns (texto, links, imagens,
forms, tabelas) e com hardening que impede crash-as-a-service.

---

### F4 — Stack de sockets userland + TLS (libcapy-net + libcapy-tls)

**Status:** 🟡 **20%** — Etapa 4 seções a (HTTP) + b (HTTPS) antecipadas via bridge kernel-side em F3.3g (2026-05-02). Seções c (libcapy-net userland) + d (libcapy-tls userland) ainda precisam ser entregues para o objetivo original (apps ring 3 fazendo rede sem bypass pelo kernel).
**Criticidade:** alta — destrava update real, browser HTTPS, qualquer IPC de rede futuro.
**Depende de:** F1 (M5 fork/exec) + parcialmente F3 (motivação real). Pode iniciar antes de F3 fechar.
**Paralelo possível com:** F3.
**Risco:** alto — toca rede, criptografia e ABI userland.
**Esforço:** 5–8 sessões.
**Branch sugerida:** `feature/f4-userland-net`.

#### Progresso 2026-05-02 (bridge kernel-side entregue em F3.3g)

- ✅ HTTP real funcional no browser: `chrome_runtime_dispatch_pending_fetch` em `src/apps/browser_chrome/runtime.c` prefixo-checa `http://` e delega a `http_get()` de `src/net/services/http/redirect_download.c` (stack TCP + headers + redirects + BearSSL TLS 1.2 já presente no kernel antes de F4).
- ✅ HTTPS reutiliza o mesmo caminho (`http_get` escolhe TLS via `use_tls` dentro do `http_parse_url`).
- ✅ Buffers IPC escalados: chrome `g_fetch_response_scratch` 1 MiB + 4 KiB em `.bss`; engine ring 3 `g_request_payload` + `g_fetch_scratch` 1 MiB + 4 KiB cada.
- ⏳ Este bridge **não é o design final de F4**: é uma ponte que mora no kernel, violando temporariamente o princípio "apps em ring 3 não chamam syscalls HTTP diretamente". Quando F4 c/d chegarem, o bridge migra para uma call em `libcapy-net` e o `chrome_runtime_dispatch_pending_fetch` passa a ser pure-orchestration.
- ⏳ Sem isolamento criptográfico em ring 3: BearSSL roda em kernel context; um bug na engine TLS afeta o kernel. F4 c/d recompilam BearSSL como lib userland.

#### Objetivo

Hoje, rede é **chamada direta de kernel** (browser chama `http_request_send` dentro do kernel). Para apps reais isolados, precisamos de **socket syscalls + libcapy-net em userland**.

#### Entregáveis

- **E4.1** — `SYS_SOCKET`, `SYS_BIND`, `SYS_CONNECT`, `SYS_SEND`, `SYS_RECV`, `SYS_CLOSE` (close já existe).
- **E4.2** — File descriptor type `FD_TYPE_SOCKET` análogo a `FD_TYPE_PIPE`. Aproveita stack de rede atual no kernel via wrappers.
- **E4.3** — `userland/lib/capylibc-net/` — wrappers `capy_socket()`, `capy_connect()`, etc.
- **E4.4** — `userland/lib/capylibc-tls/` — TLS 1.2/1.3 via BearSSL (já no `third_party/`). Compila como userland blob.
- **E4.5** — Migração do `capybrowser` (F3) para usar libcapy-net + libcapy-tls em vez de syscalls atuais bypass.
- **E4.6** — Host tests + smoke `smoke-x64-tls-handshake` (cliente TLS conecta a servidor local QEMU `httpd`).
- **E4.7** — TLS trust store userland: re-empacota `src/security/tls_trust_anchors.c` como bundle userland.

#### Critérios de aceite

- [ ] Cliente userland faz TLS handshake com servidor de teste e troca dados.
- [ ] `capybrowser` (F3) navega `https://example.org` via libcapy-tls, sem chamar kernel TLS.
- [ ] Falha de cert é reportada com erro estável.
- [ ] `net-fetch https://...` no shell continua funcionando (legacy: ainda usa kernel; refatoração futura).

#### Como será feito

1. **F4a** (2 sessões): syscalls + FD type. Reutiliza buffer pipe pattern.
2. **F4b** (2 sessões): libcapy-net stubs + smokes básicos (TCP plaintext).
3. **F4c** (3 sessões): libcapy-tls (BearSSL recompilado como userland).
4. **F4d** (1 sessão): migração capybrowser.

#### Validação

```bash
make smoke-x64-tls-handshake
make smoke-x64-browser-https
```

#### Saída esperada

Apps userland podem fazer rede sem bypass. Caminho para update real (F5) fica aberto.

---

### F5 — Update real via GitHub Releases (fetch + Ed25519)

**Criticidade:** média — produto utilizável existe sem isso, mas distribuição automática depende.
**Depende de:** F4 (TLS userland) + F2 (assinatura de release já gerada).
**Paralelo possível com:** F6 (GUI).
**Risco:** médio — toca staged update + boot slot já entregues; risco controlado.
**Esforço:** 3–4 sessões.
**Branch sugerida:** `feature/f5-update-fetch`.

#### Objetivo

`update-agent` deixa de depender de `update-import-manifest` manual e passa a:

1. Buscar manifest assinado de `https://github.com/<repo>/releases/latest`.
2. Validar Ed25519.
3. Baixar payload (kernel `.bin` + manifest novo).
4. Validar SHA-256 (já existe via M6.4).
5. Stage + arm + reboot + confirm health (já existe via M7.5).

#### Entregáveis

- **E5.1** — `update_agent_fetch_remote_manifest()` — usa libcapy-net + libcapy-tls.
- **E5.2** — Ed25519 verifier embarcado no kernel (BearSSL já fornece).
- **E5.3** — Comandos shell: `update-fetch`, `update-apply`.
- **E5.4** — Política: canal `develop` busca `develop` branch releases; canal `stable` busca tags `v*.*.*`.
- **E5.5** — Smoke `smoke-x64-update-fetch` (servidor HTTP local serve manifest fake assinado).
- **E5.6** — Documentação `docs/operations/update-from-github.md`.

#### Critérios de aceite

- [ ] `update-fetch` baixa manifest do GitHub, valida assinatura.
- [ ] `update-apply` aplica payload via `update_agent_apply_boot_slot_verified`, confirma health, faz rollback se confirmar falhar.
- [ ] Manifest mutilado é rejeitado.
- [ ] Manifest com versão menor que a atual é rejeitado (downgrade protection).
- [ ] Suite passa.

#### Como será feito

1. **F5a** Implementar fetch + verify.
2. **F5b** Adicionar comandos shell + audit log `[audit] [update] fetched ...`.
3. **F5c** Smoke ponta-a-ponta com servidor HTTP local em QEMU user-mode network.

#### Validação

```bash
make smoke-x64-update-fetch
```

#### Saída esperada

Sistema atualizável sem reinstalação manual.

---

### F6 — Sessão gráfica completa (mouse fim-a-fim, login GUI, dispatcher)

**Criticidade:** alta — sem isso, GUI fica "scaffold visível mas inerte" como hoje.
**Depende de:** F1 (release base limpo). Pode iniciar imediatamente após F1.
**Paralelo possível com:** F4, F5.
**Risco:** médio — código existe; bottleneck é integração e timing.
**Esforço:** 4–6 sessões.
**Branch sugerida:** `feature/f6-gui-session`.

#### Objetivo

Hoje:

- `desktop_init/run_frame` existe mas autostart é frágil (extended.c chama mas pode bloquear o login runtime).
- Mouse PS/2 inicializa mas não há dispatcher unificado de eventos.
- Terminal gráfico não consome shell real fim-a-fim — W1 mitigou parte (clear callback), falta o resto.
- Login é por TTY framebuffer; não há login GUI.

Depois desta fase:

- Boot → splash → login GUI → desktop com mouse/teclado/janelas/foco/abertura de apps.

#### Entregáveis

- **E6.1** — Loginwindow GUI: tela de senha em compositor, com lista de usuários, falha visível.
- **E6.2** — `desktop_run_frame` integrado ao loop principal do kernel (não mais via spin de `cmd_desktop_start`); usa timer APIC, não `for(volatile)`.
- **E6.3** — Mouse: drivers PS/2 + USB HID (xHCI já existe parcial). Eventos chegam ao compositor via `input_runtime`.
- **E6.4** — Dispatcher central de eventos: keyboard + mouse + window focus + IPC. Substitui handlers ad-hoc.
- **E6.5** — Taskbar funcional com botão de "Iniciar" (lista de apps), relógio (já existe), notificações simples.
- **E6.6** — Terminal gráfico consome shell real (já parcial via W1, fechar fim-a-fim).
- **E6.7** — Fallback CLI: tecla F1 no login GUI volta para shell TTY (recovery/dev).
- **E6.8** — Smoke `smoke-x64-gui-session` (sendkey via QEMU, valida boot → login → click em ícone → app abre).

#### Critérios de aceite

- [ ] Boot novo entra em login GUI sem comando manual.
- [ ] Login com credencial válida abre desktop.
- [ ] Mouse move, click e drag funcionam em VMware+E1000+SVGA II.
- [ ] Janela tem foco, pode ser arrastada, fechada.
- [ ] Taskbar mostra apps abertos; click alterna foco.
- [ ] Terminal gráfico aceita comandos e renderiza saída.
- [ ] CTRL+ALT+F1 muda para TTY de recovery.

#### Como será feito

1. **F6a** Migrar autostart desktop para timer-driven loop.
2. **F6b** Mouse PS/2 → input_runtime → compositor.
3. **F6c** Dispatcher unificado.
4. **F6d** Loginwindow GUI (escreve em `auth/user.c` via syscall).
5. **F6e** Taskbar polimento + smoke.

#### Validação

```bash
make smoke-x64-gui-session
make smoke-x64-mouse-events           # novo: clica em coordenada conhecida e valida hit-test
```

#### Saída esperada

Sessão gráfica utilizável fim-a-fim. Base para F7.

---

### F7 — Apps básicos do desktop

**Criticidade:** média — torna o sistema utilizável sem CLI.
**Depende de:** F3 (browser isolado serve de pattern), F6 (GUI completa).
**Paralelo possível com:** F8 (package manager).
**Risco:** baixo — apps são compositores de syscalls + libs já existentes.
**Esforço:** 1 sessão por app + 1 sessão por suite final = ~7 sessões.
**Branch sugerida:** `feature/f7-apps-basicos`.

#### Objetivo

Pacote inicial de apps do desktop:

| App | Estado atual | Meta F7 |
|---|---|---|
| `task_manager` | ✅ Funcional (W2) | Polimento: gráfico de CPU/RSS por processo |
| `text_editor` | scaffold | Editar/salvar arquivos VFS, syntax highlight básico |
| `file_manager` | scaffold | Browse, copy, move, delete em CAPYFS |
| `settings` | scaffold | Network mode, theme, language, user mgmt |
| `image_viewer` | parcial (no html_viewer) | App standalone: PNG/JPEG, zoom, próximo/anterior |
| `calculator` | inexistente | Simples: 4 ops, decimal, hex |
| `log_viewer` | inexistente | Browse `/var/log/*`, filtro por nível |

Todos como **processos ring 3 isolados** (lição de F3).

#### Entregáveis

- **E7.1** — `userland/bin/text_editor/`, `userland/bin/file_manager/`, etc.
- **E7.2** — `src/apps/<app>_chrome/` widgets do compositor (frames + IPC, igual capybrowser).
- **E7.3** — `userland/lib/libcapy-ui/` — toolkit minimalista (button, list, textbox, dialog).
- **E7.4** — Smokes por app.
- **E7.5** — Pacote inicial de ícones em `assets/icons/`.

#### Critérios de aceite

- [ ] Cada app abre, executa função primária, fecha sem crash.
- [ ] Falha de um app não derruba desktop.
- [ ] Todos passam smoke.

#### Como será feito

Por app (1 sessão):

1. Definir IPC (input/output via pipe).
2. Implementar binário userland.
3. Implementar chrome no compositor.
4. Smoke.

#### Validação

```bash
make smoke-x64-text-editor
make smoke-x64-file-manager
make smoke-x64-settings
make smoke-x64-image-viewer
make smoke-x64-calculator
make smoke-x64-log-viewer
```

#### Saída esperada

Usuário comum opera sem CLI.

---

### F8 — Package manager + SDK + ABI estável

**Criticidade:** média — destrava ecossistema, mas não é cliente-final.
**Depende de:** F4 (rede userland), F5 (update fetch reaproveita boa parte), F7 (apps reais como cliente).
**Paralelo possível com:** F9 (JS) e F10 (linguagem).
**Risco:** médio — define ABI que ficará estável a longo prazo.
**Esforço:** 6–10 sessões.
**Branch sugerida:** `feature/f8-pkgd`.

#### Objetivo

Distribuir apps separadamente do kernel. Hoje, todo binário userland é embarcado no kernel via `embedded_progs`. Isso não escala.

#### Entregáveis

- **E8.1** — Formato `.capypkg` (tar+manifest+signature).
- **E8.2** — `pkgd` (daemon ring 3): `install`, `remove`, `list`, `verify`.
- **E8.3** — Repositório oficial GitHub-hosted (`capyos-packages` repo).
- **E8.4** — `software-center` (app GUI para descoberta/install).
- **E8.5** — ABI documentada: `docs/api/abi-stable.md` listando syscall numbers, struct layouts congelados.
- **E8.6** — `libcapy-fs`, `libcapy-ui`, `libcapy-net`, `libcapy-tls` como SOs userland (links dinâmicos? ou estáticos? **Decisão de F8**: começar estáticos, links dinâmicos depois).
- **E8.7** — SDK header pack + samples + `capyos-sdk` repo separado.
- **E8.8** — Smoke `smoke-x64-pkg-install` (instala pacote local, executa, remove).

#### Critérios de aceite

- [ ] `pkg install hello` baixa, valida assinatura, instala em `/usr/bin/hello`.
- [ ] `pkg list` lista instalados com versões.
- [ ] `pkg remove hello` remove e atualiza DB.
- [ ] DB de pacotes sobrevive reboot.
- [ ] Update de pacote sem reinstalar OS.

#### Como será feito

Sequência longa; ver branch dedicada com sub-fases F8a–F8h documentadas no PR.

#### Validação

```bash
make smoke-x64-pkg-install
make smoke-x64-pkg-update
```

#### Saída esperada

Plataforma aberta para software de terceiros.

---

### F9 — JavaScript engine sandboxed (M8.6)

**Criticidade:** baixa-média — essencial para web moderna, opcional para SO.
**Depende de:** F3 (browser isolado), F4 (rede), F8 (formato de app pode reaproveitar bytecode).
**Paralelo possível com:** F10.
**Risco:** alto — JS engine é projeto enorme em si.
**Esforço:** 10+ sessões. **Decisão arquitetural**: usar QuickJS (C, 200KB, MIT) ou escrever subset (CapyJS). Decidir no início da fase.
**Branch sugerida:** `feature/f9-js-engine`.

#### Objetivo

Renderizar páginas que dependem de JS minimamente (sem React/Angular completos). Sandboxed dentro do processo `capybrowser`.

#### Entregáveis

- **E9.1** — Decisão: integrar QuickJS ou escrever CapyJS subset (ECMAScript 5 mínimo).
- **E9.2** — Bridge DOM ↔ JS no `capybrowser`.
- **E9.3** — Budget de execução (op_budget já tem): mata script de loop infinito.
- **E9.4** — Sem `eval` arbitrário, sem acesso a syscalls (só DOM bridge).
- **E9.5** — Smokes `js-basic`, `js-dom-mutation`, `js-timeout-killed`.

#### Critérios de aceite

- [ ] Página com `document.title = "Hello"` atualiza título.
- [ ] Loop infinito é interrompido em ≤ 5s pelo budget.
- [ ] M8.6 promovido para ✅.

#### Validação

```bash
make smoke-x64-js-basic
make smoke-x64-js-timeout
```

---

### F10 — CapyLang (linguagem própria)

**Criticidade:** baixa — branding/ecossistema.
**Depende de:** F8 (precisa de package manager para distribuir runtime).
**Paralelo possível com:** F9.
**Risco:** alto — projeto longo.
**Esforço:** 20+ sessões em três etapas.
**Branch sugerida:** `feature/f10-capylang`.

#### Objetivo

Linguagem nativa do CapyOS para automação primeiro, apps depois.

#### Entregáveis (em ondas)

- **Etapa 1 — automação:** parser, VM bytecode, bindings shell/FS/config. Roda como `.capyscript` interpretado.
- **Etapa 2 — apps utilitários:** módulos, FFI controlada, integração com libcapy-ui.
- **Etapa 3 — primeira-classe:** stdlib, async, LSP, formatter.

#### Critérios de aceite (Etapa 1)

- [ ] Script `.capyscript` executa em ring 3 via `capylang` interpreter.
- [ ] Bindings: `fs.read`, `fs.write`, `shell.exec`, `config.get/set`.
- [ ] Smoke `smoke-x64-capylang-hello`.

---

## 5. Roadmap macro pós-F10 (visão sem datas)

| Fase futura | Tema | Pré-requisitos |
|---|---|---|
| F11 | Secure Boot + measured boot | F2 (assinatura), F8 (chain) |
| F12 | SMP / multicore | scheduler atual está single-core |
| F13 | USB completo (mass storage, áudio, câmera) | xHCI base existe |
| F14 | GPU acelerada (drivers reais para Intel/AMD/NVIDIA) | F12 desejável |
| F15 | Compatibilidade Linux subset (syscall translation layer) | F4 + F8 |
| F16 | Office suite, multimedia stack, IDE própria | F8 + F10 + ecossistema |
| F17 | IA local (LLM small) | F12 + F14 |

---

## 6. Validação — comandos canônicos

| Cenário | Comando |
|---|---|
| Validação mínima após qualquer mudança | `make test && make layout-audit && make all64` |
| Pré-commit completo | `make release-check` |
| Pré-release | `make release-check && make smoke-x64-vmware-dhcp` (após F2) |
| Smokes M5 | `make smoke-x64-fork-cow smoke-x64-exec smoke-x64-fork-wait smoke-x64-pipe smoke-x64-fork-crash smoke-x64-capysh` |
| Smokes M4 preemptivo | `make smoke-x64-preemptive-all` |
| Smokes browser (após F3) | `make smoke-x64-browser-isolation smoke-x64-browser-watchdog` |
| Smokes GUI (após F6) | `make smoke-x64-gui-session smoke-x64-mouse-events` |

---

## 7. Layout / clean code (regras vivas)

Mantidas do M3 fechado. Não voltam a ser fase porque já estão em CI:

- ≤ 900 linhas por arquivo C/runtime/teste (exceções documentadas: `tls_trust_anchors.c` por dados estáticos).
- Headers privados em `internal/<modulo>_internal.h`.
- `make layout-audit --strict` em CI; bloqueia merge.
- Includes cruzados de `internal/` entre módulos distintos são erro.
- Funções utilitárias canônicas: `kstring.h` (`kstrlen`, `kstreq`, `kmemzero`, etc.). Duplicar é violação.

---

## 8. Critérios globais de "SO sólido"

CapyOS é considerado **estruturalmente sólido** quando:

### Plataforma
- [ ] Boot oficial previsível (M1 ✅, F2 fecha smoke).
- [ ] Update sem reinstalação (F5).
- [ ] Rollback automático (M7 ✅).
- [ ] Logs persistentes (M6.2 ✅).

### Kernel
- [x] Multitarefa real (M4 ✅).
- [x] Isolamento de processos (M4 + M5 ✅).
- [x] Syscalls definidas (M5 + ABI documentar em F8).
- [x] Panic/fault observáveis (M4 fase 4 ✅).

### Storage
- [x] Replay/journal (M7 ✅).
- [x] Fsck (M7 ✅).
- [x] Integridade autenticada (M6.5 ✅).
- [x] Política de auth (M6.1 ✅).

### Rede
- [ ] Sockets userland (F4).
- [ ] TLS (F4).
- [ ] Updates seguros (F5).
- [ ] Firewall mínimo (futuro F11+).

### Desktop
- [ ] Sessão gráfica com mouse (F6).
- [ ] Apps básicos (F7).
- [ ] UI de update (F5 + F8).

### Ecossistema
- [ ] SDK (F8).
- [ ] Formato de app (F8).
- [ ] Linguagem própria (F10).

**Score atual:** 11/22 ✅ (50% dos critérios estruturais).
**Score após F1–F8:** 21/22 ✅ (95%).

---

## 9. Sequência de releases recomendada

| Versão | Conteúdo | Fase fechada |
|---|---|---|
| `0.8.0-alpha.6` | M5 userland + W1/W2/W3 + browser ring-3 | **F1** |
| `0.8.0-alpha.6` | DHCP smoke VMware + assinatura release | **F2** |
| `0.8.0-beta.1` | Browser isolado + watchdog | **F3** |
| `0.8.0-beta.2` | Sockets userland + TLS | **F4** |
| `0.9.0-alpha.1` | Update via GitHub | **F5** |
| `0.9.0-beta.1` | GUI sessão completa | **F6** |
| `0.9.0` | Apps básicos | **F7** |
| `0.10.0` | Package manager + SDK | **F8** |
| `0.11.0+` | JS engine | **F9** |
| `0.12.0+` | CapyLang Etapa 1 | **F10 (Etapa 1)** |
| `1.0.0` | CapyLang completa + Secure Boot + SMP | F10 + F11 + F12 |

---

## 10. Manutenção deste documento

**Atualize sempre que:**

- Uma fase F1–F10 muda de status.
- Um item promovido para ✅ ganha evidência (teste/smoke/release note).
- Uma decisão arquitetural de impacto (ABI, formato de pacote, etc.) é tomada.
- Um hotfix significativo é aplicado (registrar em "Itens fora do plano formal" + abrir entrada na fase relevante).

**Não crie outros planos em `active/`.** Se for inevitável (ex.: branch experimental longo), o plano novo:

1. Refere este como pai;
2. Vive em `experimental/` se for laboratório;
3. Vai para `historical/` ao fim, com o conteúdo migrado para uma fase deste plano.

A regra é simples: **um único plano linear, evidência para cada promoção, nada paralelo sem justificativa documentada.**

---

## Apêndice A — Mapeamento dos planos antigos

Os documentos abaixo foram **arquivados em `historical/`** em 2026-05-01 e estão consolidados aqui:

| Documento antigo | Conteúdo migrado para |
|---|---|
| `capyos-robustness-master-plan.md` | §2.1 (M0–M8 entregue) + §4 (F1–F10) |
| `m5-userland-progress.md` | §2.1 (M5-userland) + F1 |
| `post-m5-ux-followups.md` | §2.1 (W1–W3) |
| `system-master-plan.md` | §3 (princípios) + §4 (F4–F10) + §5 (roadmap pós) |
| `system-roadmap.md` | §4 (distribuído por fase) + §7 (clean code) |
| `system-execution-plan.md` | §4 (etapas A–G distribuídas em F1–F10) |
| `capyos-master-improvement-plan.md` | §2.1 (fases CONCLUIDO) + §4 (F6 GUI fix integration) |
| `browser-status-roadmap.md` | §2.1 (M8.1/8.3/8.4/8.5 + W3) + F3 (M8.2) + F9 (M8.6) |
| `source-organization-roadmap.md` | §7 (regras vivas) |
| `historical/m4-finalization-progress.md` | §2.1 (M4 100%) — permanece como referência técnica |

Este apêndice serve apenas para rastreabilidade. Não consultá-los para decisões — usar sempre este master plan.

---

## Apêndice B — Itens fora dos planos formais (hotfixes registrados)

| Item | Status | Onde foi consolidado |
|---|---|---|
| Boot UEFI/EBS robusto (Print fora da janela crítica + dbgcon markers) | ✅ Aplicado | M0/F2 (referência futura em release notes) |
| `com1.c/h` → `serial_com1.c/h` (workaround exFAT macOS) | ✅ Aplicado | §7 (clean code — exceção documentada de naming) |
| Remoção do limite artificial de 1 GiB no framebuffer | ✅ Aplicado | M0 (já consolidado em master plan antigo) |
| Print de `[UEFI] GOP fb base=...` no loader | ✅ Aplicado | Diagnóstico permanente (M0) |
| W1 callback de clear context-aware | ✅ 2026-04-30 | §2.1 |
| W2 task_manager_tick + Kill button | ✅ 2026-04-30 | §2.1 |
| W3 parser yield + timeout 30s | ✅ 2026-04-30 | §2.1 |
