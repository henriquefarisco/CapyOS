# Cross-repo compatibility audit — 2026-05-23

**Status:** correcao da esteira do deploy `alpha.257` e pinagem vigente para
`alpha.258`.
**Snapshot anterior:** [`compatibility-audit-2026-05-22.md`](compatibility-audit-2026-05-22.md).
**Matriz autoritativa:** [`compatibility-matrix.md`](compatibility-matrix.md).

## Resumo

O deploy `alpha.257` quebrou no GitHub Actions porque os workflows do CapyOS
clonam o sibling `CapyUI` a partir de `main` para builds de `main` e tags.
Naquele momento, `CapyUI/main` ainda apontava para `2.13.0`, sem
`src/desktop/taskbar_display_list.c`, enquanto o Makefile do CapyOS ja
esperava gerar `build/x86_64/capyui-desktop/taskbar_display_list.o`.

Correcoes aplicadas:

- `CapyUI/main` foi fast-forwardado para `v2.13.1`, alinhando `main` e
  `develop` no commit que contem os sources display-list.
- CapyOS passa a declarar `CapyUI` `2.13.1` como versao corrente, minima e
  maxima testada para a Etapa 4.
- A nova release CapyOS `alpha.258` evita reutilizar a tag `alpha.257`, que ja
  possui runs quebrados registrados.

## Estado coordenado

| Repo | Ref esperada no CI | Versao | Observacao |
|---|---|---|---|
| `CapyOS` | `main`, `develop`, `v0.8.0-alpha.258+20260523` | `0.8.0-alpha.258+20260523` | release corrigida |
| `CapyUI` | `main` e `develop` | `2.13.1` | contem `taskbar_display_list.c` e fontes display-list relacionados |

## Gate afetado

Falha observada no release anterior:

```text
make[1]: *** No rule to make target 'build/x86_64/capyui-desktop/taskbar_display_list.o', needed by 'build/capyos64.bin'. Stop.
```

Esse erro e resolvido quando o checkout remoto de `CapyUI` contem os fontes
publicados em `2.13.1`.

## Addendum 2026-05-25 — Desktop icons damage clip

Fatia pequena de Fase B/D sem mudança no `capy-ui-widget` nem no schema de
display-list:

- CapyOS expõe invalidação de retângulo de desktop e consulta read-only do clip
  de render atual para callbacks de desktop.
- O compositor recompõe wallpaper + callback de desktop também em composição
  parcial, limitado ao retângulo dirty.
- `../CapyUI/src/desktop/desktop_icons_display_list.c` passa o clip atual para
  `capyui_display_adapter_render`, preservando `capy_display_list` schema v7 e
  evitando ABI paralela.
- `../CapyUI/src/desktop/desktop_icons.c` usa retângulos por ícone para
  selection/drag/drop quando `CAPYOS_HAVE_CAPYUI_WIDGET` está ativo; fallback
  sem widget-core preserva `compositor_invalidate_all()`.

Validação local não executada por política deste workspace. Gates externos
recomendados: `make validate` em `../CapyUI`; no CapyOS, `make layout-audit`,
`make test`, `make all64 PROFILE=full` e
`make smoke-x64-vmware-compositor-damage-track` quando disponível.

## Addendum 2026-05-25 — alpha.260 hardening + cleanup batch

Sem mudança no `capy-ui-widget`, `capy-ui-desktop-session`,
`capy-agent-component-index`, `capy-codec-image` ou outro contrato
cross-repo. Trabalho totalmente interno ao CapyOS core.

CapyOS bump: `alpha.259 → alpha.260+20260525`. Sister repos sem
mudança (CapyUI `2.13.1`, CapyAgent `0.0.4`, CapyBrowser `0.0.4`,
CapyCodecs `0.0.4`, CapyLang `0.1.3`, CapyBenchmark `0.0.4`).

ABIs novas adicionadas, todas **aditivas** e internas ao kernel:

- `include/drivers/nvme/nvme_reset.h` — pure controller-reset
  logic extraída para host-testabilidade do BUG #2 fix
  (Create I/O CQ antes de SQ após CC.EN toggle).
