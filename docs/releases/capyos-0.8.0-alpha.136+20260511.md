# CapyOS 0.8.0-alpha.136+20260511

Data: 2026-05-11

## Resumo

Este patch continua a Etapa 2 da sessao grafica operacional adicionando um
pipeline composto de UI de credenciais para a futura GUI do loginwindow. O
pipeline aplica exatamente uma acao de input e recompõe interacao, prontidao,
auditoria redigida e view model em um snapshot unico, sem expor segredo bruto,
texto mascarado ou comprimento da credencial.

## Entregue

- Adicionado `LOGIN_WINDOW_CREDENTIAL_UI_STEP_VERSION`.
- Adicionado `struct login_window_credential_ui_step` para representar uma etapa
  completa e auditavel da UI de credenciais.
- Adicionada `login_window_credential_ui_step_build()` para encadear input,
  painel/interacao, readiness, auditoria redigida e view model.
- O snapshot composto cobre append, submit, cancel e acoes desconhecidas mantendo
  submit grafico bloqueado.
- Testes planejados cobrem append composto, submit com wipe, cancel com wipe e
  acao desconhecida fail-closed.

## Segurança

- O pipeline nao carrega storage bruto, texto mascarado nem comprimento de senha.
- `raw_secret_exposed` e `masked_text_exposed` permanecem `0`.
- `credential_redacted` e `length_redacted` permanecem `1`.
- `submit_blocked` permanece `1`, `submit_enabled` permanece `0` e
  `auth_attempt_allowed` permanece `0`.

## Validacao

Validado por revisao estatica do codigo e da documentacao. Conforme restricao
operacional desta entrega, nenhum comando `make`, `git`, build ou suite de testes
foi executado.
