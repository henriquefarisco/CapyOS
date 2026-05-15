# CapyOS 0.8.0-alpha.148+20260511

Data: 2026-05-11

## Resumo

Este patch continua a Etapa 2 da sessao grafica operacional adicionando um mount
plan seguro para a tela de credenciais do loginwindow. O mount plan consome o
binding seguro e publica somente a transacao final de montagem para a futura
janela grafica, sem carregar segredo, mascara, comprimento ou snapshots
internos.

## Entregue

- Adicionado `LOGIN_WINDOW_CREDENTIAL_SCREEN_MOUNT_PLAN_VERSION`.
- Adicionado `struct login_window_credential_screen_mount_plan`.
- Adicionada `login_window_credential_screen_mount_plan_build()` para selecionar
  transacoes `credential-screen-mount-plan`, `text-recovery-mount-plan`,
  `text-login-resume-mount-plan` ou `text-login-fallback-mount-plan`.
- Testes planejados cobrem montagem de credencial, recuperacao textual, resume
  textual, submit bloqueado, acao desconhecida, binding ausente e binding
  inseguro.

## Segurança

- O mount plan exige binding seguro, rota selecionada, credenciais seguras,
  storage limpo, redacao de segredo/comprimento, ausencia de exposicao
  bruta/mascarada, submit bloqueado/desabilitado e login textual autoritativo.
- Submit grafico nunca vira autenticacao: converge para
  `text-login-fallback-mount-plan` com `gui-submit-disabled`.
- `submit_callback_bound` e `auth_callback_bound` permanecem `0`.
- `submit_enabled` permanece `0`, `submit_blocked` permanece `1` e
  `auth_attempt_allowed` permanece `0`.
- O contrato publico nao carrega storage bruto, texto mascarado, politica
  aninhada, snapshots internos ou comprimento de senha.

## Validacao

Validado por revisao estatica do codigo e da documentacao. Conforme restricao
operacional desta entrega, nenhum comando `make`, `git`, build ou suite de testes
foi executado.
