# CapyOS 0.8.0-alpha.305+20260701

**Data:** 2026-07-01
**Canal:** alpha (experimental)
**Versao:** `0.8.0-alpha.305+20260701`
**Plataforma oficial:** VMware + UEFI + E1000 (inalterada)
**Tipo:** slice (Etapa 7 — Slice B, item 4: ponte de eventos de input mouse/teclado para janelas ring-3)

## Resumo executivo

alpha.305+20260701: fecha o item 4 (o mais arriscado, sem precedente no
código) dos 5 subsistemas mapeados no `alpha.304` para tornar o navegador
gráfico coexistente com uma sessão de desktop real — a **ponte de eventos de
input**. Achado crítico do `alpha.304`: a fila que `SYS_WINDOW_POLL_EVENT` lê
(`gui_event_poll`) só recebia eventos de ciclo de vida de janela
(close/focus/blur/resize/paint, empurrados pelo compositor); mouse e teclado
do desktop real usavam um despacho síncrono completamente separado
(`gui_window_dispatch_event` → `on_mouse`/`on_key` direto), nunca chegando à
fila. Uma janela `capygfx` coexistindo ficaria visível e fechável, mas surda
a cliques e digitação.

**A correção (aditiva, um único ponto de decisão):**
1. **`struct gui_window`** (`include/gui/compositor.h`) ganha um campo NOVO
   no final — `uint32_t gfx_owner_pid` — 0 para toda janela criada por um app
   in-kernel (`compositor_create_window`, o único caminho que existia antes
   deste campo), não-zero (o pid dono) para uma janela criada via a ABI de
   syscall gráfica ring-3. Mudança aditiva de struct (permitida pelas regras
   de ABI do CapyUI: "new fields at the tail of existing structs").
2. **`kernel/syscall_gfx_init.c`**: `gfx_backend_win_create` passa a receber
   `pid` (o vtable `struct syscall_gfx_ops.win_create` foi estendido com esse
   parâmetro) e marca `win->gfx_owner_pid = pid` na criação.
3. **`CapyUI/src/window/window_dispatcher.c`** (repo irmão): um único ponto
   de decisão central, `gfx_owned_redirect()`, chamado no topo de
   `gui_window_dispatch_event()` — se a janela-alvo do evento tem
   `gfx_owner_pid != 0`, o evento é re-empurrado na fila (`gui_event_push`,
   `window_id` setado explicitamente) em vez de cair no despacho
   `on_mouse`/`on_key` normal (que seria um no-op, já que uma janela ring-3
   nunca popula esses callbacks). Como TODO input real do desktop já passa
   por `gui_window_dispatch_event` (5 pontos de chamada em
   `desktop_mouse.c`/`desktop.c`), esse único choke point basta — não foi
   necessário tocar cada call site.

**Validado com um teste de host dedicado** (`tests/gui/
test_gui_window_dispatcher.c`, `test_dispatch_gfx_owned_redirect`, 7
asserções): mouse-down e key-down são redirecionados corretamente para a
fila (com o payload intacto), a janela ainda ganha foco no clique, e —
crucial — uma janela **não**-owned continua despachando exatamente como
antes (regressão travada). `make test` verde (`Todos os testes passaram`).

**Confirmado sem regressão em runtime**: os dois smokes QEMU existentes do
`capygfx` (`smoke-x64-qemu-capygfx`, o smoke de imagem embutida do
`alpha.294`/`alpha.303`, e `smoke-x64-qemu-capygfx-desktop-spawn`, o smoke de
spawn do `alpha.304`) foram re-executados do zero e **ambos continuam
passando**.

## O que ainda falta (honestidade sobre o escopo)

- **A integração com o loop REAL `desktop_runtime_start` do CapyUI** segue
  não-testada em CI — a ponte agora existe no código de produção
  (`window_dispatcher.c` é sempre compilado quando o sibling CapyUI está
  presente), mas nenhum smoke boota até o login real, abre o navegador
  gráfico via `open-browser-graphical` e clica/digita nele. Isso exigiria
  automação de login + injeção de mouse/teclado via QEMU, fora do escopo
  desta sessão.
