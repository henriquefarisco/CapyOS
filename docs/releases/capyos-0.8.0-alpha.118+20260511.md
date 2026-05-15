# CapyOS 0.8.0-alpha.118+20260511

## Resumo executivo

Este patch continua a Etapa 2 — Sessão gráfica operacional. A entrega adiciona
um contrato de prontidão para smokes futuros da sessão gráfica, derivado do
snapshot operacional do `alpha.117`. O contrato não executa smoke, não inicia VM e
não altera runtime; apenas consolida bloqueios de `gui-session` e `mouse-events`
para validações futuras.

## Principais entregas

- `DESKTOP_SMOKE_BLOCK_*` define uma bitmask estável de bloqueios de prontidão.
- `struct desktop_session_smoke_readiness` agrega `desktop_session_health`,
  bloqueios, estado da fila, rotas do dispatcher e flags finais de prontidão.
- `desktop_session_smoke_readiness_snapshot()` preenche o contrato a partir de
  `desktop_session_health_snapshot()`.
- `gui_session_ready` exige sessão ativa, framebuffer, dimensões, mouse, cursor,
  taskbar, dispatcher saudável, rotas completas, fila saudável, ausência de
  overlays e ausência de drag ativo de janela.
- `mouse_events_ready` reutiliza a prontidão da sessão e reforça mouse/cursor e
  rotas do dispatcher.

## Segurança e compatibilidade

- A mudança é observacional e fail-closed para ponteiros nulos.
- A função zera a saída antes de calcular prontidão, evitando lixo de estado em
  snapshots parciais.
- O patch não adiciona dispatch, fila, callback, renderização, driver, smoke real,
  autenticação, storage crypto, ABI pública de userland, CapyDisplay, Wayland,
  Mesa/Vulkan ou CapyLX.

## Validação estática

- Revisão estática confirmou que os scripts temporários foram removidos.
- Revisão estática confirmou que a bitmask cobre todos os bloqueios usados pela
  função de prontidão.
- Revisão estática confirmou que a função chama apenas snapshots existentes e não
  executa comandos, testes, smokes ou side effects de runtime.
- Não foram executados `make`, `git`, build, suite de testes ou smoke VMware
  real.
