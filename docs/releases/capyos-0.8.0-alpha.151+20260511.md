# CapyOS 0.8.0-alpha.151+20260511

Data: 2026-05-11

## Resumo

Este patch continua a Etapa 2 da sessao grafica operacional adicionando um
dispatch plan seguro para a tela de credenciais do loginwindow. O dispatch plan
consome o handoff plan seguro e publica somente o ticket final declarativo para
a futura GUI/compositor, sem despachar janela real, sem carregar segredo,
mascara, comprimento ou snapshots internos.

## Entregue

- Adicionado `LOGIN_WINDOW_CREDENTIAL_SCREEN_DISPATCH_PLAN_VERSION`.
- Adicionado `struct login_window_credential_screen_dispatch_plan`.
- Adicionada `login_window_credential_screen_dispatch_plan_build()` para
  selecionar tickets `credential-screen-dispatch-ticket`,
  `text-recovery-dispatch-ticket`, `text-login-resume-dispatch-ticket` ou
  `text-login-fallback-dispatch-ticket`.
- Testes planejados cobrem dispatch declarativo de credencial, recuperacao
  textual, resume textual, submit bloqueado, acao desconhecida, handoff plan
  ausente e handoff plan inseguro.

## Segurança

- O dispatch plan exige handoff plan seguro, handoff autorizado mas nao entregue,
  ticket selecionado, rota selecionada, credenciais seguras, storage limpo,
  redacao de segredo/comprimento, ausencia de exposicao bruta/mascarada, submit
  bloqueado/desabilitado, callbacks de submit/auth zerados e login textual
  autoritativo.
- Submit grafico nunca vira autenticacao: converge para
  `text-login-fallback-dispatch-ticket` com `gui-submit-disabled`.
- `window_dispatch_delivered` permanece `0`; o contrato e declarativo e nao
  despacha a GUI real ao compositor.
- `submit_callback_bound` e `auth_callback_bound` permanecem `0`.
- `submit_enabled` permanece `0`, `submit_blocked` permanece `1` e
  `auth_attempt_allowed` permanece `0`.
- O contrato publico nao carrega storage bruto, texto mascarado, politica
  aninhada, snapshots internos, ponteiros de callback ou comprimento de senha.

## Validacao

Validado por revisao estatica do codigo e da documentacao. Conforme restricao
operacional desta entrega, nenhum comando `make`, `git`, build ou suite de testes
foi executado.
