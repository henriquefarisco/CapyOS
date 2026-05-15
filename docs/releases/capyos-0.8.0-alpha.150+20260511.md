# CapyOS 0.8.0-alpha.150+20260511

Data: 2026-05-11

## Resumo

Este patch continua a Etapa 2 da sessao grafica operacional adicionando um
handoff plan seguro para a tela de credenciais do loginwindow. O handoff plan
consome o commit plan seguro e publica somente o envelope final declarativo para
a futura GUI/compositor, sem entregar janela real, sem carregar segredo, mascara,
comprimento ou snapshots internos.

## Entregue

- Adicionado `LOGIN_WINDOW_CREDENTIAL_SCREEN_HANDOFF_PLAN_VERSION`.
- Adicionado `struct login_window_credential_screen_handoff_plan`.
- Adicionada `login_window_credential_screen_handoff_plan_build()` para
  selecionar envelopes `credential-screen-handoff-envelope`,
  `text-recovery-handoff-envelope`, `text-login-resume-handoff-envelope` ou
  `text-login-fallback-handoff-envelope`.
- Testes planejados cobrem handoff declarativo de credencial, recuperacao
  textual, resume textual, submit bloqueado, acao desconhecida, commit plan
  ausente e commit plan inseguro.

## Segurança

- O handoff plan exige commit plan seguro, commit autorizado mas nao executado,
  envelope selecionado, rota selecionada, credenciais seguras, storage limpo,
  redacao de segredo/comprimento, ausencia de exposicao bruta/mascarada, submit
  bloqueado/desabilitado, callbacks de submit/auth zerados e login textual
  autoritativo.
- Submit grafico nunca vira autenticacao: converge para
  `text-login-fallback-handoff-envelope` com `gui-submit-disabled`.
- `window_handoff_delivered` permanece `0`; o contrato e declarativo e nao
  entrega a GUI real ao compositor.
- `submit_callback_bound` e `auth_callback_bound` permanecem `0`.
- `submit_enabled` permanece `0`, `submit_blocked` permanece `1` e
  `auth_attempt_allowed` permanece `0`.
- O contrato publico nao carrega storage bruto, texto mascarado, politica
  aninhada, snapshots internos, ponteiros de callback ou comprimento de senha.

## Validacao

Validado por revisao estatica do codigo e da documentacao. Conforme restricao
operacional desta entrega, nenhum comando `make`, `git`, build ou suite de testes
foi executado.
