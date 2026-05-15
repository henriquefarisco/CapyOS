# CapyOS 0.8.0-alpha.102+20260510

## Resumo executivo

Este patch continua a Etapa 2 — Sessão gráfica operacional. A entrega separa um
tick gráfico explícito no desktop: composição completa só roda quando o
compositor tem cena suja, e o cursor só é redesenhado quando a cena foi
recomposta ou quando sua posição mudou.

## Principais entregas

- `compositor_needs_render()` expõe o estado sujo da cena sem forçar render.
- `desktop_run_frame()` consulta a cena antes de chamar `compositor_render()`.
- `desktop_session` mantém cache da última posição de cursor desenhada.
- O compositor expõe consulta para cursor inválido por posição ou cache interno.
- `compositor_render_cursor()` só é chamado quando há cena nova ou cursor mudou.

## Segurança e compatibilidade

- Nenhum driver gráfico, stack Wayland/Mesa/Vulkan, CapyDisplay ou loginwindow
  foi iniciado.
- A API nova é somente leitura do estado de composição.
- Cursor continua redesenhado após composição completa para preservar a camada
  visual sobre o frontbuffer.

## Validação estática

- Revisão estática confirmou que cena suja ainda força composição e cursor.
- Revisão estática confirmou que cursor parado com cena limpa não chama render,
  exceto quando o cache interno do cursor foi invalidado.
- Revisão estática confirmou que Etapas 3-15 continuam bloqueadas.
- Não foram executados `make`, `git`, build, suite de testes ou smoke VMware
  real.
