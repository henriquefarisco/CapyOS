# CapyOS 0.8.0-alpha.121+20260511

## Resumo executivo

Este patch continua a Etapa 2 — Sessão gráfica operacional. A entrega adiciona
um resumo determinístico dos blockers de prontidão GUI, permitindo que logs e
smokes futuros diferenciem flags conhecidas, flags desconhecidas, quantidade de
bloqueios e o primeiro blocker prioritário sem executar smokes reais.

## Principais entregas

- `struct desktop_smoke_blocker_summary` agrega flags totais, flags conhecidas,
  flags desconhecidas, contagem e primeiro blocker.
- `desktop_smoke_blocker_summary()` transforma a bitmask em resumo determinístico.
- A ordem do primeiro blocker segue a ordem estável do contrato:
  `inactive`, `framebuffer`, `dimensions`, `mouse`, `cursor`, `taskbar`,
  `dispatcher`, `dispatcher-routes`, `queue`, `overlay`, `window-drag`.
- Flags desconhecidas são preservadas em `unknown_blocker_flags` e contam como um
  grupo `unknown` quando presentes.
- `tests/test_desktop_smoke_readiness.c` ganhou cobertura planejada para saída
  nula, resumo vazio, resumo misto e resumo somente desconhecido.

## Segurança e compatibilidade

- A mudança é observacional e não altera a semântica de readiness.
- A unidade pura continua sem chamar compositor, fila, dispatcher, apps, rede,
  shell, renderização ou smokes reais.
- O patch não altera autenticação, storage crypto, ABI pública de userland,
  CapyDisplay, drivers gráficos, Wayland, Mesa/Vulkan ou CapyLX.

## Validação estática

- Revisão estática confirmou que os scripts temporários foram removidos.
- Revisão estática confirmou que o resumo zera a saída, rejeita ponteiro nulo e
  preserva flags desconhecidas.
- Revisão estática confirmou que o primeiro blocker é determinístico e que a
  unidade pura não acessa runtime gráfico.
- Não foram executados `make`, `git`, build, suite de testes ou smoke VMware real.
