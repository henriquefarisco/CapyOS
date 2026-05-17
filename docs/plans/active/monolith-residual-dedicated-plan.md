# Monolith residual dedicated plan

**Status:** ativo. Plano dedicado para os 5 monolitos restantes após a
conclusão das ondas 1-5 do refactor descrito em
`docs/architecture/monolith-refactor-roadmap.md`. Estes arquivos não
podem ser refatorados em uma única onda ou PR porque (a) tocam código
audited (Ed25519), (b) implementam um pipeline GUI complexo de ~90
estágios com contratos fail-closed entre eles (login_runtime.c/h +
test_login_runtime.c), ou (c) já passaram por múltiplos splits
anteriores (kernel_main.c).

**Autoridade:** este plano está em `docs/plans/active/` e segue a
ordem de roadmap descrita em `docs/plans/active/capyos-master-plan.md`.
Cada estágio aqui exige PRs separados com sua própria validação
externa.

## 0. Pendências cobertas

| Arquivo | LOC | Razão da deferred |
|---|---:|---|
| `src/security/ed25519.c` | 1465 | Crypto auditado RFC 8032 §5.1.3-§5.1.7. Mudanças mecânicas exigem comparação byte-a-byte contra vetores de teste do RFC. |
| `src/arch/x86_64/kernel_main.c` | 989 | Já passou por 3 splits anteriores. Reduzir mais aqui implica deslocar responsabilidades de boot e arch, que tocam ABIs sensíveis. |
| `src/auth/login_runtime.c` | 26 319 | 201 funções implementando pipeline GUI multi-estágio com 90 famílias de structs em `include/auth/login_runtime.h`. |
| `include/auth/login_runtime.h` | 10 881 | 90 famílias de structs `login_window_credential_screen_*_plan` formando state machine fail-closed. Split casado com o `.c`. |
| `tests/auth/test_login_runtime.c` | 25 663 | 356 funções de teste cobrindo o pipeline 1:1. Mirror obrigatório do split do source. |

## 1. Princípios deste plano

1. **Não mover comportamento.** Cada split deste plano é refactor por
   reorganização de arquivos, não por reescrita.
2. **Bisseção segura.** Cada PR move um subconjunto identificável e
   testável, com cobertura preservada em `tests/auth/`.
3. **Validação externa por PR.** Cada PR exige `make test` +
   `make all64` + `make layout-audit` antes do merge.
4. **Sem ABI breaks.** Headers públicos continuam expondo os mesmos
   protótipos e structs. Internamente, headers `internal/` agrupam
   types por estágio do pipeline.
5. **Comparação byte-a-byte** para ed25519. Vetores de teste RFC 8032
   acompanham cada split.
6. **Documentação contínua.** Cada PR atualiza este documento +
   `docs/architecture/monolith-refactor-roadmap.md` com métricas
   observadas (LOC pós-split, headers criados, tests movidos).

## 2. Estágio A — `src/security/ed25519.c` (1465 LOC)

**Risco:** alto (crypto auditado). Cada modificação exige rerun dos
vetores RFC 8032 do `tests/security/test_crypt_vectors_aead.c`.

### A.1 Mapa observado de seções (banners atuais)

| Seção (banner) | Linhas atuais | LOC | Responsabilidade |
|---|---|---:|---|
| Edwards25519 group constants | 29-93 | 65 | constantes do grupo + base point B |
| Edwards point ext coords + arithmetic | 94-251 | 158 | type `ge`, ge_add, ge_double, ge_cmov |
| Scalar multiplication | 253-306 | 54 | constant-time double-and-add |
| Point encode/decode | 308-458 | 151 | compressed 32-byte form (§5.1.2) |
| Scalar arithmetic mod L | 460-1240 | 781 | reduce/mul/add mod L (Barrett-like) |
| SHA-512 helpers | 1243-1284 | 42 | thin wrappers do SHA-512 core |
| Public APIs RFC 8032 | 1286-1464 | 179 | create_keypair, sign, verify |

### A.2 Split proposto (4 arquivos + 1 internal header)

| Arquivo | LOC alvo | Conteúdo | Risco |
|---|---:|---|---|
| `src/security/ed25519.c` | ~250 | public APIs + SHA-512 helpers + entry banner | baixo |
| `src/security/ed25519_group.c` | ~280 | grupo constants + Edwards point arithmetic + scalar mult | médio |
| `src/security/ed25519_encode.c` | ~180 | point encode/decode (32-byte compressed form) | médio |
| `src/security/ed25519_scalar.c` | ~800 | scalar arithmetic mod L (split candidate em onda 2 se ainda > 900) | alto |
| `src/security/internal/ed25519_internal.h` | ~80 | declarações de `struct ge`, `fe`, helpers cross-file | — |

### A.3 Sequência de PRs

1. **PR A.1** [**DONE 2026-05-15**] — `internal/ed25519_internal.h`
   criado expondo `ge_p3`, `ED_D`/`ED_SQRTM1` e os 5 group helpers
   externos (`ge_wipe`, `ge_dbl`, `ge_neg_p`, `ge_scalarmult_base`,
   `ge_double_scalarmult`). Constantes do grupo + Edwards point
   arithmetic + scalar multiplication extraidos para
   `src/security/ed25519_group.c` (~306 LOC). `ed25519.c` reduziu de
   1465 LOC para 1217 LOC (-248 LOC, -16,9%). Makefile atualizado
   (CAPYOS64_OBJS + TEST_SRCS) + audit_source_layout.py atualizado.
   **External gate ainda pendente:** rodar vetores RFC 8032 §7 + `make
   all64` antes de promover esta PR para a branch principal.
2. **PR A.2** [**DONE 2026-05-15**] — `ge_encode` + `ge_decode`
   movidos para `src/security/ed25519_encode.c` (~208 LOC).
   `internal/ed25519_internal.h` estendido com os 2 protótipos novos
   (header passou de 88 para 112 LOC). `ed25519.c` reduziu de 1217
   para 1069 LOC (-148 LOC, -12,2%). `wipe_bytes` foi duplicado como
   `static` em `ed25519_encode.c` seguindo a convenção do projeto
   (blake2b.c / argon2.c / x25519.c / ed25519.c já mantêm cópia local
   própria do mesmo helper de 6 linhas). Makefile + audit_source_layout.py
   atualizados. **External gate ainda pendente:** rerun dos vetores
   RFC 8032 §7 + `make all64`.
3. **PR A.3** [**DONE 2026-05-15**] — `ED_L_BYTES` + `load_3`/`load_4`
   + `sc_reduce64` + `sc_muladd` + `sc_is_canonical` movidos para
   `src/security/ed25519_scalar.c` (~836 LOC). `internal/ed25519_internal.h`
   estendido com os 3 protótipos scalar (header passou de 112 para 139
   LOC). `ed25519.c` reduziu de 1069 para **286 LOC** (-783 LOC, -73,2%).
   **ed25519.c saiu da baseline de monolitos** (286 < 900). `ED_L_BYTES`,
   `load_3` e `load_4` permanecem `static` em ed25519_scalar.c — nenhum
   caller externo. Makefile + audit_source_layout.py atualizados; o
   audit script removeu `ed25519.c` da lista `MONOLITH_BASELINE_EXCEPTIONS`.
   **External gate ainda pendente:** rerun final dos vetores RFC 8032
   §7 + `make all64` + `make release-check` para selar o estágio A
   completo.

### A.4 Validação obrigatória por PR

- `make test` com vetores RFC 8032 §7 (`test_ed25519_rfc8032_vectors`).
- `make all64` para garantir ABI preservado.
- Inspeção de produto: comparar SHA-256 dos `.o` antes/depois para
  cada split — diferença esperada (símbolos movidos), mas tamanhos
  agregados similares.
- `make release-check` antes do merge final do estágio.

## 3. Estágio B — `include/auth/login_runtime.h` (10 881 LOC)

**Risco:** alto (90 famílias de structs com dependências encadeadas).
A ordem de declaração entre structs importa porque cada
`login_window_credential_screen_<stage>_plan` aponta para
`<previous_stage>_plan_available` e `<previous_stage>_plan_safe`.

### B.1 Famílias identificadas (90 structs)

Agrupamento natural por **estágio do pipeline** (ordem de produção):

| Família | Structs | Responsabilidade |
|---|---:|---|
| **Policy/Buffer/Submit (auth core)** | 6 | recovery_resume, credential_policy, credential_buffer, submit_gate, submit_attempt, auth_submit |
| **Input/Field/Panel (input layer)** | 5 | input_result, field_view, credential_panel, credential_interaction, credential_readiness |
| **Audit/ViewModel (observability)** | 4 | audit_event, view_model, ui_step, ui_session |
| **Recovery/Screen ViewModel** | 4 | recovery_view_model, screen_view_model, screen_session, screen_render_plan |
| **Action/UI Event/Route** | 3 | screen_action_plan, screen_ui_event, screen_route_plan |
| **Controller/Decision/Handoff** | 3 | screen_controller, recovery_decision, session_handoff |
| **Presenter/Binding/Mount** | 3 | screen_presenter, screen_binding, screen_mount_plan |
| **Commit/Handoff/Dispatch/Queue/Activation** | 5 | screen_commit_plan, screen_handoff_plan, screen_dispatch_plan, screen_queue_plan, screen_activation_plan |
| **Frame/Surface/Compositor/Damage** | 4 | screen_frame_plan, screen_surface_plan, screen_compositor_plan, screen_damage_plan |
| **Present/Schedule/Vsync/Scanout** | 4 | screen_present_plan, screen_schedule_plan, screen_vsync_plan, screen_scanout_plan |
| **Display/Output/Blit/Framebuffer** | 4 | screen_display_plan, screen_output_plan, screen_blit_plan, screen_framebuffer_plan |
| **Flush/Barrier/Fence/Timeline/Sync** | 5 | screen_flush_plan, screen_barrier_plan, screen_fence_plan, screen_timeline_plan, screen_sync_plan |
| **Deadline/Completion/Ack/Retire/Cleanup** | 5 | screen_deadline_plan, screen_completion_plan, screen_ack_plan, screen_retire_plan, screen_cleanup_plan |
| **Seal/Audit/Record/Receipt/Ledger** | 5 | screen_seal_plan, screen_audit_plan, screen_record_plan, screen_receipt_plan, screen_ledger_plan |
| **Journal/Archive/Retention/Expiry** | 4 | screen_journal_plan, screen_archive_plan, screen_retention_plan, screen_expiry_plan |
| **Purge/Tombstone/Compaction/Reclaim/Release** | 5 | screen_purge_plan, screen_tombstone_plan, screen_compaction_plan, screen_reclaim_plan, screen_release_plan |
| **GUI/Window/WindowSurface/WindowCompositor** | 4 | screen_gui_plan, screen_window_plan, screen_window_surface_plan, screen_window_compositor_plan |
| **WindowDamage/WindowPresent/WindowSchedule/WindowVsync/WindowScanout** | 5 | screen_window_damage_plan, screen_window_present_plan, screen_window_schedule_plan, screen_window_vsync_plan, screen_window_scanout_plan |
| **WindowDisplay/WindowOutput/WindowBlit/WindowCommit/WindowFlip** | 5 | screen_window_display_plan, screen_window_output_plan, screen_window_blit_plan, screen_window_commit_plan, screen_window_flip_plan |
| **WindowVblank/WindowEvent/WindowInput** | 3 | screen_window_vblank_plan, screen_window_event_plan, screen_window_input_plan |
| **Pipeline safety report + contract + view_model + ops** | 4 | pipeline_safety_report, login_window_contract, login_window_view_model, login_runtime_ops |

Total: 91 structs em ~21 grupos lógicos.

### B.2 Split proposto (1 header público + 21 internal headers + 1 public façade)

| Header | Conteúdo | LOC alvo |
|---|---|---:|
| `include/auth/login_runtime.h` | inclui todos os internal headers como subgrupos públicos + protótipos das `login_runtime_ops` | ~150 |
| `include/auth/login_runtime/auth_core.h` | structs 1-6 (policy/buffer/submit/attempt/auth_submit) | ~400 |
| `include/auth/login_runtime/input_layer.h` | structs 7-11 (input_result/field_view/credential_panel/interaction/readiness) | ~600 |
| `include/auth/login_runtime/audit_view.h` | structs 12-15 (audit/view_model/ui_step/ui_session) | ~450 |
| `include/auth/login_runtime/recovery_screen.h` | structs 16-19 (recovery_view_model/screen_view_model/screen_session/screen_render_plan) | ~500 |
| `include/auth/login_runtime/action_route.h` | structs 20-22 (action_plan/ui_event/route_plan) | ~450 |
| `include/auth/login_runtime/controller_handoff.h` | structs 23-25 (controller/recovery_decision/session_handoff) | ~500 |
| `include/auth/login_runtime/presenter_mount.h` | structs 26-28 (presenter/binding/mount_plan) | ~500 |
| `include/auth/login_runtime/commit_dispatch.h` | structs 29-33 (commit/handoff/dispatch/queue/activation) | ~600 |
| `include/auth/login_runtime/frame_compositor.h` | structs 34-37 (frame/surface/compositor/damage) | ~500 |
| `include/auth/login_runtime/present_scanout.h` | structs 38-41 (present/schedule/vsync/scanout) | ~500 |
| `include/auth/login_runtime/display_blit.h` | structs 42-45 (display/output/blit/framebuffer) | ~500 |
| `include/auth/login_runtime/flush_sync.h` | structs 46-50 (flush/barrier/fence/timeline/sync) | ~600 |
| `include/auth/login_runtime/deadline_cleanup.h` | structs 51-55 (deadline/completion/ack/retire/cleanup) | ~600 |
| `include/auth/login_runtime/seal_ledger.h` | structs 56-60 (seal/audit/record/receipt/ledger) | ~600 |
| `include/auth/login_runtime/journal_retention.h` | structs 61-64 (journal/archive/retention/expiry) | ~500 |
| `include/auth/login_runtime/purge_reclaim.h` | structs 65-69 (purge/tombstone/compaction/reclaim/release) | ~600 |
| `include/auth/login_runtime/gui_window.h` | structs 70-73 (gui/window/window_surface/window_compositor) | ~500 |
| `include/auth/login_runtime/window_display.h` | structs 74-78 (window_damage/window_present/window_schedule/window_vsync/window_scanout) | ~600 |
| `include/auth/login_runtime/window_output.h` | structs 79-83 (window_display/window_output/window_blit/window_commit/window_flip) | ~600 |
| `include/auth/login_runtime/window_input.h` | structs 84-86 (window_vblank/window_event/window_input) | ~400 |
| `include/auth/login_runtime/pipeline_contract.h` | structs 87-91 (pipeline_safety_report/contract/view_model/ops) | ~500 |

Total: 22 headers, todos abaixo de 900 LOC. **`login_runtime.h` continua sendo o
header público — só passa a ser uma fachada que inclui os 21 grupos**.

### B.3 Sequência de PRs

