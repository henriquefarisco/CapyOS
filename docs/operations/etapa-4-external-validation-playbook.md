# Etapa 4 — External Validation Playbook

**Etapa:** 4 — CapyDisplay 2D + scheduler/multithread runtime.
**Status de abertura:** 2026-05-21 (alpha.253), imediatamente após o fechamento formal da Etapa 3.
**Plataforma oficial:** VMware Workstation/ESXi + UEFI + E1000.
**Sister repo abrindo gate:** `CapyUI` (contrato `capy-ui-widget` v2.19, display-list schema v7).
**Documento autoritativo upstream:** `docs/plans/active/capyos-master-plan.md` §7.

Este playbook é operator-facing. Sirva-o como referência sequencial
para validar externamente cada fase da Etapa 4. Mantenha sincronia
com a matriz cross-repo (`docs/reference/integration/compatibility-matrix.md`)
e com o audit técnico vigente.

---

## 1. Por que este playbook existe

A Etapa 4 abre o primeiro gate cross-repo com um sister depois do
fechamento da Etapa 3. Ela:

1. Consome o contrato real `capy-ui-widget` v2.19 (widget/display-list
   schema v7) entre o core CapyOS e o sister `CapyUI`.
2. Introduz scheduler cooperativo + multithread runtime no core.
3. Maduros o caminho 2D (damage tracking, double buffering,
   clipping, glyph cache).
4. Garante que falha de thread de app não derruba kernel/desktop.

O playbook documenta os gates externos que devem passar antes que
a Etapa 4 possa ser declarada fechada. Esta máquina é review/edit
only; nenhum gate aqui é executado localmente.

---

## 2. Critério oficial de aceite da Etapa 4

Reproduzido de `docs/plans/active/capyos-master-plan.md` §7 para
evitar drift. Em caso de divergência, o master plan é
autoritativo.

- [ ] Compositor redesenha somente regiões danificadas quando possível.
- [ ] Cursor e texto não piscam sob resize/move de janela.
- [ ] Fallback framebuffer continua funcionando.
- [ ] Apps single-threaded existentes continuam funcionais como regressão.
- [ ] Thread de app crashando não derruba kernel nem desktop.
- [ ] Widget model desacoplado consegue renderizar display list por
      adaptador CapyOS sem acessar compositor diretamente.

Adicionalmente, requisitos cross-repo:

- [x] Matriz cross-repo pina `CapyUI` `2.19.0` e
      `capy-ui-widget` v2.19/schema v7.
- [x] `docs/reference/integration/external-core-repositories.md`
      marca `capy-ui-widget` como ativo na Etapa 4 via adapter.
- [ ] Fluxo desktop/window real produz display-lists consumidas pelo
      adapter CapyOS-side.

---

## 3. Visão geral das fases

A Etapa 4 será entregue em fases sequenciais. Cada fase entrega
código, tests, docs e prepara o gate externo correspondente. O
gate externo da fase anterior deve passar antes que a próxima fase
abra.

| Fase | Sub-gate | Owner | Saída esperada |
|---|---|---|---|
| A | Adapter CapyOS-side para `capy-ui-widget` v2.19/schema v7 | CapyOS core | `include/gui/capyui_display_adapter.h` + `src/gui/widgets/capyui_display_adapter.c`; subconjunto 2D básico |
| B | Integração visual do produtor real CapyUI com o adapter | CapyOS core + CapyUI | desktop/window emite display-lists reais para o adapter sem acessar compositor diretamente |
| C | Scheduler cooperativo no runtime CapyOS | CapyOS core | `src/kernel/sched/` com cooperative scheduler + multithread runtime + smoke gate `scheduler-fairness` |
| D | Damage tracking + double buffering no compositor | CapyOS core | redraw apenas regiões damaged; cursor não pisca; smoke gate `compositor-damage-track` |
| E | Política de panic/oops controlada para threads de app | CapyOS core | thread crash não derruba kernel/desktop; smoke gate `thread-crash-survives` |
| F | Aprovação externa final + fechamento da Etapa 4 | operador | todos os gates externos verdes + acceptance criteria fechados |

Esta lista é estimativa; sub-fases podem ser reordenadas durante
implementação, mas o gate de saída da Etapa 4 (Fase F) requer
todos os critérios fechados.

