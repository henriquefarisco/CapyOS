# CapyOS 0.8.0-alpha.142+20260511

Data: 2026-05-11

## Resumo

Este patch continua a Etapa 2 da sessao grafica operacional adicionando um plano
seguro de acoes para a tela de credenciais do loginwindow. O plano valida uma
intencao da futura GUI contra o render plan seguro e retorna somente uma acao
redigida e auditavel.

## Entregue

- Adicionado `LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTION_PLAN_VERSION`.
- Adicionadas constantes de acao segura da tela de credenciais.
- Adicionado `struct login_window_credential_screen_action_plan`.
- Adicionada `login_window_credential_screen_action_plan_build()` para validar
  foco de credencial, recuperacao textual, resume textual, fallback textual,
  submit bloqueado e acao desconhecida.
- Testes planejados cobrem edit/focus, recovery, resume, submit bloqueado, acao
  desconhecida, plano inseguro e plano ausente.

## Segurança

- O plano exige render plan seguro, sessao de credenciais segura, storage limpo,
  redacao de segredo/comprimento, ausencia de exposicao bruta/mascarada, submit
  invisivel/desabilitado/bloqueado e login textual autoritativo.
- Submit grafico permanece bloqueado com `gui-submit-disabled`.
- `submit_enabled` permanece `0`, `submit_blocked` permanece `1` e
  `auth_attempt_allowed` permanece `0`.
- O contrato publico nao carrega storage bruto, texto mascarado, politica
  aninhada, snapshots internos ou comprimento de senha.

## Validacao

Validado por revisao estatica do codigo e da documentacao. Conforme restricao
operacional desta entrega, nenhum comando `make`, `git`, build ou suite de testes
foi executado.
