# CapyOS 0.8.0-alpha.115+20260511

## Resumo executivo

Este patch continua a Etapa 2 — Sessão gráfica operacional. A entrega migra o
right-click/context menu de janelas comuns para o dispatcher central GUI,
removendo o callback direto `rwin->on_context_menu()` no desktop para esse ramo.
Os caminhos de overlay, taskbar e desktop icons permanecem priorizados para
preservar comportamento modal e compatibilidade.

## Principais entregas

- `desktop_handle_mouse()` agora cria um `GUI_EVENT_MOUSE_DOWN` local para
  right-click em janela com `on_context_menu`.
- O evento é entregue por `gui_window_dispatch_event()` com `window_id` explícito.
- `dispatch_mouse_button()` segue responsável por converter o evento em chamada a
  `win->on_context_menu()` com coordenadas locais.
- O desktop continua fechando context menu/Start Menu aberto antes de abrir um
  novo menu contextual.
- Right-click no plano de fundo continua roteado diretamente para
  `desktop_icons_handle_context()`, pois desktop icons não são janela comum.

## Segurança e compatibilidade

- Inline prompt, context menu aberto, Start Menu, taskbar e botões de titlebar
  continuam tendo prioridade sobre janelas comuns.
- Janelas sem `on_context_menu` não recebem comportamento contextual novo neste
  slice.
- A captura de mouse permanece opt-in por `capture_mouse`; right-click não ativa
  captura implícita.
- O patch não altera autenticação, storage crypto, ABI pública de userland,
  CapyDisplay, drivers gráficos, Wayland, Mesa/Vulkan ou CapyLX.

## Validação estática

- Revisão estática confirmou que os scripts temporários foram removidos.
- Revisão estática confirmou que `desktop_handle_mouse()` não chama mais
  `rwin->on_context_menu()` diretamente.
- Revisão estática confirmou que o right-click de janela comum usa
  `GUI_EVENT_MOUSE_DOWN` local e `gui_window_dispatch_event()`.
- Revisão estática confirmou que desktop icons e overlays continuam fora desse
  ramo migrado.
- Não foram executados `make`, `git`, build, suite de testes ou smoke VMware
  real.
