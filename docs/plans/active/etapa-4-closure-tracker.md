# Etapa 4 — Closure Tracker (código ↔ testes ↔ docs ↔ gate externo)

**Etapa:** 4 — CapyDisplay 2D + scheduler/multithread runtime.
**Versão de referência:** `0.8.0-alpha.262+20260602`.
**Plataforma oficial:** VMware + UEFI + E1000.
**Última atualização:** 2026-06-02.

**Ordem de autoridade desta etapa:**

1. [`capyos-master-plan.md`](capyos-master-plan.md) §7 — definição e critérios de aceite (autoritativo).
2. [`../STATUS.md`](../STATUS.md) — status executivo.
3. **Este tracker** — estado por fase amarrando código, host tests e gate externo.
4. [`../../operations/etapa-4-external-validation-playbook.md`](../../operations/etapa-4-external-validation-playbook.md) — passos do operador externo.

> **Propósito:** evitar o drift que existia entre o código real (Fases
> A-E entregues) e os textos de "próximo passo" no master-plan/STATUS.
> Esta tabela é a fonte única de estado **por fase**. **Atualize este
> arquivo no MESMO commit** em que mudar código, host tests, gate ou
> status de qualquer fase. O master-plan e o STATUS devem apenas
> apontar para cá em vez de duplicar o detalhe por fase.

---

## 1. Resumo de fechamento

- **Fases A, C, D, E:** ✅ fechadas em **código + host tests** (estado
  alpha.260, empacotadas no release alpha.261).
- **Fase B:** 🟡 **capability entregue e exercitada** por fluxos reais; a
  migração dos fluxos de produção restantes é **polish não-bloqueante**
  (o critério de aceite de capability — render via adapter sem acesso
  direto ao compositor — já está atendido).
- **Fase F:** ⛔ **pendente** — validação externa em VMware oficial.
  **Não executável nesta workspace** (review/edit only).
- **Bloqueador único para declarar a Etapa 4 fechada:** Fase F.

---

## 2. Matriz por fase

| Fase | Entrega | Código (artefatos) | Host tests | Gate externo (Makefile) | Status |
|---|---|---|---|---|---|
| **A** | Adapter CapyOS-side para `capy-ui-widget` v2.22 / display-list schema v7 | `include/gui/capyui_display_adapter.h`; `src/gui/widgets/capyui_display_adapter.c`; Makefile sibling detection `CAPYOS_HAVE_CAPYUI_WIDGET` consumindo `../CapyUI/src/widget/capy_display_list.h` | `tests/gui/test_capyui_display_adapter.c` → `test_capyui_display_adapter_run()` | (sem gate exclusivo; regressão Etapa 3) | ✅ código + host tests |
| **B** | Produtor real CapyUI alimentando o adapter via seam | `capyui_display_adapter_render_producer_window()`; callers core: `terminal_display_list.c`, `context_menu_display_list.c`, `inline_prompt_display_list.c` | coberto por `test_capyui_display_adapter_run()` (usa `capy_widget_emit` real do sibling) | (sem gate exclusivo) | 🟡 capability entregue; restam fluxos de produção (não-bloqueante) — ver §4 |
| **C** | Scheduler cooperativo + multithread runtime | `include/kernel/scheduler.h` (`SCHED_POLICY_COOPERATIVE`); `src/kernel/scheduler.c` (`scheduler_yield` anti-starvation); latch em `src/kernel/scheduler_smoke*` (flag `CAPYOS_SCHEDULER_FAIRNESS_SMOKE`) | `tests/kernel/test_scheduler_smoke_gate.c` → `run_scheduler_smoke_gate_tests()` | `make smoke-x64-vmware-scheduler-fairness` → marker `[smoke] scheduler-fairness ready` | ✅ código + host tests |
| **D** | Damage tracking + double buffering + glyph cache | `src/gui/core/compositor_damage.c`, `compositor_render.c`, `compositor_smoke.c`, `compositor_smoke_io.c`; cache de glyphs 8x16; fix "cursor erase scoped to overlap" | `tests/gui/test_compositor_events.c` → `test_compositor_events_run()` (inclui `test_compositor_cursor_erase_only_on_overlap`); `test_compositor_smoke_gate_run()` | `make smoke-x64-vmware-compositor-damage-track` → marker `[smoke] compositor-damage-track ready` (latch sempre live, sem flag) | ✅ código + host tests |
| **E** | Política panic/oops: thread de app crashar não derruba kernel/desktop | `include/kernel/thread_crash_smoke.h`; `src/kernel/thread_crash_smoke.c` + `thread_crash_smoke_io.c`; alimentado por `process.c::process_exit` + `scheduler.c::scheduler_tick` sob `#ifdef CAPYOS_THREAD_CRASH_SURVIVES_SMOKE` | `tests/kernel/test_thread_crash_smoke_gate.c` → `run_thread_crash_smoke_gate_tests()` | `make smoke-x64-vmware-thread-crash-survives` → marker `[smoke] thread-crash-survives ready` | ✅ código + host tests |
| **F** | Aprovação externa final + fechamento | — | — | `make smoke-x64-vmware-etapa-4` (agregado, 5 markers em ordem) + `release-check` + regressões Etapa 3 | ⛔ pendente (off-machine) |

