# CapyOS 0.8.0-alpha.127+20260511

Data: 2026-05-11

## Resumo

Este patch continua a Etapa 2 da sessao grafica operacional adicionando um gate
fail-closed para tentativa futura de submit de credenciais do loginwindow. Mesmo
com politica pronta e buffer preenchido, o gate nao permite autenticacao grafica
e preserva o login textual como caminho autoritativo.

## Entregue

- Adicionado `LOGIN_WINDOW_CREDENTIAL_SUBMIT_GATE_VERSION`.
- Adicionado `struct login_window_credential_submit_gate` para consolidar estado
  de politica, buffer, wipe, submit e autoridade textual.
- Adicionada `login_window_credential_submit_gate_evaluate()` como unidade pura
  de decisao fail-closed.
- O gate diferencia politica ausente, campo de senha desabilitado, mascara
  ausente, wipe ausente, buffer indisponivel, buffer vazio, buffer nao mascarado,
  overflow e buffer ja limpo.
- Mesmo com buffer preenchido e mascarado, o motivo final e `gui-submit-disabled`
  e `auth_attempt_allowed` permanece `0`.
- Testes planejados cobrem politica ausente, buffer preenchido rejeitado, buffer
  vazio, buffer nao mascarado e overflow bloqueado.

## Segurança

- Nenhum caminho de autenticacao grafica foi ativado.
- `submit_allowed` e `auth_attempt_allowed` permanecem sempre `0` neste alpha.
- O gate exige wipe e preserva `text_login_authoritative` para manter o login
  textual como unica rota de autenticacao.

## Validacao

Validado por revisao estatica do codigo e da documentacao. Conforme restricao
operacional desta entrega, nenhum comando `make`, `git`, build ou suite de testes
foi executado.
