# CapyOS 0.8.0-alpha.153+20260511

Data: 2026-05-11

## Resumo

Este patch continua a Etapa 2 da sessao grafica operacional adicionando um
activation plan seguro para a tela de credenciais do loginwindow. O activation
plan consome o queue plan seguro e publica somente o ticket final declarativo
para uma futura ativacao GUI/compositor, sem aplicar janela real, sem carregar
segredo, mascara, comprimento ou snapshots internos.

## Entregue

- Adicionado `LOGIN_WINDOW_CREDENTIAL_SCREEN_ACTIVATION_PLAN_VERSION`.
- Adicionado `struct login_window_credential_screen_activation_plan`.
- Adicionada `login_window_credential_screen_activation_plan_build()` para
  selecionar tickets `credential-screen-activation-ticket`,
  `text-recovery-activation-ticket`, `text-login-resume-activation-ticket` ou
  `text-login-fallback-activation-ticket`.
- Testes planejados cobrem ativacao declarativa de credencial, recuperacao
  textual, resume textual, submit bloqueado, acao desconhecida, queue plan
  ausente e queue plan inseguro.

## Segurança

- O activation plan exige queue plan seguro, queue autorizado mas nao enfileirado,
  ticket selecionado, rota selecionada, credenciais seguras, storage limpo,
  redacao de segredo/comprimento, ausencia de exposicao bruta/mascarada, submit
  bloqueado/desabilitado, callbacks de submit/auth zerados e login textual
  autoritativo.
- Submit grafico nunca vira autenticacao: converge para
  `text-login-fallback-activation-ticket` com `gui-submit-disabled`.
- `window_activation_applied` permanece `0`; o contrato e declarativo e nao
  aplica ativacao real da GUI no compositor.
- `submit_callback_bound` e `auth_callback_bound` permanecem `0`.
- `submit_enabled` permanece `0`, `submit_blocked` permanece `1` e
  `auth_attempt_allowed` permanece `0`.
- O contrato publico nao carrega storage bruto, texto mascarado, politica
  aninhada, snapshots internos, ponteiros de callback ou comprimento de senha.

## Validacao

Validado por revisao estatica do codigo e da documentacao. Conforme restricao
operacional desta entrega, nenhum comando `make`, `git`, build ou suite de testes
foi executado.