Gate agregado da Fase F (`smoke-x64-vmware-etapa-4`) valida, no mesmo
boot e em ordem estrita:

1. `[net] DHCP: lease acquired.`
2. `[smoke] gui-session ready`
3. `[smoke] scheduler-fairness ready` (Fase C)
4. `[smoke] compositor-damage-track ready` (Fase D)
5. `[smoke] thread-crash-survives ready` (Fase E)

Regressões da Etapa 3 que devem continuar verdes:
`make smoke-x64-vmware-usb-hid-keyboard`, `make smoke-x64-vmware-storage-resilience`.

---

## 3. Critérios de aceite (master-plan §7) — rastreabilidade

Todos os critérios estão **implementados em código + host tests**;
permanecem `[ ]` (não marcados) até a **confirmação externa da Fase F**,
porque o "done" oficial da etapa é o comportamento observado em VMware.

| # | Critério de aceite | Fase | Evidência code/test | Confirmação externa |
|---|---|---|---|---|
| 1 | Compositor redesenha somente regiões danificadas | D | compositor damage stats full/parcial + `test_compositor_smoke_gate_run` | Fase F (`compositor-damage-track`) |
| 2 | Cursor e texto não piscam sob resize/move | D | cursor-erase-scoped-to-overlap + `test_compositor_cursor_erase_only_on_overlap` | Fase F |
| 3 | Fallback framebuffer continua funcionando | D | compositor render path | Fase F |
| 4 | Apps single-threaded continuam funcionais (regressão) | C/E | scheduler cooperativo preserva semântica; smokes gui-session | Fase F + regressão |
| 5 | Thread de app crashando não derruba kernel/desktop | E | `run_thread_crash_smoke_gate_tests` + latch live | Fase F (`thread-crash-survives`) |
| 6 | Widget model desacoplado renderiza via adapter sem acessar compositor | A/B | `test_capyui_display_adapter_run` + seam `render_producer_window` | Fase F |

Cross-repo (já fechados, não dependem da Fase F):

- [x] Matriz pina `CapyUI` `2.22.0` / `capy-ui-widget` v2.22 (schema v7).
- [x] `external-core-repositories.md` marca `capy-ui-widget` ativo na Etapa 4 via adapter.

---

## 4. Fase B — fluxos ligados vs. pendentes

### 4.1 Lado CapyOS core — **completo** (verificado em alpha.261)

Todo produtor de UI **owned pelo CapyOS core** (`src/gui/widgets/*`,
`src/gui/terminal/*`) já alimenta o seam
`capyui_display_adapter_render_producer_window`, com fallback legado
quando `CAPYOS_HAVE_CAPYUI_WIDGET` não está definido:

