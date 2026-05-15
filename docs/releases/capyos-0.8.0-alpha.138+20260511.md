# CapyOS 0.8.0-alpha.138+20260511

Data: 2026-05-11

## Resumo

Este patch continua a Etapa 2 da sessao grafica operacional adicionando um view
model seguro de recuperacao de credenciais para a futura GUI do loginwindow. O
contrato une a sessao one-shot de credenciais, ja limpa e redigida, com a
politica textual de recuperacao/retorno ao login normal.

## Entregue

- Adicionado `LOGIN_WINDOW_CREDENTIAL_RECOVERY_VIEW_MODEL_VERSION`.
- Adicionado `struct login_window_credential_recovery_view_model`.
- Adicionada `login_window_credential_recovery_view_model_build()` para decidir
  visibilidade/habilitacao de recuperacao textual e resume.
- Testes planejados cobrem recuperacao textual, resume pronto, sessao insegura e
  politica de recuperacao insegura.

## Segurança

- Recuperacao e retorno ao login normal permanecem text-session-only.
- O view model exige sessao de credenciais redigida, sem exposicao e com storage
  limpo antes de expor acoes.
- `submit_blocked` permanece `1`, `submit_enabled` permanece `0` e
  `auth_attempt_allowed` permanece `0`.
- O contrato publico nao carrega storage bruto, texto mascarado ou comprimento de
  senha.

## Validacao

Validado por revisao estatica do codigo e da documentacao. Conforme restricao
operacional desta entrega, nenhum comando `make`, `git`, build ou suite de testes
foi executado.