---

## 4. Pré-requisitos globais

### 4.1 Toolchain

- Mesma toolchain validada na Etapa 3 (`make check-toolchain`).
- GCC cross-toolchain `x86_64-elf-gcc` na versão pinada por `tools/scripts/check_toolchain.py`.
- Python 3.10+ para harnesses (`tools/scripts/smoke_x64_*.py`).

### 4.2 VM template

- VMware Workstation/ESXi com:
  - Firmware UEFI.
  - NIC E1000 (não VMXNET3 ainda).
  - Mínimo 2 vCPUs (a Etapa 4 exercita scheduler/multithread; 1 vCPU não exercita o caminho real).
  - 4 GB RAM mínimo.
  - Storage: ao menos AHCI **ou** NVMe (idealmente ambos para
    regredir o gate de Etapa 3 enquanto valida Etapa 4).
  - Captura serial habilitada (`serial0.fileType = "file"`).

### 4.3 Variáveis de ambiente

- `SMOKE_X64_VMWARE_ARGS` configurando `--vmx`, `--serial-log`,
  `--timeout` para os smokes.
- Build alpha alvo: `>= 0.8.0-alpha.253` (build de abertura da
  Etapa 4) ou superior.

### 4.4 Snapshot da VM antes de cada gate

Crie snapshot pré-gate. A Etapa 4 introduz scheduler/multithread
no runtime; regressões podem ser sutis. Snapshots permitem
re-execução determinística do mesmo cenário.

---

## 5. Fases detalhadas

### 5.1 Fase A — Adapter para `capy-ui-widget` v2.19/schema v7 no core

**Entrega esperada:**
- `include/gui/capyui_display_adapter.h` expondo o contrato CapyOS-side
  com `struct capy_display_list` opaco.
- `src/gui/widgets/capyui_display_adapter.c` recebendo `struct
  capy_display_list` do CapyUI e renderizando via `struct gui_surface`.
- `Makefile` detectando `../CapyUI/src/widget/capy_display_list.h` e
  definindo `CAPYOS_HAVE_CAPYUI_WIDGET` somente quando o sibling existe.
- Audit/matriz sincronizados para `CapyUI` `2.19.0` /
  `capy-ui-widget` v2.19.

**Gates esperados (host):**

```bash
make check-toolchain
make layout-audit
make test
```

Tests adicionais para o adapter (host-testable) devem rodar limpos:
`tests/gui/test_capyui_display_adapter.c`.

**Gates esperados (VMware):** nenhum exclusivo desta fase; a
regressão da Etapa 3 deve continuar passando:

```bash
make smoke-x64-vmware-usb-hid-keyboard
make smoke-x64-vmware-storage-resilience
```

**Cross-repo handshake:** a Fase A consome o header real já publicado
no sister `../CapyUI`; novas mudanças no layout do display-list ou no
pin de versão devem passar pelo workflow `cross-repo-contract-sync`.

### 5.2 Fase B — Integração visual do produtor real CapyUI

**Entrega esperada:**
- Fluxo desktop/window emite `struct capy_display_list` real do CapyUI.
- Adapter CapyOS-side consome essa display-list sem acesso direto do
  widget model ao compositor.
- Ops sem provider dedicado (`IMAGE_REF`, transforms, plugins) ficam
  explicitamente degradadas ou recebem provider antes de serem
  declaradas suportadas.

**Status parcial implementado:** o core já expõe o seam
`capyui_display_adapter_render_producer_window`, que recebe um produtor
CapyUI-side por callback e renderiza o `struct capy_display_list` na janela
via adapter/damage rect. O teste host-side usa `capy_widget_emit` real do
sibling `../CapyUI`. Os primeiros fluxos reais conectados são Calculator,
Text Editor, Settings, File Manager, Task Manager, Taskbar, Taskbar menu/recent
popups e Notification overlay do `../CapyUI`, além de Desktop icons via callback
de wallpaper com clip de damage do compositor, Terminal gráfico, Context menu e
Inline prompt do CapyOS, que emitem `capy_display_list` schema v7 quando
compilados pelo CapyOS com `CAPYOS_HAVE_CAPYUI_WIDGET`; widgets genéricos do
CapyOS usam o mesmo adapter com fallback legado;
ainda falta migrar os demais fluxos
desktop/app de produção para alimentar esse seam.