Cada PR move 1-2 grupos (3-6 structs). Critério de ordenação:
**bottom-up no pipeline** para preservar dependências forward.

1. **PR B.1** — extrai `pipeline_contract.h` (pode ser independente, é o topo do pipeline). Atualiza `login_runtime.h` para incluí-lo.
2. **PR B.2** — extrai `auth_core.h` (base do pipeline). Sem dependências cross-grupo.
3. **PR B.3** — extrai `input_layer.h` + `audit_view.h`.
4. **PR B.4** — extrai `recovery_screen.h`.
5. **PR B.5** — extrai `action_route.h` + `controller_handoff.h` + `presenter_mount.h`.
6. **PR B.6** — extrai `commit_dispatch.h` + `frame_compositor.h`.
7. **PR B.7** — extrai `present_scanout.h` + `display_blit.h` + `flush_sync.h`.
8. **PR B.8** — extrai `deadline_cleanup.h` + `seal_ledger.h`.
9. **PR B.9** — extrai `journal_retention.h` + `purge_reclaim.h`.
10. **PR B.10** — extrai `gui_window.h` + `window_display.h` + `window_output.h` + `window_input.h`.

Cada PR atualiza simultaneamente:
- O header(s) novo(s) em `include/auth/login_runtime/`.
- `include/auth/login_runtime.h` (fachada).
- Includes consumidores em `src/auth/login_runtime.c` (vão ficar sem mudança lógica, ainda incluem `auth/login_runtime.h`).
- Includes em `tests/auth/test_login_runtime.c`.

## 4. Estágio C — `src/auth/login_runtime.c` (26 319 LOC)

**Risco:** alto. 201 funções implementando o pipeline da §B. Cada
função pertence a 1 (e só 1) estágio.

### C.1 Mapping função → grupo

Cada função `login_window_*_<stage>_<verb>()` é trivialmente mapeada
para o grupo correspondente na §B. Esperado: ~10 funções por grupo
(201 funções / 21 grupos = ~10).

### C.2 Split proposto (21 arquivos + 1 fachada)

Mesma estrutura do header: um `.c` por grupo, ~1200 LOC por arquivo
(26 319 / 21 ≈ 1253), portanto ainda > 900 LOC. **Estimativa: alguns
grupos pesados (commit_dispatch, flush_sync, seal_ledger) podem
precisar de sub-split adicional.**

| Arquivo | LOC alvo | Status |
|---|---:|---|
| `src/auth/login_runtime.c` | ~150 | fachada: define `login_runtime_ops` e dispatch público |
| `src/auth/login_runtime_auth_core.c` | ~800 | funções da família auth_core |
| `src/auth/login_runtime_input_layer.c` | ~800 | funções input/field/panel/interaction/readiness |
| ... | ~800-1200 | mirror dos grupos do header |

Se algum grupo ficar > 900 LOC após o primeiro split, aplicar
sub-split por sub-família (ex: `login_runtime_flush_sync.c` →
`login_runtime_flush.c` + `login_runtime_sync.c`).

### C.3 Sequência de PRs

Mirror exato dos PRs do estágio B. Cada PR do estágio B faz o
companion `.c` split em paralelo (mesmo PR — header + source mudam
juntos por consistência). Assim os estágios B e C colapsam em **10
PRs combinados**.

## 5. Estágio D — `tests/auth/test_login_runtime.c` (25 663 LOC)

**Risco:** alto. 356 testes cobrindo o pipeline 1:1.

### D.1 Mapping teste → grupo

Cada teste `test_loginwindow_<group>_<scenario>` mapeia trivialmente
para um dos 21 grupos.

| Família de teste | Quantidade estimada |
|---|---:|
| auth_core (policy/buffer/submit/attempt/auth_submit) | ~40 |
| input_layer (input_result/field_view/credential_panel/interaction/readiness) | ~40 |
| audit_view + recovery_screen + action_route | ~30 |
| controller/handoff/presenter/mount | ~20 |
| commit/dispatch + frame/compositor + present/scanout | ~40 |
| display/blit + flush/sync + deadline/cleanup | ~50 |
| seal/ledger + journal/retention + purge/reclaim | ~50 |
| gui/window + window_display/output/input | ~50 |
| pipeline_contract (safety report, contract, view_model, ops) | ~36 |

### D.2 Split proposto (21 arquivos + 1 entry point + 1 internal header)

| Arquivo | LOC alvo |
|---|---:|
| `tests/auth/test_login_runtime.c` | ~200 (entry `run_login_runtime_tests` + shared fixture state + stubs) |
| `tests/auth/test_login_runtime_internal.h` | ~150 (extern declarations de stubs e g_* globals) |
| `tests/auth/test_login_runtime_auth_core.c` | ~800 |
| `tests/auth/test_login_runtime_input_layer.c` | ~800 |
| ... | ~800-1200 |

Stubs compartilhados (`g_shell_ctx`, `g_session_ctx`, `g_settings`,
`g_prepare_calls`, etc) vão para `test_login_runtime_internal.h` como
externs, com storage em `test_login_runtime.c`.

### D.3 Sequência de PRs

Cada PR do estágio combinado B+C inclui também o companion test
split. Total: **10 PRs combinados B+C+D**.

## 6. Estágio E — `src/arch/x86_64/kernel_main.c` (989 LOC) — DEFERRED

**Risco:** alto (boot path). Decisão: **manter na baseline**.

`kernel_main.c` já passou por 3 splits anteriores (movidos para
`kernel_init_*`, `kernel_subsystem_*`, etc). Reduzir mais aqui implica
deslocar responsabilidades fundamentais de boot que tocam:
- Multiboot/UEFI handoff
- Page table initialization
- Stack switching
- IDT/GDT setup
- Subsystem registration order

Cada uma dessas tem efeitos colaterais em outros .c. Esse split
exige seu próprio plano dedicado **só depois** do login_runtime
terminar, para não bloquear progresso paralelo.

**Recomendação:** manter na baseline até pós-conclusão dos estágios A-D.
Reavaliar então com plano dedicado próprio.

## 7. Sequência global recomendada

| Etapa | Estágio | Risco | LOC reduzido |
|---|---|---|---:|
| 1 | A (ed25519) | alto | 1465 → 4×~280 LOC |
| 2 | B+C+D PR 1 (pipeline_contract) | médio | -2000 LOC |
| 3 | B+C+D PR 2 (auth_core) | médio | -1800 LOC |
| 4 | B+C+D PR 3 (input_layer + audit_view) | médio | -3000 LOC |
| 5 | B+C+D PR 4 (recovery_screen) | médio | -2500 LOC |
| 6 | B+C+D PR 5 (action_route + controller + presenter) | médio | -3500 LOC |
| 7 | B+C+D PR 6 (commit_dispatch + frame_compositor) | médio | -4000 LOC |
| 8 | B+C+D PR 7 (present_scanout + display_blit + flush_sync) | médio | -5000 LOC |
| 9 | B+C+D PR 8 (deadline_cleanup + seal_ledger) | médio | -4000 LOC |
| 10 | B+C+D PR 9 (journal_retention + purge_reclaim) | médio | -4000 LOC |
| 11 | B+C+D PR 10 (gui/window e filhos) | médio | -5000 LOC |
| 12 | E (kernel_main) | alto | deferred |

**Total esperado:** após etapas 1-11, todos os 4 monolitos
ativos saem da baseline. `MONOLITH_BASELINE_EXCEPTIONS` fica reduzido
a apenas `kernel_main.c` (1/16 originais = 6,25% remanescente,
**93,75% de redução**).

## 8. Métricas de sucesso

| Indicador | Hoje | Após etapa 11 |
|---|---:|---:|
| Arquivos > 900 LOC em baseline | 5 | 1 |
| LOC total em baseline | ~63 350 | ~989 |
| Internal headers totais (refactor) | 11 | 32 (+21) |
| Arquivos novos criados (refactor) | 30 | ~75 (+45) |

## 9. Validação externa por PR

Como local execution policy aplica, cada PR exige no CI ou em outra
máquina:

1. `make test` — testes host-side.
2. `make all64` — kernel x86_64 build (especial para ed25519 e auth).
3. `make layout-audit` — confirma saída de baseline.
4. `make release-check` — sanity completo, **obrigatório no PR
   final de cada estágio**.

Para ed25519 especificamente, também:
- Comparação byte-a-byte de vetores RFC 8032 §7 antes/depois.
- Reproducibilidade do `.o` size por arquivo.

## 10. Risco e mitigação

| Risco | Probabilidade | Impacto | Mitigação |
|---|---|---|---|
| ABI break no header público | baixa | alto | manter `login_runtime.h` como fachada; só mover declarações para internal headers |
| Test regression no pipeline | média | alto | mirror split test 1:1, rerun `make test` por PR |
| ed25519 produz output diferente | baixa | crítico | vetores RFC 8032 §7 obrigatórios por PR |
| Algum grupo passa de 900 LOC após split | alta (commit_dispatch, flush_sync) | médio | sub-split por sub-família em PR adicional |
| Includes circulares no novo header tree | média | médio | usar `#ifdef LOGIN_RUNTIME_INTERNAL_X_H` guards padrão CapyOS |
| Ordem de declaração quebra (depend chain) | alta | médio | seguir ordem bottom-up do pipeline (auth → input → ... → gui → window) |

## 11. Critério de conclusão

Este plano é considerado concluído quando:

1. `make layout-audit` reporta apenas `kernel_main.c` na baseline.
2. `make test` passa com 356 testes de login_runtime divididos em 21 arquivos.
3. `make release-check` passa.
4. `docs/architecture/monolith-refactor-roadmap.md` reflete onda 6+ concluída.
5. Este documento é movido para `docs/plans/historical/`.

## 12. Histórico

- **2026-05-15** (criação): plano dedicado criado após conclusão das ondas 1-5 do refactor de monolitos. 11 de 16 monolitos originais já refatorados (68,75%).

---

Próximo passo: **PRs 8 + 9 + 10 automatizadas via script**.
`tools/scripts/split_login_runtime_partials.py` (criado 2026-05-15)
encoda o padrão das PRs manuais 1-7 e executa as 3 PRs restantes
sem consumir tokens AI. Pode ser invocado em CI ou outra máquina:

```sh
# aplica todas as PRs 8/9/10 (gera 8 partial headers, move ~36 structs)
python3 tools/scripts/split_login_runtime_partials.py

# inspeciona sem escrever
python3 tools/scripts/split_login_runtime_partials.py --dry-run

# aplica um grupo individualmente
python3 tools/scripts/split_login_runtime_partials.py --only deadline_cleanup
```

O script é idempotente (rodar 2x é seguro) e reporta as mudanças por
grupo. Após executá-lo:

- 8 novos partials em `include/auth/login_runtime/`:
  - `deadline_cleanup.h` + `seal_ledger.h` (PR 8, 10 structs)
  - `journal_retention.h` + `purge_reclaim.h` (PR 9, 9 structs)
  - `gui_window.h` + `window_display.h` + `window_output.h` + `window_input.h` (PR 10, ~17 structs)
