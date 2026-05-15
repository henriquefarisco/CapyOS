# CapyOS 0.8.0-alpha.97+20260510

## Resumo executivo

Este patch continua somente a Etapa 1 — CapyUI Shell Polish v1. A entrega
melhora a decoração de janelas ativa/inativa sem iniciar sessão gráfica
operacional, drivers, CapyLX, Wayland, Mesa/Vulkan ou TLS real.

## Principais entregas

- Titlebar passa a usar gradiente 2D simples derivado da paleta ativa.
- Janela focada usa texto claro e contorno/acento mais evidente.
- Janela inativa usa texto muted e controles visualmente rebaixados.
- Título da janela é truncado antes da área dos botões para evitar sobreposição.
- Botões minimizar, maximizar/restaurar e fechar ganham borda, brilho superior e
  contraste de estado sem depender de GPU 3D.

## Segurança e compatibilidade

- Nenhum contrato de input, ABI pública ou callback de janela foi alterado.
- Hit-tests existentes de minimizar, maximizar/restaurar e fechar foram mantidos.
- O polish é puramente 2D e render-time, sem driver novo ou aceleração 3D.

## Validação estática

- Revisão estática confirmou que o render respeita os limites do framebuffer.
- Revisão estática confirmou que títulos longos não invadem a área dos botões.
- Não foram executados `make`, `git`, build, suite de testes ou smoke VMware
  real.
