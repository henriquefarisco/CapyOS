# CapyOS 0.8.0-alpha.123+20260511

## Resumo executivo

Este patch continua a Etapa 2 — Sessão gráfica operacional. A entrega adiciona
um diagnóstico determinístico das rotas de input do dispatcher ao contrato de
prontidão para smokes GUI, permitindo que futuros consumidores saibam exatamente
qual rota comum/especial está ausente quando o blocker `dispatcher-routes` surgir.

## Principais entregas

- `DESKTOP_DISPATCHER_ROUTE_*` define uma bitmask estável para cada rota de input
  auditada no contrato do dispatcher.
- `desktop_dispatcher_route_name()` fornece labels estáveis para diagnóstico.
- `struct desktop_dispatcher_route_summary` agrega rotas esperadas, rotas prontas,
  rotas ausentes, contagem e primeira rota ausente.
- `desktop_dispatcher_route_summary()` deriva o resumo a partir de
  `struct gui_window_dispatcher_input_routes` e falha fechado quando as rotas são
  nulas.
- `struct desktop_session_smoke_readiness` agora embute `route_summary` junto com
  `blocker_summary`.
- `desktop_session_smoke_readiness_from_health()` deriva `dispatcher_routes_ready`
  a partir de `route_summary.missing_route_flags == 0`.
- `tests/test_desktop_smoke_readiness.c` ganhou cobertura planejada para máscara,
  nomes, resumo de rotas, rotas nulas e resumo embutido no readiness.

## Segurança e compatibilidade

- A mudança é observacional e não altera a semântica de dispatch.
- Nenhum callback, fila, mouse capture, overlay, taskbar, compositor ou app passou
  a ser chamado por este patch.
- O patch não altera autenticação, storage crypto, ABI pública de userland,
  CapyDisplay, drivers gráficos, Wayland, Mesa/Vulkan ou CapyLX.

## Validação estática

- Revisão estática confirmou que os scripts temporários foram removidos.
- Revisão estática confirmou que todas as rotas de `gui_window_dispatcher_input_routes`
  têm bit e label estáveis.
- Revisão estática confirmou que rotas nulas falham fechado com todas as rotas
  marcadas como ausentes.
- Não foram executados `make`, `git`, build, suite de testes ou smoke VMware real.
