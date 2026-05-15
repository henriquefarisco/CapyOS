# CapyOS 0.8.0-alpha.114+20260511

## Resumo executivo

Este patch continua a Etapa 2 — Sessão gráfica operacional. A entrega migra o
mouse down/up esquerdo de janelas comuns para o dispatcher central GUI,
removendo o espelhamento em fila para esse ramo e evitando callback direto
`win->on_mouse()` no desktop. Os caminhos modais e globais permanecem fora da
migração para preservar prioridade e compatibilidade.

## Principais entregas

- `desktop_handle_mouse()` não chama mais `gui_event_push_mouse_button()` no ramo
  de botão esquerdo.
- Click esquerdo em área cliente de janela comum agora cria `GUI_EVENT_MOUSE_DOWN`
  local e chama `gui_window_dispatch_event()` com `window_id` explícito.
- Mouse up esquerdo, quando não pertence a drag/move/resize do window manager,
  cria `GUI_EVENT_MOUSE_UP` e deixa o dispatcher finalizar captura de mouse.
- Captura de mouse no dispatcher passou a ser opt-in por janela via
  `capture_mouse`, evitando callbacks repetidos em apps de click simples.
- O File Manager opta por captura e aceita mouse-up capturado com `buttons == 0`
  para finalizar drag-and-drop pelo dispatcher, sem callback direto do desktop.
- Drag de ícones do desktop continua no caminho direto do desktop por não ser uma
  janela comum.

## Segurança e compatibilidade

- Inline prompt, context menu, Start Menu, taskbar e botões de titlebar continuam
  com prioridade antes do dispatcher.
- Move/resize de janelas continuam com `window_manager` e não disparam callback de
  app quando `WM_DRAG_NONE` não está ativo.
- Right-click permanece no caminho anterior até uma migração dedicada, evitando
  conflito com context menus e overlays.
- Apps que não optam por `capture_mouse` recebem apenas o click direto, sem
  captura implícita de movimentos com botão pressionado.
- O patch não altera autenticação, storage crypto, ABI pública de userland,
  CapyDisplay, drivers gráficos, Wayland, Mesa/Vulkan ou CapyLX.

## Validação estática

- Revisão estática confirmou que os scripts temporários foram removidos.
- Revisão estática confirmou que `desktop_handle_mouse()` não chama mais
  `gui_event_push_mouse_button()` nem `win->on_mouse()` diretamente no ramo de
  click esquerdo comum.
- Revisão estática confirmou que mouse move/scroll/hover continuam nos caminhos
  já migrados e que overlays continuam antes do dispatcher.
- Revisão estática confirmou que captura de mouse é opt-in e que File Manager
  drag-and-drop finaliza por mouse-up capturado via dispatcher.
- Não foram executados `make`, `git`, build, suite de testes ou smoke VMware
  real.
