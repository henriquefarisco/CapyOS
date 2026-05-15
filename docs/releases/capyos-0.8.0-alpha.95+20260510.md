# CapyOS 0.8.0-alpha.95+20260510

## Resumo executivo

Este patch continua somente a Etapa 1 — CapyUI Shell Polish v1. A entrega
avança o launcher/menu e o tray inicial sem iniciar sessão gráfica operacional,
drivers, CapyLX, Wayland, Mesa/Vulkan ou TLS real.

## Principais entregas

- Launcher `Capy` ganha campo de busca textual no topo do popup.
- Digitação com o menu aberto filtra entradas por nome ou grupo visual.
- Setas cima/baixo navegam a seleção e Enter ativa a entrada selecionada.
- O menu passa a mostrar grupos visuais `Apps`, `System` e `Session` com base
  nos separadores existentes.
- Taskbar ganha tray inicial textual `NET:<estado> USER:<usuario>` à esquerda
  do relógio.

## Segurança e compatibilidade

- O tray lê somente snapshots locais de sessão e `net_stack_status()`.
- Nenhum serviço novo, driver, handshake TLS, backend Linux ou etapa posterior
  foi ativado.
- Todas as strings novas usam buffers fixos e cópia/append com limite.

## Validação estática

- Revisão estática confirmou que o menu filtra apenas entradas visíveis e ignora
  separadores.
- Revisão estática confirmou que o tray reserva espaço antes do relógio e reduz
  a área de itens abertos para evitar sobreposição.
- Não foram executados `make`, `git`, build, suite de testes ou smoke VMware
  real.
