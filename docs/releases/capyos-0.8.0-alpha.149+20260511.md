# CapyOS 0.8.0-alpha.149+20260511

Data: 2026-05-11

## Resumo

Este patch continua a Etapa 2 da sessao grafica operacional adicionando um
commit plan seguro para a tela de credenciais do loginwindow. O commit plan
consome o mount plan seguro e publica somente a decisao final declarativa para a
futura janela grafica, sem executar commit real, sem carregar segredo, mascara,
comprimento ou snapshots internos.

## Entregue

- Adicionado `LOGIN_WINDOW_CREDENTIAL_SCREEN_COMMIT_PLAN_VERSION`.
- Adicionado `struct login_window_credential_screen_commit_plan`.
- Adicionada `login_window_credential_screen_commit_plan_build()` para selecionar
  transacoes `credential-screen-commit-plan`, `text-recovery-commit-plan`,
  `text-login-resume-commit-plan` ou `text-login-fallback-commit-plan`.
- Testes planejados cobrem commit declarativo de credencial, recuperacao textual,
  resume textual, submit bloqueado, acao desconhecida, mount plan ausente e
  mount plan inseguro.

## Segurança

- O commit plan exige mount plan seguro, arvore de widgets selecionada, rota
  selecionada, credenciais seguras, storage limpo, redacao de
  segredo/comprimento, ausencia de exposicao bruta/mascarada, submit
  bloqueado/desabilitado, callbacks de submit/auth zerados e login textual
  autoritativo.
- Submit grafico nunca vira autenticacao: converge para
  `text-login-fallback-commit-plan` com `gui-submit-disabled`.
- `window_commit_executed` permanece `0`; o contrato e declarativo e nao aplica
  a GUI real.
- `submit_callback_bound` e `auth_callback_bound` permanecem `0`.
- `submit_enabled` permanece `0`, `submit_blocked` permanece `1` e
  `auth_attempt_allowed` permanece `0`.
- O contrato publico nao carrega storage bruto, texto mascarado, politica
  aninhada, snapshots internos, ponteiros de callback ou comprimento de senha.

## Validacao

Validado por revisao estatica do codigo e da documentacao. Conforme restricao
operacional desta entrega, nenhum comando `make`, `git`, build ou suite de testes
foi executado.