**Gates esperados (sister):**

```bash
cd ../CapyUI && make validate && make package
```

**Atualização cross-repo no core:**
- Manter a pinagem de `CapyUI` em `docs/reference/integration/compatibility-matrix.md` §1 alinhada ao sister real.
- Atualizar `external-core-repositories.md` se o escopo suportado pelo adapter crescer.
- Atualizar `compatibility-audit-<date>.md` ou criar novo.

### 5.3 Fase C — Scheduler cooperativo no runtime CapyOS

**Entrega esperada:**
- `include/kernel/sched/` com API de scheduler cooperativo.
- `src/kernel/sched/` com implementação.
- Política de panic/oops para thread de app falha.
- Smoke marker novo: `[smoke] scheduler-fairness ready` (emitido após N rounds de scheduling justos).

**Status parcial implementado:** a primeira fatia da Fase C formaliza a
fairness cooperativa existente no scheduler CapyOS: `scheduler_yield()` agora
procura o próximo `READY` após a task corrente e faz wrap para o início da fila,
evitando starvation quando três ou mais tasks cooperativas estão prontas. A
semântica de prioridade e o layout de `struct task_context` permanecem
inalterados. A segunda fatia conecta o gate host-testável
`scheduler-fairness`: o latch global emite `[smoke] scheduler-fairness ready`
após observar 3 task IDs despachados pelo menos 2× cada, e o Makefile expõe
`smoke-x64-vmware-scheduler-fairness`. O target compila com
`CAPYOS_SCHEDULER_FAIRNESS_SMOKE`, que cria duas tasks auxiliares
yield-only quando o runtime adota a task corrente; isso torna o gate
determinístico sem mudar builds normais.

**Gate externo novo:**

```bash
make smoke-x64-vmware-scheduler-fairness \
  SMOKE_X64_VMWARE_ARGS="--vmx ... --serial-log ... --timeout 300"
```

Marcadores esperados no COM1, em ordem:
1. `[net] DHCP: lease acquired.`
2. `[smoke] storage-stack ready` (regressão de Etapa 3).
3. `[smoke] gui-session ready` (runtime desktop adotou scheduler).
4. `[smoke] scheduler-fairness ready` (novo).

### 5.4 Fase D — Damage tracking + double buffering no compositor

**Entrega esperada:**
- API de damage tracking no compositor.
- Double buffering com swap atômico.
- Glyph cache para fontes.
- Smoke marker: `[smoke] compositor-damage-track ready` (emitido após N frames com damage region não-total).

**Status parcial implementado:** a primeira fatia da Fase D conecta o gate
host-testável `compositor-damage-track`: o latch global emite
`[smoke] compositor-damage-track ready` após observar 2 frames parciais com
dirty rects no caminho real de `compositor_render()`. O alvo externo
`smoke-x64-vmware-compositor-damage-track` valida DHCP → gui-session →
compositor-damage-track em ordem. A mesma fatia inicializa um cache de
linhas de glyphs para a fonte padrão 8x16 e expõe stats host-testáveis
para hits/misses. A telemetria de `compositor_stats_get()` agora também
diferencia frames full vs parciais e contabiliza dirty rects apresentados,
travando a evidência host-side de partial damage.

**Follow-up 2026-05-25 (cursor erase scoped to overlap):** na composição
parcial o compositor passou a apagar a área do cursor apenas quando algum
dirty rect realmente intersecta o sprite, em vez de re-copiar a área
incondicionalmente do backbuffer. Em hosts com framebuffer lento
(Hyper-V Gen2 reportado pelo operador) o erase incondicional aparecia
como flicker brevíssimo do cursor entre dois frames sem mudança de cena
na área dele. A correção fecha o critério "cursor e texto não piscam sob
resize/move de janela" para o caso cursor parado. `compositor_render_cursor`
ganhou um caminho de erase do retângulo antigo quando o cursor MOVE
mesmo após `comp_full_presented=1`, preservando a invariante "sem rastro
de cursor". `struct compositor_stats` ganhou o contador
`cursor_erases_partial` (sempre `0` em modo full-present) que serve como
evidência host-side de que o caminho de overlap está sendo exercitado, e
o teste novo
`test_compositor_cursor_erase_only_on_overlap` em
`tests/gui/test_compositor_events.c` trava os dois casos
(disjoint não conta; overlap conta).

