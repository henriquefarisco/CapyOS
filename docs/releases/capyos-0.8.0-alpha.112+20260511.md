# CapyOS 0.8.0-alpha.112+20260511

## Resumo executivo

Este patch continua a Etapa 2 — Sessão gráfica operacional. A entrega migra o
hover/mouse move de janelas para o dispatcher central GUI, removendo o
espelhamento em fila para esse caso e evitando callback direto `on_hover` no
desktop. O window manager, overlays, cursor hint, clicks, drag/move/resize,
botões de titlebar, taskbar, context menus e desktop icons permanecem nos
caminhos atuais para preservar prioridades modais.

## Principais entregas

- `desktop_handle_mouse()` mantém `wm_handle_mouse_move()` antes do hover para
  preservar drag/move/resize de janelas.
- Context menu e Start Menu continuam recebendo hover diretamente antes do
  dispatcher central.
- Hover de janela comum agora é empacotado em `GUI_EVENT_MOUSE_MOVE` local.
- O evento local é entregue por `gui_window_dispatch_event()`.
- O caminho antigo de `gui_event_push_mouse_move()` mais chamada direta a
  `hov->on_hover()` foi removido do hover de janelas no desktop.

## Segurança e compatibilidade

- O patch não altera click esquerdo/direito, drag, resize, titlebar, taskbar,
  inline prompt, context menu, desktop icons ou cursor hint.
- O patch não chama `gui_window_dispatch()` por frame e não drena a fila de
  eventos espelhada.
- Janelas sem `on_hover` continuam falhando de forma segura no dispatcher como
  `missing_handler_total`.
- Nenhum driver gráfico, CapyDisplay, Wayland, Mesa/Vulkan, CapyLX ou Etapa 3 foi
  iniciado.

## Validação estática

- Revisão estática confirmou que os scripts temporários foram removidos.
- Revisão estática confirmou que o bloco de hover em `desktop_handle_mouse()` não
  chama mais `gui_event_push_mouse_move()` nem `hov->on_hover()` diretamente.
- Revisão estática confirmou que context menu e Start Menu continuam antes do
  dispatcher central.
- Revisão estática confirmou que click/drag/titlebar/cursor hint permanecem fora
  desta migração.
- Não foram executados `make`, `git`, build, suite de testes ou smoke VMware
  real.
