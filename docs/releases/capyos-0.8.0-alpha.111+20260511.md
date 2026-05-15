# CapyOS 0.8.0-alpha.111+20260511

## Resumo executivo

Este patch continua a Etapa 2 — Sessão gráfica operacional. A entrega migra o
scroll de mouse destinado a janelas focadas para o dispatcher central GUI,
removendo o espelhamento em fila para esse caso e evitando callback direto no
desktop. Clicks, drag/move/resize, botões de titlebar, taskbar, context menus e
desktop icons permanecem nos caminhos atuais para preservar prioridades modais.

## Principais entregas

- `desktop_handle_mouse()` preserva `desktop_overlay_active()` antes de aceitar
  scroll de janela.
- O scroll passa a ser empacotado em um `GUI_EVENT_MOUSE_SCROLL` local com alvo na
  janela focada.
- O evento local é entregue por `gui_window_dispatch_event()`.
- O caminho antigo de `gui_event_push_mouse_scroll()` mais chamada direta a
  `scroll_win->on_scroll()` foi removido do dispatch de scroll do desktop.
- O dispatcher central passa a contabilizar scroll nas métricas de dispatch e
  continua invalidando a janela recebedora via `invalidate_if_receivable()`.

## Segurança e compatibilidade

- O patch não altera click esquerdo/direito, drag, resize, titlebar, taskbar,
  inline prompt, context menu ou desktop icons.
- O patch não chama `gui_window_dispatch()` por frame e não drena a fila de
  eventos espelhada.
- Janelas sem `on_scroll` continuam falhando de forma segura no dispatcher como
  `missing_handler_total`.
- Nenhum driver gráfico, CapyDisplay, Wayland, Mesa/Vulkan, CapyLX ou Etapa 3 foi
  iniciado.

## Validação estática

- Revisão estática confirmou que os scripts temporários foram removidos.
- Revisão estática confirmou que o bloco de scroll em `desktop_handle_mouse()` não
  chama mais `gui_event_push_mouse_scroll()` nem `scroll_win->on_scroll()`.
- Revisão estática confirmou que overlays continuam bloqueando scroll antes do
  dispatcher central.
- Revisão estática confirmou que click/drag/context menu permanecem fora desta
  migração.
- Não foram executados `make`, `git`, build, suite de testes ou smoke VMware
  real.
