# CapyOS 0.8.0-alpha.137+20260511

Data: 2026-05-11

## Resumo

Este patch continua a Etapa 2 da sessao grafica operacional adicionando uma
sessao one-shot segura de UI de credenciais para a futura GUI do loginwindow. A
sessao inicializa storage efemero, executa uma etapa composta de UI, propaga
apenas flags redigidas e limpa storage/scratch antes de retornar.

## Entregue

- Adicionado `LOGIN_WINDOW_CREDENTIAL_UI_SESSION_VERSION`.
- Adicionado `struct login_window_credential_ui_session` para representar a
  saida publica redigida de uma sessao one-shot.
- Adicionada `login_window_credential_ui_session_build()` para inicializar o
  buffer, compor a etapa de UI, copiar flags seguras e limpar storage/scratch.
- Testes planejados cobrem append com wipe final, submit vazio bloqueado, cancel
  com wipe e storage ausente fail-closed.

## Segurança

- O contrato publico nao carrega storage bruto, texto mascarado ou comprimento de
  senha.
- `raw_secret_exposed` e `masked_text_exposed` permanecem `0`.
- `credential_redacted` e `length_redacted` permanecem `1`.
- `submit_blocked` permanece `1`, `submit_enabled` permanece `0` e
  `auth_attempt_allowed` permanece `0`.
- Storage e scratch de mascara sao limpos antes da saida quando disponiveis.

## Validacao

Validado por revisao estatica do codigo e da documentacao. Conforme restricao
operacional desta entrega, nenhum comando `make`, `git`, build ou suite de testes
foi executado.