| Fluxo core | Produtor | Entry guard "try DL, else legacy" |
|---|---|---|
| Terminal gráfico | `src/gui/widgets/terminal_display_list.c` | `gui/terminal/terminal.c:271` |
| Widgets genéricos (todos os tipos) | `src/gui/widgets/widget_display_list.c` | `widget.c::widget_paint` (linha 204-206) |
| Inline prompt | `src/gui/widgets/inline_prompt_display_list.c` | `inline_prompt.c:158` |
| Context menu | `src/gui/widgets/context_menu_display_list.c` | `context_menu.c:149` |
| Desktop icons | callback de wallpaper com clip de damage | compositor |

`widget_paint` despacha **todos** os tipos de widget via adapter, então
não há subtipo de widget core renderizando direto fora do fallback.

### 4.2 Lado sibling `../CapyUI` — parcial (fora do escopo deste repo)

Ligados via `capy_widget_emit` (no repo `../CapyUI`): Calculator, Text
Editor, Settings, File Manager, Task Manager, Taskbar (+ menu/recent
popups), Notification overlay. A migração dos **demais fluxos desktop/app
de produção** pertence ao repo `../CapyUI` e deve seguir o workflow
`cross-repo-contract-sync`. **Não é editável a partir do core CapyOS.**

### 4.3 Holdout core conhecido: login window

A janela de login (`src/auth/login_runtime/*`, owned pelo core) é
anterior ao adapter (Etapa 2) e **renderiza direto** — `grep` por
`capy_display_list`/`CAPYOS_HAVE_CAPYUI_WIDGET`/`capyui_display_adapter`
em `src/auth/` retorna zero. É o único produtor core fora do adapter.
**Deliberadamente diferido:** é o caminho de UI mais sensível (entrada de
auth) e um pipeline grande; migrar é um slice de risco alto, não um polish.
Só deve ser feito como tarefa dedicada, com fallback legado preservado e
host tests, nunca silenciosamente.

### 4.4 Decisão de escopo

> O critério de aceite #6 mede **capability** (render via adapter sem
> acesso direto ao compositor) — atendido e testado pelo lado core. A
> migração dos fluxos restantes (`../CapyUI` + login window) é polish e
> **não bloqueia** o fechamento da Etapa 4. Reabrir como bloqueante exige
> alteração explícita no master-plan §7.

---

## 5. Como fechar a Etapa 4

1. **Operador externo** roda em VMware oficial (build alpha.262+):
   ```bash
   make smoke-x64-vmware-etapa-4 \
     SMOKE_X64_VMWARE_ARGS="--vmx ... --serial-log ... --timeout 600"
   ```
   + regressões `usb-hid-keyboard` / `storage-resilience` + `make release-check`.
2. Com os 5 markers verdes em ordem, marcar os 6 critérios de aceite
   `[x]` no master-plan §7 e atualizar a coluna Status deste tracker.
3. Invocar o workflow `.windsurf/workflows/etapa-transition.md` para
   fechar a Etapa 4 e abrir a **Etapa 5 — TLS userland real**. Preparação
   read-only da Etapa 5 (auditoria + 1º slice) já disponível em
   [`../../architecture/etapa-5-tls-userland-readiness.md`](../../architecture/etapa-5-tls-userland-readiness.md).

---

## 6. Referências cruzadas

- [`capyos-master-plan.md`](capyos-master-plan.md) §7 — definição autoritativa.
- [`../STATUS.md`](../STATUS.md) — status executivo.
- [`../../operations/etapa-4-external-validation-playbook.md`](../../operations/etapa-4-external-validation-playbook.md) — playbook do operador.
- [`../../reference/integration/compatibility-matrix.md`](../../reference/integration/compatibility-matrix.md) — pin `CapyUI` `2.22.0`.
- [`../../architecture/smoke-marker-pattern.md`](../../architecture/smoke-marker-pattern.md) — pattern dos markers.
- `.windsurf/workflows/etapa-transition.md` — fechamento/abertura de etapa.
- `.windsurf/workflows/cross-repo-contract-sync.md` — coordenação com `CapyUI`.
