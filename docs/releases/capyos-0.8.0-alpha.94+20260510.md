# CapyOS 0.8.0-alpha.94+20260510

## Resumo executivo

Este patch inicia a Etapa 1 do novo plano sequencial: CapyUI Shell Polish v1.
A entrega é visual e incremental, aproximando o desktop de uma mistura
Ubuntu/Windows 7 sem exigir GPU 3D, drivers novos ou dependência Linux.

## Principais entregas

- Adiciona o tema `classic-modern`, com aliases legados controlados `classic`
  e `ubuntu7`.
- Integra `classic-modern` ao compositor, fallback framebuffer, `config-theme`
  e Settings.
- Refina a taskbar com botão `Capy`, realce superior, itens com borda de foco
  e relógio em pill.
- Atualiza notificações/toasts para usar a paleta ativa em vez de cores
  hardcoded.
- Mantém a Etapa 1 em andamento; launcher com busca, apps fixados completos e
  system tray avançado seguem pendentes.

## Segurança e compatibilidade

- Nenhuma etapa posterior do plano foi iniciada.
- Nenhum driver, handshake TLS, CapyLX, Wayland ou Mesa/Vulkan foi ativado.
- O tema novo é opt-in por configuração e preserva os temas existentes.
- O fallback framebuffer reconhece o mesmo nome de tema para evitar divergência
  entre boot/console e desktop.

## Validação estática

- Revisão estática confirmou que a lista de temas está alinhada entre
  compositor, Settings e `config-theme`.
- Revisão estática confirmou que notificações usam `compositor_theme()` e
  mantêm bounds fixos.
- Não foram executados `make`, `git`, build, suite de testes ou smoke VMware
  real.
