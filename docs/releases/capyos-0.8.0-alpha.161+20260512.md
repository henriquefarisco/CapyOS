# CapyOS 0.8.0-alpha.161+20260512

Data: 2026-05-12

## Resumo

Este patch continua a Etapa 2 da sessao grafica operacional adicionando um
scanout plan seguro para a tela de credenciais do loginwindow. O scanout plan
consome o vsync plan seguro e publica somente um ticket declarativo de scanout
para futura integracao GUI/compositor/display, sem anexar buffer real, sem
commitar modo de display, sem fazer page flip, sem enviar damage real, sem
submeter compositor real, sem autenticar pela GUI e sem carregar segredo,
mascara, comprimento ou snapshots internos.

## Entregue

- Adicionado `LOGIN_WINDOW_CREDENTIAL_SCREEN_SCANOUT_PLAN_VERSION`.
- Adicionado `struct login_window_credential_screen_scanout_plan`.
- Adicionada `login_window_credential_screen_scanout_plan_build()` para
  selecionar tickets `credential-screen-scanout-ticket`,
  `text-recovery-scanout-ticket`, `text-login-resume-scanout-ticket` ou
  `text-login-fallback-scanout-ticket`.
- Testes planejados cobrem scanout declarativo de credencial, recuperacao
  textual, resume textual, submit bloqueado, acao desconhecida, vsync plan
  ausente, vsync plan inseguro e tentativa de propagar estado unsafe ja
  submetido.

## Segurança

- O scanout plan exige vsync plan seguro, vsync autorizado mas nao submetido,
  schedule/present/damage/compositor nao submetidos, vsync ticket selecionado,
  rota selecionada, credenciais seguras, storage limpo, redacao de segredo e
  comprimento, ausencia de exposicao bruta/mascarada, submit
  bloqueado/desabilitado, callbacks de submit/auth zerados e login textual
  autoritativo.
- Submit grafico nunca vira autenticacao: converge para
  `text-login-fallback-scanout-ticket` com `gui-submit-disabled`.
- `scanout_submitted`, `scanout_buffer_attached`, `scanout_buffer_submitted`,
  `display_mode_committed`, `vsync_submitted`, `schedule_submitted`,
  `present_submitted`, `damage_submitted`, `compositor_damage_submitted`,
  `frame_timer_armed`, `compositor_wake_submitted` e `page_flip_submitted`
  permanecem `0`; o contrato e declarativo e nao toca display real.
- `scanout_allowed`, `scanout_ticket_selected` e `scanout_target_selected`
  preparam escalabilidade futura sem executar buffer attach, mode commit,
  compositor ou page flip real neste patch.
- O contrato publico nao carrega storage bruto, texto mascarado, politica
  aninhada, snapshots internos, ponteiros de callback ou comprimento de senha.

## Validacao

Validado por revisao estatica do codigo e da documentacao. Conforme restricao
operacional desta entrega, nenhum comando `make`, `git`, build ou suite de testes
foi executado.