- `include/auth/login_runtime.h` reduzido de 7814 LOC para ~2500 LOC
  (apenas function prototypes + #defines + 1 input_result_internal).

**Estágio B+C+D PRs 1-7 CONCLUÍDOS 2026-05-15** (header-only
extraction):

- **PR 1** — 4 structs top-of-pipeline (`pipeline_safety_report`,
  `login_window_contract`, `login_window_view_model`, `login_runtime_ops`)
  movidos de `include/auth/login_runtime.h` para o novo partial header
  `include/auth/login_runtime/pipeline_contract.h` (156 LOC, depois
  152 LOC após PR 2 ajustou comentários).
  `login_runtime.h` reduziu de 10881 para **10784 LOC** (-97 LOC).
  Source e tests não tocados nesta PR — alteração é puramente
  estrutural na facade.
- **PR 2** — 6 structs auth_core (`login_recovery_resume_policy`,
  `login_window_credential_policy`, `_buffer`, `_submit_gate`,
  `_submit_attempt`, `_auth_submit`) + typedef
  `login_window_credential_authenticate_fn` movidos de
  `include/auth/login_runtime.h` para o novo partial header
  `include/auth/login_runtime/auth_core.h` (148 LOC).
  `pipeline_contract.h` agora inclui `auth_core.h` diretamente e
  passou a ser **standalone-includable**. `login_runtime.h` reduziu
  de 10784 para **10692 LOC** (-92 LOC). Source/tests não tocados.
- **PR 3** — 9 structs em 2 partials novos:
  - **input_layer.h** (157 LOC): `input_result`, `field_view`,
    `credential_panel`, `credential_interaction`, `credential_readiness`.
    Inclui `auth_core.h` porque `input_result` contém
    `submit_attempt` por valor.
  - **audit_view.h** (157 LOC): `audit_event`, `view_model`, `ui_step`,
    `ui_session`. Fully standalone (só primitives + const char*).
  `login_runtime.h` reduziu de 10692 para **10458 LOC** (-234 LOC).
- **PR 4** — 4 structs em `recovery_screen.h` (179 LOC):
  `recovery_view_model`, `screen_view_model`, `screen_session`,
  `screen_render_plan`. Standalone. `login_runtime.h`: 10458 →
  **10318 LOC** (-140 LOC).
- **PR 5** — 9 structs em 3 partials standalone:
  - **action_route.h** (132 LOC): `screen_action_plan`,
    `screen_ui_event`, `screen_route_plan`.
  - **controller_handoff.h** (157 LOC): `screen_controller`,
    `recovery_decision`, `session_handoff`.
  - **presenter_mount.h** (175 LOC): `screen_presenter`,
    `screen_binding`, `screen_mount_plan`.
  `login_runtime.h`: 10318 → **9945 LOC** (-373 LOC).
- **PR 6** — 9 structs em 2 partials standalone:
  - **commit_dispatch.h** (~260 LOC): `screen_commit_plan`,
    `screen_handoff_plan`, `screen_dispatch_plan`,
    `screen_queue_plan`, `screen_activation_plan`.
  - **frame_compositor.h** (~240 LOC): `screen_frame_plan`,
    `screen_surface_plan`, `screen_compositor_plan`,
    `screen_damage_plan`.
  `login_runtime.h`: 9945 → **9419 LOC** (-526 LOC).
- **PR 7** (a + b + c) — 13 structs em 3 partials standalone:
  - **present_scanout.h** (403 LOC): `present_plan`, `schedule_plan`,
    `vsync_plan`, `scanout_plan` (PR 7a).
  - **display_blit.h** (589 LOC): `display_plan`, `output_plan`,
    `blit_plan`, `framebuffer_plan` (PR 7b).
  - **flush_sync.h** (679 LOC): `flush_plan`, `barrier_plan`,
    `fence_plan`, `timeline_plan`, `sync_plan` (PR 7c).
  `login_runtime.h`: 9419 → **7814 LOC** (-1605 LOC cumulativo).
  PR 7 foi dividida em 7a + 7b + 7c por causa do tamanho das
  structs (~100-170 linhas cada — acumulam fields up-pipeline).
  Source/tests não tocados em nenhuma das 7 PRs do Estágio B+C+D.
- **PR 8** — 10 structs em 2 partials, fully wired and cleaned:
  - **deadline_cleanup.h** (807 LOC): `deadline_plan`, `completion_plan`,
    `ack_plan`, `retire_plan`, `cleanup_plan`. Standalone.
  - **seal_ledger.h** (1050 LOC): `seal_plan`, `audit_plan`,
    `record_plan`, `receipt_plan`, `ledger_plan`. Standalone.
  Os 2 partial headers estão **incluídos no facade** e as 10
  definições inline duplicadas foram **fisicamente deletadas**.
  `login_runtime.h`: 7814 → **6009 LOC** (-1805 LOC). Source/tests
  inalterados — 8 PRs do Estágio B+C+D concluídos.
  O script `tools/scripts/split_login_runtime_partials.py` foi
  enhanced nesta sessão (2026-05-15) com a função
  `find_if0_block_around` para casos futuros onde wiring parcial
  deixe `#if 0` wrappers; idempotente.
- **PR 11 add-on** — function declarations partial header:
  - **function_declarations.h** (358 LOC): 60+ prototypes do pipeline
    build chain (login_window_contract_evaluate, todos os *_plan_build,
    login_window_view_model_build, login_runtime_run).
  O facade agora termina com `#include` desse partial logo antes do
  `#endif`. `login_runtime.h`: 6009 → **5699 LOC** (-310 LOC). Esta
  PR é add-on ao plano original (B+C+D PRs 1-10 focavam apenas em
  structs); separa decla​rations de defini​ções. Source/tests
  inalterados — todos consomem `auth/login_runtime.h` (facade).
- **PR 12 add-on** — version constants partial header:
  - **version_constants.h** (130 LOC): 90+ `#define LOGIN_*_VERSION`
    ABI stamps + password buffer limits + input/screen action codes.
  O facade inclui esse partial logo após os headers padrão, antes
  de qualquer struct partial header. `login_runtime.h`: 5699 →
  **5609 LOC** (-90 LOC). Também add-on ao plano original.
- **PR 9a** — journal_retention partial header (4 structs):
  - **journal_retention.h** (1135 LOC): `journal_plan`, `archive_plan`,
    `retention_plan`, `expiry_plan`. Standalone.
  Partial header included no facade e as 4 definições inline
  duplicadas foram **fisicamente deletadas**. `login_runtime.h`:
  5609 → **4474 LOC** (-1135 LOC). PR 9 dividida em 9a + 9b por
  causa do tamanho dos structs.
- **PR 9b** — purge_reclaim partial header (5 structs):
  - **purge_reclaim.h** (948 LOC): `purge_plan`, `tombstone_plan`,
    `compaction_plan`, `reclaim_plan`, `release_plan`. Standalone
    (apenas primitivos + `const char *`). Extração byte-for-byte.
  Partial header included no facade logo após `journal_retention.h`
  e as 5 definições inline duplicadas foram **fisicamente deletadas**
  (sem wrapper `#if 0` remanescente). `login_runtime.h`:
  4474 → **3553 LOC** (-921 LOC). Conclui o Estágio B+C+D PR 9 do
  plano original.
- **PR 10** — gui/window family partial headers (17 structs em 4
  partials, executado manualmente 2026-05-15 via extração byte-for-byte
  com `sed`):
  - **PR 10a** — `gui_window.h` (485 LOC, 4 structs: `gui_plan`,
    `window_plan`, `window_surface_plan`, `window_compositor_plan`).
    `login_runtime.h`: 3553 → **3096 LOC** (-457 LOC).
  - **PR 10b** — `window_display.h` (1010 LOC, 5 structs:
    `window_damage_plan`, `window_present_plan`, `window_schedule_plan`,
    `window_vsync_plan`, `window_scanout_plan`).
    `login_runtime.h`: 3096 → **2115 LOC** (-981 LOC).
  - **PR 10c** — `window_output.h` (1264 LOC, 5 structs:
    `window_display_plan`, `window_output_plan`, `window_blit_plan`,
    `window_commit_plan`, `window_flip_plan`).
    `login_runtime.h`: 2115 → **882 LOC** (-1233 LOC).
  - **PR 10d** — `window_input.h` (770 LOC, 3 structs:
    `window_vblank_plan`, `window_event_plan`, `window_input_plan`).
    `login_runtime.h`: 882 → **142 LOC** (-740 LOC).
  Todas as 17 definições inline foram fisicamente deletadas, sem
  wrapper `#if 0` remanescente. Total PR 10: **-3411 LOC**. O facade
  ficou com apenas guards, 4 includes padrão e 19 `#include`s de
  partial headers. **`include/auth/login_runtime.h` saiu da baseline
  de monolitos** (`MONOLITH_BASELINE_EXCEPTIONS`).
- **PR 11** — per-struct split dos 5 partial headers que ainda
  excediam o limite de 900 LOC após PR 10 (executado manualmente
  2026-05-15 via extração byte-for-byte com `sed`). Cada um virou
  um aggregator fino (~28 LOC) que apenas `#include` partial
  headers individuais por struct:
  - **PR 11a** — `purge_reclaim.h` 948 → **28 LOC**. 5 sub-partials
    criados: `purge_plan.h` (353), `tombstone_plan.h` (372),
    `compaction_plan.h` (87), `reclaim_plan.h` (89),
    `release_plan.h` (102). Todos sob 400 LOC.
  - **PR 11b** — `window_display.h` 1010 → **30 LOC**. 5
    sub-partials criados: `window_damage_plan.h` (168),
    `window_present_plan.h` (192), `window_schedule_plan.h` (223),
    `window_vsync_plan.h` (244), `window_scanout_plan.h` (237).
  - **PR 11c** — `seal_ledger.h` 1050 → **29 LOC**. 5 sub-partials
    criados: `seal_plan.h` (198), `audit_plan.h` (207),
    `record_plan.h` (216), `receipt_plan.h` (233),
    `ledger_plan.h` (252).
  - **PR 11d** — `journal_retention.h` 1161 → **27 LOC**. 4
    sub-partials criados: `journal_plan.h` (271),
    `archive_plan.h` (290), `retention_plan.h` (309),
    `expiry_plan.h` (332).
  - **PR 11e** — `window_output.h` 1264 → **30 LOC**. 5
    sub-partials criados: `window_display_plan.h` (253),
    `window_output_plan.h` (269), `window_blit_plan.h` (248),
    `window_commit_plan.h` (264), `window_flip_plan.h` (282).
  Total PR 11: 5 aggregators + 24 sub-partials. **Cada header sob
  `include/auth/login_runtime/` agora está abaixo de 900 LOC** sem
  precisar de exceção no `MONOLITH_BASELINE_EXCEPTIONS`. O maior
  partial restante é `deadline_cleanup.h` (807 LOC), criado em PR 8
  e ainda dentro do limite.

**Estágio A CONCLUÍDO 2026-05-15** (pendente apenas external gate
combinado das 3 PRs):

- **PR A.1** — ed25519.c 1465 → 1217 LOC. ed25519_group.c criado
  (306 LOC). Internal header criado (88 LOC). Makefile + audit script
  atualizados.
- **PR A.2** — ed25519.c 1217 → 1069 LOC. ed25519_encode.c criado
  (208 LOC). Internal header estendido 88 → 112 LOC. Makefile + audit
  script atualizados.
- **PR A.3** — ed25519.c 1069 → **286 LOC**. ed25519_scalar.c criado
  (836 LOC). Internal header estendido 112 → 139 LOC. **ed25519.c
  saiu da baseline de monolitos**. Makefile + audit script atualizados
  (linha do ed25519.c removida de `MONOLITH_BASELINE_EXCEPTIONS`).

## Estágio C — Plano de Refactor `src/auth/login_runtime.c`

**Status**: em execução 2026-05-15. Stage B (header tree) concluído;
agora começamos a refatorar o `.c` correspondente.

### Diagnóstico inicial

`src/auth/login_runtime.c` tem **26.320 LOC** e **195 definições de
função** top-level. Características favoráveis ao split:

- **Zero variáveis estáticas a nível de arquivo** (verificado via
  `grep -cE "^static (const )?[A-Za-z_]+.*[a-zA-Z_]+[ ]*[=;[]"`,
  retornou 0). Não há estado compartilhado a preservar.
- **2 includes apenas**: `auth/login_runtime.h` e
  `lang/localization.h`.
- Funções organizadas por struct/domínio com prefixos consistentes
  (`login_window_credential_screen_<plan>_<verb>`), mapeando 1:1
  com os 47 partial headers em `include/auth/login_runtime/`.
- **4 helpers `static` no topo do arquivo** (`dbg_login_putc`,
  `dbg_login_puts`, `ops_ready`, `login_service_poll`,
  `login_maintenance_mode_active`) compartilhados por toda a
  pipeline; precisam ser movidos para um internal header.

### Estratégia

Mirror exato da estrutura do `include/auth/login_runtime/`. Cada
partial header (ou grupo afim) ganha um sibling `.c` em
`src/auth/login_runtime/`. O facade `src/auth/login_runtime.c`
fica com apenas o entry point público (`login_runtime_run` +
controllers de alto nível).

### Plano de PRs

**Fase 1 — Fundação (1 PR):**

- **PR C.0 DONE 2026-05-15** — Criado
  `src/auth/internal/login_runtime_internal.h` (74 LOC) com os 5
  helpers como `static inline` (`dbg_login_putc`, `dbg_login_puts`,
  `ops_ready`, `login_service_poll`, `login_maintenance_mode_active`).
  Removidos de `login_runtime.c` (-43 LOC). Adicionado `#include`
  correspondente. Nenhuma alteração no Makefile (ainda 1 arquivo .c
  até PR C.1). `login_runtime.c`: 26320 → **26277 LOC**.

**Fase 2 — Pré-pipeline / non-`*_plan_*` (extracts iniciais, 5-7 PRs):**

- **PR C.1 DONE 2026-05-15** — `contract_policy.c` criado (161 LOC,
  3 funcs: `contract_evaluate`, `recovery_resume_policy_evaluate`,
  `credential_policy_from_contract`). Byte-for-byte verificado.
  Makefile: +1 entrada em kernel obj list (linha 153) + adicionado
  ao test infrastructure list. `login_runtime.c`: 26277 → **26137
  LOC** (-140).
- **PR C.2 DONE 2026-05-15** — `credential_buffer.c` criado (295
  LOC, 7 funcs: 5 buffer helpers + `submit_gate_evaluate` +
  `submit_attempt_consume`). Makefile: +2 entradas. `login_runtime.c`:
  26137 → **25870 LOC** (-267).
- **PR C.3 DONE 2026-05-15** — `credential_input.c` criado (350 LOC,
  4 funcs: `input_result_init` (static), `input_apply`,
  `field_view_build`, `panel_build`). Confirmado que
  `input_result_init` só é chamado dentro do mesmo TU; static
  linkage preservada. Makefile: +2 entradas. `login_runtime.c`:
  25870 → **25542 LOC** (-328).
- **PR C.4 DONE 2026-05-15** — `credential_interaction.c` criado
  (346 LOC, 3 funcs: `interaction_step`, `readiness_evaluate`,
  `audit_event_build`). Makefile: +2 entradas. `login_runtime.c`:
  25542 → **25220 LOC** (-322).
- **PR C.5 DONE 2026-05-15** — `credential_view_model.c` criado
  (590 LOC, 5 view-model builders: `view_model_build`,
  `ui_step_build`, `ui_session_build`, `recovery_view_model_build`,
  `screen_view_model_build`). Makefile: +2 entradas. `login_runtime.c`:
  25220 → **24655 LOC** (-565).
- **PR C.6 DONE 2026-05-15** — `session_pipeline.c` criado (186 LOC,
  3 funcs: `session_clear_io` (static), `session_defaults` (static),
  `session_build`). Verificado que os dois helpers static só são
  chamados de `session_build` no mesmo TU; linkage preservada.
  Makefile: +2 entradas. `login_runtime.c`: 24655 → **24491 LOC**
  (-164).

**Fase 3 — Pipeline plan builders (revisão do plano):**

A ordem semântica original (`screen_builders.c` agregando
ui_event/controller/presenter/binding) foi REVISADA porque as
funções alvo NÃO são contíguas no arquivo. Em vez disso adotamos
extrações por blocos contíguos (técnica mecânica simples,
preserva sed ranges, sem reorganizar a ordem original do código):

- **PR C.7 DONE 2026-05-15** — `render_action_ui_event.c` criado
  (416 LOC, 3 funcs: `render_plan_build`, `action_plan_build`,
  `ui_event_build`). Makefile: +2 entradas. `login_runtime.c`:
  24491 → **24100 LOC** (-391).
- **PR C.8 DONE 2026-05-15** — `route_controller.c` criado (364 LOC,
  2 funcs: `route_plan_build`, `controller_build`). Makefile: +2
  entradas. `login_runtime.c`: 24100 → **23759 LOC** (-341).
- **PR C.9 DONE 2026-05-15** — `presenter_binding.c` criado (472 LOC,
  2 funcs: `presenter_build`, `binding_build`). Makefile: +2 entradas.
- **PR C.10 DONE 2026-05-15** — `mount_commit.c` criado (557 LOC,
  2 funcs: `mount_plan_build`, `commit_plan_build`). Makefile: +2
  entradas (kernel obj list + test infrastructure list — wired
  retroativamente junto com PR C.11).
- **PR C.11 DONE 2026-05-15** — `handoff_dispatch.c` criado (602 LOC,
  2 funcs: `handoff_plan_build`, `dispatch_plan_build`). Diff
  byte-for-byte verificado. Makefile: +2 entradas. `login_runtime.c`:
  22776 → **22196 LOC** (-580).
- **PR C.12 DONE 2026-05-15** — `queue_activation.c` criado (641 LOC,
  2 funcs: `queue_plan_build`, `activation_plan_build`). Diff
  byte-for-byte verificado. Makefile: +2 entradas. `login_runtime.c`:
  22196 → **21577 LOC** (-619).
- **PR C.13 DONE 2026-05-15** — `frame_surface.c` criado (645 LOC,
  2 funcs: `frame_plan_build`, `surface_plan_build`). Diff
  byte-for-byte verificado. Makefile: +2 entradas. `login_runtime.c`:
  21577 → **20953 LOC** (-624).
- **PR C.14 DONE 2026-05-15** — `compositor_damage.c` criado (656 LOC,
  2 funcs: `compositor_plan_build`, `damage_plan_build`). Diff
  byte-for-byte verificado. Makefile: +2 entradas. `login_runtime.c`:
  20953 → **20319 LOC** (-634).
- **PR C.15 DONE 2026-05-15** — `present_plan.c` criado (366 LOC,
  1 func solo: `present_plan_build`). Diff byte-for-byte verificado.
  Makefile: +2 entradas. `login_runtime.c`: 20319 → **19974 LOC**
  (-345). Concluida Fase 3 (PRs C.7-C.15, 9 PRs totalizando -3785
  LOC removidas de `login_runtime.c`).

**Fase 4 — Per-plan reset/is_safe pairs (26 PRs, ~300-625 LOC cada)
DONE 2026-05-15:**

Cada par `*_plan_reset` + `*_plan_*_is_safe` virou um `.c` espelhando
o partial header correspondente. Concluida sequencialmente:

- **PR C.16 DONE** `schedule_plan.c` (292 LOC) — `login_runtime.c`:
  19974 → **19704 LOC** (-270).
- **PR C.17 DONE** `vsync_plan.c` (316 LOC) — 19704 → **19410 LOC**
  (-294).
- **PR C.18 DONE** `scanout_plan.c` (344 LOC) — 19410 → **19088 LOC**
  (-322).
- **PR C.19 DONE** `display_plan.c` (373 LOC) — 19088 → **18737 LOC**
  (-351).
- **PR C.20 DONE** `output_plan.c` (401 LOC) — 18737 → **18358 LOC**
  (-379).
- **PR C.21 DONE** `blit_plan.c` (432 LOC) — 18358 → **17948 LOC**
  (-410).
- **PR C.22 DONE** `framebuffer_plan.c` (468 LOC) — 17948 →
  **17502 LOC** (-446).
- **PR C.23 DONE** `flush_plan.c` (507 LOC) — 17502 → **17017 LOC**
  (-485).
- **PR C.24 DONE** `barrier_plan.c` (310 LOC) — 17017 → **16731 LOC**
  (-286). Primeira com `_is_safe` helper.
- **PR C.25 DONE** `fence_plan.c` (299 LOC) — 16731 → **16456 LOC**
  (-275).
- **PR C.26 DONE** `timeline_plan.c` (314 LOC) — 16456 → **16166 LOC**
  (-290).
- **PR C.27 DONE** `sync_plan.c` (331 LOC) — 16166 → **15859 LOC**
  (-307).
- **PR C.28 DONE** `deadline_plan.c` (340 LOC) — 15859 → **15543 LOC**
  (-316).
- **PR C.29 DONE** `completion_plan.c` (353 LOC) — 15543 →
  **15214 LOC** (-329).
- **PR C.30 DONE** `ack_plan.c` (371 LOC) — 15214 → **14867 LOC**
  (-347).
- **PR C.31 DONE** `retire_plan.c` (383 LOC) — 14867 → **14508 LOC**
  (-359).
- **PR C.32 DONE** `cleanup_plan.c` (401 LOC) — 14508 → **14131 LOC**
  (-377).
- **PR C.33 DONE** `seal_plan.c` (419 LOC) — 14131 → **13736 LOC**
  (-395).
- **PR C.34 DONE** `audit_plan.c` (434 LOC) — 13736 → **13326 LOC**
  (-410).
- **PR C.35 DONE** `record_plan.c` (451 LOC) — 13326 → **12900 LOC**
  (-426).
- **PR C.36 DONE** `receipt_plan.c` (567 LOC) — 12900 → **12358 LOC**
  (-542).
- **PR C.37 DONE** `ledger_plan.c` (610 LOC) — 12358 → **11773 LOC**
  (-585).
- **PR C.38 DONE** `journal_plan.c` (458 LOC) — 11773 → **11340 LOC**
  (-433).
- **PR C.39 DONE** `archive_plan.c` (488 LOC) — 11340 → **10877 LOC**
  (-463).
- **PR C.40 DONE** `retention_plan.c` (518 LOC) — 10877 → **10384 LOC**
  (-493).
- **PR C.41 DONE** `expiry_plan.c` (550 LOC) — 10384 → **9860 LOC**
  (-524).

Total Fase 4: 26 PRs, **-10114 LOC removidos** de `login_runtime.c`.
Todos os arquivos novos sob 900 LOC. Linkage estatica preservada
(cada `.c` proprio TU).

**Fase 5 — Purge/reclaim section (5 PRs) DONE 2026-05-15:**

- **PR C.42 DONE** `purge_plan.c` (655 LOC, 10 static helpers + 1
  builder) — 9860 → **9241 LOC** (-619).
- **PR C.43 DONE** `tombstone_plan.c` (577 LOC, 3 helpers + 1
  builder) — 9241 → **8692 LOC** (-549).
- **PR C.44 DONE** `compaction_plan.c` (448 LOC, 3 helpers + 1
  builder) — 8692 → **8272 LOC** (-420).
- **PR C.45 DONE** `reclaim_plan.c` (236 LOC, 3 helpers + 1
  builder) — 8272 → **8064 LOC** (-208).
- **PR C.46 DONE** `release_plan.c` (250 LOC, 3 helpers + 1
  builder) — 8064 → **7843 LOC** (-221).

Total Fase 5: 5 PRs, **-2017 LOC removidos** de `login_runtime.c`.

**Fase 6 — GUI/Window section (17 PRs) DONE 2026-05-15:**

- **PR C.47 DONE** `gui_plan.c` (270 LOC) — 7843 → **7602 LOC** (-241).
- **PR C.48 DONE** `window_plan.c` (287 LOC) — 7602 → **7343 LOC**
  (-259).
- **PR C.49 DONE** `window_surface_plan.c` (313 LOC) — 7343 →
  **7059 LOC** (-284).
- **PR C.50 DONE** `window_compositor_plan.c` (344 LOC) — 7059 →
  **6745 LOC** (-314).
- **PR C.51 DONE** `window_damage_plan.c` (408 LOC) — 6745 →
  **6366 LOC** (-379).
- **PR C.52 DONE** `window_present_plan.c` (439 LOC) — 6366 →
  **5956 LOC** (-410).
- **PR C.53 DONE** `window_schedule_plan.c` (499 LOC) — 5956 →
  **5486 LOC** (-470).
- **PR C.54 DONE** `window_vsync_plan.c` (463 LOC, 2 funcs) —
  5486 → **5049 LOC** (-437).
- **PR C.55 DONE** `window_scanout_plan.c` (466 LOC) — 5049 →
  **4609 LOC** (-440).
- **PR C.56 DONE** `window_display_plan.c` (492 LOC) — 4609 →
  **4143 LOC** (-466).
- **PR C.57 DONE** `window_output_plan.c` (519 LOC) — 4143 →
  **3650 LOC** (-493).
- **PR C.58 DONE** `window_blit_plan.c` (524 LOC) — 3650 →
  **3152 LOC** (-498).
- **PR C.59 DONE** `window_commit_plan.c` (536 LOC) — 3152 →
  **2642 LOC** (-510).
- **PR C.60 DONE** `window_flip_plan.c` (563 LOC) — 2642 →
  **2105 LOC** (-537).
- **PR C.61 DONE** `window_vblank_plan.c` (582 LOC) — 2105 →
  **1549 LOC** (-556).
- **PR C.62 DONE** `window_event_plan.c` (650 LOC) — 1549 →
  **925 LOC** (-624).
- **PR C.63 DONE** `window_input_plan.c` (382 LOC) — 925 →
  **570 LOC** (-355).

Total Fase 6: 17 PRs, **-7273 LOC removidos** de `login_runtime.c`.

**Fase 7 — Final cleanup (2 PRs) DONE 2026-05-15:**

- **PR C.64 DONE** `pipeline_safety.c` (233 LOC,
  `pipeline_safety_report_reset` static + public
  `pipeline_safety_report_build`) — 570 → **363 LOC** (-207).
- **PR C.65 DONE** `view_model.c` (104 LOC, `login_window_view_model_build`
  publico) — 363 → **282 LOC** (-81).

`src/auth/login_runtime.c` agora e um facade fino com 282 LOC
contendo somente:

1. As 3 inclusoes padrao;
2. 5 helpers `static` privados do entry point (`login_consume_recovery_login_request`,
   `login_show_maintenance_notice`, `login_view_text`,
   `login_render_window_preview`, `login_render_screen`);
3. O entry point publico `login_runtime_run`.

`src/auth/login_runtime.c` REMOVIDO de `MONOLITH_BASELINE_EXCEPTIONS`
em `tools/scripts/audit_source_layout.py` (comentario descritivo
substitui a entrada `Path(...)`).

### Resumo executivo do Estagio C

- **Origem**: `src/auth/login_runtime.c` com 22776 LOC (monolito).
- **Resultado**: `src/auth/login_runtime.c` com **282 LOC** (facade)
  + **65 arquivos novos** em `src/auth/login_runtime/` (todos < 900
  LOC, total 27616 LOC) + 1 internal header em `src/auth/internal/`
  (74 LOC, 5 helpers `static inline`).
- **Total de PRs**: 66 (C.0-C.65).
- **Distribuicao por fase**: Phase 2: 6 PRs; Phase 3: 9 PRs;
  Phase 4: 26 PRs; Phase 5: 5 PRs; Phase 6: 17 PRs; Phase 7: 2 PRs.
- **Maiores arquivos extraidos** (top 5):
  `compositor_damage.c` (656 LOC), `purge_plan.c` (655 LOC),
  `window_event_plan.c` (650 LOC), `frame_surface.c` (645 LOC),
  `queue_activation.c` (641 LOC).
  Todos sob o limite de 900 LOC.
- **LOC removidos**: -22494 LOC do arquivo original; reorganizados
  em arquivos focados por dominio.
- **ABI**: preservada byte-for-byte em cada PR (verificado via
  `diff <(sed ...) <(sed ...)`).
- **Linkage estatica**: preservada (cada `.c` proprio TU).
- **Makefile**: atualizado em sincronia (kernel obj list + test
  infrastructure list).
- **Audit**: `src/auth/login_runtime.c` removido da baseline; todos
  os 65 arquivos novos sob 900 LOC.

### Total realizado

- **65 arquivos novos** em `src/auth/login_runtime/` (total 27616 LOC).
- **1 internal header** em `src/auth/internal/login_runtime_internal.h`
  (74 LOC, 5 helpers `static inline`).
- **65 entradas novas** no Makefile (objeto + lista de testes).
- **66 PRs** (C.0-C.65) concluidos numa sessao continua 2026-05-15.
- Todos os arquivos resultantes < 900 LOC (rule `make layout-audit`).
- `src/auth/login_runtime.c` reduzido de 22776 LOC para 282 LOC
  (facade fino com 5 helpers privados + `login_runtime_run`).
- `src/auth/login_runtime.c` removido de `MONOLITH_BASELINE_EXCEPTIONS`.

### Padrão de cada PR

1. Identificar range de linhas das funções a extrair via `grep -n`.
2. Criar novo arquivo em `src/auth/login_runtime/<nome>.c` com:
   - `#include "auth/login_runtime.h"`
   - `#include "auth/internal/login_runtime_internal.h"`
   - `#include "lang/localization.h"` (se necessário)
   - Funções extraídas via `sed -n 'START,ENDp'` (byte-for-byte).
3. Adicionar entrada `.o` ao kernel object list (Makefile linha
   ~152).
4. Adicionar `src/auth/login_runtime/<nome>.c` ao test infrastructure
   list (Makefile linha ~1161).
5. Remover as funções extraídas de `login_runtime.c` via
   `sed -i '' 'START,ENDd'`.
6. Verificar byte-for-byte com `diff <(sed range original) <(sed
   range novo arquivo)`.
7. Verificar que `login_runtime.c` não tem mais nenhuma referência
   inline às funções extraídas.

### Riscos identificados

- **Mudanças no Makefile** não podem ser validadas localmente
  (política de execução). Recomendar `make all64` + `make test`
  como gate externo após cada PR significativa.
- **Funções `static` no `.c` original** precisam manter linkage de
  módulo. Solução: marcar como `static` no novo arquivo também
  (cada `.c` é seu próprio TU).
- **Funções não-`static` (públicas)** precisam ter seus protótipos
  resolvíveis. Solução: já estão declarados no header
  `function_declarations.h` do partial header tree.

## 13. Estágio D — `tests/auth/test_login_runtime.c` — **CONCLUÍDO 2026-05-16**

Após a conclusão do Estágio C (66 PRs C.0-C.65 em 2026-05-15), o
arquivo `tests/auth/test_login_runtime.c` (originalmente 25 662 LOC,
22 113 LOC depois das ondas anteriores) foi dividido em **46 arquivos
companion** + 1 internal header + 1 entry/facade fino em **41 PRs
manuais** (D.7-D.47) numa sessão contínua 2026-05-16.

### D.0 Decisão de design

- **Espelho 1:1 com Estágio C**: cada `_cases()` companion file
  cobre o mesmo stage do pipeline credential screen que o seu
  source TU equivalente em `src/auth/login_runtime/`.
- **Internal test header**: `tests/auth/test_login_runtime_internal.h`
  (381 LOC) declara 47 entry points `_cases()` + 57 helpers `build_*`
  compartilhados, permitindo que stages downstream construam sobre
  fixtures upstream sem duplicar setup.
- **Helper chaining**: cada companion file expõe seu `build_*` helper
  (mudou de `static int` para `int`) ao ser usado por stages
  subsequentes. Assim a chain de 58 stages
  (`controller → presenter → ... → window_input → pipeline_safety_report`)
  é montada via composição.
- **Byte-for-byte parity**: cada um dos 41 PRs foi verificado com
  `diff` antes da deleção do bloco original.

### D.1 Sequência executada

| Fase | PRs | Stages cobertos |
|---|---:|---|
| Pre-pipeline | D.7-D.12 | pre_pipeline, input_view, audit_view, ui_session, screen, action_event |
| Controller/presenter | D.13-D.16 | route_controller, presenter_binding, mount_commit, handoff_dispatch |
| Queue/frame | D.17-D.19 | queue_activation, frame_surface, compositor_damage |
| Present chain | D.20-D.22 | present_plan, schedule_vsync, scanout_display |
| Output/blit chain | D.23-D.28 | output_blit, framebuffer_flush, barrier_fence, timeline_sync, deadline_completion, ack_retire |
| Audit/seal chain | D.24-D.27 | cleanup_seal, audit_record, receipt_ledger, journal_archive |
| Retention chain | D.28-D.32 | retention_expiry, purge, tombstone, compaction_reclaim, release_gui |
| Window chain | D.33-D.46 | window_surface → window_input (14 stages, vários splits por LOC) |
| Final | D.47 | pipeline_safety_report |

### D.2 Resultado final

- **Arquivo principal**: `tests/auth/test_login_runtime.c`
  reduzido de **22 113 LOC** (após onda 6) para **529 LOC** (97,6%
  de redução, -21 584 LOC).
- **Companion files**: 46 arquivos novos em `tests/auth/`, todos
  sob 900 LOC. Maior: `test_login_runtime_credential_screen.c`
  (857 LOC); menor: `test_login_runtime_credential_present_plan.c`
  (288 LOC). Total: 27 750 LOC organizados.
- **Internal header**: `tests/auth/test_login_runtime_internal.h`
  (381 LOC) com 47 entry points + 57 helpers compartilhados.
- **Makefile**: 46 entradas novas na lista de testes
  (`SRCS_test_auth_login_runtime`).
- **Audit baseline**: `tests/auth/test_login_runtime.c` removido de
  `MONOLITH_BASELINE_EXCEPTIONS` em `tools/scripts/audit_source_layout.py`
  (substituído por comentário documentando o split).
- **PRs concluídos**: 41 (D.7-D.47), cada um verificado byte-for-byte
  via `diff` antes da deleção.
- **Testes**: 351 funções de teste extraídas + 47 entry points
  `_cases()` (alguns reagrupados em entries combinados).

### D.3 Risco e mitigação aplicados

| Risco | Mitigação aplicada |
|---|---|
| Mudança de comportamento nos testes | Parity byte-for-byte verificado via `diff` em cada PR |
| Cross-TU static linkage | Mantida: cada companion file é seu próprio TU |
| Helper chain quebrar | Helpers `build_*` expostos via `int` (não `static int`) com declarações no internal header |
| Companion file > 900 LOC | 5 splits adicionais aplicados (purge_tombstone, present_schedule, vsync_scanout, etc.) |
| Makefile out-of-sync | Atualizado em cada PR, validado por inspeção visual |

### D.4 Validação externa pendente

Esta máquina é review/edit only (política operacional). Os gates
externos abaixo devem ser executados em CI ou outra máquina:

- `make test` — exercita os 47 entry points via `run_login_runtime_tests`
  no arquivo principal, depois propaga para os 46 companion files.
- `make layout-audit` — confirma que `tests/auth/test_login_runtime.c`
  saiu da baseline e que nenhum dos 46 companion files passa de 900 LOC.
- `make release-check` — sanity completo (recomendado após PR final).

### D.5 Critério de conclusão

Estágio D é considerado concluído quando:

1. ✅ `tests/auth/test_login_runtime.c` < 900 LOC (atual: 529 LOC).
2. ✅ Todos os 46 companion files < 900 LOC (verificado via
   `wc -l tests/auth/test_login_runtime_credential_*.c`).
3. ✅ `tests/auth/test_login_runtime.c` removido de
   `MONOLITH_BASELINE_EXCEPTIONS`.
4. ✅ Internal test header criado com declarações compartilhadas.
5. ✅ Makefile atualizado com 46 entradas novas.
6. ✅ Plano dedicado documentado (esta seção).
7. ⏳ Validação externa (`make test` + `make layout-audit` +
   `make release-check`) — pendente, depende de CI ou outra máquina.

Após validação externa positiva, **este documento deve ser movido
para `docs/plans/historical/`** e o roadmap principal
(`docs/architecture/monolith-refactor-roadmap.md`) atualizado para
refletir 12/12 monolitos refatorados (100%), excluindo apenas
`kernel_main.c` deferred conforme Estágio E.

## 14. Hardening preventivo de near-violation — **CONCLUÍDO 2026-05-16**

Após o fechamento técnico do Estágio D, uma varredura por arquivos
próximos do teto de 900 LOC do audit identificou
`src/security/volume_provider.c` em **899/900 LOC** — literalmente 1
linha do gatilho. Embora o arquivo NÃO estivesse listado em
`MONOLITH_BASELINE_EXCEPTIONS` (não era violação ainda), qualquer
correção futura de segurança ou nova feature derrubaria o audit.

### 14.1 Decisões de design

1. **Divisão por superfície de orquestração, não por função.** A TU
   original ligava o estado estável de volume (`install` / `open`)
   à orquestração de rekey (preflight, plan, dry-run execute,
   checkpoint). A separação natural espelha o que já existia nos
   siblings `volume_provider_rekey_execute.c` /
   `_rekey_commit.c` / `_rekey_recovery.c` / `_rekey_orchestrator.c`
   (write-enabled): cada TU é independente, com seus próprios
   helpers `static`.
2. **Helpers duplicados por TU.** `vp_wipe` / `vp_get_u32_le` /
   `vp_put_u32_le` / `vp_crc32` são reimplementados como `static`
   em cada TU. Isso preserva o padrão "no link-time coupling"
   estabelecido por `volume_header.c` e pelos siblings de rekey
   write-side, e mantém auditabilidade individual de cada TU.
3. **ABI público preservado.** `include/security/volume_provider.h`
   permanece inalterado. Todas as 8 funções públicas
   (`volume_provider_install`, `_open`, `_rekey_preflight`,
   `_rekey_plan`, `_rekey_execute`, `_rekey_checkpoint_init`,
   `_rekey_checkpoint_serialize`, `_rekey_checkpoint_parse`)
   mantêm assinatura idêntica; somente o ponto de definição muda.
4. **Capyfs.h removido da TU principal.** Após a extração,
   `volume_provider.c` não usa mais nenhum símbolo de
   `fs/capyfs.h` (era consumido apenas por
   `vp_capyfs_plain_super_valid`, que foi para o sibling).
   O `#include` foi retirado para minimizar acoplamento.
5. **Validação byte-a-byte.** Cada um dos 8 blocos extraídos
   (helpers + 3 validators + 5 funções rekey públicas) foi
   verificado via `diff` contra o original antes da remoção da
   monólito; todos retornaram 0 bytes de diferença.

### 14.2 Resultado final

| Arquivo | LOC antes | LOC depois | Headroom até 900 |
|---|---:|---:|---:|
| `src/security/volume_provider.c` | 899 | **296** | 604 |
| `src/security/volume_provider_rekey.c` | — (novo) | **679** | 221 |

Ambos com folga substancial para correções de segurança futuras
sem disparar o audit. O TU principal mantém apenas
`volume_provider_install`, `volume_provider_open`, `vp_wipe` e
constantes. O sibling novo possui as 6 funções rekey públicas + 3
validators `static` + suas próprias cópias dos helpers de
baixo-nível.

### 14.3 Mudanças efetuadas

1. **Novo arquivo**: `src/security/volume_provider_rekey.c` (679 LOC).
2. **Arquivo modificado**: `src/security/volume_provider.c` (899 → 296 LOC).
3. **Makefile** (`CAPYOS64_OBJS`): adicionado
   `$(BUILD)/x86_64/security/volume_provider_rekey.o` entre
   `volume_provider.o` e `volume_provider_rekey_execute.o`.
4. **Makefile** (`TEST_SRCS`): adicionado
   `src/security/volume_provider_rekey.c` na linha que já lista
   `volume_provider.c` + os siblings de rekey write-side, de modo
   que `test_volume_provider*` continuam linkando todas as TUs.
5. **Audit script** (`tools/scripts/audit_source_layout.py`):
   adicionado comentário explicando o split preventivo (o arquivo
   não entra em `MONOLITH_BASELINE_EXCEPTIONS` porque nunca
   violou o limite).
6. **Comentários de cabeçalho**: ambos os arquivos ganharam blocos
   de comentário explicando o split, a relação entre TUs e o
   padrão de helpers duplicados.

### 14.4 Validação externa recomendada

Como esta máquina é apenas de revisão/edição, recomenda-se executar
em CI ou outra máquina:

- `make test` — exercita `test_volume_provider*` que cobrem as 8
  funções públicas atravessadas pelo split.
- `make layout-audit` — confirma que ambos os arquivos estão sob o
  limite de 900 LOC e que nenhum outro arquivo foi quebrado.
- `make all64` — confirma compilação x86_64 com kernel obj list
  atualizada.
- `make release-check` — sanidade final.

### 14.5 Watchlist remanescente

Outros arquivos near-violation identificados na mesma varredura
(não tocados nesta entrega, candidatos para hardening preventivo
futuro caso ganhem novas features):

| Arquivo | LOC | Headroom |
|---|---:|---:|
| `tests/security/test_volume_provider_rekey_execute.c` | 896 | 4 |
| `src/security/volume_provider_rekey_execute.c` | 893 | 7 |
| `src/shell/commands/system_control/jobs_updates.c` | 892 | 8 |
| `tests/security/test_volume_provider.c` | 867 | 33 |
| `src/gui/desktop/desktop.c` | 859 | 41 |
| `tests/auth/test_login_runtime_credential_screen.c` | 857 | 43 |

Estes não são violações ativas; cada um terá split preventivo
agendado caso novas features sejam adicionadas. Documentados aqui
para visibilidade.

## 15. Hardening preventivo segundo par (rekey_execute + tests) — **CONCLUÍDO 2026-05-16**

Após o split de `volume_provider.c` (§14), os próximos dois alvos do
watchlist eram o par fonte/teste do executor de rekey:

- `src/security/volume_provider_rekey_execute.c` em **893/900** (7 LOC).
- `tests/security/test_volume_provider_rekey_execute.c` em **896/900** (4 LOC).

Ambos a 1% do gatilho do audit. A próxima security fix ou novo
status enum disparou o audit.

### 15.1 Decisões de design

1. **Extrair a fase de copy step.** Das três fases write-enabled
   (`_execute_checkpoint`, `_execute_stage_header`, `_execute_copy_step`),
   `_copy_step` é a maior em LOC (307 linhas, ~35% do arquivo) e a
   mais isolada por responsabilidade: faz uma cópia reverse + re-
   encrypt de um bloco do domínio legacy para o header-managed,
   atualiza o checkpoint no scratch e verifica via read-back. Mover
   essa função sozinha libera o máximo de headroom com a menor
   superfície de mudança.
2. **Helpers duplicados por TU (igual §14).** A nova TU
   `volume_provider_rekey_copy.c` carrega suas próprias cópias
   `static` de `vp_rekey_exec_wipe`, `vp_rekey_block_same`,
   `vp_rekey_checkpoint_same`, `vp_rekey_offset_view` (struct +
   read + write callbacks + `vp_rekey_offset_ops` vtable) e
   `VP_REKEY_EXEC_BLOCK_SIZE`. Preserva o padrão "no link-time
   coupling" estabelecido em `volume_header.c` e em todos os
   siblings do rekey.
3. **ABI público preservado.** `volume_provider_rekey_execute_copy_step`
   mantém assinatura idêntica. Apenas o ponto de definição muda; o
   header `include/security/volume_provider.h` não é tocado.
4. **Test split simétrico.** O arquivo de teste sofreu split
   espelhado: os 5 testes `test_copy_step_*` foram movidos para
   `tests/security/test_volume_provider_rekey_copy.c` (com sua
   própria infraestrutura de fixture `rekey_copy_*`). O novo
   runner `run_volume_provider_rekey_copy_tests` foi declarado e
   invocado em `tests/test_runner.c`, mantendo a ordem de execução
   próxima ao runner do executor.
5. **Validação byte-a-byte do source.** O corpo da função
   `_copy_step` (lines 587-893 do original) foi verificado via
   `diff` contra a nova localização (lines 118-424 da nova TU):
   0 bytes de diferença.

### 15.2 Resultado final

| Arquivo | LOC antes | LOC depois | Headroom |
|---|---:|---:|---:|
| `src/security/volume_provider_rekey_execute.c` | 893 | **585** | 315 |
| `src/security/volume_provider_rekey_copy.c` | — (novo) | **424** | 476 |
| `tests/security/test_volume_provider_rekey_execute.c` | 896 | **723** | 177 |
| `tests/security/test_volume_provider_rekey_copy.c` | — (novo) | **442** | 458 |

O test do executor ainda fica a 177 LOC do limite. Isso é
aceitável dado que o restante do arquivo (checkpoint tests +
stage_header tests + stage_manifest test) é estável; nova feature
disparará novo split (provavelmente extraindo os
`test_stage_header_*` para `test_volume_provider_rekey_stage_header.c`).

### 15.3 Mudanças efetuadas

1. **Nova fonte**: `src/security/volume_provider_rekey_copy.c` (424 LOC).
2. **Fonte modificada**: `src/security/volume_provider_rekey_execute.c`
   (893 → 585 LOC, função `_copy_step` removida).
3. **Novo teste**: `tests/security/test_volume_provider_rekey_copy.c`
   (442 LOC) com 5 testes `test_copy_step_*` + fixture própria +
   runner `run_volume_provider_rekey_copy_tests`.
4. **Teste modificado**: `tests/security/test_volume_provider_rekey_execute.c`
   (896 → 723 LOC, 5 testes copy_step + 5 chamadas no runner removidas).
5. **Test runner** (`tests/test_runner.c`): declaração + invocação do
   novo `run_volume_provider_rekey_copy_tests` adicionadas após as
   entradas do `_rekey_execute_tests` correspondentes.
6. **Makefile** (`CAPYOS64_OBJS`): adicionado
   `$(BUILD)/x86_64/security/volume_provider_rekey_copy.o` entre
   `volume_provider_rekey_execute.o` e `volume_provider_rekey_commit.o`.
7. **Makefile** (`TEST_SRCS`): adicionado tanto
   `src/security/volume_provider_rekey_copy.c` quanto
   `tests/security/test_volume_provider_rekey_copy.c` na linha que
   linka os testes de rekey write-side.

### 15.4 Validação externa recomendada

Como esta máquina é apenas de revisão/edição, recomenda-se executar
em CI ou outra máquina:

- `make test` — exercita os 5 `test_copy_step_*` no novo runner
  + verifica que os 15 testes restantes em `_execute_tests` continuam
  passando.
- `make layout-audit` — confirma que todos os 4 arquivos do par
  estão sob o limite de 900 LOC.
- `make all64` — confirma compilação x86_64 com o novo kernel obj
  `volume_provider_rekey_copy.o` linkado.
- `make release-check` — sanidade final.

### 15.5 Watchlist atualizada

Após §14 e §15, os arquivos near-violation remanescentes são:

| Arquivo | LOC | Headroom |
|---|---:|---:|
| `src/shell/commands/system_control/jobs_updates.c` | 892 | 8 |
| `tests/security/test_volume_provider.c` | 867 | 33 |
| `src/gui/desktop/desktop.c` | 859 | 41 |
| `tests/auth/test_login_runtime_credential_screen.c` | 857 | 43 |
| `tests/security/test_volume_provider_rekey_execute.c` | 723 | 177 |

`jobs_updates.c` continua sendo o mais urgente fora de security/.

## 16. Hardening preventivo terceiro arquivo (jobs_updates) — **CONCLUÍDO 2026-05-16**

Terceiro item do watchlist: `src/shell/commands/system_control/jobs_updates.c`
em **892/900** LOC, a 8 LOC do gatilho. Esse é o primeiro split
preventivo fora de `src/security/` nesta esteira de hardening.

### 16.1 Decisões de design

1. **Extrair comandos por afinidade de domínio.** O arquivo tem
   1 comando `cmd_job_run` (work queue) + 4 helpers static + 14
   comandos `cmd_update_*` (update agent). Dos 14 comandos de
   update, 4 (`cmd_update_arm`, `cmd_update_clear`,
   `cmd_update_import_manifest`, `cmd_update_channel`) só tocam o
   estado persistente do update_agent — sem download, prepare ou
   apply real. Esses 4 formam o grupo de "boot/config armarmento"
   e foram extraídos juntos para o novo TU
   `src/shell/commands/system_control/updates_arm.c`.
2. **Promover helpers compartilhados para extern linkage.** Dois
   dos 4 helpers `static` (`update_runtime_writer` e
   `refresh_update_agent_service_state`) são usados pelos
   comandos extraídos. Em vez de duplicar (~80 LOC), foram
   promovidos para extern linkage e declarados em
   `internal/system_control_internal.h` sob um novo header
   "jobs_updates shared helpers". O padrão `static` é mantido
   para os outros dois helpers (`update_runtime_bytes_writer`,
   `update_runtime_remover`) que só atendem callers que
   permanecem no TU pai — preservando o princípio de menor
   superfície externa.
3. **ABI público preservado.** Os 4 comandos extraídos mantêm
   assinatura idêntica. As declarações na `system_control_internal.h`
   continuam idênticas, então `power_runtime_registry.c` (que
   registra os 4 comandos no command table) não precisa de mudança
   alguma — só recompila contra os mesmos protótipos.
4. **Naming pragmático.** O arquivo `jobs_updates.c` mantém o nome
   apesar de agora conter mais updates do que jobs. Renomear
   teria impacto em Makefile, audit script e em qualquer ferramenta
   que rastreia history por path. O ganho de clareza não justifica
   o churn.
5. **Validação byte-a-byte.** Cada um dos 4 corpos extraídos
   (`cmd_update_arm`, `_clear`, `_import_manifest`, `_channel`) foi
   verificado via `diff` contra a nova localização: 0 bytes de
   diferença.

### 16.2 Resultado final

| Arquivo | LOC antes | LOC depois | Headroom |
|---|---:|---:|---:|
| `src/shell/commands/system_control/jobs_updates.c` | 892 | **642** | 258 |
| `src/shell/commands/system_control/updates_arm.c` | — (novo) | **270** | 630 |

### 16.3 Mudanças efetuadas

1. **Novo arquivo**: `src/shell/commands/system_control/updates_arm.c`
   (270 LOC) com 4 comandos + bloco de cabeçalho explicando o split.
2. **Arquivo modificado**: `src/shell/commands/system_control/jobs_updates.c`
   (892 → 642 LOC, 4 comandos removidos, 2 helpers promovidos para
   extern).
3. **Header modificado**:
   `src/shell/commands/system_control/internal/system_control_internal.h`
   ganhou nova seção "jobs_updates shared helpers" declarando
   `update_runtime_writer` e `refresh_update_agent_service_state`.
4. **Makefile** (`CAPYOS64_OBJS`): adicionado
   `$(BUILD)/x86_64/shell/commands/system_control/updates_arm.o`
   entre `jobs_updates.o` e `service_target_resume.o`.

### 16.4 Validação externa recomendada

- `make test` — exercita o comando registry e qualquer integração
  com os 4 comandos extraídos.
- `make layout-audit` — confirma que ambos os arquivos do par
  estão sob o limite de 900 LOC.
- `make all64` — confirma compilação x86_64 com o novo kernel obj
  `updates_arm.o` linkado, incluindo as referências externas dos
  dois helpers promovidos.
- `make release-check` — sanidade final.

### 16.5 Watchlist após §16

Após §14, §15 e §16, os arquivos near-violation remanescentes são:

| Arquivo | LOC | Headroom |
|---|---:|---:|
| `tests/security/test_volume_provider.c` | 867 | 33 |
| `src/gui/desktop/desktop.c` | 859 | 41 |
| `tests/auth/test_login_runtime_credential_screen.c` | 857 | 43 |
| `src/shell/commands/system_control/jobs_updates.c` | 642 | 258 |
| `tests/security/test_volume_provider_rekey_execute.c` | 723 | 177 |

## 17. Hardening preventivo quarto arquivo (test_volume_provider) — **CONCLUÍDO 2026-05-16**

Quarto item do watchlist: `tests/security/test_volume_provider.c`
em **867/900** LOC, a 33 LOC do gatilho. Mirror espelho do split
de source feito em §14 (`volume_provider.c` foi separado em
`volume_provider.c` + `volume_provider_rekey.c`).

### 17.1 Decisões de design

1. **Espelhar o split de source.** O source-side está separado em
   `volume_provider.c` (install/open) e `volume_provider_rekey.c`
   (preflight/plan/dry-run-execute/checkpoint). Os 8 testes de
   rekey (4 preflight + 4 plan) foram movidos para um TU de teste
   paralelo `test_volume_provider_rekey.c` para deixar a estrutura
   de teste simétrica com a estrutura de source.
2. **Helpers privados por TU.** Cada TU de teste carrega sua
   própria cópia de `struct ram_dev`, `ram_read_block`,
   `ram_write_block`, `g_ram_ops`, `ram_alloc`, `ram_free`,
   `expect_int`, `expect_true`, `test_put_u32_le` e
   `write_legacy_capyfs_super`. Todos `static` — sem collision
   de linker. Mesmo padrão de §15
   (`test_volume_provider_rekey_copy.c`).
3. **Runner próprio.** Novo `run_volume_provider_rekey_tests`
   declarado e invocado em `tests/test_runner.c` diretamente após
   `run_volume_provider_tests`. Os 17 `fails +=` calls do
   dispatcher original ficaram 9 (só install/open) no parent; os
   outros 8 vão para o runner novo.
4. **Validação byte-a-byte.** Os 8 corpos de teste (linhas 540-840
   do original) foram verificados via `diff` contra a nova
   localização (linhas 170-470 do novo): 0 bytes de diferença.

### 17.2 Resultado final

| Arquivo | LOC antes | LOC depois | Headroom |
|---|---:|---:|---:|
| `tests/security/test_volume_provider.c` | 867 | **555** | 345 |
| `tests/security/test_volume_provider_rekey.c` | — (novo) | **488** | 412 |

### 17.3 Mudanças efetuadas

1. **Novo arquivo**: `tests/security/test_volume_provider_rekey.c`
   (488 LOC) com 8 testes de rekey + fixture própria + runner
   `run_volume_provider_rekey_tests`.
2. **Arquivo modificado**: `tests/security/test_volume_provider.c`
   (867 → 555 LOC, 8 testes de rekey removidos + 8 `fails +=`
   calls removidos do `run_volume_provider_tests`).
3. **Test runner** (`tests/test_runner.c`): declaração + invocação
   do novo `run_volume_provider_rekey_tests` adicionadas após
   `run_volume_provider_tests`.
4. **Makefile** (`TEST_SRCS`): adicionado
   `tests/security/test_volume_provider_rekey.c` na linha de testes
   de volume_provider.

### 17.4 Validação externa recomendada

- `make test` — exercita os 8 testes de rekey no novo runner +
  verifica que os 9 testes restantes de install/open continuam
  passando.
- `make layout-audit` — confirma que ambos os arquivos do par
  estão sob o limite de 900 LOC.
- `make release-check` — sanidade final.

### 17.5 Watchlist após §17

Após §14, §15, §16 e §17, os arquivos near-violation remanescentes
são:

| Arquivo | LOC | Headroom |
|---|---:|---:|
| `src/gui/desktop/desktop.c` | 859 | 41 |
| `tests/auth/test_login_runtime_credential_screen.c` | 857 | 43 |
| `tests/security/test_volume_provider_rekey_execute.c` | 723 | 177 |
| `src/shell/commands/system_control/jobs_updates.c` | 642 | 258 |
| `tests/security/test_volume_provider.c` | 555 | 345 |
| `tests/security/test_volume_provider_rekey.c` | 488 | 412 |

Todos os top-headroom-deficit files críticos (≤10 LOC) e os
próximos dois mais urgentes (33-43 LOC) já foram parcialmente
endereçados. Os 2 remanescentes em <50 LOC headroom
(`desktop.c` e `test_login_runtime_credential_screen.c`) ainda
podem ser objeto de hardening preventivo futuro se ganharem novas
features.

## 18. Hardening preventivo quinto arquivo (desktop) — **CONCLUÍDO 2026-05-16**

Quinto item do watchlist: `src/gui/desktop/desktop.c` em
**859/900** LOC, a 41 LOC do gatilho. Primeiro split preventivo
fora de `src/security/` e shell — este é GUI/desktop, perfil de
risco mais baixo.

### 18.1 Decisões de design

1. **Extrair a maior função em um único corte.** O arquivo tem 35
   funções (static helpers, menu actions, lifecycle, terminal,
   input/mouse, frame loop). A função `desktop_handle_mouse`
   sozinha tem ~253 LOC — o maior bloco contíguo. Movê-la para um
   sibling resolve o headroom em um único corte cirúrgico.
2. **Sibling de domínio.** O diretório `src/gui/desktop/` já tem
   o padrão `desktop_*.c` (desktop_icons.c, desktop_runtime.c,
   desktop_smoke_readiness.c). Adicionar `desktop_mouse.c` segue
   exatamente esse padrão.
3. **Helper duplicado.** A única função `static` usada pelo
   handler de mouse, `desktop_overlay_active` (4 LOC,
   `return inline_prompt_is_open() || context_menu_is_open() ||
   (ds && ds->taskbar.menu_open);`), foi duplicada como `static`
   no novo TU. Padrão "no link-time coupling" preservado.
4. **ABI pública preservada.** Declaração de
   `desktop_handle_mouse` em `include/gui/desktop.h` inalterada.
   O caller `desktop_run_frame` (que continua em `desktop.c`)
   resolve via linkage externa normal.
5. **Includes mínimos.** O novo TU só inclui o que precisa:
   `gui/desktop.h`, `gui/desktop_runtime.h`, `gui/compositor.h`,
   `gui/taskbar.h`, `gui/event.h`, `gui/context_menu.h`,
   `gui/desktop_icons.h`, `gui/inline_prompt.h`,
   `drivers/input/mouse.h`, `<stddef.h>`. Os includes de apps
   (calculator/file_manager/text_editor/settings/task_manager),
   font/widget/rtc/kstring/kmem/session/app_language/net/apic
   ficam só no parent — não são necessários para o mouse handler.
6. **Validação byte-a-byte.** O corpo da função (linhas 527-779
   do original, lines 40-292 do novo TU) verificado via `diff`:
   0 bytes de diferença.

### 18.2 Resultado final

| Arquivo | LOC antes | LOC depois | Headroom |
|---|---:|---:|---:|
| `src/gui/desktop/desktop.c` | 859 | **605** | 295 |
| `src/gui/desktop/desktop_mouse.c` | — (novo) | **292** | 608 |

### 18.3 Mudanças efetuadas

1. **Novo arquivo**: `src/gui/desktop/desktop_mouse.c` (292 LOC)
   com `desktop_handle_mouse` + duplicate de `desktop_overlay_active`
   + bloco de cabeçalho explicando o split.
2. **Arquivo modificado**: `src/gui/desktop/desktop.c` (859 → 605
   LOC, função `desktop_handle_mouse` removida).
3. **Makefile** (`CAPYOS64_OBJS`): adicionado
   `$(BUILD)/x86_64/gui/desktop/desktop_mouse.o` entre
   `desktop.o` e `desktop_smoke_readiness.o`.
4. **Header inalterado**: `include/gui/desktop.h:227` já declara
   `desktop_handle_mouse` publicamente; nenhuma mudança necessária.

### 18.4 Validação externa recomendada

- `make test` — exercita os testes de input/desktop que dependem
  do mouse handler.
- `make layout-audit` — confirma que ambos os arquivos do par
  estão sob o limite de 900 LOC.
- `make all64` — confirma compilação x86_64 com o novo kernel obj
  `desktop_mouse.o` linkado e a chamada externa resolvida.
- `make release-check` — sanidade final.

### 18.5 Watchlist após §18

Após §14, §15, §16, §17 e §18, os arquivos near-violation
remanescentes são:

| Arquivo | LOC | Headroom |
|---|---:|---:|
| `tests/auth/test_login_runtime_credential_screen.c` | 857 | 43 |
| `tests/security/test_volume_provider_rekey_execute.c` | 723 | 177 |
| `src/shell/commands/system_control/jobs_updates.c` | 642 | 258 |
| `src/gui/desktop/desktop.c` | 605 | 295 |
| `tests/security/test_volume_provider.c` | 555 | 345 |
| `tests/security/test_volume_provider_rekey.c` | 488 | 412 |
| `src/gui/desktop/desktop_mouse.c` | 292 | 608 |

Apenas 1 arquivo com headroom <50 LOC remanescente:
`test_login_runtime_credential_screen.c` em 43 LOC. Esse é o
último candidato natural para hardening preventivo nesta esteira.

## 19. Hardening preventivo sexto arquivo (test_login_runtime_credential_screen) — **CONCLUÍDO 2026-05-16**

Sexto e último item do watchlist:
`tests/auth/test_login_runtime_credential_screen.c` em **857/900**
LOC, a 43 LOC do gatilho. Este é um **split de segunda geração** —
o arquivo já é um companion file extraído de
`test_login_runtime.c` no monolith refactor de 2026-05-15
(PR D.6, Estágio D), e agora cresceu novamente até quase tocar o
limite.

### 19.1 Decisões de design

1. **Split companion-companion.** O padrão Estágio D usa:
   - Um runner `run_login_runtime_tests` em `test_login_runtime.c`
     que chama uma série de `_cases()` declarados no internal
     header `test_login_runtime_internal.h`.
   - Cada companion file implementa exatamente um `_cases()`.

   Mantemos esse padrão: o novo companion declara seu próprio
   `_cases()`, registrado no internal header e invocado pelo
   runner pai. O parent companion continua existindo com seu
   `_cases()` original mas só chama os tests remanescentes.
2. **Critério de corte.** Os 15 testes do parent dividem-se em 3
   fases lógicas: 6 view_model + 5 session + 5 render_plan. Os
   view_model são o maior grupo contíguo (~342 LOC de corpos de
   teste). Movê-los inteiros para um sibling deixa o parent com
   10 testes session+render_plan e ~492 LOC de body.
3. **Header compartilhado.** Ambos os companions incluem
   `tests/auth/test_login_runtime_internal.h`, que já declara os
   helpers compartilhados (`reset_test_state`, `build_ops`,
   `expect_true`, `strings_equal`, `g_runtime_maintenance_active`).
   Nenhuma duplicação de fixture necessária — diferente dos splits
   de §15/§17 onde cada TU de teste tinha sua própria cópia.
4. **Validação byte-a-byte.** Os 6 corpos de teste (linhas 33-374
   do original) verificados via `diff` contra a nova localização
   (linhas 28-369 do novo file): 0 bytes de diferença.
5. **Header do parent atualizado.** O bloco-comentário no topo do
   parent agora reflete: "originally carved out [...] at the
   2026-05-15 monolith refactor [...] and further split at the
   2026-05-16 preventive refactor". A história fica preservada
   in-file.

### 19.2 Resultado final

| Arquivo | LOC antes | LOC depois | Headroom |
|---|---:|---:|---:|
| `tests/auth/test_login_runtime_credential_screen.c` | 857 | **509** | 391 |
| `tests/auth/test_login_runtime_credential_screen_view_model.c` | — (novo) | **380** | 520 |

### 19.3 Mudanças efetuadas

1. **Novo arquivo**:
   `tests/auth/test_login_runtime_credential_screen_view_model.c`
   (380 LOC) com 6 testes de view_model + runner
   `test_login_runtime_credential_screen_view_model_cases` + bloco
   de cabeçalho.
2. **Arquivo modificado**:
   `tests/auth/test_login_runtime_credential_screen.c` (857 → 509
   LOC, 6 testes view_model removidos + 6 `fails +=` calls
   removidos do `_cases()` + header atualizado).
3. **Internal header**
   (`tests/auth/test_login_runtime_internal.h`): declaração do
   novo `_screen_view_model_cases()` adicionada na linha 72,
   diretamente após `_screen_cases()`.
4. **Test runner** (`tests/auth/test_login_runtime.c`): invocação
   do novo `_screen_view_model_cases()` adicionada na linha 479,
   diretamente após `_screen_cases()`.
5. **Makefile** (`TEST_SRCS`): adicionado
   `tests/auth/test_login_runtime_credential_screen_view_model.c`
   na linha 1226, diretamente após o parent companion.

### 19.4 Validação externa recomendada

- `make test` — exercita os 6 view_model tests no novo runner +
  verifica que os 10 tests restantes de session/render_plan
  continuam passando no parent.
- `make layout-audit` — confirma que ambos os companions estão
  sob o limite de 900 LOC.
- `make release-check` — sanidade final.

### 19.5 Watchlist após §19 — **CICLO COMPLETO**

Após §14, §15, §16, §17, §18 e §19, todos os arquivos com headroom
<50 LOC foram endereçados. Estado final do top-headroom-deficit
list:

| Arquivo | LOC | Headroom |
|---|---:|---:|
| `tests/security/test_volume_provider_rekey_execute.c` | 723 | 177 |
| `src/shell/commands/system_control/jobs_updates.c` | 642 | 258 |
| `src/gui/desktop/desktop.c` | 605 | 295 |
| `tests/security/test_volume_provider.c` | 555 | 345 |
| `tests/auth/test_login_runtime_credential_screen.c` | 509 | 391 |
| `tests/security/test_volume_provider_rekey.c` | 488 | 412 |
| `tests/auth/test_login_runtime_credential_screen_view_model.c` | 380 | 520 |
| `src/gui/desktop/desktop_mouse.c` | 292 | 608 |
| `src/shell/commands/system_control/updates_arm.c` | 270 | 630 |

**Nenhum arquivo restante com headroom <175 LOC.** O esforço
preventivo de hardening do watchlist está completo nesta esteira.

## 20. Hardening preventivo sétimo arquivo (test_login_runtime_credential_input_view) — **CONCLUÍDO 2026-05-16**

Sétimo arquivo descoberto **após** declarar o ciclo "completo" em
§19.5 — uma varredura fresh do workspace revelou
`tests/auth/test_login_runtime_credential_input_view.c` em
**856/900** LOC, a 44 LOC do gatilho, que estava ausente do
watchlist inicial. Este é outro split de segunda geração, similar
a §19.

### 20.1 Decisões de design

1. **Padrão idêntico a §19.** Esse é o peer de `_screen.c` na
   família de companions D.3 — mesmas decisões valem:
   - Split companion-companion via `_cases()` entries.
   - Helpers e fixtures permanecem em
     `test_login_runtime_internal.h` (zero duplicação).
   - Header do parent atualizado para refletir a 2ª geração.
2. **Critério de corte.** Os 16 testes do parent dividem-se em 4
   fases lógicas de 4 testes cada:
   - input_reducer (4 tests, ~188 LOC) — fica no parent
   - field_view (4 tests, ~180 LOC) — fica no parent
   - credential_panel (4 tests, ~228 LOC) — vai para sibling
   - credential_interaction (4 tests, ~202 LOC) — vai para sibling

   As 2 fases "composed" (panel + interaction) somam ~430 LOC e
   formam um grupo natural — a sibling cobre exatamente o
   `credential_panel_build` + `credential_interaction_step` enquanto
   o parent cobre exatamente o `credential_input_apply` +
   `credential_field_view_build` (as primitives).
3. **Validação byte-a-byte.** Os 8 corpos de teste (linhas 405-835
   do original) verificados via `diff` contra a nova localização
   (linhas 30-460 do novo file): 0 bytes de diferença.

### 20.2 Resultado final

| Arquivo | LOC antes | LOC depois | Headroom |
|---|---:|---:|---:|
| `tests/auth/test_login_runtime_credential_input_view.c` | 856 | **416** | 484 |
| `tests/auth/test_login_runtime_credential_input_view_panel.c` | — (novo) | **473** | 427 |

### 20.3 Mudanças efetuadas

1. **Novo arquivo**:
   `tests/auth/test_login_runtime_credential_input_view_panel.c`
   (473 LOC) com 8 testes panel+interaction + runner
   `test_login_runtime_credential_input_view_panel_cases` + bloco
   de cabeçalho.
2. **Arquivo modificado**:
   `tests/auth/test_login_runtime_credential_input_view.c`
   (856 → 416 LOC, 8 testes panel/interaction removidos + 8
   `fails +=` calls removidos do `_cases()` + header atualizado).
3. **Internal header**
   (`tests/auth/test_login_runtime_internal.h:69`): declaração do
   novo `_input_view_panel_cases()` adicionada, diretamente após
   `_input_view_cases()`.
4. **Test runner** (`tests/auth/test_login_runtime.c:476`):
   invocação do novo `_input_view_panel_cases()` adicionada,
   diretamente após `_input_view_cases()`.
5. **Makefile** (`TEST_SRCS:1223`): adicionado
   `tests/auth/test_login_runtime_credential_input_view_panel.c`,
   diretamente após o parent companion.

### 20.4 Validação externa recomendada

- `make test` — exercita os 8 panel+interaction tests no novo
  runner + verifica que os 8 tests restantes de
  input_reducer+field_view continuam passando no parent.
- `make layout-audit` — confirma que ambos os companions estão
  sob o limite de 900 LOC.
- `make release-check` — sanidade final.

### 20.5 Watchlist após §20

Após varredura fresh completa do workspace, **nenhum arquivo
permanece com headroom <50 LOC**. Top-headroom-deficit list final
(restritos ao mais relevantes, ignorando exceções de baseline):

| Arquivo | LOC | Headroom |
|---|---:|---:|
| `tests/kernel/linux_compat/test_linux_vfs_router.c` | 841 | 59 |
| `src/security/ed25519_scalar.c` | 836 | 64 |
| `tests/auth/test_login_runtime_credential_retention_expiry.c` | 832 | 68 |
| `src/gui/desktop/desktop_icons.c` | 823 | 77 |
| `src/drivers/hyperv/vmbus_transport.c` | 818 | 82 |
| `include/auth/login_runtime/deadline_cleanup.h` | 807 | 93 |

Observações:

- `src/security/ed25519_scalar.c` (836 LOC) já foi criado no split
  baseline anterior (§? do plano baseline) e está documentado no
  comentário do `audit_source_layout.py`. Não está em violação.
- `src/drivers/hyperv/vmbus_transport.c` (818 LOC) — Hyper-V é
  declarado **non-authoritative** na project authority memory; não
  é candidato prioritário.
- Os outros 4 arquivos (>50 LOC headroom) **podem aguardar** —
  nenhum está em zona crítica.

**Exceções de baseline conhecidas (não tocadas)**:

- `src/security/tls_trust_anchors.c` (8196 LOC) — em
  `STATIC_DATA_EXCEPTIONS`, é dado gerado (TLS root certs).
- `src/arch/x86_64/kernel_main.c` (988 LOC) — em
  `MONOLITH_BASELINE_EXCEPTIONS`, roadmap dedicado pendente.

**Conclusão**: o ciclo de hardening preventivo dos 7 arquivos com
headroom <50 LOC está completo. O codebase tem agora uma postura
de auditoria saudável com folga substancial para todos os arquivos
que tocam código novo ativamente.

## 21. Hardening preventivo oitavo arquivo (test_linux_vfs_router) — **CONCLUÍDO 2026-05-16**

Primeiro item da banda 50-100 LOC headroom:
`tests/kernel/linux_compat/test_linux_vfs_router.c` em **841/900**
LOC, a 59 LOC do gatilho. O arquivo tem 69 testes router em 7
fases lógicas (/dev, /dev/shm, unknown_fd, /proc, /tmp, specialfd,
prefix priority).

### 21.1 Decisões de design

1. **Padrão per-TU duplicated fixture.** Como §15/§17 (security
   tests), cada TU carrega sua própria cópia do fixture estática
   (install_router + 5 helpers + TEST/PASS/FAIL macros +
   tests_run/tests_passed). Não há header compartilhado.
2. **Critério de corte.** As 7 fases têm tamanhos:
   - /dev (9 tests)
   - /dev/shm (5 tests)
   - unknown_fd (2 tests)
   - /proc (6 tests)
   - /tmp (5 tests)
   - **specialfd (34 tests)** ← maior grupo
   - prefix priority (1 test)

   A fase specialfd (34 tests covering eventfd/signalfd/timerfd/
   memfd_secret/memfd_family/pidfd/inotify/epoll/fanotify/
   userfaultfd/landlock) é cohesiva — todos seguem o mesmo padrão
   "fd criado por syscall + roteado via linux_vfs". Movê-la para
   um sibling deixa o parent com 35 tests focados em paths de
   arquivo (/dev, /proc, /tmp) + a regressão de prefix priority.
3. **Helpers movidos junto.** `router_now_ns`/`g_router_now_ns` só
   são usados pelo timerfd test (specialfd group), então
   movem para o sibling. `create_landlock_ruleset_fd` só é usado
   pelos 4 landlock tests (specialfd group), também move.
4. **Limpeza no parent.** Removidos `g_router_now_ns` (decl),
   `g_router_now_ns = 0` (init em install_router), e
   `router_now_ns()` (helper) — todos seriam unused warnings após
   o split. O parent continua resetando todos os subsystems em
   `install_router` (linux_eventfd, linux_memfd, etc.) por
   precaução — mesmo que o parent não use diretamente, o reset
   garante limpeza global.
5. **Bug encontrado e corrigido durante write.** Primeira tentativa
   do sibling teve typo: `FAIL("userfaultfd lseek not surfaced")`
   no t_userfaultfd_lseek_via_router_espipe, enquanto o original
   tem `FAIL("userfaultfd lseek not routed")`. Detectado pelo
   diff de parity check — corrigido antes de fazer o strip do
   parent.

### 21.2 Resultado final

| Arquivo | LOC antes | LOC depois | Headroom |
|---|---:|---:|---:|
| `tests/kernel/linux_compat/test_linux_vfs_router.c` | 841 | **441** | 459 |
| `tests/kernel/linux_compat/test_linux_vfs_router_specialfd.c` | — (novo) | **521** | 379 |

### 21.3 Mudanças efetuadas

1. **Novo arquivo**:
   `tests/kernel/linux_compat/test_linux_vfs_router_specialfd.c`
   (521 LOC) com 34 testes specialfd + fixture duplicada + runner
   `test_linux_vfs_router_specialfd_run`.
2. **Arquivo modificado**:
   `tests/kernel/linux_compat/test_linux_vfs_router.c` (841 → 441
   LOC):
   - 34 testes specialfd removidos
   - `g_router_now_ns` declaração removida
   - `router_now_ns()` helper removido
   - `g_router_now_ns = 0;` removido de install_router
   - `create_landlock_ruleset_fd` removido
   - 34 dispatcher calls removidos do
     `test_linux_vfs_router_run`
3. **Test runner** (`tests/test_runner.c`):
   - Declaração `test_linux_vfs_router_specialfd_run` adicionada
     na linha 103 após `test_linux_vfs_router_run`.
   - Invocação adicionada na linha 280 após
     `test_linux_vfs_router_run()`.
4. **Makefile** (`TEST_SRCS:1400`): adicionado
   `tests/kernel/linux_compat/test_linux_vfs_router_specialfd.c`
   na mesma linha do parent.

### 21.4 Validação externa recomendada

- `make test` — exercita os 34 specialfd tests no novo runner +
  verifica que os 35 tests restantes (/dev /proc /tmp prefix)
  continuam passando no parent.
- `make layout-audit` — confirma que ambos os arquivos do par
  estão sob o limite de 900 LOC.
- `make release-check` — sanidade final.

### 21.5 Watchlist após §21

Após varredura fresh, os arquivos restantes no top-headroom-deficit
list (excluindo exceções de baseline conhecidas):

| Arquivo | LOC | Headroom |
|---|---:|---:|
| `src/security/ed25519_scalar.c` | 836 | 64 |
| `tests/auth/test_login_runtime_credential_retention_expiry.c` | 832 | 68 |
| `src/gui/desktop/desktop_icons.c` | 823 | 77 |
| `src/drivers/hyperv/vmbus_transport.c` | 818 | 82 |
| `include/auth/login_runtime/deadline_cleanup.h` | 807 | 93 |

Notas:

- `ed25519_scalar.c` já é resultado de um split baseline anterior
  documentado no `audit_source_layout.py`. Não é candidato natural
  para novo split — toda a math de scalar reduction precisa estar
  na mesma TU.
- `vmbus_transport.c` é parte do stack **Hyper-V**, declarado
  non-authoritative na project authority memory.
- Os outros 3 arquivos podem ser candidatos futuros se ganharem
  novas features — mas todos têm folga >65 LOC.

## 22. Hardening preventivo nono arquivo (test_login_runtime_credential_retention_expiry) — **CONCLUÍDO 2026-05-16**

Segundo item da banda 50-100 LOC headroom:
`tests/auth/test_login_runtime_credential_retention_expiry.c` em
**832/900** LOC, a 68 LOC do gatilho. Outro split de segunda
geração (parent companion já é resultado do refactor D.28).

### 22.1 Decisões de design

1. **Padrão idêntico a §19/§20.** Companion-companion via
   `_cases()` entries. Helpers e fixtures permanecem em
   `test_login_runtime_internal.h` (zero duplicação). Header do
   parent atualizado para refletir a 2ª geração.
2. **Critério de corte.** Os 8 testes do parent dividem-se em 2
   fases lógicas de 4 testes cada:
   - retention_plan (4 tests, ~373 LOC) — fica no parent
   - expiry_plan (4 tests, ~395 LOC) — vai para sibling

   Split exatamente meio-a-meio. A fase expiry depende da fase
   retention via o helper
   `build_loginwindow_credential_screen_expiry_plan_for_action`,
   que chama
   `build_loginwindow_credential_screen_retention_plan_for_action`
   (definido no parent). Como ambos os helpers são `extern` e
   declarados no `test_login_runtime_internal.h`, o cross-TU
   linkage funciona transparentemente.
3. **Helper migrado junto.** O helper extern
   `build_loginwindow_credential_screen_expiry_plan_for_action`
   foi movido para o novo sibling. Seu único consumidor externo
   (`test_login_runtime_credential_purge.c`) continua linkando
   contra o mesmo test binary, então a ABI externa fica
   preservada.
4. **Validação byte-a-byte.** Os 4 corpos de teste + helper
   (linhas 411-820 do original) verificados via `diff` contra a
   nova localização (linhas 40-448 do novo file): apenas 1 linha
   em branco trailing de diferença (cosmético, sem impacto
   funcional).
5. **Limpeza extra.** O parent foi atualizado para deixar de
   mencionar o helper movido na sua documentação de cabeçalho.

### 22.2 Resultado final

| Arquivo | LOC antes | LOC depois | Headroom |
|---|---:|---:|---:|
| `tests/auth/test_login_runtime_credential_retention_expiry.c` | 832 | **418** | 482 |
| `tests/auth/test_login_runtime_credential_expiry_plan.c` | — (novo) | **457** | 443 |

### 22.3 Mudanças efetuadas

1. **Novo arquivo**:
   `tests/auth/test_login_runtime_credential_expiry_plan.c`
   (457 LOC) com 4 testes de expiry plan + helper
   `build_loginwindow_credential_screen_expiry_plan_for_action`
   + runner `test_login_runtime_credential_expiry_plan_cases`.
2. **Arquivo modificado**:
   `tests/auth/test_login_runtime_credential_retention_expiry.c`
   (832 → 418 LOC, 4 testes expiry removidos + helper expiry
   removido + 4 `fails +=` calls removidos do `_cases()` +
   header atualizado).
3. **Internal header**
   (`tests/auth/test_login_runtime_internal.h:96`): declaração do
   novo `_expiry_plan_cases()` adicionada, diretamente após
   `_retention_expiry_cases()`.
4. **Test runner** (`tests/auth/test_login_runtime.c:515`):
   invocação do novo `_expiry_plan_cases()` adicionada,
   diretamente após `_retention_expiry_cases()`.
5. **Makefile** (`TEST_SRCS:1250`): adicionado
   `tests/auth/test_login_runtime_credential_expiry_plan.c`,
   diretamente após o parent companion.

### 22.4 Validação externa recomendada

- `make test` — exercita os 4 expiry tests no novo runner +
  verifica que os 4 retention tests + os tests da
  test_login_runtime_credential_purge.c (que consomem o helper)
  continuam passando.
- `make layout-audit` — confirma que ambos os companions estão
  sob o limite de 900 LOC.
- `make release-check` — sanidade final.

### 22.5 Watchlist após §22

Após varredura fresh completa, **nenhum arquivo permanece com
headroom <77 LOC** (excluindo as exceções de baseline conhecidas).
Top-headroom-deficit list final:

| Arquivo | LOC | Headroom |
|---|---:|---:|
| `src/security/ed25519_scalar.c` | 836 | 64 |
| `src/gui/desktop/desktop_icons.c` | 823 | 77 |
| `src/drivers/hyperv/vmbus_transport.c` | 818 | 82 |
| `include/auth/login_runtime/deadline_cleanup.h` | 807 | 93 |

Notas:

- `ed25519_scalar.c` (836 LOC, 64 headroom) — não-candidato (output
  de split baseline anterior; tight scalar math).
- `desktop_icons.c` (823 LOC, 77 headroom) — peer do `desktop.c`
  já splitado. Próximo candidato natural se ganhar features novas.
- `vmbus_transport.c` (818 LOC, 82 headroom) — Hyper-V
  non-authoritative.
- `deadline_cleanup.h` (807 LOC, 93 headroom) — header file, peer
  de outros `login_runtime` headers já refinados.

**Conclusão**: o ciclo de hardening preventivo dos 9 arquivos
endereçados nesta esteira está completo. O codebase tem agora
folga substancial (≥77 LOC) para todos os arquivos
non-baseline-exempt.

## 23. Hardening preventivo décimo arquivo (deadline_cleanup.h) — **CONCLUÍDO 2026-05-16**

Décimo split desta esteira preventiva e PRIMEIRO split de header
file: `include/auth/login_runtime/deadline_cleanup.h` em **807/900**
LOC, a 93 LOC do gatilho. O arquivo era um "partial header" que
carregava 5 struct definitions sequenciais.

### 23.1 Decisões de design

1. **Pattern per-struct partial.** O diretório
   `include/auth/login_runtime/` já segue o padrão "1 partial
   header por struct" (vide `archive_plan.h`, `expiry_plan.h`,
   `purge_plan.h`, `retention_plan.h`, etc., todos 200-400 LOC
   com 1 struct cada). `deadline_cleanup.h` era um outlier com 5
   structs em 807 LOC. Fragmentação para 5 partials se alinha com
   o padrão arquitetural existente.