- `include/kernel/thread_crash_smoke.h` — latch + marker
  `[smoke] thread-crash-survives ready` para a Etapa 4 Fase E.
- `struct compositor_stats::cursor_erases_partial` — novo campo
  contador zerado em init/shutdown; sempre 0 em modo full-present.

Frentes desta release (resumo):

1. **Sub-slice 3E.4.C concluída** — ~158 sites adicionais
   `dbg_*` → `klog`/`klog_hex` em 11 TUs (5 LF + 6 CRLF) + 2
   headers limpos (`capyfs_runtime_internal.h`,
   `kernel_volume_runtime_internal.h`).
2. **Sub-slice 3E.5.B entregue** — `nvme_controller_reset` refatorado
   para usar planner puro; 13 host tests novos cobrem ordem CQ→SQ
   (BUG #2 fix do alpha.252).
3. **Etapa 4 Fase E entregue** — smoke gate `thread-crash-survives`
   com 10 host tests + novo target externo
   `make smoke-x64-vmware-thread-crash-survives`.
4. **Etapa 4 Fase D follow-up** — cursor erase escopado para overlap
   fecha o critério "cursor não pisca sob resize/move"; 1 host test
   novo.
5. **P1 hardening** — P1-A (storage Hyper-V retry work item), P1-C
   (`vmbus_transport_drain_simp` bounded), P1-E (mouse bounds
   preservados), P1-F (USB poll guard defense-in-depth). P1-B
   classificado como design intencional; P1-D permanece gated.

Etapa 4 — todos os 6 critérios de aceite marcados como atendidos em
código + host tests. Pendente apenas execução externa em VMware
oficial (Fase F).

Sister repos `docs/compatibility.md` files não precisam de update —
o pin do CapyOS core na sister side pode permanecer em `alpha.259`
porque nenhum contrato sister mudou. A matriz row do CapyOS foi
movida para alpha.260 para refletir o core ativo.

## Addendum 2026-05-29 — reconciliação de bookkeeping cross-repo

Revisão regressiva cross-repo identificou **dívida de sincronização** (sem
mudança de contrato/ABI; bookkeeping puro). Os repos irmãos haviam
taggeado novas versões que nunca foram refletidas na matriz, no
`STATUS.md` nem nos pins dos próprios irmãos. Correções aplicadas
nesta janela (somente documentação):

1. **Versões irmãs na matriz e no STATUS** alinhadas ao tag commitado:
   - `CapyAgent` `0.0.4 → 0.0.5`
   - `CapyBrowser` `0.0.4 → 0.0.5`
   - `CapyCodecs` `0.0.4 → 0.0.5`
   - `CapyBenchmark` `0.0.4 → 0.0.5`
   - `CapyLang` `0.1.3 → 0.1.6`
   - `CapyUI` permanece `2.13.1` (já estava correto).
   Nenhuma dessas releases mudou ABI ou superfície de contrato — são
   bumps internos (CI/clippy/packaging/file-mode). Por isso continuam
   `n/a (roadmap-blocked)` ou host-only na coluna de compatibilidade.

2. **Pins do CapyOS core nos 6 `docs/compatibility.md` irmãos**
   movidos de `0.8.0-alpha.244+20260520` (+ audit `2026-05-20`) para
   `0.8.0-alpha.260+20260525` (+ audit `2026-05-23`), refletindo o core
   ativo da Etapa 4. O pin estava ~14 alphas atrás.

3. **Descrição da ABI `capy-lang-host`** corrigida na matriz e no
   STATUS: o sister CapyLang entregou S1-S7 (lexer/parser/diagnostics/
   bytecode v0/opcodes+verifier/emitter/VM/host bridge) host-only, não
   apenas "S1 lexer". A ABI de integração `capy-lang-host` permanece
   `v0 parcial` e roadmap-blocked até a Etapa 15.

4. **Inconsistências internas de doc** corrigidas: tabela §3 do
   `capyos-master-plan.md` (Etapa 3 → Concluída, Etapa 4 → Em andamento);
   §9 da matriz (audit atual passa a ser `2026-05-23`).

5. **`.windsurf` desatualizado** corrigido: regras do `CapyUI`
   (`capy-ui-widget` `v0.6`/schema `2` → `v2.13`/schema `7`) e o
   `.windsurf` do workspace (skill `workspace-overview` + regras
   `00-workspace-authority`/`10-workspace-layout`: core, Etapa ativa e
   versões irmãs).

### Pendências NÃO endereçadas nesta janela (fora de política/escopo de edição)

- **Commit + tag de `alpha.259/260`**: o trabalho está na árvore do CapyOS
  porém não commitado; o git HEAD ainda é `v0.8.0-alpha.258+20260523`.
  Existe nota de release solta de `alpha.259` e **falta** a de `alpha.260`.
- **Etapa 4 Fase F**: gate externo `make smoke-x64-vmware-etapa-4` em
  VMware oficial (validação externa) continua pendente.
- **P0 signer Ed25519 do CapyAgent**: continua não publicado; repos
  `signed` seguem fail-closed (`CAPYPKG_ERR_SIGNATURE`) — bloqueio de
  release da Etapa 9, não desta janela.

Validação local não executada por política deste workspace.

## Addendum 2026-05-29 (parte 2) — release alpha.261 + CapyUI 2.19.0

Nova release CapyOS `alpha.260 → alpha.261+20260529` e bump do sister
CapyUI `2.13.1 → 2.19.0` (ABI `capy-ui-widget` v2.13 → v2.19; display-list
schema **7 inalterado** — os minors 2.14–2.19 estendem apenas o estado
dos advanced widgets, aditivos). Nenhum outro contrato sister mudou
(CapyAgent/CapyBrowser/CapyCodecs `0.0.6`, CapyLang `0.1.7`,
CapyBenchmark `0.0.6`).

**Mudança de código (CapyOS core, 1 TU):** corrige o bug em que o
first-boot wizard (`src/config/first_boot/program.c`) criava o usuário
admin (`user_record_init` + `userdb_add`) **sem** chamar
`user_home_prepare`, ao contrário do comando `add-user`
(`user_manage.c`) e do recovery (`service_helpers.c`). Consequência: o
usuário primário de instalação ficava com home vazio, sem
`Desktop`/`Documents`/`Personal`/`Professional`, então nada aparecia no
desktop (CapyUI `desktop.c` renderiza `<home>/Desktop` no wallpaper) nem
no file manager (`file_manager.c` abre em `<home>`). Fix: include de
`auth/user_home.h` + chamada `user_home_prepare(admin_home, admin_uid,
admin_gid)` no ponto onde o home do admin é confirmado (cobre admin novo
E pré-existente, idempotente; falha não-fatal com warning
`SYS_UI_ADMIN_HOME_PERM_WARNING`). Cobertura host pré-existente em
`tests/auth/test_user_home.c` (valida as 4 pastas). **Sem mudança de
ABI/manifest/quota/assinatura** — `services/capypkg`,
`kernel/module_gate.c`, descriptor Ed25519 e quotas intactos.

**Propagação de versão aplicada nesta janela:**

- CapyOS source-of-truth: `VERSION.yaml` (current/extended/current_summary
  + history entry alpha.261) e `include/core/version.h`.
- Matriz: linha `CapyOS` → alpha.261; linha `CapyUI` → 2.19.0 / v2.19;
  ABI naming `capy-ui-widget` → v2.19; rows de compatibilidade entre etapas.
- `STATUS.md`, `capyos-master-plan.md`, `README.md`: versão bumpada.
- Pins do CapyOS core nos 6 sisters (`docs/compatibility.md` + READMEs +
  guias): `alpha.260 → alpha.261`.
- `.windsurf` (workspace + CapyUI): versão/ABI atualizadas.
- Nova release note `docs/releases/capyos-0.8.0-alpha.261+20260529.md`.

**Pendente (fora desta máquina, política review/edit-only):** `make test`,
`make all64`, `make iso-uefi`, `make smoke-x64-vmware-etapa-4`,
`make release-check`; a tag `v0.8.0-alpha.261+20260529` será criada
manualmente pelo operador. O P0 do signer Ed25519 do CapyAgent permanece
em aberto (Etapa 9).
