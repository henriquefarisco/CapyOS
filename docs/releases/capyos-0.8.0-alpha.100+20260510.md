# CapyOS 0.8.0-alpha.100+20260510

## Resumo executivo

Este patch fecha a Etapa 1 — CapyUI Shell Polish v1. A entrega consolida os
critérios de aceite do polish visual, adiciona o atalho Super/Windows para o
launcher e desbloqueia a Etapa 2 como próxima etapa permitida, sem iniciar
sessão gráfica operacional, drivers, CapyLX, Wayland, Mesa/Vulkan ou TLS real.

## Principais entregas

- Etapa 1 marcada como concluída no plano sequencial.
- Etapa 2 marcada apenas como próxima desbloqueada, sem implementação iniciada.
- Tecla Super/Windows passa a abrir/fechar o launcher Capy nos backends PS/2 e
  Hyper-V que expõem os scancodes estendidos correspondentes.
- Critérios de aceite da Etapa 1 foram consolidados por revisão estática.
- Histórico de `alpha.99` foi normalizado no manifesto de versão.

## Segurança e compatibilidade

- Nenhum serviço, driver gráfico, stack Wayland/Mesa/Vulkan ou TLS real foi
  iniciado.
- O novo keycode interno `KEY_SUPER` não altera caracteres ASCII existentes nem
  os códigos especiais já usados por setas/F1-F12.
- O launcher ignora Super quando um inline prompt está ativo para não interromper
  operações de rename/new file.

## Validação estática

- Revisão estática confirmou que o atalho Super usa scancodes estendidos
  `E0 5B` e `E0 5C` nos caminhos PS/2/Hyper-V.
- Revisão estática confirmou que a Etapa 2 permanece sem código iniciado.
- Não foram executados `make`, `git`, build, suite de testes ou smoke VMware
  real.
