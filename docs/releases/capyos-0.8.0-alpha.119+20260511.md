# CapyOS 0.8.0-alpha.119+20260511

## Resumo executivo

Este patch continua a Etapa 2 — Sessão gráfica operacional. A entrega torna o
contrato de prontidão para smokes GUI mais confiável ao extrair a derivação pura
para uma unidade pequena e testável, evitando acoplar o `desktop.c` completo ao
runner host. Nenhum teste, build, git ou smoke foi executado neste slice.

## Principais entregas

- `src/gui/desktop/desktop_smoke_readiness.c` concentra a lógica pura de
  prontidão a partir de `struct desktop_session_health`.
- `desktop_session_smoke_readiness_from_health()` foi exposta para testes e para
  reuso por `desktop_session_smoke_readiness_snapshot()`.
- `desktop_session_smoke_readiness_snapshot()` permanece no `desktop.c` como
  coletor do health snapshot real e delega a derivação para a unidade pura.
- `tests/test_desktop_smoke_readiness.c` adiciona cobertura host planejada para
  saída nula, health nulo, sessão saudável, bloqueios base, rota ausente, fila
  não saudável e bloqueios de overlay/drag.
- `Makefile` e `tests/test_runner.c` foram atualizados para incluir a nova unidade
  e o teste host planejado.

## Segurança e compatibilidade

- A extração não altera semântica de prontidão nem o runtime do desktop.
- A unidade pura não chama compositor, fila, dispatcher, apps, rede, shell,
  renderização ou smokes reais.
- A cobertura host evita linkar `src/gui/desktop/desktop.c` inteiro, reduzindo
  acoplamento com apps/widgets/backends e risco de dependências artificiais.
- O patch não altera autenticação, storage crypto, ABI pública de userland,
  CapyDisplay, drivers gráficos, Wayland, Mesa/Vulkan ou CapyLX.

## Validação estática

- Revisão estática confirmou que os scripts temporários foram removidos.
- Revisão estática confirmou que a lógica pura preserva os mesmos blockers e flags
  do `alpha.118`.
- Revisão estática confirmou que o teste host cobre caminhos prontos e bloqueados
  sem exigir o desktop runtime completo.
- Não foram executados `make`, `git`, build, suite de testes ou smoke VMware
  real.
