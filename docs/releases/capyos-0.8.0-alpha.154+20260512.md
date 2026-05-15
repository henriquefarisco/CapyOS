# CapyOS 0.8.0-alpha.154+20260512

Data: 2026-05-12

## Resumo

Este patch continua a Etapa 2 da sessao grafica operacional adicionando um frame
plan seguro para a tela de credenciais do loginwindow. O frame plan consome o
activation plan seguro e publica somente um ticket final declarativo de moldura
visual para futura composicao GUI/compositor, sem renderizar janela real, sem
aplicar foco real, sem autenticar pela GUI e sem carregar segredo, mascara,
comprimento ou snapshots internos.

## Entregue

- Adicionado `LOGIN_WINDOW_CREDENTIAL_SCREEN_FRAME_PLAN_VERSION`.
- Adicionado `struct login_window_credential_screen_frame_plan`.
- Adicionada `login_window_credential_screen_frame_plan_build()` para selecionar
  tickets `credential-screen-frame-ticket`, `text-recovery-frame-ticket`,
  `text-login-resume-frame-ticket` ou `text-login-fallback-frame-ticket`.
- Testes planejados cobrem moldura declarativa de credencial, recuperacao
  textual, resume textual, submit bloqueado, acao desconhecida, activation plan
  ausente e activation plan inseguro.

## Segurança

- O frame plan exige activation plan seguro, ativacao autorizada mas nao aplicada,
  ticket selecionado, rota selecionada, credenciais seguras, storage limpo,
  redacao de segredo/comprimento, ausencia de exposicao bruta/mascarada, submit
  bloqueado/desabilitado, callbacks de submit/auth zerados e login textual
  autoritativo.
- Submit grafico nunca vira autenticacao: converge para
  `text-login-fallback-frame-ticket` com `gui-submit-disabled`.
- `window_frame_rendered` permanece `0`; o contrato e declarativo e nao renderiza
  janela real nem aplica foco real no compositor.
- `submit_callback_bound` e `auth_callback_bound` permanecem `0`.
- `submit_enabled` permanece `0`, `submit_blocked` permanece `1` e
  `auth_attempt_allowed` permanece `0`.
- O contrato publico nao carrega storage bruto, texto mascarado, politica
  aninhada, snapshots internos, ponteiros de callback ou comprimento de senha.

## Validacao

Validado por revisao estatica do codigo e da documentacao. Conforme restricao
operacional desta entrega, nenhum comando `make`, `git`, build ou suite de testes
foi executado.
