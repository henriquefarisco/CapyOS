# CapyOS 0.8.0-alpha.117+20260511

## Resumo executivo

Este patch continua a Etapa 2 — Sessão gráfica operacional. A entrega adiciona
um snapshot operacional da sessão gráfica para consolidar, em uma API leve, a
prontidão de framebuffer, dimensões, mouse, taskbar, overlays, window manager,
foco e dispatcher central. O objetivo é preparar os smokes futuros `gui-session`
e `mouse-events` sem executar build, testes ou VMware neste slice.

## Principais entregas

- `struct desktop_session_health` expõe estado operacional essencial da sessão.
- `desktop_session_health_snapshot()` retorna 0 para entrada inválida e, em caso
  válido, preenche estado ativo, framebuffer, dimensões, mouse, cursor, taskbar,
  overlays, menu iniciar, drag do window manager, janela focada e amostras do
  dispatcher.
- O snapshot inclui `struct gui_window_dispatcher_health`, preservando o contrato
  de rotas de input adicionado no `alpha.116`.
- A função não muda o loop do desktop e não incrementa os contadores de amostragem
  usados pelo runtime normal.

## Segurança e compatibilidade

- A mudança é observacional: não altera dispatch de input, renderização, taskbar,
  overlays, desktop icons, window manager ou callbacks de apps.
- O snapshot permite detectar prontidão e estados modais antes de smokes reais,
  sem depender de comandos externos.
- O patch não altera autenticação, storage crypto, ABI pública de userland,
  CapyDisplay, drivers gráficos, Wayland, Mesa/Vulkan ou CapyLX.

## Validação estática

- Revisão estática confirmou que os scripts temporários foram removidos.
- Revisão estática confirmou que `desktop_session_health_snapshot()` possui
  contrato fail-closed para ponteiros nulos.
- Revisão estática confirmou que o snapshot inclui saúde atual do dispatcher e
  não adiciona nova chamada de dispatch, fila, callback ou renderização.
- Não foram executados `make`, `git`, build, suite de testes ou smoke VMware
  real.
