# CapyOS 0.8.0-alpha.147+20260511

Data: 2026-05-11

## Resumo

Este patch continua a Etapa 2 da sessao grafica operacional adicionando um
binding seguro para a tela de credenciais do loginwindow. O binding consome o
presenter seguro e publica somente comandos finais de montagem de widgets para a
futura janela grafica, sem carregar segredo, mascara, comprimento ou snapshots
internos.

## Entregue

- Adicionado `LOGIN_WINDOW_CREDENTIAL_SCREEN_BINDING_VERSION`.
- Adicionado `struct login_window_credential_screen_binding`.
- Adicionada `login_window_credential_screen_binding_build()` para selecionar
  bindings `credential-screen-bindings`, `text-recovery-bindings`,
  `text-login-resume-bindings` ou `text-login-fallback-bindings`.
- Testes planejados cobrem widgets de credencial, recuperacao textual, resume
  textual, submit bloqueado, acao desconhecida, presenter ausente e presenter
  inseguro.

## Segurança

- O binding exige presenter seguro, rota selecionada, credenciais seguras,
  storage limpo, redacao de segredo/comprimento, ausencia de exposicao
  bruta/mascarada, submit bloqueado/desabilitado e login textual autoritativo.
- Submit grafico nunca vira autenticacao: converge para
  `text-login-fallback-bindings` com `gui-submit-disabled`.
- `submit_enabled` permanece `0`, `submit_blocked` permanece `1` e
  `auth_attempt_allowed` permanece `0`.
- O contrato publico nao carrega storage bruto, texto mascarado, politica
  aninhada, snapshots internos ou comprimento de senha.

## Validacao

Validado por revisao estatica do codigo e da documentacao. Conforme restricao
operacional desta entrega, nenhum comando `make`, `git`, build ou suite de testes
foi executado.
