# CapyOS 0.8.0-alpha.103+20260510

## Resumo executivo

Este patch continua a Etapa 2 — Sessão gráfica operacional. A entrega prepara o
loginwindow GUI com um contrato fail-closed no runtime de login existente,
sem trocar o fluxo textual atual e sem coletar credenciais em uma janela GUI.

## Principais entregas

- `struct login_window_contract` descreve prontidão, bloqueios e dependências.
- `login_window_contract_evaluate()` informa se o loginwindow GUI pode ser
  ofertado com segurança.
- O contrato bloqueia GUI em manutenção/recovery dinâmico, sem input,
  runtime incompleto ou callbacks essenciais ausentes.
- `login_runtime_run()` passa a usar o contrato para estado de input e modo de
  manutenção, preservando o fluxo textual atual.

## Segurança e compatibilidade

- Nenhuma senha passa por widget GUI neste patch.
- O modo manutenção/recovery continua priorizando o caminho textual seguro.
- O contrato é fail-closed: qualquer dependência ausente mantém `ready=0` e
  fornece `blocked_reason` determinístico.
- Nenhum driver gráfico, stack Wayland/Mesa/Vulkan, CapyDisplay ou Etapa 3 foi
  iniciado.

## Validação estática

- Revisão estática confirmou que o contrato não autentica nem armazena senhas.
- Revisão estática confirmou que manutenção/recovery bloqueia o loginwindow GUI.
- Revisão estática confirmou que Etapas 3-15 continuam bloqueadas.
- Não foram executados `make`, `git`, build, suite de testes ou smoke VMware
  real.