2. **Aggregator thin.** O arquivo original `deadline_cleanup.h`
   foi reescrito como um aggregator de ~40 LOC que apenas inclui
   os 5 partials novos. O único consumidor externo
   (`include/auth/login_runtime.h`, o master aggregator) continua
   incluindo `deadline_cleanup.h` e recebe todas as definições
   transitivamente.
3. **Cross-TU forward declarations.** `function_declarations.h`
   (sibling header com protótipos de funções) usa apenas ponteiros
   para os 5 structs (`struct X *`), então as forward declarations
   continuam funcionando sem incluir os partials diretamente.
4. **Validação byte-a-byte.** Cada um dos 5 corpos de struct foi
   verificado via `diff` contra o original: 0 bytes de diferença
   em todos os 5 casos.
5. **Nomes consistentes.** Cada partial segue exatamente o padrão
   já estabelecido:
   - Guard macro: `AUTH_LOGIN_RUNTIME_<UPPER>_H`
   - File doc comment apontando para
     `docs/plans/active/monolith-residual-dedicated-plan.md`
   - `#include <stddef.h>`
   - Single struct definition
   - `#endif /* GUARD */`

### 23.2 Resultado final

| Arquivo | LOC antes | LOC depois | Headroom |
|---|---:|---:|---:|
| `include/auth/login_runtime/deadline_cleanup.h` (aggregator) | 807 | **40** | 860 |
| `include/auth/login_runtime/deadline_plan.h` (novo) | — | **160** | 740 |
| `include/auth/login_runtime/completion_plan.h` (novo) | — | **165** | 735 |
| `include/auth/login_runtime/ack_plan.h` (novo) | — | **172** | 728 |
| `include/auth/login_runtime/retire_plan.h` (novo) | — | **181** | 719 |
| `include/auth/login_runtime/cleanup_plan.h` (novo) | — | **190** | 710 |

