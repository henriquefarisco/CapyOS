# CapyOS 0.8.0-alpha.101+20260510

## Resumo executivo

Este patch inicia a Etapa 2 — Sessão gráfica operacional. A entrega foca no
primeiro ponto de desempenho do runtime gráfico: o caminho ocioso do desktop
passa a usar espera cooperativa/HLT em vez de manter um spin longo por frame.

## Principais entregas

- `desktop_frame_delay()` usa `task_sleep(1)` quando o scheduler possui tick
  observado e alvo seguro para troca de contexto.
- Quando não há alvo seguro para sleep cooperativo, o runtime usa `hlt` no x86_64
  apenas após observar tick; antes disso usa fallback PIT/pause limitado.
- Builds não x86_64 ou unit-test mantêm fallback PIT/pause curto e limitado.
- O scheduler expõe `scheduler_can_sleep_current()` para evitar dormir a tarefa
  atual sem tick, peer pronto ou idle preemptável.

## Segurança e compatibilidade

- Nenhum driver gráfico, stack Wayland/Mesa/Vulkan, CapyDisplay ou loginwindow
  foi iniciado.
- O helper do scheduler é somente leitura de estado interno e não altera política
  de escalonamento.
- O fallback preserva responsividade de mouse ao checar `mouse_pending()`.

## Validação estática

- Revisão estática confirmou que `task_sleep(1)` só é chamado após tick
  observado e com peer pronto ou idle preemptável.
- Revisão estática confirmou que Etapas 3-15 continuam bloqueadas.
- Não foram executados `make`, `git`, build, suite de testes ou smoke VMware
  real.