**Gate externo novo:**

```bash
make smoke-x64-vmware-compositor-damage-track \
  SMOKE_X64_VMWARE_ARGS="--vmx ... --serial-log ... --timeout 300"
```

Marcadores esperados, em ordem:
1. `[net] DHCP: lease acquired.`
2. `[smoke] gui-session ready` (regressão de Etapa 2).
3. `[smoke] compositor-damage-track ready` (novo).

### 5.5 Fase E — Thread crash não derruba kernel/desktop

**Entrega esperada:**
- Política de panic/oops para falha de thread de app.
- Smoke marker: `[smoke] thread-crash-survives ready` (emitido após uma thread de app falhar deliberadamente e o desktop permanecer responsivo).

**Status parcial implementado:** a primeira fatia da Fase E formaliza
o latch host-testável `thread-crash-survives` no mesmo padrão das
Fases C e D. O kernel-side ganhou `include/kernel/thread_crash_smoke.h`,
`src/kernel/thread_crash_smoke.c` (latch puro) e
`src/kernel/thread_crash_smoke_io.c` (emissão COM1). A semântica do
latch é "uma saída de processo com `exit_code >= 128` seguida de
`THREAD_CRASH_SMOKE_REQUIRED_TICKS_AFTER_CRASH` (4) ticks de scheduler".
O `exit_code >= 128` é o encoding POSIX-style de morte-por-sinal já
usado por `process_exit(128 + (int)vector)` no fault dispatcher em
`src/arch/x86_64/interrupts.c::x64_exception_dispatch`. A
contenção de fault de user-mode em si continua locked por
`tests/test_fault_classify.c`. A integração live alimenta o latch a
partir de `src/kernel/process.c::process_exit` e do
`src/kernel/scheduler.c::scheduler_tick`, ambos guardados por
`#ifdef CAPYOS_THREAD_CRASH_SURVIVES_SMOKE` para que builds de
produção paguem custo zero. O target externo
`smoke-x64-vmware-thread-crash-survives` compila com essa flag, e o
scheduler runtime spawn um helper kernel-task one-shot que alimenta
o latch com `128 + 14` (vetor page fault) na primeira dispatch e
depois `task_yield()` para sempre. 10 testes host novos cobrem o
latch (`tests/kernel/test_thread_crash_smoke_gate.c`).

**Gate externo novo:**

```bash
make smoke-x64-vmware-thread-crash-survives \
  SMOKE_X64_VMWARE_ARGS="--vmx ... --serial-log ... --timeout 300"
```

Marcadores esperados, em ordem:
1. `[net] DHCP: lease acquired.`
2. `[smoke] gui-session ready`.
3. `[smoke] thread-crash-survives ready` (novo).

### 5.6 Fase F — Aprovação externa final + fechamento da Etapa 4

Quando todas as fases anteriores fecharem em código + host tests
(estado em alpha.260: todas as 5 fases A-E atendidas), executar a
validação externa em VMware oficial.

**Caminho recomendado (single-boot, alpha.260+):**

```bash
make smoke-x64-vmware-etapa-4 \
  SMOKE_X64_VMWARE_ARGS="--vmx ... --serial-log ... --timeout 600"
```

Este target agregado faz um único `make clean` + build com ambas
as flags `CAPYOS_SCHEDULER_FAIRNESS_SMOKE` e
`CAPYOS_THREAD_CRASH_SURVIVES_SMOKE` ativadas, depois roda uma
única invocação do harness `smoke_x64_vmware.py` que valida cinco
markers IN ORDER no mesmo serial log (o harness usa
`markers_in_order` que é estrita: marker N deve aparecer DEPOIS de
N-1):

1. `[net] DHCP: lease acquired.` (regressão Etapa 2)
2. `[smoke] gui-session ready` (regressão Etapa 2)
3. `[smoke] scheduler-fairness ready` (Fase C)
4. `[smoke] compositor-damage-track ready` (Fase D — latch sempre
   live, sem flag de compilação dedicada)