### 23.3 Mudanças efetuadas

1. **5 novos arquivos**:
   - `include/auth/login_runtime/deadline_plan.h` (160 LOC)
   - `include/auth/login_runtime/completion_plan.h` (165 LOC)
   - `include/auth/login_runtime/ack_plan.h` (172 LOC)
   - `include/auth/login_runtime/retire_plan.h` (181 LOC)
   - `include/auth/login_runtime/cleanup_plan.h` (190 LOC)
2. **Arquivo modificado**:
   `include/auth/login_runtime/deadline_cleanup.h` (807 → 40 LOC):
   - 5 structs inline removidos
   - `#include <stddef.h>` substituído por 5 `#include
     "auth/login_runtime/<X>_plan.h"`
   - Doc comment atualizado para refletir o novo papel de
     aggregator.

### 23.4 Validação externa recomendada

- `make test` — exercita os tests
  (`test_login_runtime_credential_deadline_completion.c` e peers)
  que consomem essas structs via `auth/login_runtime.h`.
- `make all64` — confirma compilação x86_64 com o novo include
  chain.
- `make layout-audit` — confirma que todos os 6 arquivos
  (1 aggregator + 5 partials) estão sob o limite de 900 LOC.
- `make release-check` — sanidade final.

### 23.5 Watchlist após §23