- **Governança cross-repo pendente**: esta mudança altera o comportamento do
  dispatcher do CapyUI (`capy-ui-desktop-session` v1), uma ABI cuja evolução
  é regida pelo próprio repo `CapyUI` (`.windsurf/rules/20-abi-compatibility.md`).
  A mudança É aditiva (campo no final da struct; nenhum comportamento
  existente muda, provado pelo teste de regressão), mas o bump formal de
  versão do CapyUI (`VERSION`: `2.22.6` → próximo patch), a atualização de
  `CapyUI/docs/compatibility.md`, e o registro na
  `../CapyOS/docs/reference/integration/compatibility-matrix.md` **não
  foram feitos nesta sessão** — decisão deliberada para não arriscar um bump
  incorreto/incompleto no fim de uma sessão já muito longa. Recomendo que o
  operador execute o workflow `bump-capyos-pin` (ou o processo equivalente)
  antes de considerar esta ponte "oficialmente" parte do contrato
  `capy-ui-desktop-session`.

## Mudancas

- `include/gui/compositor.h`: novo campo `gfx_owner_pid` no final de `struct gui_window`.
- `src/gui/core/compositor.c`: `compositor_create_window` zera `gfx_owner_pid` explicitamente (reset de slot reusado).
- `include/kernel/syscall_gfx.h` + `src/kernel/syscall_gfx.c` + `src/kernel/syscall_gfx_init.c`: `struct syscall_gfx_ops.win_create` ganha o parâmetro `pid`; `gfx_backend_win_create` marca a janela.
- `tests/kernel/test_syscall_gfx.c`: fake `fk_win_create` atualizado para a nova assinatura.
- `CapyUI/src/window/window_dispatcher.c` (repo irmão): novo `gfx_owned_redirect()` chamado no topo de `gui_window_dispatch_event()`.
- `tests/gui/test_gui_window_dispatcher.c`: novo `test_dispatch_gfx_owned_redirect` (7 asserções).

## Validacao

- `make test` (CapyOS) — verde, `Todos os testes passaram`, incl. as 7 novas asserções do redirect.
- `make validate` (CapyUI) — verde (348 contratos de widget inalterados; a mudança fica fora de `src/widget/`).
- `make layout-audit` — limpo (Warnings: none).
- `make all64` **default** (clean, sem flags de smoke) — verde, 0 erros. `capyos64.bin` cresce ~4 KiB (2125408→2129504) em relação ao alpha.304 — esperado: o campo novo em `struct gui_window` e a lógica de `gfx_owned_redirect` agora fazem parte do dispatcher sempre compilado (não há gate de smoke para essa parte específica, pois o redirect precisa estar ativo sempre que QUALQUER janela ring-3 exista).
- **`make smoke-x64-qemu-capygfx` (regressão alpha.294/303) — PASSOU** após a mudança.
- **`make smoke-x64-qemu-capygfx-desktop-spawn` (regressão alpha.304) — PASSOU** após a mudança.
- `make version-audit` — verde (current=`0.8.0-alpha.305`).
- **VMware oficial:** inalterado nesta release (nenhum novo alvo de smoke; a ponte de input é exercitada pelos mesmos smokes existentes, sem cenário fim-a-fim de login+clique ainda).

## Compliance de versoes

| Repo | De | Para | Observacao |
|---|---|---|---|
| **CapyOS** | `0.8.0-alpha.304+20260701` | `0.8.0-alpha.305+20260701` | Slice B item 4: ponte de eventos de input mouse/teclado para janelas ring-3, host-testada + regressão QEMU confirmada. |
| **CapyUI** | `2.22.6` | `2.22.6` (**bump pendente**) | `window_dispatcher.c` alterado (aditivo); bump formal + atualização de `docs/compatibility.md` + matriz de compatibilidade **não feitos nesta sessão** — ver seção "O que ainda falta". |

_Build: `0.8.0-alpha.305+20260701`_
