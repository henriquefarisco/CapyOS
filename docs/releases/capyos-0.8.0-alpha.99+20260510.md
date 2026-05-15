# CapyOS 0.8.0-alpha.99+20260510

## Resumo executivo

Este patch continua somente a Etapa 1 — CapyUI Shell Polish v1. A entrega
expande o tray da taskbar com estados de rede, som, serviços e usuário sem
iniciar sessão gráfica operacional, drivers, CapyLX, Wayland, Mesa/Vulkan ou
TLS real.

## Principais entregas

- Tray passa de `NET/USER` para `NET/SND/SYS/USR`.
- `SND:n/a` registra estado fail-safe até existir backend de áudio real.
- `SYS` agrega o estado local do `service_manager` como `ok`, `wait`, `warn`
  ou `err`.
- O texto do tray passa a usar limite maior e continua truncado de forma segura.
- Atualização permanece acoplada ao tick do relógio para evitar polling extra.

## Segurança e compatibilidade

- Nenhum serviço, driver ou daemon novo foi criado.
- O tray consome apenas snapshots locais já existentes.
- Serviços críticos bloqueados/parados em boot são tratados como erro agregado.
- Estados desconhecidos ou em inicialização aparecem como espera, não sucesso.

## Validação estática

- Revisão estática confirmou que o tray usa buffer com limite fixo.
- Revisão estática confirmou que `SYS` valida retorno de cada snapshot.
- Não foram executados `make`, `git`, build, suite de testes ou smoke VMware
  real.
