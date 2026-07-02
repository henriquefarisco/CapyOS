# CapyOS 0.8.0-alpha.304+20260701

**Data:** 2026-07-01
**Canal:** alpha (experimental)
**Versao:** `0.8.0-alpha.304+20260701`
**Plataforma oficial:** VMware + UEFI + E1000 (inalterada)
**Tipo:** slice (Etapa 7 — primeiros passos da "Slice B": navegador gráfico
lançável a partir de uma sessão de desktop já rodando, não mais só boot-exclusivo)

## Resumo executivo

alpha.304+20260701: primeiros 3 dos 5 subsistemas independentes identificados
como bloqueio para lançar `capygfx` (o navegador gráfico) como um app real
dentro de uma sessão de desktop CapyUI **já rodando**, em vez de um boot
exclusivo que substitui o login inteiro. Entregue e **provado em QEMU real**:

1. **Backend de produção do `syscall_gfx` desacoplado do boot-exclusivo**:
   `syscall_gfx_install_default_ops()` agora também pode ser instalado sob
   demanda (idempotente) por `kernel_spawn_capygfx_desktop`, não só pelo
   `kernel_main` sob `CAPYOS_GFX_SMOKE`.
2. **Novo mecanismo de spawn NÃO-noreturn**: `kernel_spawn_capygfx_desktop()`
   (`include/kernel/user_init.h`) cria o processo, arma a main thread para
   primeiro dispatch (`user_task_arm_for_first_dispatch_with_rax`) e a
   enfileira (`scheduler_add`) **sem** chamar `process_enter_user_mode`
   (que é `noreturn` e sequestraria quem chamou) — o primeiro mecanismo de
   spawn ring-3 deste repositório pensado para ser chamado de dentro de um
   contexto que continua rodando (ex.: um comando de shell disparado do
   terminal do desktop), em vez de todo `kernel_boot_run_*` existente
   (boot-exclusivo, `noreturn` no sucesso).
3. **`capygfx` embutido fora do gate de smoke**: novo flag
   `CAPYOS_DESKTOP_GRAPHICAL_BROWSER` embute o blob para este propósito,
   independente de `CAPYOS_GFX_SMOKE`.
4. **Comando de shell `open-browser-graphical`**: presente incondicionalmente
   em qualquer build não-core-only (mensagem amigável quando o kernel não foi
   compilado com `CAPYOS_DESKTOP_GRAPHICAL_BROWSER`), mesmo padrão de
   `open-calculator`/`open-files`/etc.
5. **Prova mecânica de que o spawn é de fato escalonado**: novo smoke
   dedicado (`CAPYOS_DESKTOP_GRAPHICAL_BROWSER_SMOKE`) que espelha
   exatamente `kernel_boot_run_two_busy_users` (já provado) — `hello` entra
   em ring 3 primeiro, `capygfx` é enfileirado via
   `kernel_spawn_capygfx_desktop`, e quando `hello` sai, o **yield
   cooperativo do scheduler** (nenhuma dependência de
   `CAPYOS_PREEMPTIVE_SCHEDULER`; `context_switch` é agnóstico de quem o
   disparou) despacha `capygfx`, que roda todos os syscalls gráficos até o
   fim e sai 0.

**Resultado do smoke real (QEMU):**
```
[user_init] CAPYOS_DESKTOP_GRAPHICAL_BROWSER_SMOKE; init compositor + spawning hello+capygfx.
[smoke] capygfx ready
hello, capyland
[ok]   + '[smoke] capygfx ready'
[ok] qemu-marker smoke passed
```

## O que isso PROVA e o que NÃO prova (honestidade sobre o escopo)

Uma investigação dedicada (subagent, só-leitura) mapeou **5 subsistemas
independentes** que hoje pressupõem exclusividade mútua entre o boot-smoke e
uma sessão de desktop real. Este release fecha os itens 1, 2 e 5 (instalação
do backend, mecanismo de spawn, blob fora do gate) e **prova mecanicamente**
o item 3 (coexistência real de scheduler) — mas via um "supervisor" sintético
(`hello`) que espelha `kernel_boot_run_two_busy_users`, **não** a sessão real
`desktop_runtime_start` do CapyUI. Ainda faltam:

- **A integração com o loop REAL do CapyUI** (`desktop_runtime_start`/
  `desktop_scheduler_adopt_current`): esta release prova que o MECANISMO
  (spawn + scheduler) funciona com as primitivas já comprovadas do kernel,
  mas não foi testado especificamente dentro do loop cooperativo real do
  desktop (que usa uma task própria via `task_create_kernel`, um caminho
  ligeiramente diferente do usado aqui). Chamar `open-browser-graphical`
  durante uma sessão de desktop de fato logada continua sem ser exercitado
  em CI.