5. `[smoke] thread-crash-survives ready` (Fase E)

Cada marker é emitido exatamente uma vez por boot pelos latches
correspondentes, então a ordem é determinística para um build
dado. Se um operador externo observar consistentemente uma ordem
diferente entre os markers de Fase C/D/E, reordenar as linhas
`--marker` no target `smoke-x64-vmware-etapa-4` do Makefile para
casar com a ordem observada (cada smoke latch é "emit once" por
boot, então a ordem nunca varia em runs subsequentes do mesmo
build).

Vantagem: uma única VM boot, um único build full. Custo: ~10 min
de build + ~3-5 min de boot/marker capture.

**Caminho triagem (per-phase, quando algum marker falhar):**

Cada Fase tem seu target dedicado para isolar a falha:

- `make smoke-x64-vmware-scheduler-fairness` (Fase C, clean + flag isolada)
- `make smoke-x64-vmware-compositor-damage-track` (Fase D, sem flag)
- `make smoke-x64-vmware-thread-crash-survives` (Fase E, clean + flag isolada)

**Regressão Etapa 3 (antes ou depois do agregado):**

- `make smoke-x64-vmware-usb-hid-keyboard` (Etapa 3 Slice 3D)
- `make smoke-x64-vmware-storage-resilience` (Etapa 3 Slice 3E)

**Fechamento:**

1. `make release-check` deve passar limpo.
2. Cross-check dos acceptance criteria em §2 — todos `[x]`.
3. Invocar workflow `etapa-transition` para fechar Etapa 4 e
   abrir Etapa 5 (TLS userland real).

---

## 6. Reporting format expected from the operator

Quando reportar resultado de gate executado externamente, envie:

1. **Build alvo:** alpha tag exato (e.g. `0.8.0-alpha.260+20260603`).
2. **Comando executado:** linha completa do `make` invocado.
3. **Resultado:** PASS / FAIL.
4. **Tail do serial:** últimos ~50 linhas do `--serial-log` (especialmente os markers observados).
5. **Tempo total:** segundos até PASS ou timeout.
6. **Anomalias:** qualquer warning, klog ERROR ou comportamento
   inesperado no boot.
7. **Anexos:** se FAIL, anexar `--summary-log` completo e qualquer
   arquivo de evidência.

Exemplo de report PASS:

```
Build: 0.8.0-alpha.260+20260603
Gate: make smoke-x64-vmware-scheduler-fairness
Resultado: PASS
Tempo: 187s
Markers no COM1 (em ordem):
  [net] DHCP: lease acquired.
  [smoke] storage-stack ready
  [smoke] scheduler-fairness ready
Anomalias: nenhuma.
```

---

## 7. Local execution policy

Esta workspace é review/edit only. Nenhum dos gates listados
neste playbook deve ser executado nesta máquina. O CapyOS agent
neste workspace:

- Edita código, tests e documentação.
- Valida mudanças por inspeção estática.
- Recomenda gates externos.
- **Não** invoca `make`, `git push`, ou comandos com side-effects.

Operador externo (humano ou CI) executa os gates em VMware
oficial ou em CI provisionada.

---

## 8. Referências cruzadas

- `docs/plans/active/capyos-master-plan.md` §7 — definição
  autoritativa da Etapa 4.
- `docs/reference/integration/compatibility-matrix.md` — versões
  pinadas cross-repo.
- `docs/reference/integration/compatibility-audit-2026-05-22.md` —
  snapshot técnico vigente.
- `docs/architecture/smoke-marker-pattern.md` — pattern canônico
  para os markers novos que esta Etapa introduz.
- `docs/operations/etapa-3-external-validation-playbook.md` —
  template para este playbook (Slice 3D).
- `docs/operations/etapa-3-slice-3e-validation-playbook.md` —
  template para este playbook (Slice 3E).
- `.windsurf/workflows/etapa-transition.md` — workflow usado para
  fechar a Etapa 4 quando todas as fases passarem.
- `.windsurf/workflows/cross-repo-contract-sync.md` — workflow
  usado para coordenar com o sister `CapyUI` nas fases A e B.
