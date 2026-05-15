# CapyOS 0.8.0-alpha.131+20260511

Data: 2026-05-11

## Resumo

Este patch continua a Etapa 2 da sessao grafica operacional adicionando um
painel seguro de credenciais para o futuro loginwindow GUI. O painel combina a
view mascarada do campo com o ultimo resultado de input, expondo estado
renderizavel e auditavel sem retornar segredo bruto nem habilitar autenticacao
grafica.

## Entregue

- Adicionado `LOGIN_WINDOW_CREDENTIAL_PANEL_VERSION`.
- Adicionado `struct login_window_credential_panel` para consolidar field view,
  input, wipe, mascara e estado de painel.
- Adicionada `login_window_credential_panel_build()` para compor painel seguro a
  partir de politica, buffer, ultimo input e output mascarado.
- O painel propaga estados `filled`, `editing`, `submit-blocked`, `cancelled` e
  `input-blocked` sem expor storage bruto.
- Submit e cancel refletem wipe, mas continuam sem autenticar pela GUI.
- Testes planejados cobrem painel renderizavel, input aceito, submit bloqueado,
  cancel consumido, politica insegura e input bloqueado.

## Segurança

- `submit_allowed` e `auth_attempt_allowed` permanecem sempre `0`.
- O painel depende da field view mascarada e nao acessa segredo bruto.
- Input bloqueado e politica insegura nao tornam o painel autenticavel.
- O output mascarado e limpo antes da composicao do painel.

## Validacao

Validado por revisao estatica do codigo e da documentacao. Conforme restricao
operacional desta entrega, nenhum comando `make`, `git`, build ou suite de testes
foi executado.
