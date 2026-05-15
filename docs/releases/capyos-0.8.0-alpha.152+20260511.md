# CapyOS 0.8.0-alpha.152+20260511

Data: 2026-05-11

## Resumo

Este patch continua a Etapa 2 da sessao grafica operacional adicionando um queue
plan seguro para a tela de credenciais do loginwindow. O queue plan consome o
dispatch plan seguro e publica somente o ticket final declarativo para uma
futura fila GUI/compositor, sem enfileirar janela real, sem carregar segredo,
mascara, comprimento ou snapshots internos.

## Entregue

- Adicionado `LOGIN_WINDOW_CREDENTIAL_SCREEN_QUEUE_PLAN_VERSION`.
- Adicionado `struct login_window_credential_screen_queue_plan`.
- Adicionada `login_window_credential_screen_queue_plan_build()` para selecionar
  tickets `credential-screen-queue-ticket`, `text-recovery-queue-ticket`,
  `text-login-resume-queue-ticket` ou `text-login-fallback-queue-ticket`.
- Testes planejados cobrem queue declarativo de credencial, recuperacao textual,
  resume textual, submit bloqueado, acao desconhecida, dispatch plan ausente e
  dispatch plan inseguro.

## Segurança

- O queue plan exige dispatch plan seguro, dispatch autorizado mas nao entregue,
  ticket selecionado, rota selecionada, credenciais seguras, storage limpo,
  redacao de segredo/comprimento, ausencia de exposicao bruta/mascarada, submit
  bloqueado/desabilitado, callbacks de submit/auth zerados e login textual
  autoritativo.
- Submit grafico nunca vira autenticacao: converge para
  `text-login-fallback-queue-ticket` com `gui-submit-disabled`.
- `window_queue_enqueued` permanece `0`; o contrato e declarativo e nao
  enfileira GUI real no compositor.
- `submit_callback_bound` e `auth_callback_bound` permanecem `0`.
- `submit_enabled` permanece `0`, `submit_blocked` permanece `1` e
  `auth_attempt_allowed` permanece `0`.
- O contrato publico nao carrega storage bruto, texto mascarado, politica
  aninhada, snapshots internos, ponteiros de callback ou comprimento de senha.

## Validacao

Validado por revisao estatica do codigo e da documentacao. Conforme restricao
operacional desta entrega, nenhum comando `make`, `git`, build ou suite de testes
foi executado.