Após varredura fresh completa, o estado dos arquivos não-exempt
restantes:

| Arquivo | LOC | Headroom |
|---|---:|---:|
| `src/security/ed25519_scalar.c` | 836 | 64 |
| `src/gui/desktop/desktop_icons.c` | 823 | 77 |
| `src/drivers/hyperv/vmbus_transport.c` | 818 | 82 |
| `include/auth/login_runtime/window_input.h` | 770 | 130 |

Notas:

- `ed25519_scalar.c`: baseline output, tight math, não-candidato.
- `desktop_icons.c`: complexo (requer internal header para
  `g_di` + 8 helpers).
- `vmbus_transport.c`: Hyper-V non-authoritative.
- `window_input.h`: header peer, 130 LOC de folga, baixa
  prioridade.

**Conclusão**: Após 10 splits preventivos nesta esteira (§14-§23),
o codebase está com folga ≥77 LOC em todos os arquivos
non-baseline-exempt que não são intencionalmente densos
(ed25519_scalar) ou não-autoritativos (Hyper-V).

## 24. Hardening preventivo décimo-primeiro arquivo (desktop_icons.c) — **CONCLUÍDO 2026-05-16**

Décimo-primeiro split desta esteira preventiva e primeiro com
introdução de **internal header** pattern em `src/gui/`:
`src/gui/desktop/desktop_icons.c` em **823/900** LOC, a 77 LOC do
gatilho.

