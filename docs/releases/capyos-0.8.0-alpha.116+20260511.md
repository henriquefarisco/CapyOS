# CapyOS 0.8.0-alpha.116+20260511

## Resumo executivo

Este patch continua a Etapa 2 — Sessão gráfica operacional. A entrega adiciona
um contrato de rotas de input ao snapshot de saúde do dispatcher central GUI,
expondo de forma estática e auditável quais caminhos comuns já passam pelo
dispatcher e quais continuam diretos por prioridade modal ou por pertencerem ao
plano de fundo.

## Principais entregas

- `struct gui_window_dispatcher_input_routes` registra rotas de teclado, scroll,
  hover, click esquerdo, right-click/context menu, captura opt-in e ausência de
  fila espelhada.
- `struct gui_window_dispatcher_health` agora inclui `routes` junto de métricas do
  dispatcher e snapshot da fila.
- `gui_window_dispatcher_health_snapshot()` preenche o contrato de rotas no mesmo
  ponto em que já expõe backlog, drops e captura obsoleta.
- `tests/test_gui_window_dispatcher.c` passa a cobrir estaticamente o contrato de
  rotas no teste-fonte existente de snapshot.

## Segurança e compatibilidade

- As rotas diretas de overlays, window manager, titlebar, taskbar e desktop icons
  são explicitamente registradas como caminhos especiais preservados.
- O contrato não muda despacho de eventos em runtime; apenas torna o estado da
  migração inspecionável pelo desktop e por testes host futuros.
- A captura de mouse permanece opt-in por janela via `capture_mouse`.
- O patch não altera autenticação, storage crypto, ABI pública de userland,
  CapyDisplay, drivers gráficos, Wayland, Mesa/Vulkan ou CapyLX.

## Validação estática

- Revisão estática confirmou que os scripts temporários foram removidos.
- Revisão estática confirmou que `gui_window_dispatcher_health_snapshot()` preenche
  `routes` antes de avaliar fila/backlog/drops.
- Revisão estática confirmou que o teste-fonte confere todos os campos do contrato
  de rotas.
- Revisão estática confirmou que a mudança não adiciona nova chamada de dispatch,
  fila, callback de app ou caminho modal.
- Não foram executados `make`, `git`, build, suite de testes ou smoke VMware
  real.
