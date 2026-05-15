# CapyOS 0.8.0-alpha.128+20260511

Data: 2026-05-11

## Resumo

Este patch continua a Etapa 2 da sessao grafica operacional adicionando o
contrato de consumo de tentativa de submit do loginwindow. A tentativa avalia o
gate fail-closed, nao habilita autenticacao grafica e executa wipe obrigatorio
do buffer quando ele existe.

## Entregue

- Adicionado `LOGIN_WINDOW_CREDENTIAL_SUBMIT_ATTEMPT_VERSION`.
- Adicionado `struct login_window_credential_submit_attempt` para registrar
  tentativa, gate avaliado, segredo presente, wipe e autoridade textual.
- Adicionada `login_window_credential_submit_attempt_consume()` para consumir a
  tentativa futura de submit sem autenticar pela GUI.
- A tentativa herda o motivo estavel do gate, como `gui-submit-disabled`,
  `policy-unavailable`, `credential-overflow-blocked` ou `buffer-unavailable`.
- Quando o buffer existe, a tentativa sempre chama
  `login_window_credential_buffer_wipe()` e registra se o wipe foi tentado e
  concluido.
- Testes planejados cobrem buffer preenchido, politica ausente, politica sem
  wipe, overflow e buffer ausente, sempre sem liberar autenticacao grafica.

## Segurança

- `submit_allowed` e `auth_attempt_allowed` permanecem sempre `0` neste alpha.
- Buffer preenchido, overflowed ou sem politica e limpo durante a tentativa.
- Buffer ausente nao reporta wipe bem-sucedido, evitando falsa garantia de
  limpeza.

## Validacao

Validado por revisao estatica do codigo e da documentacao. Conforme restricao
operacional desta entrega, nenhum comando `make`, `git`, build ou suite de testes
foi executado.
