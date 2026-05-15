# CapyOS 0.8.0-alpha.129+20260511

Data: 2026-05-11

## Resumo

Este patch continua a Etapa 2 da sessao grafica operacional adicionando um
redutor puro de input para o futuro campo de credenciais do loginwindow. O
redutor aceita acoes discretas de append, backspace, submit e cancel, reutiliza
o buffer e a tentativa fail-closed existentes e continua sem habilitar
autenticacao grafica.

## Entregue

- Adicionado `LOGIN_WINDOW_CREDENTIAL_INPUT_RESULT_VERSION`.
- Adicionadas acoes `LOGIN_WINDOW_CREDENTIAL_INPUT_APPEND`, `BACKSPACE`,
  `SUBMIT` e `CANCEL`.
- Adicionado `struct login_window_credential_input_result` para expor acao,
  aceite, mudanca de buffer, submit/cancel consumidos, wipe e autoridade textual.
- Adicionada `login_window_credential_input_apply()` como redutor puro de input.
- Append/backspace exigem politica segura e buffer inicializado antes de alterar
  storage.
- Submit consome `login_window_credential_submit_attempt_consume()` e executa
  wipe obrigatorio sem autorizar autenticacao grafica.
- Cancel limpa o buffer sem tentar submit.
- Testes planejados cobrem append/backspace, submit com wipe, cancel com wipe,
  politica ausente e acao desconhecida.

## Segurança

- `submit_allowed` e `auth_attempt_allowed` permanecem sempre `0`.
- Texto de credencial continua exigindo mascara.
- Input sem politica segura ou acao desconhecida nao altera o buffer.
- Submit e cancel limpam o buffer quando ele existe.

## Validacao

Validado por revisao estatica do codigo e da documentacao. Conforme restricao
operacional desta entrega, nenhum comando `make`, `git`, build ou suite de testes
foi executado.