- **Ponte de eventos de input (mouse/teclado)**: achado crítico da
  investigação — a fila que `SYS_WINDOW_POLL_EVENT` drena
  (`gui_event_poll`) só recebe eventos de ciclo de vida de janela
  (close/focus/blur/resize/paint, empurrados pelo compositor). Cliques e
  teclas do desktop real usam um despacho síncrono totalmente separado
  (`gui_window_dispatch_event`) que nunca alimenta essa fila. Uma janela do
  `capygfx` coexistindo hoje ficaria **visível, e responde a fechar pela
  barra de título (X)** (evento de ciclo de vida já funciona), mas **surda a
  mouse/teclado** — a página não pode ser rolada nem links clicados. Fechar
  essa ponte exige mudanças no repo irmão `CapyUI` (`desktop_mouse.c`/
  `window_dispatcher.c`), fora do escopo desta sessão por cruzar a fronteira
  de repositório (exigiria bump de versão do CapyUI + atualização da matriz
  de compatibilidade).
- **Login real + automação de teclado no smoke**: o novo smoke QEMU usa um
  `hello` sintético no lugar do desktop real para provar o scheduler sem
  precisar automatizar login+digitação de comando via serial/teclado — isso
  também fica para uma iteração futura caso se queira um smoke fim-a-fim
  literal (boot → login → `desktop` → `open-browser-graphical`).

`userland/bin/capygfx/main.c` também ganhou um loop interativo opt-in
(`CAPYGFX_DESKTOP_INTERACTIVE`, OFF por padrão) que fica aberto até observar
`WINDOW_CLOSE` (evento de ciclo de vida já funcional) ou um teto de iterações
— pronto para quando a integração real do desktop estiver disponível, mas
ainda não exercitado em nenhum smoke desta release (o smoke de
spawn/scheduler usa o `capygfx` no modo single-shot padrão, sem essa flag).

## Mudancas

- `include/kernel/user_init.h` + `src/kernel/user_init.c`: `kernel_spawn_capygfx_desktop()` (spawn não-noreturn) + `kernel_boot_run_capygfx_desktop_spawn_smoke()` (orquestrador do novo smoke).
- `src/kernel/embedded_progs.c`: guard do blob `/bin/capygfx` estendido para `CAPYOS_GFX_SMOKE` OU `CAPYOS_DESKTOP_GRAPHICAL_BROWSER`.
- `src/kernel/process.c`: hook do marcador `[smoke] capygfx ready` estendido para também disparar sob `CAPYOS_DESKTOP_GRAPHICAL_BROWSER_SMOKE`.
- `src/arch/x86_64/kernel_main.c`: novo branch de boot sob `CAPYOS_DESKTOP_GRAPHICAL_BROWSER_SMOKE`; includes de `gui/compositor.h`/`kernel/syscall_gfx.h` estendidos para o mesmo gate.
- `src/shell/commands/extended.c`: novo comando `open-browser-graphical` (incondicional em builds não-core-only; mensagem amigável se o blob não foi embutido).
- `userland/bin/capygfx/main.c`: loop interativo opt-in `CAPYGFX_DESKTOP_INTERACTIVE` (aguarda `WINDOW_CLOSE`), OFF por padrão.
- `Makefile`: `CAPYOS_DESKTOP_GRAPHICAL_BROWSER`(+`_SMOKE`) + alvos `smoke-x64-vmware-capygfx-desktop-spawn` (oficial) e `smoke-x64-qemu-capygfx-desktop-spawn` (dev).

## Validacao

- `make test` — verde, incl. `browser_pipeline_tests: 19/19` (sem regressão).
- `make layout-audit` — limpo (Warnings: none).
- `make all64` com os novos flags de smoke — verde, `capyos64.bin` construído e linkado sem erros.
- **`make smoke-x64-qemu-capygfx-desktop-spawn` — PASSOU em QEMU real**: `[smoke] capygfx ready` observado após `capygfx` ser despachado pelo scheduler atrás de `hello`.
- `make all64` **default** (clean, sem flags) — verde, 0 erros. `capyos64.bin` cresce ~48 bytes (2125360→2125408) em relação ao alpha.303 — **não é byte-idêntico desta vez**, por design: o novo comando `open-browser-graphical` existe incondicionalmente em qualquer build não-core-only (mesmo padrão dos outros `open-*`), não é código morto atrás de `#ifdef`.
- `make version-audit` — verde (current=`0.8.0-alpha.304`).
- Sem mudança de ABI cross-repo; **nenhuma mudança no repo `CapyUI`** nesta release (a ponte de input, quando implementada, exigirá uma).
- **VMware oficial:** `smoke-x64-vmware-capygfx-desktop-spawn` definido e pronto, não executado nesta sessão (passo do operador).

## Compliance de versoes

| Repo | De | Para | Observacao |
|---|---|---|---|
| **CapyOS** | `0.8.0-alpha.303+20260701` | `0.8.0-alpha.304+20260701` | Slice B (parcial): backend syscall_gfx desacoplado do boot-exclusivo + spawn não-noreturn + prova mecânica de coexistência de scheduler, via QEMU real. **Sem mudança de ABI.** |

_Build: `0.8.0-alpha.304+20260701`_