### 24.1 Decisões de design

1. **Internal header pattern.** Diferente dos splits anteriores
   (per-TU duplicated fixture / 2nd-gen companion via shared test
   header / per-struct partials), esse split exigiu um padrão
   novo para GUI source: um header interno
   `src/gui/desktop/internal/desktop_icons_internal.h` (71 LOC).
   O pattern já era estabelecido em `src/auth/internal/` e
   `src/shell/commands/system_control/internal/`; só faltava
   ser adotado em `src/gui/`.

2. **Critério de corte.** O context-menu block (linhas 683-823 do
   original, ~140 LOC) é cohesivo:
   - 1 entry point público (`desktop_icons_handle_context`)
   - 3 callbacks internos (`di_ctx_pick`, `di_rename_submit`,
     `di_create_submit`)

   Esse bloco tem dependências únicas (`apps/file_manager.h`,
   `apps/text_editor.h`, `gui/context_menu.h`,
   `gui/inline_prompt.h`) que o resto de `desktop_icons.c` não usa,
   tornando o corte particularmente limpo de separar.

3. **Renomeação cuidadosa do struct anônimo.** O original definia
   `g_di` como uma struct anônima:
   ```c
   static struct { ... } g_di = {0};
   ```
   Para expor via `extern` no internal header, foi necessário:
   - Definir `struct di_state` (named) no header
   - Declarar `extern struct di_state g_di;` no header
   - No source: `struct di_state g_di = {0};` (no longer static, no
     longer anonymous)

   Mudança ABI: zero impact (mesmo símbolo, mesmo layout, mesmo
   tamanho). Apenas a TYPE TAG mudou de anonymous para `di_state`.

