# CapyOS 0.8.0-alpha.134+20260511

Data: 2026-05-11

## Resumo

Este patch continua a Etapa 2 da sessao grafica operacional adicionando um
evento auditavel redigido para o futuro fluxo de credenciais do loginwindow GUI.
O evento registra estado, acao, input, wipe e motivo de bloqueio sem expor
segredo bruto, texto mascarado ou comprimento da credencial.

## Entregue

- Adicionado `LOGIN_WINDOW_CREDENTIAL_AUDIT_EVENT_VERSION`.
- Adicionado `struct login_window_credential_audit_event` para representar
  auditoria redigida de credenciais.
- Adicionada `login_window_credential_audit_event_build()` para derivar evento a
  partir de readiness e interacao opcional.
- Eventos classificam readiness, input aceito, input bloqueado, submit bloqueado,
  cancelamento e ausencia de readiness.
- Testes planejados cobrem painel pronto redigido, submit bloqueado redigido,
  input bloqueado sem mutacao e ausencia de readiness.

## Segurança

- `raw_secret_exposed` e `masked_text_exposed` permanecem sempre `0`.
- `secret_redacted` e `length_redacted` permanecem sempre `1`.
- `submit_blocked` permanece ativo e `submit_allowed`/`auth_attempt_allowed`
  permanecem sempre `0`.
- O evento nao carrega storage bruto, texto mascarado nem comprimento de senha.

## Validacao

Validado por revisao estatica do codigo e da documentacao. Conforme restricao
operacional desta entrega, nenhum comando `make`, `git`, build ou suite de testes
foi executado.
