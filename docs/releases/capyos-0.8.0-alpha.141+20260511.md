# CapyOS 0.8.0-alpha.141+20260511

Data: 2026-05-11

## Resumo

Este patch continua a Etapa 2 da sessao grafica operacional adicionando um plano
seguro de renderizacao para a tela de credenciais do loginwindow. O plano consome
a sessao one-shot segura e produz somente flags de layout, foco, botoes, avisos e
acao primaria para a futura GUI.

## Entregue

- Adicionado `LOGIN_WINDOW_CREDENTIAL_SCREEN_RENDER_PLAN_VERSION`.
- Adicionado `struct login_window_credential_screen_render_plan`.
- Adicionada `login_window_credential_screen_render_plan_build()` para derivar
  layout/foco/acoes UI a partir da sessao segura de credenciais.
- Testes planejados cobrem input de senha seguro, recuperacao textual, resume,
  sessao insegura e sessao ausente.

## Segurança

- O plano exige tela construida, sessao de credenciais segura, storage limpo,
  redacao de segredo/comprimento, ausencia de exposicao bruta/mascarada, submit
  bloqueado e login textual autoritativo antes de habilitar qualquer acao visual.
- `submit_button_visible` permanece `0`, `submit_button_enabled` permanece `0`,
  `submit_blocked` permanece `1` e `auth_attempt_allowed` permanece `0`.
- O contrato publico nao carrega storage bruto, texto mascarado, politica
  aninhada, snapshots internos ou comprimento de senha.

## Validacao

Validado por revisao estatica do codigo e da documentacao. Conforme restricao
operacional desta entrega, nenhum comando `make`, `git`, build ou suite de testes
foi executado.
