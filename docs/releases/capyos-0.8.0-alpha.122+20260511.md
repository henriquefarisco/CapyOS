# CapyOS 0.8.0-alpha.122+20260511

## Resumo executivo

Este patch continua a Etapa 2 — Sessão gráfica operacional. A entrega embute o
resumo determinístico de blockers diretamente em `struct desktop_session_smoke_readiness`,
permitindo que consumidores futuros de `gui-session` e `mouse-events` obtenham
flags, resumo, contagem e primeiro blocker em uma única leitura do snapshot.

## Principais entregas

- `struct desktop_session_smoke_readiness` agora contém `blocker_summary` além de
  `blocker_flags`.
- `desktop_session_smoke_readiness_from_health()` preenche `blocker_summary` após
  calcular a bitmask de blockers.
- O campo legado `blocker_flags` permanece disponível para compatibilidade com
  consumidores simples.
- `tests/test_desktop_smoke_readiness.c` passou a verificar o resumo embutido nos
  cenários saudável, blockers base, rota ausente, fila bloqueada e overlay/drag.

## Segurança e compatibilidade

- A mudança é observacional e não altera a semântica de readiness.
- A unidade pura continua sem chamar compositor, fila, dispatcher, apps, rede,
  shell, renderização ou smokes reais.
- O patch não altera autenticação, storage crypto, ABI pública de userland,
  CapyDisplay, drivers gráficos, Wayland, Mesa/Vulkan ou CapyLX.

## Validação estática

- Revisão estática confirmou que os scripts temporários foram removidos.
- Revisão estática confirmou que `blocker_summary.blocker_flags` acompanha
  `blocker_flags` nos snapshots bloqueados.
- Revisão estática confirmou que readiness saudável recebe resumo vazio com
  `first_blocker_name == "none"`.
- Não foram executados `make`, `git`, build, suite de testes ou smoke VMware real.
