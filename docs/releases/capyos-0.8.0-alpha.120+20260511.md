# CapyOS 0.8.0-alpha.120+20260511

## Resumo executivo

Este patch continua a Etapa 2 — Sessão gráfica operacional. A entrega melhora a
observabilidade do contrato de prontidão para smokes GUI ao adicionar uma máscara
conhecida e nomes estáveis para cada blocker. Isso prepara diagnósticos futuros
para `gui-session` e `mouse-events` sem executar smokes reais.

## Principais entregas

- `DESKTOP_SMOKE_BLOCK_KNOWN_MASK` agrega todos os blockers conhecidos do contrato.
- `desktop_smoke_block_known_mask()` expõe a máscara conhecida por API pequena.
- `desktop_smoke_block_name()` converte cada blocker conhecido para label estável:
  `inactive`, `framebuffer`, `dimensions`, `mouse`, `cursor`, `taskbar`,
  `dispatcher`, `dispatcher-routes`, `queue`, `overlay` e `window-drag`.
- Bits desconhecidos retornam `unknown` para manter diagnóstico fail-closed.
- `tests/test_desktop_smoke_readiness.c` ganhou cobertura planejada para máscara,
  labels e fallback `unknown`.

## Segurança e compatibilidade

- A mudança é observacional e não altera a semântica de readiness.
- A unidade pura continua sem chamar compositor, fila, dispatcher, apps, rede,
  shell, renderização ou smokes reais.
- O patch não altera autenticação, storage crypto, ABI pública de userland,
  CapyDisplay, drivers gráficos, Wayland, Mesa/Vulkan ou CapyLX.

## Validação estática

- Revisão estática confirmou que os scripts temporários foram removidos.
- Revisão estática confirmou que todos os blockers usados pelo contrato têm label.
- Revisão estática confirmou que bits desconhecidos são reportados como `unknown`.
- Não foram executados `make`, `git`, build, suite de testes ou smoke VMware real.
