# CapyOS 0.8.0-alpha.110+20260511

## Resumo executivo

Este patch continua a Etapa 2 — Sessão gráfica operacional. A entrega migra o
teclado destinado a janelas focadas para o dispatcher central GUI como caminho
autoritativo, removendo o espelhamento em fila para esse caso e evitando duplo
callback. Atalhos globais, overlays, Start Menu e mouse permanecem nos caminhos
atuais para reduzir risco antes da migração fim-a-fim.

## Principais entregas

- `desktop_handle_input()` preserva os guardrails existentes para `KEY_SUPER`,
  inline prompt, Escape de overlays e navegação do Start Menu.
- Após esses guardrails, o input de teclado para janela focada é empacotado em um
  `GUI_EVENT_KEY_DOWN` local.
- O evento local é entregue diretamente por `gui_window_dispatch_event()`.
- O caminho antigo de `gui_event_push_key()` mais chamada direta a
  `focused->on_key()` foi removido do dispatch de teclado do desktop.
- O dispatcher central passa a contabilizar o teclado de janelas nas métricas de
  `dispatched_total`, `handled_total`, `missing_target_total` e
  `missing_handler_total`.

## Segurança e compatibilidade

- O patch não altera o comportamento de overlays ou atalhos globais.
- O patch não migra mouse neste alpha.
- O patch não drena a fila de eventos por frame e não chama `gui_window_dispatch()`
  a partir do desktop.
- A fila espelhada deixa de crescer para teclas de janelas focadas, reduzindo
  backlog sem remover os helpers de fila usados por testes e outros produtores.
- Nenhum driver gráfico, CapyDisplay, Wayland, Mesa/Vulkan, CapyLX ou Etapa 3 foi
  iniciado.

## Validação estática

- Revisão estática confirmou que os scripts temporários foram removidos.
- Revisão estática confirmou que `desktop_handle_input()` não chama mais
  `gui_event_push_key()` nem `focused->on_key()` diretamente.
- Revisão estática confirmou que o teclado de janelas focadas entra em
  `gui_window_dispatch_event()` uma única vez.
- Revisão estática confirmou que overlays e Start Menu continuam antes do
  dispatcher central.
- Não foram executados `make`, `git`, build, suite de testes ou smoke VMware
  real.