4. **Helpers promovidos.** 5 helpers foram promovidos de `static`
   para extern linkage (declarados no internal header, definições
   ficam em `desktop_icons.c`):
   - `di_strcpy`, `di_join`: file path manipulation
   - `di_icon_position`: layout coord computation
   - `di_is_text`: file extension check
   - `di_request_delete_selected`: shared deletion flow

   Outros 15+ helpers (di_strlen, di_streq, di_basename, di_fill_rect,
   di_mix_color, etc.) permanecem `static` no parent — não são
   usados pelo sibling.

5. **DI_CTX_* constants migrados.** As 6 constantes
   (`DI_CTX_OPEN`, `DI_CTX_DELETE`, `DI_CTX_RENAME`,
   `DI_CTX_NEW_FILE`, `DI_CTX_NEW_DIR`, `DI_CTX_REFRESH`) foram
   movidas do parent para o internal header. Ambos os TUs as veem
   via include.

6. **Public ABI inalterada.** `include/gui/desktop_icons.h`
   continua o mesmo. `desktop_icons_handle_context` continua
   sendo chamado pelo `desktop_handle_mouse` em
   `src/gui/desktop/desktop_mouse.c` (que foi splitado em §18).

### 24.2 Resultado final

| Arquivo | LOC antes | LOC depois | Headroom |
|---|---:|---:|---:|
| `src/gui/desktop/desktop_icons.c` | 823 | **659** | 241 |
| `src/gui/desktop/desktop_icons_context.c` (novo) | — | **170** | 730 |
| `src/gui/desktop/internal/desktop_icons_internal.h` (novo) | — | **71** | 829 |

### 24.3 Mudanças efetuadas

1. **Novo internal header**:
   `src/gui/desktop/internal/desktop_icons_internal.h` (71 LOC)
   com `struct di_entry`, `struct di_state`, `extern g_di`,
   `DI_CTX_*` defines, e declarações dos 5 helpers extern.

2. **Novo source TU**: `src/gui/desktop/desktop_icons_context.c`
   (170 LOC) com `di_ctx_pick`, `di_rename_submit`,
   `di_create_submit`, e `desktop_icons_handle_context`.

3. **Arquivo modificado**: `src/gui/desktop/desktop_icons.c`
   (823 → 659 LOC):
   - Include do internal header adicionado
   - `struct di_entry` removido (agora no header)
   - Struct anônima `g_di` substituída por
     `struct di_state g_di = {0};` (extern linkage via header)
   - 6 `DI_CTX_*` defines removidos
   - 5 helpers promovidos de `static` para extern linkage
   - Bloco context menu (140 LOC) removido + breadcrumb comment

4. **Makefile**: `desktop_icons_context.o` adicionado em
   `CAPYOS64_OBJS` diretamente após `desktop_icons.o`.

### 24.4 Validação externa recomendada

- `make all64` — exercita o build com o novo TU `desktop_icons_context.o`
  linkado + o include chain do internal header.
- `make test` — exercita testes que dependem do desktop runtime.
- `make layout-audit` — confirma que `desktop_icons.c` agora está
  comfortably under 900 LOC.
- `make release-check` — sanidade final.

### 24.5 Watchlist após §24

| Arquivo | LOC | Headroom |
|---|---:|---:|
| `src/security/ed25519_scalar.c` | 836 | 64 |
| `src/drivers/hyperv/vmbus_transport.c` | 818 | 82 |
| `include/auth/login_runtime/window_input.h` | 770 | 130 |

Notas:

- `ed25519_scalar.c`: baseline output, não-candidato.
- `vmbus_transport.c`: Hyper-V non-authoritative.
- `window_input.h`: header peer, 130 LOC de folga, não urgente.

**Conclusão**: Após 11 splits preventivos nesta esteira (§14-§24),
**todos os arquivos non-baseline-exempt com headroom <100 LOC foram
endereçados**. Os 3 remanescentes >800 LOC têm motivos explícitos
para não serem splitados (baseline output / non-authoritative /
intentionally dense).

